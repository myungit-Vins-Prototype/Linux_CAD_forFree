#include "fk_extrude.h"

#include <stdexcept>

#include "fk_curve_ops.h"
#include "fk_pcurve.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

// Un loop della faccia superiore durante la costruzione.
struct LoopPlan {
    std::vector<ProfileSegment> segments;  // nel verso del loop della faccia superiore
    std::vector<VertexId> bottom;          // vertice iniziale di ogni tratto, quota 0
    std::vector<VertexId> top;             // gli stessi alla quota dell'estrusione
    std::vector<FinId> fins;               // fin del loop superiore su ogni tratto (poi del fianco)
    LoopId loop;
};

// Spigoli e faccia laterale generati da un tratto.
struct SegmentPieces {
    EdgeId bottom, top, vertical;  // vertical: spigolo verticale all'inizio del tratto (se c'e')
    FaceId side;
};

// Catena di speroni a partire dalla fin `tip` (vedi mev): restituisce la fin
// che parte dall'ultimo vertice e aggiunge i nuovi vertici a `vertices`.
FinId extendChain(Body &body, FinId tip, const std::vector<Vec3> &points, std::size_t from, std::vector<VertexId> &vertices) {
    for (std::size_t i = from; i < points.size(); ++i) {
        const Body::MevResult spur = body.mev(tip, points[i]);
        vertices.push_back(spur.vertex);
        tip = body.edge(spur.edge).backward;
    }
    return tip;
}

// Estrusione traslazionale di un loop della faccia superiore. Con piu' tratti:
// uno sperone verticale da ogni vertice e un mef tra le cime consecutive (una
// faccia laterale per tratto). Con un solo tratto chiuso: sperone, mef chiuso
// in cima, kemr dello sperone e spostamento del loop di base nella faccia
// laterale, che resta con due loop e senza cucitura.
std::vector<SegmentPieces> sweepLoop(Body &body, LoopPlan &plan, const Vec3 &offset) {
    const std::size_t count = plan.segments.size();
    std::vector<SegmentPieces> pieces(count);
    plan.fins.resize(count);
    if (count == 1) plan.fins[0] = body.loop(plan.loop).first;
    else
        for (std::size_t j = 0; j < count; ++j) plan.fins[j] = body.findFin(plan.loop, plan.bottom[j]);
    for (std::size_t j = 0; j < count; ++j) pieces[j].bottom = body.fin(plan.fins[j]).edge;

    if (count == 1) {
        const Body::MevResult spur = body.mev(plan.fins[0], body.vertex(plan.bottom[0]).point + offset);
        const FinId down = body.edge(spur.edge).backward;
        const Body::MefResult cap = body.mef(down, down);
        body.kemr(spur.edge);
        body.kfmrh(body.mfkrh(plan.loop), cap.face);
        plan.top = {spur.vertex};
        pieces[0].top = cap.edge;
        pieces[0].side = cap.face;
        return pieces;
    }
    std::vector<FinId> down(count);
    for (std::size_t j = 0; j < count; ++j) {
        const Body::MevResult spur = body.mev(plan.fins[j], body.vertex(plan.bottom[j]).point + offset);
        plan.top.push_back(spur.vertex);
        down[j] = body.edge(spur.edge).backward;
        pieces[j].vertical = spur.edge;
    }
    for (std::size_t j = 0; j < count; ++j) {
        const Body::MefResult side = body.mef(down[j], body.findFin(plan.loop, plan.top[(j + 1) % count]));
        pieces[j].top = side.edge;
        pieces[j].side = side.face;
    }
    return pieces;
}

