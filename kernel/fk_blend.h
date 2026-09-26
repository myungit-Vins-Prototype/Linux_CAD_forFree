#ifndef FORGECAD_FK_BLEND_H
#define FORGECAD_FK_BLEND_H

#include <vector>

#include "fk_topology.h"

// Raccordi (fillet) e smussi (chamfer) sugli spigoli di un solido.
//
// Ogni spigolo si tratta nella sua sezione: il piano normale a uno spigolo
// rettilineo, o il semipiano per l'asse di uno spigolo circolare (chiuso o
// arco). Li' le due facce sono rette o cerchi (piano; cilindro con l'asse
// parallelo allo spigolo; per gli spigoli circolari piano perpendicolare
// all'asse, cilindro, cono, sfera e toro coassiali), il raccordo e' il
// cerchio tangente a entrambe (intersezione delle curve spostate del raggio)
// e lo smusso la corda tra i punti a distanza `size` dallo spigolo. La zona
// tra lo spigolo e il raccordo (o lo smusso), estrusa lungo lo spigolo o
// fatta ruotare attorno all'asse (per l'angolo dell'arco), si toglie dal
// solido se lo spigolo e' convesso o si aggiunge se e' concavo (booleane
// esatte): le facce nuove stanno su cilindri, tori, piani e coni esatti.
//
// Estremi degli spigoli:
//  - la zona finisce nel piano normale allo spigolo nel vertice se le altre
//    facce del vertice vi sono normali (piano perpendicolare, piano per
//    l'asse di un arco) o continuano in modo tangente una delle due facce;
//  - catene di spigoli tangenti (segmenti e archi di un contorno raccordato):
//    le zone si incontrano nella stessa sezione e i loro utensili si cuciono
//    in uno solo (le superfici dei raccordi vi si toccano tangenti);
//  - uno spigolo rettilineo che finisce contro una faccia piana obliqua (un
//    vertice con tre facce): la zona si allunga oltre il vertice e si taglia
//    con il semispazio della faccia;
//  - due spigoli scelti in un vertice: angolo a mitra, come OCCT;
//  - tre spigoli rettilinei convessi tra tre facce piane: pezza d'angolo. Il
//    raccordo finisce sulla sfera tangente ai tre piani (i cilindri
//    lasciano il posto alla sfera nei piani per il suo centro normali agli
//    spigoli); lo smusso sul triangolo per i punti in cui si incontrano i
//    bordi degli smussi (negli angoli non retti OCCT usa invece una pezza
//    B-spline di riempimento).
//
// I bordi di forma libera (spline, NURBS, ellissi) tra una faccia piana e i
// suoi fianchi paralleli alla normale, con i segmenti e gli archi che li
// continuano nel contorno, passano a blendPlanarChains (fk_blend_loop.h):
// palla rotolante, senza booleane.
//
// Non gestiti (std::domain_error): altri tipi di facce, spigoli
// circolari tra facce non coassiali, estremi contro facce curve non normali
// o non tangenti, piu' di tre spigoli in un vertice o pezze d'angolo tra
// facce curve, raggi che non stanno nelle facce.
namespace ForgeCad::Kernel {

// `size`: raggio del raccordo o distanza dello smusso dallo spigolo su
// ciascuna faccia.
Body blendEdges(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer);

// Edge del body piu' vicino al punto (non valido se nessuno dista meno di `tolerance`).
EdgeId nearestEdge(const Body &body, const Vec3 &point, double tolerance);

}

#endif
