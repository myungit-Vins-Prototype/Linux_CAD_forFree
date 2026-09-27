#ifndef FORGECAD_FK_TRANSFORM_H
#define FORGECAD_FK_TRANSFORM_H

#include "fk_topology.h"

// Trasformazioni rigide e scale uniformi dei body, esatte: ogni curva e ogni
// superficie resta del suo tipo (un cilindro scalato e' un cilindro di raggio
// s R, una B-spline ha i poli trasformati), le curve e le superfici condivise
// restano condivise, le SP-curve si ricalcolano.
namespace ForgeCad::Kernel {

// `transform` deve essere una similitudine (rotazione, traslazione, scala
// uniforme positiva, anche con una simmetria): altrimenti std::domain_error.
// Con una simmetria (determinante negativo) le superfici analitiche hanno il
// sistema speculare reso destrorso (stesso insieme di punti, u al contrario),
// i loop si percorrono al contrario e il verso delle facce viene dalla
// normale trasformata.
Body transformBody(const Body &body, const Transform3 &transform);

// Immagine speculare rispetto al piano per `point` normale a `normal`.
Body mirrorBody(const Body &body, const Vec3 &point, const Vec3 &normal);

// Scala uniforme di fattore `factor` > 0 attorno a `center`.
Body scaleBody(const Body &body, const Vec3 &center, double factor);

// Curva e superficie trasformate, dello stesso tipo. Il parametro della
// curva trasformata e' k t (`parameterScale` = k: le rette hanno la direzione
// unitaria, quindi k e' la scala; per le altre curve k = 1).
CurvePtr<3> transformCurve(const CurvePtr<3> &curve, const Transform3 &transform, double *parameterScale = nullptr);
SurfacePtr transformSurface(const SurfacePtr &surface, const Transform3 &transform);

}

#endif