// Superficie della faccia laterale del tratto e verso rispetto alla normale
// uscente, che sta a destra del verso di percorrenza (materiale a sinistra).
void assignSideSurface(Face &face, const ProfileSegment &segment, const Frame3 &frame) {
    const double middle = 0.5 * (segment.range.lo + segment.range.hi);
    Vec2 d[2];
    segment.curve->evaluate(middle, 1, d);
    const Vec3 tangent = frame.directionToGlobal(Vec3(d[1].x(), d[1].y(), 0.0));
    const Vec3 outward = normalized(cross(tangent, frame.zDir()));
    const Vec3 point = frame.toGlobal(Vec3(d[0].x(), d[0].y(), 0.0));

    const Curve<2> *basis = segment.curve.get();
    while (basis->type() == CurveType::Trimmed) basis = static_cast<const TrimmedCurve<2> *>(basis)->basis().get();
    if (basis->type() == CurveType::Line) {
        face.surface = std::make_shared<Plane>(Frame3(point, outward, tangent));
        face.sense = true;
    } else if (basis->type() == CurveType::Circle) {
        const auto *circle = static_cast<const Circle<2> *>(basis);
        const Vec3 center = frame.toGlobal(Vec3(circle->center().x(), circle->center().y(), 0.0));
        const Vec3 xAxis = frame.directionToGlobal(Vec3(circle->xAxis().x(), circle->xAxis().y(), 0.0));
        face.surface = std::make_shared<CylindricalSurface>(Frame3(center, frame.zDir(), xAxis), circle->radius());
        face.sense = dot(point - center, outward) > 0.0;  // la normale del cilindro e' radiale uscente
    } else {
        // Normale di C(u) + v Z: C'(u) x Z, cioe' la normale uscente se il
        // tratto e' percorso nel verso del parametro (come qui).
        face.surface = std::make_shared<ExtrusionSurface>(embedCurve(segment.curve, frame, 0.0), frame.zDir());
        face.sense = dot(face.surface->normal(middle, 0.0), outward) > 0.0;
    }
}

}

