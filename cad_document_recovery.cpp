#include "cad_document_recovery.h"
#include "cad_document_io.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <utility>

namespace ForgeCad {
namespace {
constexpr auto kSlots = {"snapshot-0.fcr", "snapshot-1.fcr"};
struct Snapshot { RecoveryEntry entry; DocumentState state; };

bool sessionDirectory(const QString &root, const QString &path) {
    const QFileInfo info(path);
    return !info.isSymLink() && info.isDir() && !QUuid(info.fileName()).isNull()
        && info.absolutePath() == QDir(root).absolutePath();
}

bool readSlot(const QString &path, Snapshot &snapshot) {
    const QFileInfo info(path);
    if (info.isSymLink() || info.size() > DocumentReadLimits{}.fileBytes) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(4) != "FCR1") return false;
    QDataStream stream(&file); quint32 metadataSize = 0; stream >> metadataSize;
    if (stream.status() != QDataStream::Ok || metadataSize > 65536 || metadataSize > file.bytesAvailable()) return false;
    const auto metadata = QJsonDocument::fromJson(file.read(metadataSize)).object();
    const auto payload = file.readAll();
    if (metadata.value("version").toInt() != 1 || payload.isEmpty()
        || metadata.value("sha256").toString().toLatin1() != QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()) return false;
    DocumentState state;
    if (!decodeDocument(payload,state).isEmpty()) return false;
    RecoveryEntry entry; entry.directory=info.absolutePath();
    entry.originalPath=metadata.value("originalPath").toString(); entry.title=metadata.value("title").toString();
    entry.savedAt=QDateTime::fromString(metadata.value("savedAt").toString(),Qt::ISODateWithMs);
    entry.sequence=metadata.value("sequence").toString().toULongLong(); entry.bytes=info.size();
    if (!entry.savedAt.isValid() || !entry.sequence) return false;
    snapshot={entry,std::move(state)}; return true;
}

bool latestSnapshot(const QString &directory, Snapshot &snapshot) {
    bool found=false;
    for (auto slot:kSlots) {
        Snapshot candidate;
        if (readSlot(QDir(directory).filePath(QLatin1String(slot)),candidate)
            && (!found || candidate.entry.sequence>snapshot.entry.sequence)) {
            snapshot=std::move(candidate); found=true;
        }
    }
    return found;
}

QString removeSlots(const QString &directory) {
    for (auto slot:kSlots) {
        const auto path=QDir(directory).filePath(QLatin1String(slot));
        if (QFileInfo::exists(path) && !QFile::remove(path)) return QStringLiteral("Impossibile eliminare la copia di recupero %1.").arg(path);
    }
    return {};
}
}

class RecoveryWorker final : public QObject {
    Q_OBJECT
public:
    RecoveryWorker(QString root,QString session):root_(std::move(root)),directory_(QDir(root_).filePath(session)){}
    ~RecoveryWorker() override { lock_.reset(); QDir().rmdir(directory_); }
    void finish() {lock_.reset();QDir().rmdir(directory_);emit stopped();QThread::currentThread()->quit();}

