#ifndef FORGECAD_DOCUMENT_IO_H
#define FORGECAD_DOCUMENT_IO_H

#include <QString>

#include "cad_types.h"

// File di ForgeCAD (.prt): la sola definizione parametrica del documento
// (schizzi con i parametri esatti delle curve, funzioni con i loro
// parametri), in binario compresso. Le forme B-rep e le tassellazioni non si
// salvano: all'apertura si rigenerano dalla definizione con il kernel attivo,
// quindi il file resta piccolo e la geometria esatta.
//
// Formato: "FCAD" (4 byte), versione (quint16, big endian), metodo di
// compressione (quint8: 1 = zlib), poi il blocco qCompress (lunghezza
// originale su 4 byte + dati zlib al livello 9) del contenuto scritto con
// QDataStream (Qt_6_0, double a 64 bit): i valori sono esatti, bit per bit.
namespace ForgeCad {

inline constexpr const char *kDocumentSuffix = "prt";

// Restituisce l'errore (vuoto se riuscito). Il salvataggio e' atomico (QSaveFile).
QString saveDocumentFile(const QString &path, const DocumentState &state);
QString loadDocumentFile(const QString &path, DocumentState &state);

}

#endif
