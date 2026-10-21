#ifndef FORGECAD_DOCUMENT_IO_H
#define FORGECAD_DOCUMENT_IO_H

#include <QString>
#include <QLockFile>
#include <memory>

#include "cad_types.h"

// File di ForgeCAD (.prt): la sola definizione parametrica del documento
// (schizzi con i parametri esatti delle curve, funzioni con i loro
// parametri), in binario compresso. La definizione basta sempre a rigenerare
// tutto. Dal formato 14 c'e' anche uno snapshot dei corpi calcolati (B-rep
// esatti); le cache nuove comprendono inoltre la tassellazione pronta per il
// viewport. La cache 5 condivide senza perdita i blocchi B-rep fra gli stadi
// e salva i vettori float della mesh a 32 bit. Le mesh delle cache precedenti
// si rigenerano per eliminare eventuali artefatti del vecchio upload CUDA.
// La cache 6 aggiunge le origini dichiarate dagli algoritmi, con gli operandi
// identificati da indice e ID della feature; la definizione resta formato 49.
// La cache 7 conserva anche le etichette delle facce emesse dalla costruzione
// (coperchi e tratti dello schizzo), leggendo ancora le cache precedenti.
// Il formato 50 aggiunge identita' del documento, check-in e base della
// copia di lavoro; la cache geometrica resta 7. Legge i formati precedenti.
// Fanno eccezione i
// corpi importati, che non
// hanno una definizione: il loro body e' in `importData` come testo STEP del
// kernel (dal formato 9; nel formato 8 era un B-rep OpenCASCADE, che non si
// legge piu': quei corpi vanno importati di nuovo).
//
// Formato: "FCAD" (4 byte), versione (quint16, big endian), metodo di
// compressione (quint8: 1 = zlib), poi il blocco qCompress (lunghezza
// originale su 4 byte + dati zlib al livello 9) del contenuto scritto con
// QDataStream (Qt_6_0, double a 64 bit): i valori sono esatti, bit per bit.
// Dal formato 14 la lunghezza del blocco (quint32) lo precede e dopo c'e' un
// secondo blocco compresso con lo snapshot calcolato (fk_body_io e mesh). Se
// appartiene alla stessa definizione, all'apertura non si ricalcola; appena si
// modifica o rigenera la storia viene sostituito dal risultato del kernel.
namespace ForgeCad {

inline constexpr const char *kDocumentSuffix = "prt";
inline constexpr quint16 kDocumentFormatVersion = 50;

// Budget in byte, indipendenti dal numero di feature. I chiamanti che devono
// aprire documenti piu' grandi possono passarne di espliciti agli overload.
// Una cache fuori budget viene ignorata: la definizione resta leggibile e
// la geometria si rigenera. cacheBytes limita anche l'espansione dei chunk.
struct DocumentReadLimits {
    qsizetype fileBytes = qsizetype(1024) * 1024 * 1024;
    qsizetype definitionBytes = qsizetype(256) * 1024 * 1024;
    qsizetype cacheBytes = qsizetype(1024) * 1024 * 1024;
};

// Codec senza accesso al filesystem, usato anche dall'archivio revisioni.
// La codifica puo' rappresentare uno snapshot archiviato; il normale Salva
// su file richiede invece una copia di lavoro.
QString encodeDocument(const DocumentState &state, QByteArray &bytes, bool bodies = true, int compressionLevel = 9);
QString decodeDocument(const QByteArray &bytes, DocumentState &state, bool previewCache = false);
QString decodeDocument(const QByteArray &bytes, DocumentState &state, const DocumentReadLimits &limits,
                       bool previewCache = false);
// Copia indipendente: stessi parametri/geometria, nuova identita' e nessuna base.
void forkDocumentIdentity(DocumentState &state);

// Il lock vive accanto al documento, anche su una condivisione di rete.
// Non scade per anzianita': una sessione di modifica puo' durare indefinitamente.
QString canonicalDocumentPath(const QString &path);
// Impronta della copia su disco, per non riapplicare modifiche locali a una
// copia cambiata mentre la finestra non possedeva l'esclusiva di scrittura.
QString documentFileFingerprint(const QString &path, QByteArray &fingerprint);
class DocumentFileLock final {
public:
    explicit DocumentFileLock(const QString &path);
    QString acquire();
    bool owns(const QString &path) const;
    QString path() const { return path_; }
private:
    QString path_;
    QLockFile lock_;
    QByteArray identity_;
};

// Restituisce l'errore (vuoto se riuscito). Il salvataggio e' atomico (QSaveFile).
// Con `bodies` si salva anche la copia dei corpi calcolati.
// Senza lock del documento aperto, acquisisce un lock per tutta la scrittura.
QString saveDocumentFile(const QString &path, const DocumentState &state, bool bodies = true,
                         const DocumentFileLock *lock = nullptr);
// `previewCache` e' conservato per compatibilita' con il chiamante: gli
// snapshot moderni e storici della stessa definizione sono validi anche dopo
// un aggiornamento del kernel e vengono ricalcolati alla prima modifica.
QString loadDocumentFile(const QString &path, DocumentState &state, bool previewCache = false);
QString loadDocumentFile(const QString &path, DocumentState &state, const DocumentReadLimits &limits,
                         bool previewCache = false);

}

#endif