Body makeExtrusion(const Frame3 &inputFrame, const ProfileRegion &region, double height) {
    if (!(std::fabs(height) > kLinearResolution)) throw std::invalid_argument("makeExtrusion: altezza nulla");
    if (region.outer.segments.empty()) throw std::invalid_argument("makeExtrusion: profilo vuoto");
    // Altezza negativa: stessa estrusione partendo dalla quota piu' bassa.
    const Frame3 frame = height > 0.0 ? inputFrame
        : Frame3(inputFrame.origin() + height * inputFrame.zDir(), inputFrame.zDir(), inputFrame.xDir());
    const double h = std::fabs(height);
    const Vec3 up = h * frame.zDir();
    auto at = [&](const Vec2 &p) { return frame.toGlobal(Vec3(p.x(), p.y(), 0.0)); };
    auto starts = [&](const std::vector<ProfileSegment> &segments) {
        std::vector<Vec3> points;
        for (const ProfileSegment &segment : segments) points.push_back(at(segment.start()));
        return points;
    };

    std::vector<LoopPlan> plans;
    Body body;

    // 1. Contorno esterno (antiorario): lamina con la faccia del mvfs (sara'
    //    il coperchio) e la faccia del mef di chiusura (la base).
    {
        LoopPlan plan;
        plan.segments = (signedArea(region.outer) > 0.0 ? region.outer : reversed(region.outer)).segments;
        const std::vector<Vec3> points = starts(plan.segments);
        const Body::MvfsResult start = body.mvfs(points[0]);
        plan.bottom.push_back(start.vertex);
        plan.loop = start.loop;
        if (points.size() == 1) {
            body.mef(start.loop);
        } else {
            const Body::MevResult firstEdge = body.mev(start.loop, points[1]);
            plan.bottom.push_back(firstEdge.vertex);
            const FinId tip = extendChain(body, body.edge(firstEdge.edge).backward, points, 2, plan.bottom);
            body.mef(tip, body.edge(firstEdge.edge).forward);
        }
        plans.push_back(std::move(plan));
    }
    const FaceId top = body.loop(plans[0].loop).face;
    FaceId bottom;
    for (FaceId f : body.faces())
        if (f != top) bottom = f;

    // 2. Fori (orari): ponte dal primo vertice del contorno, catena del foro,
    //    mef (faccia tappo), kemr del ponte (anello del coperchio), kfmrh del
    //    tappo (anello della base). La catena si costruisce nell'ordine
    //    q0, q_{m-1}, ..., q1 perche' il kemr lascia l'anello percorso q0, q1, ...
    for (const ProfileLoop &holeLoop : region.holes) {
        LoopPlan plan;
        plan.segments = (signedArea(holeLoop) < 0.0 ? holeLoop : reversed(holeLoop)).segments;
        const std::vector<Vec3> q = starts(plan.segments);
        const std::size_t m = q.size();
        const Body::MevResult bridge = body.mev(body.findFin(plans[0].loop, plans[0].bottom[0]), q[0]);
        const FinId holeStart = body.edge(bridge.edge).backward;
        std::vector<VertexId> chain{bridge.vertex};
        FaceId plug;
        if (m == 1) {
            plug = body.mef(holeStart, holeStart).face;
        } else {
            std::vector<Vec3> order{q[0]};
            for (std::size_t j = m - 1; j >= 1; --j) order.push_back(q[j]);
            const Body::MevResult holeEdge = body.mev(holeStart, order[1]);
            chain.push_back(holeEdge.vertex);
            const FinId tip = extendChain(body, body.edge(holeEdge.edge).backward, order, 2, chain);
            plug = body.mef(body.edge(holeEdge.edge).forward, tip).face;
        }
        plan.loop = body.kemr(bridge.edge);
        body.kfmrh(plug, bottom);
        plan.bottom.resize(m);
        plan.bottom[0] = chain[0];
        for (std::size_t j = 1; j < m; ++j) plan.bottom[j] = chain[m - j];
        plans.push_back(std::move(plan));
    }

    // 3. Estrusione di ogni loop del coperchio.
    std::vector<std::vector<SegmentPieces>> pieces;
    for (LoopPlan &plan : plans) pieces.push_back(sweepLoop(body, plan, up));

    // 4. Geometria.
    body.face(bottom).surface = std::make_shared<Plane>(Frame3(frame.origin(), -frame.zDir(), frame.xDir()));
    body.face(top).surface = std::make_shared<Plane>(Frame3(frame.origin() + up, frame.zDir(), frame.xDir()));
    for (std::size_t l = 0; l < plans.size(); ++l) {
        const LoopPlan &plan = plans[l];
        const std::size_t count = plan.segments.size();
        for (std::size_t j = 0; j < count; ++j) {
            const ProfileSegment &segment = plan.segments[j];
            const SegmentPieces &piece = pieces[l][j];
            // Base: la fin del fianco percorre il tratto nel verso del loop.
            if (!body.fin(plan.fins[j]).sense) body.reverseEdge(piece.bottom);
            body.edge(piece.bottom).curve = embedCurve(segment.curve, frame, 0.0);
            body.edge(piece.bottom).range = segment.range;
            // Coperchio: la fin del coperchio percorre il tratto nel verso del loop.
            if (body.finFace(body.edge(piece.top).forward) != top) body.reverseEdge(piece.top);
            body.edge(piece.top).curve = embedCurve(segment.curve, frame, h);
            body.edge(piece.top).range = segment.range;
            if (piece.vertical.valid()) {
                body.edge(piece.vertical).curve = std::make_shared<Line<3>>(body.vertex(plan.bottom[j]).point, frame.zDir());
                body.edge(piece.vertical).range = {0.0, h};
            }
            assignSideSurface(body.face(piece.side), segment, frame);
            // Vertici tolleranti dove i tratti si toccano solo entro la tolleranza dello schizzo.
            const double gap = distance(plan.segments[(j + count - 1) % count].end(), segment.start());
            if (gap > kLinearResolution) {
                body.vertex(plan.bottom[j]).tolerance = 1.01 * gap;
                body.vertex(plan.top[j]).tolerance = 1.01 * gap;
                // Lo spigolo verticale sta anche sulla faccia del tratto precedente.
                if (piece.vertical.valid()) body.edge(piece.vertical).tolerance = 1.01 * gap;
            }
        }
    }
    // SP-curve: tutte esatte (rette e curve del profilo sui piani,
    // isoparametriche sui fianchi).
    computePCurves(body);
    return body;
}


