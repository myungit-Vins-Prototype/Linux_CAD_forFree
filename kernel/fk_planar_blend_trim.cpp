#include "fk_planar_blend_trim.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_history_tools.h"
#include "fk_offset_support.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_sheet.h"
#include "fk_surface.h"

namespace ForgeCad::Kernel::detail {
namespace {

FaceId adjacent(const Body &sheet, EdgeId id, FaceId face) {
    const Edge &edge = sheet.edge(id);
    for (FinId fin : {edge.forward, edge.backward})
        if (fin.valid() && sheet.finFace(fin) != face) return sheet.finFace(fin);
    return {};
}

FinId boundaryFin(const Body &body, EdgeId id) {
    const auto &edge = body.edge(id);
    return edge.forward.valid() ? edge.forward : edge.backward;
}

bool sameSpan(const Edge &a, const Edge &b) {
    return a.curve == b.curve && a.range.lo == b.range.lo && a.range.hi == b.range.hi;
}

bool containsBoundarySpan(const Body &body, const std::vector<EdgeId> &boundary, const Edge &span) {
    return std::any_of(boundary.begin(), boundary.end(), [&](EdgeId edge) { return sameSpan(body.edge(edge), span); });
}

CurvePtr<2> sectionPCurve(const Edge &edge, const SurfacePtr &surface, double tolerance) {
    if (edge.curve->type() != CurveType::Circle)
        return fitPCurveOnPlane(edge.curve, surface, edge.range, 0.1 * tolerance, tolerance);

    // Il taglio di un cilindro conserva il parametro angolare esatto.
    // Eccentricita' e residuo fuori piano si delimitano sui tre
    // coefficienti (costante, coseno, seno), senza campionare l'arco.
    const auto pcurve = exactPCurve(*surface, edge.curve, edge.range, tolerance);
    const auto *uv = dynamic_cast<const Circle<2> *>(pcurve.get());
    if (!uv) return {};
    const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
    const auto &frame = static_cast<const Plane &>(*surface).frame();
    const auto lift = [&](const Vec2 &v) { return v.x() * frame.xDir() + v.y() * frame.yDir(); };
    const double center = norm((circle.center() - frame.origin()) - lift(uv->center()));
    const double cosine = norm(circle.radius() * circle.xAxis() - lift(uv->radius() * uv->xAxis()));
    const double sine = norm(circle.radius() * circle.yAxis() - lift(uv->radius() * uv->yAxis()));
    const double padding = 64.0 * kMachineEpsilon *
        std::max({1.0, norm(circle.center()), norm(frame.origin()), norm(uv->center()), circle.radius()});
    return center + cosine + sine + padding <= tolerance ? pcurve : CurvePtr<2>{};
}

Body makeSectionCap(const Body &side, const std::vector<EdgeId> &section, SurfacePtr surface,
                    bool sense, double tolerance) {
    // Percorri il ciclo opposto alle fin della lamina. Gli estremi vengono
    // dalla topologia del taglio, senza riassociare le curve per vicinanza.
    std::map<int, FinId> outgoing;
    for (EdgeId edge : section) {
        const FinId fin = boundaryFin(side, edge);
        if (!outgoing.emplace(side.finEnd(fin).index, fin).second)
            throw std::domain_error("spessore: sezione del raccordo ramificata");
    }
    if (outgoing.empty())
        throw std::domain_error("spessore: sezione del raccordo vuota");
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFin> fins;
    const int first = outgoing.begin()->first;
    int start = first;
    for (std::size_t i = 0; i < section.size(); ++i) {
        const auto next = outgoing.find(start);
        if (next == outgoing.end())
            throw std::domain_error("spessore: sezione del raccordo non chiusa");
        const FinId fin = next->second;
        const auto &use = side.fin(fin);
        const auto &edge = side.edge(use.edge);
        const int from = int(i), to = int((i + 1) % section.size());
        const auto pcurve = sectionPCurve(edge, surface, tolerance);
        if (!pcurve)
            throw std::domain_error("spessore: bordo della chiusura planare non certificabile");
        points.push_back(side.vertex(VertexId{start}).point);
        edges.push_back({use.sense ? to : from, use.sense ? from : to, edge.curve, edge.range, edge.tolerance});
        fins.push_back({from, !use.sense, pcurve, tolerance});
        start = side.finStart(fin).index;
        outgoing.erase(next);
    }
    if (start != first)
        throw std::domain_error("spessore: sezione del raccordo non chiusa");
    return Body::buildSheet(points, edges, {{std::move(surface), sense, {std::move(fins)}}});
}

} // namespace

// Le fasce interne si contraggono una pezza alla volta. Ogni bordo
// trasversale deve unire due pezze contratte; la parete esterna conserva
// quindi tutti i bordi liberi originali.
PlanarBlendCollapse findPlanarBlendCollapse(const Body &sheet, double distance) {
    std::map<int, CollapsedPlanarBlend> collapsed;
    for (FaceId f : sheet.faces()) {
        cancellationPoint();
        if (auto blend = collapsedPlanarBlend(sheet, f, distance, linearResolution()))
            collapsed.emplace(f.index, *blend);
    }
    PlanarBlendCollapse layer;
    for (FaceId f : sheet.faces())
        if (!collapsed.count(f.index)) layer.faces.push_back(f);
    for (const auto &[face, blend] : collapsed) {
        for (EdgeId edge : blend.transverse) {
            const auto neighbor = collapsed.find(adjacent(sheet, edge, FaceId{face}).index);
            if (neighbor == collapsed.end() ||
                std::find(neighbor->second.transverse.begin(), neighbor->second.transverse.end(), edge) == neighbor->second.transverse.end())
                return {};
        }
        const auto a = adjacent(sheet, blend.planarRail, FaceId{face}), b = adjacent(sheet, blend.oppositeRail, FaceId{face});
        if (!a.valid() || !b.valid() || a == b || collapsed.count(a.index) || collapsed.count(b.index)) return {};
        layer.rails.emplace_back(blend.planarRail, blend.oppositeRail);
    }
    return layer;
}

PlanarBlendTrim findPlanarBlendTrim(const Body &sheet, double distance) {
    std::map<int, CollapsedPlanarBlend> band;
    for (FaceId face : sheet.faces()) {
        cancellationPoint();
        if (auto blend = overrunPlanarBlend(sheet, face, distance, linearResolution()))
            band.emplace(face.index, *blend);
    }
    if (band.empty()) return {};
    PlanarBlendTrim trim;
    std::set<int> planarRails;
    std::set<int> oppositeRails;
    for (const auto &[index, blend] : band) {
        const FaceId face{index};
        const auto plane = adjacent(sheet, blend.planarRail, face);
        const auto side = adjacent(sheet, blend.oppositeRail, face);
        if (!plane.valid() || !side.valid() || plane == side || band.count(side.index) ||
            (trim.support.valid() && trim.support != plane) ||
            !planarRails.insert(blend.planarRail.index).second ||
            !oppositeRails.insert(blend.oppositeRail.index).second)
            return {};
        trim.support = plane;
        trim.rails.push_back(blend.oppositeRail);
        for (EdgeId edge : blend.transverse) {
            const auto neighbor = band.find(adjacent(sheet, edge, face).index);
            if (neighbor == band.end() ||
                std::find(neighbor->second.transverse.begin(), neighbor->second.transverse.end(), edge) == neighbor->second.transverse.end())
                return {};
        }
    }
    const auto &plane = sheet.face(trim.support);
    if (plane.loops.size() != 1) return {};
    const auto arcs = sheet.loopFins(plane.loops.front());
    if (arcs.size() != planarRails.size() || std::any_of(arcs.begin(), arcs.end(), [&](FinId fin) {
            return !planarRails.count(sheet.fin(fin).edge.index);
        }))
        return {};
    for (FaceId face : sheet.faces())
        if (face != trim.support && !band.count(face.index)) trim.faces.push_back(face);
    return trim;
}

OffsetResult offsetWithPlanarBlendTrim(const Body &sheet, const PlanarBlendTrim &trim, double distance,
                                       double accuracy, double tolerance, double joinAngle) {
    std::vector<EdgeId> rails;
    auto offset = offsetWithExposedRails(sheet, trim.faces, distance, accuracy, joinAngle, trim.rails, rails);
    const auto &support = sheet.face(trim.support);
    const auto &frame = static_cast<const Plane &>(*support.surface).frame();
    const auto normal = frame.zDir();
    const auto origin = frame.origin() + (support.sense ? distance : -distance) * normal;
    const auto sectionFrame = Frame3::fromAxes(origin, frame.xDir(), frame.yDir(), normal);
    double reach = 0.0;
    for (VertexId v : offset.body.vertices()) reach = std::max(reach, norm(offset.body.vertex(v).point - origin));
    const auto tool = makePlaneSheet(sectionFrame, 2.0 * reach);
    std::set<int> affected;
    std::set<int> railIds;
    for (EdgeId rail : rails) {
        affected.insert(offset.body.finFace(boundaryFin(offset.body, rail)).index);
        railIds.insert(rail.index);
    }
    std::vector<EdgeId> exterior;
    std::set<const Curve<3> *> sourceCurves;
    for (EdgeId edge : offset.body.edges()) {
        sourceCurves.insert(offset.body.edge(edge).curve.get());
        if (offset.body.isLaminar(edge) && !railIds.count(edge.index)) exterior.push_back(edge);
    }

    std::vector<BodyHistory> histories;
    auto pieces = splitSheet(offset.body, tool, {tolerance, false}, &histories);
    // Il pezzo conservato mantiene ogni faccia superstite una sola volta.
    // Il taglio non deve interessare altre facce né il bordo globale.
    const auto originalFaces = offset.body.faces();
    std::vector<std::vector<EdgeId>> boundaries;
    int kept = -1;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto &piece = pieces[i];
        const auto &history = histories[i];
        auto &boundary = boundaries.emplace_back();
        for (EdgeId edge : piece.edges())
            if (piece.isLaminar(edge)) boundary.push_back(edge);
        const bool full = std::all_of(originalFaces.begin(), originalFaces.end(), [&](FaceId f) {
            return history.descendants(EntityKind::Face, 0, f.index).size() == 1;
        });
        const bool sameExterior = std::all_of(exterior.begin(), exterior.end(), [&](EdgeId old) {
            return containsBoundarySpan(piece, boundary, offset.body.edge(old));
        });
        if (full && sameExterior) {
            if (kept >= 0)
                throw std::domain_error("spessore: rifilatura del raccordo con piu' regioni complete");
            kept = int(i);
        }
    }
    if (kept < 0 || pieces.size() < 2)
        throw std::domain_error("spessore: piano del raccordo senza rifilatura completa");
    std::set<int> discardedRails;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        for (FaceId face : pieces[i].faces()) {
            const auto &origins = histories[i].origins(EntityKind::Face, face.index);
            if (origins.size() != 1 || origins.front().operand != 0 ||
                origins.front().kind != EntityKind::Face ||
                (!affected.count(origins.front().index) &&
                 (int(i) != kept || origins.front().relation != HistoryRelation::Same)))
                throw std::domain_error("spessore: il piano del raccordo modifica facce estranee alla fascia");
        }
        if (int(i) == kept) continue;
        for (EdgeId rail : rails)
            if (containsBoundarySpan(pieces[i], boundaries[i], offset.body.edge(rail)))
                discardedRails.insert(rail.index);
    }
    if (discardedRails.size() != rails.size())
        throw std::domain_error("spessore: il taglio non elimina tutti i bordi del raccordo");
    const auto &side = pieces[std::size_t(kept)];
    const auto &sideHistory = histories[std::size_t(kept)];
    std::set<int> retained;
    std::vector<EdgeId> section;
    for (EdgeId id : boundaries[std::size_t(kept)]) {
        const auto &edge = side.edge(id);
        const auto found = std::find_if(exterior.begin(), exterior.end(), [&](EdgeId old) {
            return sameSpan(edge, offset.body.edge(old));
        });
        if (found != exterior.end()) {
            if (!retained.insert(found->index).second)
                throw std::domain_error("spessore: la rifilatura duplica un bordo originale");
            continue;
        }
        const auto &origins = sideHistory.origins(EntityKind::Face, side.finFace(boundaryFin(side, id)).index);
        if (!affected.count(origins.front().index) || sourceCurves.count(edge.curve.get()))
            throw std::domain_error("spessore: rimane un bordo della fascia dopo la rifilatura");
        bool shared = false;
        for (std::size_t i = 0; i < pieces.size() && !shared; ++i)
            if (int(i) != kept) shared = containsBoundarySpan(pieces[i], boundaries[i], edge);
        if (!shared)
            throw std::domain_error("spessore: bordo di sezione senza riscontro nella fascia rimossa");
        section.push_back(id);
    }
    if (retained.size() != exterior.size())
        throw std::domain_error("spessore: la rifilatura interrompe il bordo originale");
    auto cap = makeSectionCap(side, section, std::make_shared<Plane>(sectionFrame), support.sense, tolerance);
    auto sideOrigins = makeHistory({&sheet}, side);
    for (FaceId face : side.faces()) {
        const auto &source = sideHistory.origins(EntityKind::Face, face.index).front();
        sideOrigins.declareOrigins(EntityKind::Face, face.index, offset.history.origins(EntityKind::Face, source.index));
    }
    const auto capOrigins = generatedFaces(sheet, cap, {{0, EntityKind::Face, trim.support.index, HistoryRelation::Modified}});
    BodyHistory sewnHistory;
    auto sewn = sewSheets({&side, &cap}, tolerance, false, &sewnHistory);
    if (sewn.freeEdges != int(exterior.size()) || sewn.body.faces().size() != side.faces().size() + 1 ||
        sewn.body.shells().size() != side.shells().size())
        throw std::domain_error("spessore: chiusura incompleta della sezione del raccordo");
    offset.history = transportHistory(sewnHistory, {&sideOrigins, &capOrigins});
    offset.body = std::move(sewn.body);
    return offset;
}

}
