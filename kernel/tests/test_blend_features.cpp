#include <cmath>

#include "fk_blend.h"
#include "fk_blend_surface.h"
#include "fk_body_check.h"
#include "fk_curve_ops.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sweep.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

// Raccordi e smussi sui corpi di sweep e loft (facce B-spline, pezze che si
// cambiano lungo gli spigoli, angoli a mitra), con il modulo generale a palla
// rotolante (fk_blend_surface): contro il modulo analitico sulla stessa
// geometria esatta e contro i volumi esatti (Pappus).
using namespace fktest;

namespace {

ProfileRegion circleRegion(double r) {
    ProfileRegion region;
    region.outer.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    return region;
}
ProfileRegion rectangleRegion(double a, double b) {
    ProfileRegion region;
    region.outer.segments = {lineSegment(Vec2(-a / 2, -b / 2), Vec2(a / 2, -b / 2)), lineSegment(Vec2(a / 2, -b / 2), Vec2(a / 2, b / 2)),
                             lineSegment(Vec2(a / 2, b / 2), Vec2(-a / 2, b / 2)), lineSegment(Vec2(-a / 2, b / 2), Vec2(-a / 2, -b / 2))};
    return region;
}
LoftSection circleAt(double z, double r) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    s.loop = circleRegion(r).outer;
    return s;
}
LoftSection rectangleAt(double z, double a, double b) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    s.loop = rectangleRegion(a, b).outer;
    return s;
}
LoftSection polygonAt(double z, double radius, const Vec2 &shift = Vec2(), double rotation = 0.0) {
    LoftSection section;
    section.frame = Frame3(Vec3(shift.x(), shift.y(), z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    constexpr int count = 9;
    for (int k = 0; k < count; ++k) {
        const double a = rotation + kTwoPi * k / count;
        const double b = rotation + kTwoPi * (k + 1) / count;
        section.loop.segments.push_back(lineSegment(Vec2(radius * std::cos(a), radius * std::sin(a)),
                                                    Vec2(radius * std::cos(b), radius * std::sin(b))));
    }
    return section;
}
LoftSection notchedAt(double z, double scale, const Vec2 &shift = Vec2()) {
    LoftSection section;
    section.frame = Frame3(Vec3(shift.x(), shift.y(), z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const std::vector<Vec2> points{{-3, -2}, {3, -2}, {3, 2}, {0.6, 2}, {0.6, 0}, {-0.6, 0}, {-0.6, 2}, {-3, 2}};
    for (std::size_t k = 0; k < points.size(); ++k)
        section.loop.segments.push_back(lineSegment(scale * points[k], scale * points[(k + 1) % points.size()]));
    return section;
}
LoftSection splitRectangleAt(double z, double a, double b, double split, const Vec2 &shift = Vec2()) {
    LoftSection section;
    section.frame = Frame3(Vec3(shift.x(), shift.y(), z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const std::vector<Vec2> points{{-a, -b}, {split, -b}, {a, -b}, {a, b}, {split, b}, {-a, b}};
    for (std::size_t k = 0; k < points.size(); ++k)
        section.loop.segments.push_back(lineSegment(points[k], points[(k + 1) % points.size()]));
    return section;
}

// Gli spigoli del contorno della faccia (come il clic su una faccia nell'app).
std::vector<EdgeId> contour(const Body &body, FaceId face) {
    std::vector<EdgeId> edges;
    for (LoopId l : body.face(face).loops)
        for (FinId fin : body.loopFins(l)) edges.push_back(body.fin(fin).edge);
    return edges;
}

// La faccia piana con la normale uscente data.
FaceId planarFace(const Body &body, const Vec3 &normal) {
    for (FaceId f : body.faces()) {
        const Face &face = body.face(f);
        if (face.surface->type() != SurfaceType::Plane) continue;
        const Vec3 n = static_cast<const Plane &>(*face.surface).frame().zDir();
        if (dot(face.sense ? n : -n, normal) > 1.0 - 1e-9) return f;
    }
    throw std::logic_error("faccia non trovata");
}

double checkedVolume(const Body &body, double tolerance = 1e-10) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
    return massProperties(body, tolerance).volume;
}

// Sezione tolta da un raccordo di raggio r tra due facce ortogonali: area e distanza del baricentro da ciascuna.
constexpr double kSpandrel = 1.0 - kPi / 4.0;
double spandrelCentroid(double r) { return r * (10.0 - 3.0 * kPi) / (12.0 - 3.0 * kPi); }

}

FK_TEST(BlendGeneralMatchesAnalytic) {
    // Il modulo generale (forzato) sulle stesse facce piane del modulo analitico.
    // Parallelepipedo: il volume esatto delle mitre (i due prismi tolti lungo gli
    // spigoli si sovrappongono nell'angolo per r^3 (5/3 - pi/2)); il modulo
    // analitico vi arriva a 2e-9, quello generale a 1e-11.
    const double r = 0.3;
    const Body box = makeBox(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4.0, 3.0, 2.0);
    const std::vector<EdgeId> boxTop = contour(box, planarFace(box, Vec3(0, 0, 1)));
    const double exact = 24.0 - kSpandrel * r * r * 14.0 + 4.0 * r * r * r * (5.0 / 3.0 - kPi / 2.0);
    FK_CHECK_NEAR(checkedVolume(blendSurfaceChains(box, boxTop, r, false)), exact, 1e-10 * exact);
    FK_CHECK_NEAR(checkedVolume(blendEdges(box, boxTop, r, false)), exact, 5e-9 * exact);
    // Smusso: prismi triangolari, sovrapposti nell'angolo in una piramide di volume r^3 / 3.
    const double chamfered = 24.0 - 0.5 * r * r * 14.0 + 4.0 * r * r * r / 3.0;
    FK_CHECK_NEAR(checkedVolume(blendSurfaceChains(box, boxTop, r, true)), chamfered, 1e-10 * chamfered);
    // Tronco di piramide rettangolare con pendenze diverse sui lati: mitre asimmetriche
    // (il raccordo piu' profondo si taglia anche con il fianco dell'altro).
    const Body frustum = loftSolid({rectangleAt(0.0, 4.0, 2.0), rectangleAt(2.0, 2.4, 1.6)}, true);
    for (const Vec3 &normal : {Vec3(0, 0, 1), Vec3(0, 0, -1)}) {
        const std::vector<EdgeId> edges = contour(frustum, planarFace(frustum, normal));
        FK_CHECK(edges.size() == 4);
        for (bool chamfer : {false, true}) {
            const double analytic = checkedVolume(blendEdges(frustum, edges, r, chamfer));
            const double general = checkedVolume(blendSurfaceChains(frustum, edges, r, chamfer));
            FK_CHECK_NEAR(general, analytic, 1e-9 * analytic);
        }
    }
}

FK_TEST(BlendSequentialNearbyEdges) {
    // Seconda lavorazione su un bordo adiacente a un raccordo gia' presente:
    // il nuovo raccordo/smusso termina sulla superficie cilindrica della prima
    // operazione, invece che su una faccia piana originale del parallelepipedo.
    const Body box = makeBox(Frame3(), 4.0, 3.0, 2.0);
    const EdgeId vertical = nearestEdge(box, Vec3(4.0, 0.0, 1.0), 1e-9);
    FK_CHECK(vertical.valid());
    for (bool firstChamfer : {false, true}) {
        const Body first = blendEdges(box, {vertical}, 0.55, firstChamfer);
        FK_CHECK(checkedVolume(first) < checkedVolume(box));
        const EdgeId topFront = nearestEdge(first, Vec3(2.0, 0.0, 2.0), 1e-7);
        FK_CHECK(topFront.valid());
        FK_CHECK(first.edge(topFront).curve->type() == CurveType::Line);
        for (bool secondChamfer : {false, true}) {
            const Body second = blendEdges(first, {topFront}, 0.3, secondChamfer);
            const double volume = checkedVolume(second, 1e-9);
            FK_CHECK(volume > 0.0 && volume < checkedVolume(first));
        }
    }
    // Ordine inverso: il secondo bordo termina sulla faccia curva del primo
    // raccordo proprio nel vecchio vertice comune.
    const EdgeId top = nearestEdge(box, Vec3(2.0, 0.0, 2.0), 1e-9);
    FK_CHECK(top.valid());
    for (bool firstChamfer : {false, true}) {
        const Body first = blendEdges(box, {top}, 0.55, firstChamfer);
        const EdgeId adjacent = nearestEdge(first, Vec3(4.0, 0.0, 1.0), 1e-7);
        FK_CHECK(adjacent.valid());
        for (bool secondChamfer : {false, true}) {
            const Body second = blendEdges(first, {adjacent}, 0.3, secondChamfer);
            const double volume = checkedVolume(second, 1e-9);
            FK_CHECK(volume > 0.0 && volume < checkedVolume(first));
        }
    }
}

FK_TEST(BlendSweptTubeCap) {
    // Tubo lungo un percorso rettilineo dato come B-spline: i fianchi sono B-spline
    // interpolate (due pezze, il cerchio del coperchio diviso in due archi).
    // Pappus: la sezione tolta ruota attorno all'asse.
    const double R = 0.5, L = 3.0, r = 0.1;
    const auto path = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                        std::vector<Vec3>{Vec3(0, 0, 0), Vec3(0, 0, 0.7), Vec3(0, 0, 2.1), Vec3(0, 0, L)});
    const Body tube = sweepRegions(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), {circleRegion(R)}, {{path, {0.0, 1.0}}});
    const double full = kPi * R * R * L;
    FK_CHECK_NEAR(checkedVolume(tube), full, 1e-9 * full);
    const std::vector<EdgeId> cap = contour(tube, planarFace(tube, Vec3(0, 0, 1)));
    FK_CHECK(cap.size() == 2);
    const double filleted = checkedVolume(blendEdges(tube, cap, r, false), 1e-9);
    FK_CHECK_NEAR(full - filleted, kSpandrel * r * r * kTwoPi * (R - spandrelCentroid(r)), 1e-8);
    const double chamfered = checkedVolume(blendEdges(tube, cap, r, true), 1e-9);
    FK_CHECK_NEAR(full - chamfered, 0.5 * r * r * kTwoPi * (R - r / 3.0), 1e-8);
}

FK_TEST(BlendRuledLoftCone) {
    // Loft rigato tra due cerchi: un tronco di cono come B-spline. Raccordo e smusso
    // dei due bordi contro il cono esatto (sezioni analitiche).
    const Body loft = loftSolid({circleAt(0.0, 2.0), circleAt(3.0, 1.0)}, true);
    const Body cone = makeCone(Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0, 1.0, 3.0);
    for (const Vec3 &normal : {Vec3(0, 0, -1), Vec3(0, 0, 1)})
        for (bool chamfer : {false, true}) {
            const double reference = checkedVolume(blendEdges(cone, contour(cone, planarFace(cone, normal)), 0.2, chamfer));
            const double general = checkedVolume(blendEdges(loft, contour(loft, planarFace(loft, normal)), 0.2, chamfer), 1e-9);
            FK_CHECK_NEAR(general, reference, 1e-8 * reference);
        }
}

FK_TEST(BlendSweepAcrossPathJoint) {
    // Rettangolo lungo segmento + arco: lo spigolo esterno in alto prosegue tangente
    // dal piano al toro (e dal fianco piano al cilindro). Volume tolto esatto:
    // sezione per la lunghezza del segmento, Pappus sull'arco.
    const Frame3 plane(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const auto line = std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0));
    const auto arc = std::make_shared<Circle<2>>(makeCircle(Vec2(4, 3), 3.0));
    const std::vector<PathSegment> path{{embedCurve(line, plane), {0.0, 4.0}}, {embedCurve(arc, plane), {-kHalfPi, 0.0}}};
    // Profilo nel piano x = 0: X del profilo = Z del modello, Y del profilo = -Y del modello.
    const Body body = sweepRegions(Frame3(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)), {rectangleRegion(1.0, 1.0)}, path);
    const double full = 1.0 * (4.0 + kHalfPi * 3.0);
    FK_CHECK_NEAR(checkedVolume(body), full, 1e-10 * full);
    // Lo spigolo in alto (z = 0.5) all'esterno della curva (y = -0.5 sul segmento).
    const EdgeId start = nearestEdge(body, Vec3(2.0, -0.5, 0.5), 1e-9);
    FK_CHECK(start.valid());
    const double r = 0.2;
    const double removed = full - checkedVolume(blendEdges(body, {start}, r, false), 1e-9);
    FK_CHECK_NEAR(removed, kSpandrel * r * r * (4.0 + kHalfPi * (3.5 - spandrelCentroid(r))), 1e-8);
}

