#include <array>
#include <cmath>

#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_sheet.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_sew.h"
#include "fk_test.h"
#include "fk_thicken.h"

using namespace ForgeCad::Kernel;

namespace {

void checkSolid(const Body &body) {
    FK_CHECK(!body.isSheet());
    for (const CheckIssue &issue : checkBody(body)) fktest::reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
}

// Faccia della scatola [0, 10] x [0, 6] x [0, 4] con la normale data.
FaceId boxFace(const Body &box, const Vec3 &normal) {
    for (FaceId f : box.faces()) {
        const Frame3 &frame = static_cast<const Plane &>(*box.face(f).surface).frame();
        const Vec3 n = box.face(f).sense ? frame.zDir() : -1.0 * frame.zDir();
        if (dot(n, normal) > 0.999) return f;
    }
    return FaceId();
}

// Lamina cilindrica: arco di raggio R (angoli a..b attorno all'asse X, 0 lungo
// +Y e pi/2 lungo +Z) estruso per L lungo X.
Body arcSheet(double R, double a, double b, double L) {
    const Frame3 frame(Vec3(), Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0));
    ProfileLoop chain;
    chain.segments.push_back({std::make_shared<Circle<2>>(makeCircle(Vec2(0.0, 0.0), R)), {a, b}});
    return makeSheetExtrusion(frame, {chain}, L);
}

double zRange(const Body &body, bool upper) {
    double value = upper ? -1e300 : 1e300;
    for (VertexId v : body.vertices()) value = upper ? std::max(value, body.vertex(v).point.z()) : std::min(value, body.vertex(v).point.z());
    return value;
}

}

// Faccia piana: volume esatto e lato dello spessore (normale, opposto, meta' per parte).
FK_TEST(ThickenPlanarFaceSides) {
    const Body box = makeBox(Frame3(), 10.0, 6.0, 4.0);
    const FaceId top = boxFace(box, Vec3(0.0, 0.0, 1.0));
    FK_CHECK(top.valid());
    ThickenOptions options;
    options.thickness = 2.0;
    const Body up = thickenSheet(box, {top}, options);
    checkSolid(up);
    FK_CHECK_NEAR(massProperties(up).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(up, false), 4.0, 1e-12);
    FK_CHECK_NEAR(zRange(up, true), 6.0, 1e-12);
    options.side = ThickenSide::Backward;
    const Body down = thickenSheet(box, {top}, options);
    checkSolid(down);
    FK_CHECK_NEAR(massProperties(down).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(down, false), 2.0, 1e-12);
    options.side = ThickenSide::Both;
    const Body both = thickenSheet(box, {top}, options);
    checkSolid(both);
    FK_CHECK_NEAR(massProperties(both).volume, 120.0, 1e-9);
    FK_CHECK_NEAR(zRange(both, false), 3.0, 1e-12);
    FK_CHECK_NEAR(zRange(both, true), 5.0, 1e-12);
    options.thickness = 0.0;
    FK_CHECK_THROWS(thickenSheet(box, {top}, options));
    // Tutte le facce della scatola: superficie chiusa, niente bordi liberi.
    options.thickness = 1.0;
    FK_CHECK_THROWS(thickenSheet(box, {}, options));
}

// Superficie curva lungo la normale: settore di anello, V = theta R t L (meta' per parte).
FK_TEST(ThickenCylindricalSheetAlongNormal) {
    const double R = 5.0, L = 8.0, t = 0.6, a = 0.3, b = 2.1;
    const Body sheet = arcSheet(R, a, b, L);
    ThickenOptions options;
    options.thickness = t;
    options.side = ThickenSide::Both;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, (b - a) * R * t * L, 1e-7);
    // Un lato solo: anello tra R e R + t o tra R - t e R, secondo il verso della normale.
    options.side = ThickenSide::Forward;
    const double forward = massProperties(thickenSheet(sheet, {}, options)).volume;
    options.side = ThickenSide::Backward;
    const double backward = massProperties(thickenSheet(sheet, {}, options)).volume;
    const double outer = 0.5 * (b - a) * ((R + t) * (R + t) - R * R) * L, inner = 0.5 * (b - a) * (R * R - (R - t) * (R - t)) * L;
    FK_CHECK(std::fabs(forward - outer) < 1e-7 || std::fabs(forward - inner) < 1e-7);
    FK_CHECK_NEAR(forward + backward, outer + inner, 1e-7);
    // Verso l'asse oltre il raggio: la superficie a distanza degenera.
    options.thickness = 2.0 * R;
    options.side = ThickenSide::Both;
    bool diagnosed = false;
    try {
        thickenSheet(sheet, {}, options);
    } catch (const OffsetCurvatureError &failure) {
        diagnosed = true;
        FK_CHECK_NEAR(failure.radius, R, 1e-12);
        FK_CHECK(failure.sourcePoint.has_value());
        if (failure.sourcePoint)
            FK_CHECK(classifyPointOnFace(sheet, sheet.faces().front(), *failure.sourcePoint, 1e-7) == PointLocation::Boundary);
    }
    FK_CHECK(diagnosed);
}

