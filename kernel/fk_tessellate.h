#ifndef FORGECAD_FK_TESSELLATE_H
#define FORGECAD_FK_TESSELLATE_H

#include <array>
#include <vector>

#include "fk_topology.h"

// Approssimazione a triangoli e polilinee di un body, SOLO per disegnare e
// selezionare a video (come BRepMesh di OCCT): nessun calcolo del kernel la
// usa.
//
// Gli edge si campionano sulle loro curve esatte (scarto dalla corda e angolo
// tra le tangenti); ogni faccia si triangola nello spazio (u, v) partendo dai
// campioni degli edge portati sulle SP-curve delle fin. Le superfici
// periodiche non hanno cucitura: il dominio si taglia in una finestra di un
// periodo (i loop che attraversano il taglio si spezzano e si ricongiungono
// lungo il bordo della finestra, che comprende i poli). Il poligono con i
// suoi fori si triangola "a orecchie" (ear clipping, come earcut) e poi si
// raffina dividendo i lati interni finche' scarto e angolo tra le normali
// restano sotto i limiti, con scambi di diagonale di Delaunay.
namespace ForgeCad::Kernel {

struct TessellationOptions {
    double deflection = 0.01;  // scarto massimo tra corde/triangoli e geometria esatta
    double angle = 0.25;       // angolo massimo (radianti) tra tangenti o normali vicine
};

struct FaceMesh {
    FaceId face;
    std::vector<Vec3> points;
    std::vector<Vec3> normals;  // normale della faccia (verso della faccia) in ogni punto
    std::vector<Vec2> parameters;  // (u, v) di ogni punto sulla superficie
    std::vector<std::array<int, 3>> triangles;  // antiorari attorno alla normale della faccia
};

struct Tessellation {
    std::vector<FaceMesh> faces;
    std::vector<std::vector<Vec3>> edges;  // polilinee degli edge
    int failedFaces = 0;                   // facce che non si e' riusciti a triangolare
};

Tessellation tessellate(const Body &body, const TessellationOptions &options);

// Parametri degli edge usati per campionare la curva (nel verso della curva).
std::vector<double> sampleCurve(const Curve<3> &curve, const Interval &range, const TessellationOptions &options);

// Triangolazione di un poligono con fori (earcut): `outer` antiorario, `holes`
// orari. Restituisce terne di indici nell'insieme dei punti (outer seguito
// dai fori nell'ordine dato), antiorarie.
std::vector<std::array<int, 3>> triangulatePolygon(const std::vector<Vec2> &outer, const std::vector<std::vector<Vec2>> &holes);

}

#endif