    void checkpoint(DocumentState state,const QString &originalPath,quint64 revision) {
        QString error; bool written=false;
        try {
            // Nessun distacco delle mesh nel thread GUI. La copia di lavoro
            // conserva temporaneamente solo i B-rep immutabili necessari ad
            // aggiornare i riferimenti topologici durante encodeDocument.
            QHash<quint64,ModelBody> models;
            for (const auto &model:std::as_const(state.modelBodies)) models.insert(model.id,model);
            for (auto &feature:state.extrusions) {
                const auto model=models.constFind(feature.modelBodyId);
                if (model!=models.cend()) feature.visible=feature.featureId==model->tipFeatureId && model->visible;
                feature.display={}; feature.cachedGeometry=false;
            }
            QByteArray payload;
            error=encodeDocument(state,payload,false,1);
            if (error.isEmpty()) {
                const auto hash=QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex();
                if (hash!=lastHash_ || originalPath!=lastPath_) {
                    error=ensureSession();
                    if (error.isEmpty()) {
                        const auto next=sequence_+1;
                        QJsonObject metadata{{"version",1},{"sequence",QString::number(next)},
                            {"savedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                            {"originalPath",originalPath},{"title",originalPath.isEmpty()?QStringLiteral("Senza nome"):QFileInfo(originalPath).fileName()},
                            {"sha256",QString::fromLatin1(hash)}};
                        const auto json=QJsonDocument(metadata).toJson(QJsonDocument::Compact);
                        QSaveFile file(QDir(directory_).filePath(QStringLiteral("snapshot-%1.fcr").arg(next%2)));
                        if (!file.open(QIODevice::WriteOnly)) error=file.errorString();
                        else {
                            file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
                            QDataStream stream(&file); stream.writeRawData("FCR1",4); stream<<quint32(json.size());
                            if (stream.status()!=QDataStream::Ok || file.write(json)!=json.size() || file.write(payload)!=payload.size()) error=file.errorString();
                            else if (!file.commit()) error=file.errorString();
                            else { sequence_=next; lastHash_=hash; lastPath_=originalPath; written=true; }
                        }
                    }
                }
            }
        } catch(const std::exception &e) { error=QString::fromUtf8(e.what()); }
        emit completed(revision,written,error);
    }
    void clear(quint64 revision) {
        const auto error=removeSlots(directory_);
        if (error.isEmpty()) { lastHash_.clear(); lastPath_.clear(); }
        emit cleared(revision,error);
    }
    void scan() {
        QVector<RecoveryEntry> entries; QString error;
        QDir root(root_);
        if (root.exists() && !QFileInfo(root_).isReadable()) error=QStringLiteral("Cartella di recupero non leggibile.");
        for (const auto &directory:root.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
            if (!sessionDirectory(root_,directory.absoluteFilePath())) continue;
            QLockFile guard(directory.absoluteFilePath()+QStringLiteral("/session.lock")); guard.setStaleLockTime(0);
            if (!guard.tryLock(0)) continue; // documento ancora aperto in un'altra finestra/processo
            Snapshot snapshot;
            if (latestSnapshot(directory.absoluteFilePath(),snapshot)) entries.append(snapshot.entry);
        }
        std::sort(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.savedAt>b.savedAt;});
        emit listed(entries,error);
    }
    void restore(const RecoveryEntry &entry) {
        Snapshot snapshot; QString error;
        if (!sessionDirectory(root_,entry.directory)) error=QStringLiteral("Copia di recupero non valida.");
        else {
            QLockFile guard(entry.directory+QStringLiteral("/session.lock"));guard.setStaleLockTime(0);
            if (!guard.tryLock(0)) error=QStringLiteral("Questo documento e' ancora aperto in un'altra sessione.");
            else if (!latestSnapshot(entry.directory,snapshot)) error=QStringLiteral("Nessuna copia di recupero integra disponibile.");
        }
        emit restored(snapshot.state,error.isEmpty()?snapshot.entry:entry,error);
    }
    void discard(const RecoveryEntry &entry) {
        QString error;
        if (!sessionDirectory(root_,entry.directory)) error=QStringLiteral("Copia di recupero non valida.");
        else {
            { QLockFile guard(entry.directory+QStringLiteral("/session.lock"));guard.setStaleLockTime(0);
              if (!guard.tryLock(0)) error=QStringLiteral("Il documento e' ancora aperto in un'altra sessione.");
              else error=removeSlots(entry.directory); }
            if (error.isEmpty()) QDir().rmdir(entry.directory);
        }
        emit discarded(error);
    }
signals:
    void completed(quint64 revision,bool written,const QString &error);
    void cleared(quint64 revision,const QString &error);
    void listed(const QVector<ForgeCad::RecoveryEntry> &entries,const QString &error);
    void restored(const DocumentState &state,const ForgeCad::RecoveryEntry &entry,const QString &error);
    void discarded(const QString &error);
    void stopped();
private:
    QString ensureSession() {
        if (lock_) return {};
        const bool created=!QDir(root_).exists();
        if (!QDir().mkpath(directory_)) return QStringLiteral("Impossibile creare la cartella di recupero.");
        if (created) QFile::setPermissions(root_,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
        QFile::setPermissions(directory_,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
        auto lock=std::make_unique<QLockFile>(directory_+QStringLiteral("/session.lock"));lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) return QStringLiteral("Impossibile prenotare la sessione di recupero.");
        lock_=std::move(lock);return {};
    }
    QString root_,directory_,lastPath_;
    QByteArray lastHash_;
    quint64 sequence_=0;
    std::unique_ptr<QLockFile> lock_;
};

QString DocumentRecovery::defaultDirectory() {
    const auto override=qEnvironmentVariable("FORGECAD_RECOVERY_DIR");
    return override.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/recovery"):override;
}
DocumentRecovery::DocumentRecovery(Capture capture,QObject *parent,QString directory,int quietMs,int maximumMs)
    :QObject(parent),capture_(std::move(capture)),directory_(QDir(directory.isEmpty()?defaultDirectory():directory).absolutePath()),
     session_(QUuid::createUuid().toString(QUuid::WithoutBraces)),quietMs_(quietMs) {
    qRegisterMetaType<DocumentState>();qRegisterMetaType<RecoveryEntry>();qRegisterMetaType<QVector<RecoveryEntry>>();
    quiet_.setSingleShot(true);maximum_.setSingleShot(true);maximum_.setInterval(maximumMs);
    connect(&quiet_,&QTimer::timeout,this,&DocumentRecovery::capture);
    connect(&maximum_,&QTimer::timeout,this,&DocumentRecovery::capture);
}
DocumentRecovery::~DocumentRecovery() {
    // Nessun join o attesa disco sul thread GUI. Il worker finisce i comandi
    // gia' accodati, rilascia il lock e termina il proprio event loop.
    if (worker_&&!stopping_) {auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker]{worker->finish();},Qt::QueuedConnection);}
}
void DocumentRecovery::startWorker() {
    if (worker_) return;
    thread_=new QThread;worker_=new RecoveryWorker(directory_,session_);worker_->moveToThread(thread_);
    connect(thread_,&QThread::finished,worker_,&QObject::deleteLater);
    connect(thread_,&QThread::finished,thread_,&QObject::deleteLater);
    connect(worker_,&RecoveryWorker::completed,this,[this](quint64 revision,bool written,const QString &error){
        busy_=false;
        if (revision==revision_ && error.isEmpty()) dirty_=false;
        emit checkpointFinished(revision,written,error);
        if (dirty_&&!stopping_) {quiet_.start(error.isEmpty()?quietMs_:30000);if(!maximum_.isActive()&&error.isEmpty())maximum_.start();}
    });
    connect(worker_,&RecoveryWorker::cleared,this,&DocumentRecovery::cleared);
    connect(worker_,&RecoveryWorker::listed,this,&DocumentRecovery::entriesFound);
    connect(worker_,&RecoveryWorker::restored,this,&DocumentRecovery::restored);
    connect(worker_,&RecoveryWorker::discarded,this,&DocumentRecovery::discarded);
    connect(worker_,&RecoveryWorker::stopped,this,[this]{worker_=nullptr;thread_=nullptr;stopping_=false;busy_=false;emit stopped();});
    thread_->start(QThread::LowPriority);
}
void DocumentRecovery::changed() {
    if(stopping_)return;
    ++revision_;dirty_=true;
    if (busy_) return;
    quiet_.start(quietMs_);if(!maximum_.isActive())maximum_.start();
}
void DocumentRecovery::capture() {
    quiet_.stop();maximum_.stop();
    if (!dirty_ || busy_) return;
    DocumentState state;QString path;
    if (!capture_(state,path)) {quiet_.start(500);return;}
    startWorker();busy_=true;const auto revision=revision_;auto *worker=worker_;
    QMetaObject::invokeMethod(worker,[worker,state=std::move(state),path,revision]() mutable {worker->checkpoint(std::move(state),path,revision);},Qt::QueuedConnection);
}
quint64 DocumentRecovery::reset() {
    ++revision_;dirty_=false;quiet_.stop();maximum_.stop();
    const auto revision=revision_;
    if (!worker_) QTimer::singleShot(0,this,[this,revision]{emit cleared(revision,{});});
    else {auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker,revision]{worker->clear(revision);},Qt::QueuedConnection);}
    return revision;
}
void DocumentRecovery::scan() {startWorker();auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker]{worker->scan();},Qt::QueuedConnection);}
void DocumentRecovery::restore(const RecoveryEntry &entry) {startWorker();auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker,entry]{worker->restore(entry);},Qt::QueuedConnection);}
void DocumentRecovery::discard(const RecoveryEntry &entry) {startWorker();auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker,entry]{worker->discard(entry);},Qt::QueuedConnection);}
void DocumentRecovery::shutdown() {
    if(stopping_)return;
    stopping_=true;dirty_=false;quiet_.stop();maximum_.stop();
    if(!worker_){QTimer::singleShot(0,this,[this]{stopping_=false;emit stopped();});return;}
    auto *worker=worker_;QMetaObject::invokeMethod(worker,[worker]{worker->finish();},Qt::QueuedConnection);
}
}
#include "cad_document_recovery.moc"
