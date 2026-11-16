#include "fk_open_offset.h"
#include "fk_history_tools.h"
#include "fk_planar.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_bspline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <iterator>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
struct LoopData {
    std::vector<PathSegment> curves;
    std::vector<HistoryLink> origins;
};
struct Patch {
    Body body;
    Frame3 frame;
    std::vector<HistoryLink> origins;
    bool closure = false;
};
struct Layer {
    Body body;
    BodyHistory history;
    std::set<int> closures;
};
void uniqueOrigins(std::vector<HistoryLink> &links) {
    const auto key = [](const HistoryLink &l) { return std::make_tuple(l.operand, int(l.kind), l.index, int(l.relation)); };
    std::sort(links.begin(), links.end(), [&](const auto &a, const auto &b) { return key(a) < key(b); });
    links.erase(std::unique(links.begin(), links.end(), [&](const auto &a, const auto &b) { return key(a) == key(b); }), links.end());
}
bool samePlane(const Frame3 &a, const Frame3 &b, double tolerance) {
    return norm(cross(a.zDir(), b.zDir())) <= angularResolution() && std::abs(dot(a.zDir(), b.origin() - a.origin())) <= tolerance;
}
std::vector<LoopData> boundaryLoops(const Body &body, const BodyHistory &ancestry) {
    std::set<int> remaining;
    std::map<int, int> valence;
    for (auto e : body.edges())
        if (body.isLaminar(e)) {
            remaining.insert(e.index);
            ++valence[body.edgeStart(e).index];
            ++valence[body.edgeEnd(e).index];
        }
    for (const auto &[vertex, n] : valence) {
        (void)vertex;
        if (n != 2)
            throw std::domain_error("offset aperto: bordo ramificato o non chiuso");
    }
    std::vector<LoopData> loops;
    while (!remaining.empty()) {
        LoopData loop;
        std::set<int> vertices{body.edgeStart(EdgeId{*remaining.begin()}).index};
        bool added = true;
        while (added) {
            added = false;
            for (auto it = remaining.begin(); it != remaining.end();) {
                const EdgeId e{*it};
                const auto &edge = body.edge(e);
                if (!vertices.count(body.edgeStart(e).index) && !vertices.count(body.edgeEnd(e).index)) {
                    ++it;
                    continue;
                }
                vertices.insert(body.edgeStart(e).index);
                vertices.insert(body.edgeEnd(e).index);
                loop.curves.push_back({edge.curve, edge.range});
                const auto face = body.finFace(edge.forward.valid() ? edge.forward : edge.backward);
                for (auto origin : ancestry.origins(EntityKind::Face, face.index)) {
                    origin.relation = HistoryRelation::Generated;
                    loop.origins.push_back(origin);
                }
                it = remaining.erase(it);
                added = true;
            }
        }
        uniqueOrigins(loop.origins);
        loops.push_back(std::move(loop));
    }
    return loops;
}
std::vector<Patch> planarPatches(const std::vector<LoopData> &loops, double tolerance) {
    struct Group {
        Frame3 frame;
        std::vector<std::vector<PathSegment>> loops;
        std::vector<HistoryLink> origins;
    };
    std::vector<Group> groups;
    for (const auto &loop : loops) {
        const auto single = planarSheet({loop.curves}, tolerance);
        if (single.faces().size() != 1)
            throw std::domain_error("offset aperto: apertura planare ambigua");
        const auto frame = static_cast<const Plane &>(*single.face(single.faces().front()).surface).frame();
        auto group = std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return samePlane(g.frame, frame, tolerance); });
        if (group == groups.end()) {
            groups.push_back({frame, {}, {}});
            group = std::prev(groups.end());
        }
        group->loops.push_back(loop.curves);
        group->origins.insert(group->origins.end(), loop.origins.begin(), loop.origins.end());
    }
    std::vector<Patch> patches;
    for (auto &group : groups) {
        uniqueOrigins(group.origins);
        patches.push_back({planarSheet(group.loops, tolerance), group.frame, std::move(group.origins), false});
    }
    return patches;
}
std::set<int> mapClosures(const BodyHistory &stage, const std::vector<std::set<int>> &sources) {
    std::set<int> result;
    for (int i = 0; i < stage.resultSize(EntityKind::Face); ++i) {
        const auto &links = stage.origins(EntityKind::Face, i);
        if (!links.empty() && std::all_of(links.begin(), links.end(), [&](const HistoryLink &l) {
                return l.kind == EntityKind::Face && sources.at(std::size_t(l.operand)).count(l.index);
            }))
            result.insert(i);
    }
    return result;
}
Layer sewLayer(const Body &root, const Layer &main, const std::vector<Patch> &patches, double connection, bool closed) {
    std::vector<BodyHistory> histories;
    histories.reserve(patches.size());
    std::vector<const Body *> parts{&main.body};
    std::vector<const BodyHistory *> sources{&main.history};
    std::vector<std::set<int>> markers{main.closures};
    for (const auto &patch : patches) {
        parts.push_back(&patch.body);
        histories.push_back(generatedFaces(root, patch.body, patch.origins));
        sources.push_back(&histories.back());
        std::set<int> mark;
        if (patch.closure)
            for (auto f : patch.body.faces())
                mark.insert(f.index);
        markers.push_back(std::move(mark));
    }
    BodyHistory stage;
    auto sewn = sewSheets(parts, connection, false, &stage);
    if (closed && !sewn.closed)
        throw std::domain_error("offset aperto: le chiusure planari non chiudono la lamina");
    auto mark = mapClosures(stage, markers);
    return {std::move(sewn.body), transportHistory(stage, sources), std::move(mark)};
}
Layer capped(const Body &sheet, double connection) {
    Layer main{sheet, indexHistory(sheet, sheet, HistoryRelation::Same), {}};
    auto patches = planarPatches(boundaryLoops(sheet, main.history), connection);
    if (patches.empty())
        throw std::domain_error("offset aperto: mancano le aperture");
    for (auto &patch : patches)
        patch.closure = true;
    return sewLayer(sheet, main, patches, connection, true);
}
Layer extract(const Layer &layer, const std::vector<FaceId> &faces) {
    BodyHistory stage;
    auto body = facesAsSheet(layer.body, faces, &stage);
    auto markers = mapClosures(stage, {layer.closures});
    return {std::move(body), BodyHistory::compose(layer.history, stage, 0), std::move(markers)};
}
// Una gola cilindrica puo' perdere la larghezza assiale quando le due pareti
// piane raggiungono lo stesso piano. Il cilindro di area nulla e le due
// corone opposte vengono sostituiti dal contorno planare rimasto. Le superfici
// e i bordi sopravvissuti non vengono rifittati o spostati.
Layer cleanCollapsedGrooves(const Body &root, const Body &closed, const BodyHistory &offsetHistory, Layer layer, double tolerance,
                            double connection) {
    std::set<int> removed, planes;
    for (auto f : layer.body.faces()) {
        const auto &face = layer.body.face(f);
        if (face.surface->type() != SurfaceType::Cylinder)
            continue;
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        std::set<int> neighbors;
        bool bounded = true;
        for (auto l : face.loops)
            for (auto id : layer.body.loopFins(l)) {
                const auto &fin = layer.body.fin(id);
                const auto other = layer.body.otherFin(id);
                if (other.valid() && layer.body.finFace(other) != f)
                    neighbors.insert(layer.body.finFace(other).index);
                // Il fit delle SP-curve e' una B-spline a pesi positivi: il suo
                // hull contiene l'intero intervallo v, anche fra i campioni.
                const auto *s = dynamic_cast<const BSplineCurve<2> *>(fin.pcurve.get());
                if (s)
                    for (const auto &p : s->poles()) {
                        lo = std::min(lo, p.y());
                        hi = std::max(hi, p.y());
                    }
                else if (const auto *line = dynamic_cast<const Line<2> *>(fin.pcurve.get())) {
                    const auto range = layer.body.edge(fin.edge).range;
                    for (double t : {range.lo, range.hi}) {
                        const double v = line->point(t).y();
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                } else
                    bounded = false;
            }
        if (!bounded || !(hi >= lo) || neighbors.size() != 2)
            continue;
        const auto a = *neighbors.begin(), b = *std::next(neighbors.begin());
        const auto *first = dynamic_cast<const Plane *>(layer.body.face(FaceId{a}).surface.get());
        const auto *second = dynamic_cast<const Plane *>(layer.body.face(FaceId{b}).surface.get());
        if (!first || !second)
            continue;
        const auto &cylinder = static_cast<const CylindricalSurface &>(*face.surface);
        const auto axis = cylinder.frame().zDir();
        if (norm(cross(first->frame().zDir(), axis)) > angularResolution() ||
            norm(cross(second->frame().zDir(), axis)) > angularResolution())
            continue;
        // Oltre il collasso esatto servirebbe eliminare una regione intera:
        // rifiutare una fascia invertita anziche' conservarla nel solido.
        const auto &oa = offsetHistory.origins(EntityKind::Face, a), &ob = offsetHistory.origins(EntityKind::Face, b);
        if (oa.size() == 1 && ob.size() == 1 && oa[0].kind == EntityKind::Face && ob[0].kind == EntityKind::Face) {
            const auto *pa = dynamic_cast<const Plane *>(closed.face(FaceId{oa[0].index}).surface.get());
            const auto *pb = dynamic_cast<const Plane *>(closed.face(FaceId{ob[0].index}).surface.get());
            if (pa && pb) {
                const double before = dot(axis, pb->frame().origin() - pa->frame().origin());
                const double after = dot(axis, second->frame().origin() - first->frame().origin());
                if (before * after < -tolerance * std::abs(before))
                    throw std::domain_error("offset aperto: lo spessore supera la larghezza della gola cilindrica");
            }
        }
        if (hi - lo > tolerance || !samePlane(first->frame(), second->frame(), tolerance))
            continue;
        removed.insert(f.index);
        removed.insert(a);
        removed.insert(b);
        planes.insert(a);
        planes.insert(b);
    }
    if (removed.empty())
        return layer;
    std::vector<FaceId> keep;
    for (auto f : layer.body.faces())
        if (!removed.count(f.index))
            keep.push_back(f);
    auto remaining = extract(layer, keep);
    auto patches = planarPatches(boundaryLoops(remaining.body, remaining.history), connection);
    for (auto &patch : patches) {
        patch.origins.clear();
        bool matched = false;
        patch.closure = true;
        for (int index : planes) {
            const auto &plane = static_cast<const Plane &>(*layer.body.face(FaceId{index}).surface);
            if (!samePlane(plane.frame(), patch.frame, tolerance))
                continue;
            matched = true;
            patch.closure = patch.closure && bool(layer.closures.count(index));
            const auto &origins = layer.history.origins(EntityKind::Face, index);
            patch.origins.insert(patch.origins.end(), origins.begin(), origins.end());
        }
        if (!matched)
            throw std::domain_error("offset aperto: contorno del collasso non planare");
        uniqueOrigins(patch.origins);
    }
    return sewLayer(root, remaining, patches, connection, true);
}
Layer offsetLayer(const Body &root, const Layer &closed, double distance, double tolerance, double connection) {
    std::vector<FaceOffset> specs;
    for (auto f : closed.body.faces())
        specs.push_back({f, closed.closures.count(f.index) ? 0.0 : distance, false});
    auto result =
        offsetJoinedFaces(closed.body, specs, tolerance, distance < 0 ? OffsetExtension::CurvatureContinuous : OffsetExtension::Polynomial);
    Layer layer{std::move(result.body), BodyHistory::compose(closed.history, result.history, 0),
                mapClosures(result.history, {closed.closures})};
    layer = cleanCollapsedGrooves(root, closed.body, result.history, std::move(layer), tolerance, connection);
    std::vector<FaceId> keep;
    for (auto f : layer.body.faces())
        if (!layer.closures.count(f.index))
            keep.push_back(f);
    return extract(layer, keep);
}
bool sharp(const Body &body) {
    for (auto e : body.edges()) {
        if (body.isLaminar(e))
            continue;
        const auto &edge = body.edge(e);
        const auto &a = body.fin(edge.forward), &b = body.fin(edge.backward);
        const auto &fa = body.face(body.finFace(edge.forward)), &fb = body.face(body.finFace(edge.backward));
        if (!a.pcurve || !b.pcurve)
            return false;
        // I loft suddivisi su pieghe interne conservano il percorso storico,
        // che mantiene isoparametrici i bordi da estendere. Questo raccordo
        // aggiuntivo serve alle giunzioni fra famiglie di supporti diverse.
        if (fa.surface->type() == fb.surface->type())
            continue;
        for (double fraction : {.25, .5, .75}) {
            const double t = edge.range.lo + fraction * edge.range.length();
            const auto ua = a.pcurve->point(t), ub = b.pcurve->point(t);
            const auto na = (fa.sense ? 1. : -1.) * fa.surface->normal(ua.x(), ua.y());
            const auto nb = (fb.sense ? 1. : -1.) * fb.surface->normal(ub.x(), ub.y());
            if (dot(na, nb) < std::cos(2. * kPi / 180.))
                return true;
        }
    }
    return false;
}
} // namespace
bool canOffsetOpenSheet(const Body &sheet, double tolerance) {
    if (!sheet.isSheet())
        return false;
    try {
        if (!sharp(sheet))
            return false;
        const auto ancestry = indexHistory(sheet, sheet, HistoryRelation::Same);
        return !planarPatches(boundaryLoops(sheet, ancestry), tolerance).empty();
    } catch (const std::exception &) {
        return false;
    }
}
OffsetResult offsetOpenSheet(const Body &sheet, double distance, double tolerance) {
    const double connection = std::max(tolerance, booleanConnectionTolerance());
    const auto closed = capped(sheet, connection);
    auto layer = offsetLayer(sheet, closed, distance, tolerance, connection);
    OffsetResult result;
    result.body = std::move(layer.body);
    result.history = std::move(layer.history);
    result.shells = int(result.body.shells().size());
    return result;
}
Body thickenOpenSheet(const Body &sheet, double lower, double upper, double tolerance, BodyHistory *history) {
    const double connection = std::max(tolerance, booleanConnectionTolerance());
    const auto closed = capped(sheet, connection);
    const auto side = [&](double distance) {
        return distance == 0.0 ? Layer{sheet, indexHistory(sheet, sheet, HistoryRelation::Same), {}}
                               : offsetLayer(sheet, closed, distance, tolerance, connection);
    };
    auto low = side(lower), high = side(upper);
    auto loops = boundaryLoops(low.body, low.history);
    const auto other = boundaryLoops(high.body, high.history);
    loops.insert(loops.end(), other.begin(), other.end());
    auto rims = planarPatches(loops, connection);
    std::vector<BodyHistory> rimHistories;
    rimHistories.reserve(rims.size());
    std::vector<const Body *> parts{&high.body, &low.body};
    std::vector<const BodyHistory *> sources{&high.history, &low.history};
    for (const auto &rim : rims) {
        parts.push_back(&rim.body);
        rimHistories.push_back(generatedFaces(sheet, rim.body, rim.origins));
        sources.push_back(&rimHistories.back());
    }
    BodyHistory stage;
    auto result = sewSheets(parts, connection, true, history ? &stage : nullptr);
    if (!result.solid)
        throw std::domain_error("spessore: le pareti planari non chiudono un solido");
    if (history)
        *history = transportHistory(stage, sources);
    return std::move(result.body);
}
} // namespace ForgeCad::Kernel::detail
