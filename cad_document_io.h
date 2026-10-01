#ifndef FORGECAD_DOCUMENT_IO_H
#define FORGECAD_DOCUMENT_IO_H

#include <QString>

#include "cad_types.h"

// File di ForgeCAD (.prt): la sola definizione parametrica del documento
// (schizzi con i parametri esatti delle curve, funzioni con i loro
// parametri), in binario compresso. La definizione basta a rigenerare tutto;
// le tassellazioni non si salvano. Dal formato 14 c'e' anche una copia dei
// corpi calcolati (B-rep esatti), solo per non ricalcolarli all'apertura
// (sotto). Fanno eccezione i corpi importati, che non
// hanno una definizione: il loro body e' in `importData` come testo STEP del
// kernel (dal formato 9; nel formato 8 era un B-rep OpenCASCADE, che non si
// legge piu': quei corpi vanno importati di nuovo).
//
// Formato: "FCAD" (4 byte), versione (quint16, big endian), metodo di
// compressione (quint8: 1 = zlib), poi il blocco qCompress (lunghezza
// originale su 4 byte + dati zlib al livello 9) del contenuto scritto con
// QDataStream (Qt_6_0, double a 64 bit): i valori sono esatti, bit per bit.
// Dal formato 14 la lunghezza del blocco (quint32) lo precede e dopo c'e' un
// secondo blocco compresso con la copia dei corpi calcolati (fk_body_io): se
// e' stata scritta con gli stessi sorgenti della geometria e per la stessa
// definizione, all'apertura i corpi non si ricalcolano.
namespace ForgeCad {

inline constexpr const char *kDocumentSuffix = "prt";

// Restituisce l'errore (vuoto se riuscito). Il salvataggio e' atomico (QSaveFile).
// Con `bodies` si salva anche la copia dei corpi calcolati.
QString saveDocumentFile(const QString &path, const DocumentState &state, bool bodies = true);
// `previewCache` permette al solo riquadro della finestra Apri di leggere la
// copia B-rep salvata anche se l'impronta del kernel e' cambiata. La geometria
// non viene marcata come cache valida e non puo' quindi entrare nel documento.
QString loadDocumentFile(const QString &path, DocumentState &state, bool previewCache = false);

}

#endif