// Lungo una direzione: volume = area proiettata sul piano normale alla direzione x spessore.
FK_TEST(ThickenAlongDirection) {
    const double R = 5.0, L = 8.0, t = 1.5;
    const Body cap = arcSheet(R, M_PI / 6.0, 5.0 * M_PI / 6.0, L);  // calotta: n . Z lontano da zero
    ThickenOptions options;
    options.thickness = t;
    options.useDirection = true;
    options.direction = Vec3(0.0, 0.0, 1.0);
    const Body solid = thickenSheet(cap, {}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, L * R * std::sqrt(3.0) * t, 1e-7);
    options.side = ThickenSide::Both;
    const Body both = thickenSheet(cap, {}, options);
    checkSolid(both);
    FK_CHECK_NEAR(massProperties(both).volume, L * R * std::sqrt(3.0) * t, 1e-7);
    // Direzione obliqua: conta solo la componente lungo la normale media.
    options.side = ThickenSide::Forward;
    options.direction = Vec3(0.3, 0.0, 1.0);
    FK_CHECK_NEAR(massProperties(thickenSheet(cap, {}, options)).volume, L * R * std::sqrt(3.0) * t / std::sqrt(1.09), 1e-7);
    // Oltre i fianchi la superficie si ripiega rispetto a Z: errore.
    const Body folded = arcSheet(R, -0.2, M_PI + 0.2, L);
    options.direction = Vec3(0.0, 0.0, 1.0);
    FK_CHECK_THROWS(thickenSheet(folded, {}, options));
}

// Spigolo vivo: lungo la normale e' rifiutato, lungo una direzione che vede
// entrambe le facce funziona (V = somma delle aree proiettate x spessore).
FK_TEST(ThickenSharpCreaseAlongDirection) {
    const Body box = makeBox(Frame3(), 10.0, 6.0, 4.0);
    const FaceId top = boxFace(box, Vec3(0.0, 0.0, 1.0)), front = boxFace(box, Vec3(0.0, -1.0, 0.0));
    FK_CHECK(top.valid() && front.valid());
    ThickenOptions options;
    options.thickness = 1.0;
    bool diagnosed = false;
    try {
        thickenSheet(box, {top, front}, options);
    } catch (const ThickenCreaseError &failure) {
        diagnosed = true;
        FK_CHECK(failure.curve != nullptr);
        if (failure.curve)
            for (double t : {failure.range.lo, failure.range.hi}) {
                const Vec3 point = failure.curve->point(t);
                FK_CHECK_NEAR(point.y(), 0.0, 1e-12);
                FK_CHECK_NEAR(point.z(), 4.0, 1e-12);
            }
    }
    FK_CHECK(diagnosed);
    options.useDirection = true;
    options.direction = Vec3(0.0, -1.0, 1.0);
    const Body solid = thickenSheet(box, {top, front}, options);
    checkSolid(solid);
    FK_CHECK_NEAR(massProperties(solid).volume, (60.0 + 40.0) / std::sqrt(2.0), 1e-9);
}