FK_TEST(BlendLoftCircleToSquare) {
    // Loft liscio da un cerchio a un quadrato: le pezze sono tangenti solo sul
    // cerchio, dove il raccordo passa dall'una all'altra con una piccola mitra
    // sullo spigolo tra le pezze; sul quadrato angoli a mitra tra facce B-spline.
    const Body loft = loftSolid({circleAt(0.0, 2.0), rectangleAt(3.0, 2.0, 2.0)}, false);
    const double full = checkedVolume(loft);
    for (const Vec3 &normal : {Vec3(0, 0, -1), Vec3(0, 0, 1)}) {
        const std::vector<EdgeId> edges = contour(loft, planarFace(loft, normal));
        const double filleted = checkedVolume(blendEdges(loft, edges, 0.2, false), 1e-8);
        const double chamfered = checkedVolume(blendEdges(loft, edges, 0.2, true), 1e-8);
        // Tolgono poco (la sezione, dell'ordine di r^2, per il perimetro, meno di 16).
        FK_CHECK(filleted < full && chamfered < full);
        FK_CHECK(full - filleted < 0.2 * 0.2 * 16.0 && full - chamfered < 0.2 * 0.2 * 16.0);
    }
}

FK_TEST(BlendSmoothLoftPolygonCap) {
    // Regressione del raccordo sulla faccia terminale di un loft poligonale:
    // sui fianchi B-spline il verso parametrico può essere opposto al verso
    // topologico e le mitre arrivano in prossimità di vertici quasi singolari.
    const Body loft = loftSolid({polygonAt(0.0, 5.0), polygonAt(5.0, 4.2, Vec2(0.7, -0.3), 0.08),
                                 polygonAt(10.0, 3.5, Vec2(0.2, 0.4), -0.04)}, false);
    const std::vector<EdgeId> cap = contour(loft, planarFace(loft, Vec3(0, 0, 1)));
    FK_CHECK(cap.size() == 9);
    const double full = checkedVolume(loft, 1e-8);
    const double filleted = checkedVolume(blendEdges(loft, cap, 0.45, false), 1e-7);
    FK_CHECK(filleted > 0.0 && filleted < full);
}

