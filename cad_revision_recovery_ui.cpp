#include "cad_revision_recovery_ui.h"
#include "cad_revision_store.h"
#include "cad_workspace.h"
#include "cad_drawing_compute.h"
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <optional>
#include <stdexcept>
namespace ForgeCad {
void showCatalogRecoveryDialog(QWidget *owner,const QString &documentPath){
    QDialog dialog(owner);dialog.setObjectName("catalogRecoveryDialog");dialog.setWindowTitle(QStringLiteral("Verifica e recupero catalogo"));auto *layout=new QVBoxLayout(&dialog);
    auto *description=new QLabel(QStringLiteral("Verifica il catalogo e tutti i contenuti archiviati. Il recupero riapplica l'ultimo stato registrato e conserva il catalogo danneggiato. I documenti di lavoro e le richieste locali rimangono al loro posto."),&dialog);description->setWordWrap(true);layout->addWidget(description);
    auto *summary=new QPlainTextEdit(&dialog);summary->setObjectName("catalogRecoverySummary");summary->setReadOnly(true);layout->addWidget(summary,1);
    auto *status=new QLabel(&dialog);status->setObjectName("catalogRecoveryStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);auto *verify=buttons->addButton(QStringLiteral("Verifica"),QDialogButtonBox::ActionRole),*protect=buttons->addButton(QStringLiteral("Attiva checkpoint"),QDialogButtonBox::ActionRole),*restore=buttons->addButton(QStringLiteral("Ripristina catalogo"),QDialogButtonBox::ActionRole);
    verify->setObjectName("catalogRecoveryVerify");protect->setObjectName("catalogRecoveryProtect");restore->setObjectName("catalogRecoveryRestore");protect->setEnabled(false);restore->setEnabled(false);layout->addWidget(buttons);
    WorkspacePaths paths;const auto located=locateDocumentWorkspace(documentPath,paths);std::optional<RevisionCatalogRecovery> inspected;bool busy=false;
    const QStringList phases{QStringLiteral("Lettura checkpoint"),QStringLiteral("Verifica catalogo e contenuti"),QStringLiteral("Verifica dipendenze"),QStringLiteral("Preparazione recupero"),QStringLiteral("Pubblicazione catalogo"),QStringLiteral("Verifica completata")};
    const auto display=[&](const RevisionCatalogRecovery &info){inspected=info;QString state=info.catalogMatches?QStringLiteral("Catalogo coerente"):info.canRecover?QStringLiteral("Catalogo da recuperare"):QStringLiteral("Recupero bloccato");summary->setPlainText(QStringLiteral("%1\n\nArchivio: %2\nDocumenti: %3\nCheck-in: %4\nPrenotazioni: %5\nRilasci verificati: %6\n\nCheckpoint: %7\nData: %8\n%9").arg(state,info.archiveId.toString(QUuid::WithoutBraces)).arg(info.documents).arg(info.revisions).arg(info.checkouts).arg(info.releases).arg(info.protectedCatalog?QString::fromLatin1(info.checkpointHash.toHex()):QStringLiteral("Non ancora attivo"),info.createdUtc.isValid()?info.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"):QStringLiteral("—"),info.conflict));protect->setEnabled(!info.protectedCatalog&&info.catalogMatches);restore->setEnabled(info.canRecover);};
    const auto execute=[&](int operation){protect->setEnabled(false);restore->setEnabled(false);if(!located.isEmpty()){status->setText(located);return;}const auto expected=inspected.value_or(RevisionCatalogRecovery{});RevisionCatalogRecovery result;const auto error=runDrawingComputation(&dialog,busy,operation==2?QStringLiteral("Recupero catalogo"):QStringLiteral("Verifica archivio"),result,[directory=paths.revisions,operation,expected](auto phase){RevisionStore store(directory);RevisionCatalogRecovery info;phase(1);const auto failure=operation==2?store.recoverCatalog(expected,info):operation==1?store.protectCatalog(info):store.inspectCatalogRecovery(info);if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());phase(5);return info;},phases);
        if(!error.isEmpty()){
            // Un annullamento successivo alla pubblicazione non perde la ricevuta.
            RevisionCatalogRecovery receipt;bool reading=false;const auto failure=operation>0?runDrawingComputation(&dialog,reading,QStringLiteral("Verifica esito recupero"),receipt,[directory=paths.revisions](auto phase){RevisionCatalogRecovery info;const auto e=RevisionStore(directory).inspectCatalogRecovery(info);if(!e.isEmpty())throw std::runtime_error(e.toStdString());phase(5);return info;},phases):error;
            if(!failure.isEmpty()||!receipt.protectedCatalog||!receipt.catalogMatches||(operation==2&&receipt.checkpointHash!=expected.checkpointHash)){inspected.reset();summary->clear();status->setText(error);return;}
            result=std::move(receipt);
        }
        display(result);if(!result.evidencePath.isEmpty())summary->appendPlainText(QStringLiteral("\nFile precedente conservato:\n%1").arg(result.evidencePath));status->setText(operation==2?QStringLiteral("Catalogo ripristinato e verificato. L'eventuale file precedente e' conservato nella cartella di recupero."):operation==1?QStringLiteral("Checkpoint attivato. Le scritture successive aggiornano lo stato di recupero."):QStringLiteral("Verificati catalogo, contenuti e rilasci."));layout->activate();
    };
    QObject::connect(verify,&QPushButton::clicked,&dialog,[&]{execute(0);});QObject::connect(protect,&QPushButton::clicked,&dialog,[&]{execute(1);});QObject::connect(restore,&QPushButton::clicked,&dialog,[&]{execute(2);});QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QTimer::singleShot(0,&dialog,[&]{execute(0);});dialog.resize(800,570);dialog.exec();
}
}