// Faccia rifilata (un foro) su una B-spline con una piega di 0,5 gradi lungo
// un nodo C0 (come lo sweep di Mouse.prt lungo una spline solo C1): l'offset
// divide la faccia lungo l'isoparametrica della piega e lo spessore lungo la
// normale si chiude in un solido (pieghe fino a 2 gradi assorbite nella cucitura).
FK_TEST(ThickenTrimmedFaceWithInternalCrease) {
    const double rise = 5.0 * std::tan(0.5 * M_PI / 180.0);
    const std::vector<Vec3> poles{{0, 0, 0}, {0, 5, 0}, {0, 10, rise}, {10, 0, 0}, {10, 5, 0}, {10, 10, rise}};
    const auto surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 2, 2}, 2, 3, poles);
    const std::vector<Vec3> corners{surface->point(0, 0), surface->point(1, 0), surface->point(1, 2), surface->point(0, 2)};
    const std::vector<Body::BuildEdge> edges{{0, 1, surface->vIso(0.0), {0, 1}, 0}, {1, 2, surface->uIso(1.0), {0, 2}, 0},
                                             {3, 2, surface->vIso(2.0), {0, 1}, 0}, {0, 3, surface->uIso(0.0), {0, 2}, 0}};
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0)), 0}, {1, true, std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)), 0},
                   {2, false, std::make_shared<Line<2>>(Vec2(0, 2), Vec2(1, 0)), 0}, {3, false, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)), 0}}};
    const Body plate = Body::buildSheet(corners, edges, {face});
    // Foro cilindrico lontano dalla piega: la faccia non e' piu' il rettangolo (u, v).
    const Body drill = makeCylinder(Frame3(Vec3(3.0, 2.5, -5.0), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0)), 1.2, 10.0);
    const Body sheet = trimSheet(plate, drill, Vec3(8.0, 8.0, 0.1));
    FK_CHECK(sheet.faces().size() == 1);
    double area = 0.0;
    for (FaceId f : sheet.faces()) area += faceArea(sheet, f, 1e-11);
    // L'offset (anche senza unire le pieghe) divide la faccia sulla piega invece di fallire.
    const OffsetResult offset = offsetFaces(sheet, sheet.faces(), 0.4);
    FK_CHECK(offset.body.faces().size() == 2);
    ThickenOptions options;
    options.thickness = 0.4;
    options.side = ThickenSide::Both;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    FK_CHECK(std::fabs(massProperties(solid).volume - area * 0.4) < 1e-3 * area * 0.4);
}

// B-spline liscia senza pieghe: striscia parabolica sviluppabile. Per lo
// spessore simmetrico il volume e' esattamente area della lamina x spessore.
FK_TEST(ThickenSmoothBSplineSheet) {
    const auto surface = std::make_shared<BSplineSurface>(2, 1,
        std::vector<double>{0, 0, 0, 1, 1, 1}, std::vector<double>{0, 0, 1, 1}, 3, 2,
        std::vector<Vec3>{{-2, 0, 1}, {-2, 3, 1}, {0, 0, -1}, {0, 3, -1}, {2, 0, 1}, {2, 3, 1}});
    const std::vector<Vec3> corners{surface->point(0, 0), surface->point(1, 0), surface->point(1, 1), surface->point(0, 1)};
    const std::vector<Body::BuildEdge> edges{
        {0, 1, surface->vIso(0), {0, 1}, 0}, {1, 2, surface->uIso(1), {0, 1}, 0},
        {3, 2, surface->vIso(1), {0, 1}, 0}, {0, 3, surface->uIso(0), {0, 1}, 0}};
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0)), 0},
                   {1, true, std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)), 0},
                   {2, false, std::make_shared<Line<2>>(Vec2(0, 1), Vec2(1, 0)), 0},
                   {3, false, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)), 0}}};
    const Body sheet = Body::buildSheet(corners, edges, {face});
    ThickenOptions options;
    options.thickness = 0.2;
    options.offsetTolerance = 1e-7;
    options.side = ThickenSide::Both;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    const double area = 6.0 * (std::sqrt(2.0) + std::asinh(1.0));
    FK_CHECK_NEAR(massProperties(solid).volume, area * options.thickness, 1e-5);
}

