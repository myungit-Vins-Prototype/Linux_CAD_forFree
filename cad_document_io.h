#ifndef FORGECAD_DOCUMENT_IO_H
#define FORGECAD_DOCUMENT_IO_H

#include <QString>

#include "cad_types.h"

// File di ForgeCAD (.prt): la sola definizione parametrica del documento
// (schizzi con i parametri esatti delle curve, funzioni con i loro
// parametri), in binario compresso. La definizione basta sempre a rigenerare
// tutto. Dal formato 14 c'e' anche uno snapshot dei corpi calcolati (B-rep
// esatti); le cache nuove comprendono inoltre la tassellazione pronta per il
// viewport. La cache 5 condivide senza perdita i blocchi B-rep fra gli stadi
// e salva i vettori float della mesh a 32 bit. Le mesh delle cache precedenti
// si rigenerano per eliminare eventuali artefatti del vecchio upload CUDA.
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

// Restituisce l'errore (vuoto se riuscito). Il salvataggio e' atomico (QSaveFile).
// Con `bodies` si salva anche la copia dei corpi calcolati.
QString saveDocumentFile(const QString &path, const DocumentState &state, bool bodies = true);
// `previewCache` e' conservato per compatibilita' con il chiamante: gli
// snapshot moderni e storici della stessa definizione sono validi anche dopo
// un aggiornamento del kernel e vengono ricalcolati alla prima modifica.
QString loadDocumentFile(const QString &path, DocumentState &state, bool previewCache = false);

}

#endif
