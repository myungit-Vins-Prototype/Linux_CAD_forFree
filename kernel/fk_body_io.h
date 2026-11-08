#ifndef FORGECAD_FK_BODY_IO_H
#define FORGECAD_FK_BODY_IO_H

#include <string>
#include <string_view>

#include "fk_topology.h"

// Body in binario, com'e' in memoria: tutte le entita' (anche quelle morte)
// con i loro indici, le tolleranze locali, le SP-curve delle fin e la
// geometria esatta (curve e superfici condivise restano condivise), double a
// 64 bit bit per bit. Serve a salvare nei documenti il risultato delle
// funzioni costose (booleane tra B-spline, sweep) e a rileggerlo senza
// ricalcolarlo. Dal formato 2 le istanze NURBS conservano base e similitudine,
// senza espandere i poli; il lettore accetta anche il formato 1. Non e' un formato di scambio (per quello fk_step, fk_iges).
//
// I costruttori delle curve normalizzano gli assi (cerchi, ellissi, rette,
// direzioni delle estrusioni e delle rivoluzioni): su vettori gia' unitari
// il risultato puo' differire dall'originale nell'ultimo bit. I sistemi di
// riferimento delle superfici si rileggono esatti (Frame3::fromAxes).
namespace ForgeCad::Kernel {

// Lancia std::domain_error se il body contiene una curva o una superficie di
// un tipo che il formato non conosce (chi salva puo' fare a meno della copia).
std::string writeBodyBinary(const Body &body);
// Lancia std::invalid_argument se i dati non sono validi (versione, indici,
// lunghezze).
Body readBodyBinary(const std::string &data);

// Pool locale alla lettura di un documento: condivide soltanto NURBS con
// coefficienti bit per bit identici fra body diversi. Preserva la distinta
// identita' dei record nello stesso body, la topologia e il formato binario.
// Thread-safe; nessuna cache globale o prolungamento della vita dei documenti.
class BodyReadPool;
std::shared_ptr<BodyReadPool> makeBodyReadPool();
Body readBodyBinary(std::string_view data, BodyReadPool &pool);

}

#endif