namespace {

// Tubo aperto a z=0, con tappo piano a z=4. Il raccordo di raggio 2
// è composto da quattro pezze razionali con sezioni a quarto di cerchio.
Body roundedTube(double bump) {
    constexpr int sectors = 4;
    constexpr double round = 2.0, outer = 6.0, axisRadius = outer - round;
    const std::array<Vec3, sectors> radial{{{1,0,0}, {0,1,0}, {-1,0,0}, {0,-1,0}}};
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    for (const Vec3 &direction : radial)
        vertices.insert(vertices.end(), {outer * direction, outer * direction + Vec3(0,0,2),
                                         axisRadius * direction + Vec3(0,0,4)});

    const auto uvLine = [](double u, double v, bool alongU) {
        return std::make_shared<Line<2>>(Vec2(u,v), alongU ? Vec2(1,0) : Vec2(0,1));
    };
    const std::vector<double> knots{0,0,0,1,1,1};
    const std::array<double, 3> weights{{1.0, std::sqrt(0.5), 1.0}};
    const std::array<double, 3> radii{{outer, outer, axisRadius}};
    const std::array<double, 3> heights{{2,4,4}};

    Body::BuildFace cap;
    cap.surface = std::make_shared<Plane>(Frame3(Vec3(0,0,4), Vec3(0,0,1), Vec3(1,0,0)));
    cap.loops.emplace_back();
    const auto cylinder = std::make_shared<CylindricalSurface>(Frame3(), outer);
    for (int i = 0; i < sectors; ++i) {
        const int next = (i + 1) % sectors;
        const std::array<Vec3, 3> direction{{radial[i], radial[i] + radial[next], radial[next]}};
        BSplineCurve<3> longitude(2, knots, {direction.begin(), direction.end()},
                                          {weights.begin(), weights.end()});
        const bool perturb = i == 0 && bump != 0.0;
        if (i == 0)
            for (double u : {0.09, 0.12}) longitude = longitude.insertKnot(u, 2);
        std::vector<Vec3> poles;
        std::vector<double> patchWeights;
        for (int u = 0; u < longitude.poleCount(); ++u)
            for (int v = 0; v < 3; ++v) {
                Vec3 pole = radii[v] * longitude.poles()[u] + Vec3(0,0,heights[v]);
                if (perturb && u == 3 && v == 1) pole += Vec3(0,0,bump);
                poles.push_back(pole);
                patchWeights.push_back(longitude.weight(u) * weights[v]);
            }
        const auto blend = std::make_shared<BSplineSurface>(2, 2, longitude.knots(), knots,
                                                          longitude.poleCount(), 3, poles, patchWeights);

        // Bordi: base libera, contatto col cilindro, contatto col tappo,
        // poi i due bordi trasversali all'inizio del settore.
        edges.insert(edges.end(), {
            {3*i, 3*next, std::make_shared<Circle<3>>(makeCircle(Frame3(), outer)),
             {i * M_PI/2, (i + 1) * M_PI/2}},
            {3*i+1, 3*next+1, blend->vIso(0), {0,1}},
            {3*i+2, 3*next+2, blend->vIso(1), {0,1}},
            {3*i, 3*i+1, std::make_shared<Line<3>>(vertices[3*i], Vec3(0,0,1)), {0,2}},
            {3*i+1, 3*i+2, blend->uIso(0), {0,1}}
        });
        faces.push_back({cylinder, true, {{{5*i,true}, {5*next+3,true},
                                           {5*i+1,false}, {5*i+3,false}}}});
        faces.push_back({blend, true, {{{5*i+1,true,uvLine(0,0,true)},
                                        {5*next+4,true,uvLine(1,0,false)},
                                        {5*i+2,false,uvLine(0,1,true)},
                                        {5*i+4,false,uvLine(0,0,false)}}}});
        cap.loops.front().push_back({5*i+2,true});
    }
    faces.push_back(std::move(cap));
    Body sheet = Body::buildSheet(vertices, edges, faces);
    FK_CHECK(computePCurves(sheet) == 0);
    return sheet;
}

}

// A spessore 2 le quattro pezze diventano uno spigolo circolare. Il vuoto
// interno è un cilindro di raggio 4 e altezza 2. Sotto tale spessore il
// raccordo ha ancora area su entrambe le pelli.
FK_TEST(ThickenExactToroidalRoundOverCollapse) {
    const Body sheet = roundedTube(0.0);
    ThickenOptions options;
    options.thickness = 2.0;
    options.side = ThickenSide::Backward;
    const Body solid = thickenSheet(sheet, {}, options);
    checkSolid(solid);
    FK_CHECK(solid.shells().size() == 1);
    for (EdgeId edge : solid.edges()) FK_CHECK(!solid.isLaminar(edge));
    FK_CHECK_NEAR(massProperties(solid).volume, M_PI * (232.0 / 3.0 + 8.0 * M_PI), 2e-5);

    options.thickness -= 1e-3;
    const Body near = thickenSheet(sheet, {}, options);
    checkSolid(near);
    FK_CHECK(near.faces().size() == solid.faces().size() + 4);
}