Body makeSheetExtrusion(const Frame3 &inputFrame, const std::vector<ProfileLoop> &chains, double height) {
    if (!(std::fabs(height) > kLinearResolution)) throw std::invalid_argument("makeSheetExtrusion: altezza nulla");
    const Frame3 frame = height > 0.0 ? inputFrame
        : Frame3(inputFrame.origin() + height * inputFrame.zDir(), inputFrame.zDir(), inputFrame.xDir());
    const double h = std::fabs(height);
    auto at = [&](const Vec2 &p, double z) { return frame.toGlobal(Vec3(p.x(), p.y(), z)); };
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    for (const ProfileLoop &chain : chains) {
        const std::size_t n = chain.segments.size();
        if (n == 0) continue;
        // Vertici in basso e in alto a ogni estremo di tratto (n + 1).
        const int base = int(vertices.size());
        std::vector<double> gaps(n + 1, 0.0);
        for (std::size_t i = 0; i <= n; ++i) {
            const Vec2 p = i < n ? chain.segments[i].start() : chain.segments[n - 1].end();
            if (i > 0 && i < n) gaps[i] = distance(chain.segments[i - 1].end(), p);
            vertices.push_back(at(p, 0.0));
            vertices.push_back(at(p, h));
        }
        auto bottom = [&](std::size_t i) { return base + 2 * int(i); };
        auto top = [&](std::size_t i) { return base + 2 * int(i) + 1; };
        const int verticalBase = int(edges.size());
        for (std::size_t i = 0; i <= n; ++i) {
            Body::BuildEdge vertical;
            vertical.start = bottom(i);
            vertical.end = top(i);
            vertical.curve = std::make_shared<Line<3>>(vertices[std::size_t(bottom(i))], frame.zDir());
            vertical.range = {0.0, h};
            vertical.tolerance = gaps[i] > kLinearResolution ? 1.01 * gaps[i] : 0.0;
            edges.push_back(vertical);
        }
        for (std::size_t j = 0; j < n; ++j) {
            const ProfileSegment &segment = chain.segments[j];
            Body::BuildEdge lower, upper;
            lower.start = bottom(j);
            lower.end = bottom(j + 1);
            lower.curve = embedCurve(segment.curve, frame, 0.0);
            lower.range = segment.range;
            upper.start = top(j);
            upper.end = top(j + 1);
            upper.curve = embedCurve(segment.curve, frame, h);
            upper.range = segment.range;
            lower.tolerance = upper.tolerance = std::max(gaps[j], gaps[j + 1]) > kLinearResolution ? 1.01 * std::max(gaps[j], gaps[j + 1]) : 0.0;
            const int lowerIndex = int(edges.size());
            edges.push_back(lower);
            edges.push_back(upper);
            // La faccia sta a sinistra: in basso nel verso del tratto, su
            // alla fine, in alto all'indietro, giu' all'inizio.
            Face face;
            assignSideSurface(face, segment, frame);
            Body::BuildFace built;
            built.surface = face.surface;
            built.sense = face.sense;
            auto fin = [](int edge, bool sense) {
                Body::BuildFin result;
                result.edge = edge;
                result.sense = sense;
                return result;
            };
            built.loops.push_back({fin(lowerIndex, true), fin(verticalBase + int(j) + 1, true), fin(lowerIndex + 1, false),
                                   fin(verticalBase + int(j), false)});
            faces.push_back(std::move(built));
        }
    }
    if (faces.empty()) throw std::invalid_argument("makeSheetExtrusion: nessun tratto");
    Body body = Body::buildSheet(vertices, edges, faces);
    computePCurves(body);
    return body;
}

}