FK_TEST(BlendConcaveSmoothLoftEdge) {
    // Un raccordo concavo lungo il fianco termina sui coperchi del loft: i
    // due loro edge devono essere prolungati fino ai contatti, non accorciati
    // al vecchio vertice (caso dei fianchi guidati del documento applicativo).
    const Body loft = loftSolid({notchedAt(0.0, 1.0), notchedAt(4.0, 0.9, Vec2(0.1, 0.05)),
                                 notchedAt(8.0, 1.1, Vec2(-0.1, 0.0))}, false);
    const EdgeId edge = nearestEdge(loft, Vec3(0.55, 0.02, 4.0), 0.25);
    FK_CHECK(edge.valid());
    const double full = checkedVolume(loft, 1e-8);
    const double filleted = checkedVolume(blendEdges(loft, {edge}, 0.15, false), 1e-7);
    FK_CHECK(filleted > full);
}

FK_TEST(BlendSmoothLoftSplitSidePatches) {
    // Un lato della sezione e' suddiviso in pezze collineari. Variando il
    // punto di suddivisione tra le sezioni, il contatto del raccordo sul
    // coperchio attraversa le cuciture dei fianchi B-spline: deve trovare il
    // punto interno alla cucitura e non fermarsi al vecchio vertice.
    const Body loft = loftSolid({splitRectangleAt(0.0, 3.0, 2.0, -1.2),
                                 splitRectangleAt(4.0, 2.7, 1.8, 0.9, Vec2(0.35, -0.2)),
                                 splitRectangleAt(8.0, 2.4, 1.6, -0.6, Vec2(-0.15, 0.25))}, false);
    const std::vector<EdgeId> cap = contour(loft, planarFace(loft, Vec3(0, 0, 1)));
    FK_CHECK(cap.size() == 6);
    const double full = checkedVolume(loft, 1e-8);
    const double filleted = checkedVolume(blendEdges(loft, cap, 0.2, false), 1e-7);
    FK_CHECK(filleted > 0.0 && filleted < full);
}
