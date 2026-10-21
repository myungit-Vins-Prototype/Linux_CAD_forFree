#pragma once

#include "cad_types.h"
#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <functional>

class QThread;
namespace ForgeCad {
class RecoveryWorker;

struct RecoveryEntry {
    QString directory, originalPath, title;
    QDateTime savedAt;
    quint64 sequence = 0;
    qint64 bytes = 0;
};

// La GUI acquisisce soltanto una copia implicitamente condivisa. Codec,
// compressione e filesystem appartengono a un worker a priorita' bassa.
class DocumentRecovery final : public QObject {
    Q_OBJECT
public:
    using Capture = std::function<bool(DocumentState &, QString &)>;
    explicit DocumentRecovery(Capture capture, QObject *parent = nullptr,
                              QString directory = {}, int quietMs = 1500, int maximumMs = 10000);
    ~DocumentRecovery() override;
    static QString defaultDirectory();
    QString directory() const { return directory_; }
    void changed();
    quint64 reset(); // Salva riuscito o abbandono esplicito: elimina solo la sessione corrente.
    void scan();
    void restore(const RecoveryEntry &entry);
    void discard(const RecoveryEntry &entry);
    void shutdown(); // Rilascia lock e worker in modo asincrono, senza cancellare copie.
    bool busy() const { return busy_; }
    bool pending() const { return dirty_; }
signals:
    void checkpointFinished(quint64 revision, bool written, const QString &error);
    void cleared(quint64 revision, const QString &error);
    void entriesFound(const QVector<ForgeCad::RecoveryEntry> &entries, const QString &error);
    void restored(const DocumentState &state, const ForgeCad::RecoveryEntry &entry, const QString &error);
    void discarded(const QString &error);
    void stopped();
private:
    void startWorker();
    void capture();
    Capture capture_;
    QString directory_, session_;
    QTimer quiet_, maximum_;
    int quietMs_;
    quint64 revision_ = 0;
    bool dirty_ = false, busy_ = false, stopping_ = false;
    QThread *thread_ = nullptr;
    RecoveryWorker *worker_ = nullptr;
};
}
Q_DECLARE_METATYPE(ForgeCad::RecoveryEntry)
Q_DECLARE_METATYPE(QVector<ForgeCad::RecoveryEntry>)
Q_DECLARE_METATYPE(DocumentState)