// Spostare un polo interno conserva i quattro bordi esatti. Il supporto
// [0,09; 0,12] cade tra le sezioni ai quarti; le sezioni in tale intervallo
// non hanno più un raggio comune.
FK_TEST(ThickenRejectsNonCircularRoundOverWithCircularEnds) {
    const Body sheet = roundedTube(-3e-6);
    ThickenOptions options;
    options.thickness = 2.0;
    options.side = ThickenSide::Backward;
    bool diagnosed = false;
    try {
        thickenSheet(sheet, {}, options);
    } catch (const OffsetCurvatureError &) {
        diagnosed = true;
    }
    FK_CHECK(diagnosed);
}

// Oltre il raggio, il fondo piano interno interseca il cilindro
// a z = 4 - spessore e a distanza 6 - spessore dall'asse.
FK_TEST(ThickenToroidalRoundOverPastRadius) {
    const Body sheet = roundedTube(0.0);
    ThickenOptions options;
    options.side = ThickenSide::Backward;
    for (double thickness : {2.00001, 2.01, 2.1, 2.5}) {
        options.thickness = thickness;
        const Body solid = thickenSheet(sheet, {}, options);
        checkSolid(solid);
        FK_CHECK(checkBody(solid, {true, true}).empty());
        FK_CHECK(solid.shells().size() == 1);
        const double innerRadius = 6.0 - thickness;
        const double innerHeight = 4.0 - thickness;
        const double outerVolume = M_PI * (328.0 / 3.0 + 8.0 * M_PI);
        FK_CHECK_NEAR(massProperties(solid).volume,
                      outerVolume - M_PI * innerRadius * innerRadius * innerHeight, 2e-5);
        int joins = 0;
        for (EdgeId id : solid.edges()) {
            const bool laminar = solid.isLaminar(id);
            FK_CHECK(!laminar);
            if (laminar) continue;
            const auto &edge = solid.edge(id);
            const auto &forward = solid.face(solid.finFace(edge.forward));
            const auto &backward = solid.face(solid.finFace(edge.backward));
            const auto forwardType = forward.surface->type();
            const auto backwardType = backward.surface->type();
            if (!((forwardType == SurfaceType::Plane && backwardType == SurfaceType::Cylinder) ||
                  (forwardType == SurfaceType::Cylinder && backwardType == SurfaceType::Plane)))
                continue;
            const Vec3 midpoint = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
            if (std::abs(midpoint.z()) < 1e-7) continue; // Bordo di apertura.
            FK_CHECK_NEAR(midpoint.z(), innerHeight, 1e-7);
            FK_CHECK_NEAR(std::hypot(midpoint.x(), midpoint.y()), innerRadius, 1e-7);
            ++joins;
        }
        FK_CHECK(joins == 4);
    }
}

FK_TEST(ThickenRejectsOverTrimPastOpenRim) {
    const Body sheet = roundedTube(0.0);
    ThickenOptions options;
    options.thickness = 4.01; // Il piano interno finirebbe sotto l'apertura a z = 0.
    options.side = ThickenSide::Backward;
    bool diagnosed = false;
    try {
        thickenSheet(sheet, {}, options);
    } catch (const OffsetCurvatureError &) {
        diagnosed = true;
    }
    FK_CHECK(diagnosed);
}

FK_TEST(OffsetSewnSheetRecoversToroidalRoundOver) {
    const Body sheet = roundedTube(0.0);
    for (double distance : {-2.0, -2.01, -2.5}) {
        const OffsetResult offset = offsetFaces(sheet, sheet.faces(), distance, 1e-7, true);
        FK_CHECK(offset.body.isSheet());
        FK_CHECK(offset.shells == 1);
        FK_CHECK(offset.body.faces().size() == 5);
        FK_CHECK(checkBody(offset.body, {true, true}).empty());
        int free = 0;
        for (EdgeId edge : offset.body.edges()) {
            if (!offset.body.isLaminar(edge)) continue;
            ++free;
            const auto &data = offset.body.edge(edge);
            FK_CHECK_NEAR(data.curve->point(0.5 * (data.range.lo + data.range.hi)).z(), 0.0, 1e-7);
        }
        FK_CHECK(free == 4);
    }
    auto partial = sheet.faces();
    partial.pop_back();
    for (const auto &selection : {partial, sheet.faces()}) {
        bool diagnosed = false;
        try {
            offsetFaces(sheet, selection, -2.01, 1e-7, selection.size() == partial.size());
        } catch (const OffsetCurvatureError &) {
            diagnosed = true;
        }
        FK_CHECK(diagnosed);
    }
}
