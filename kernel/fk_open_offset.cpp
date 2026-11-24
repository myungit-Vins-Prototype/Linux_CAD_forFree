#include "fk_open_offset.h"
#include "fk_history_tools.h"
#include "fk_planar.h"
#include "fk_pcurve.h"
#include "fk_ruled_wall.h"
#include "fk_sew.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_body_check.h"
#include "fk_cancel.h"
#include "fk_classify_uv.h"
#include "fk_intersect.h"
#include "fk_surface_curve_bounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

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
Layer capped(const Body &sheet, const Layer &main, double connection) {
    auto patches = planarPatches(boundaryLoops(main.body, main.history), connection);
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
struct HoleLoop {
    FaceId face;
    LoopId loop;
};

bool holeCarrier(const Surface &surface) {
    const auto *spline = dynamic_cast<const BSplineSurface *>(&surface);
    return spline && !spline->basis() && !spline->isUPeriodic() && !spline->isVPeriodic()
        && spline->uDegree() >= 2 && spline->uDegree() <= 3
        && spline->vDegree() >= 2 && spline->vDegree() <= 3;
}

const BSplineCurve<3> *holeCurve(const Edge &edge) {
    const auto *curve = dynamic_cast<const BSplineCurve<3> *>(edge.curve.get());
    return curve && curve->degree() == 3 && !curve->isRational()
        && edge.range.isFinite() && edge.range.length() > 0
        && curve->domain().lo == edge.range.lo && curve->domain().hi == edge.range.hi ? curve : nullptr;
}

std::vector<HoleLoop> internalOpenings(const Body &sheet) {
    std::vector<HoleLoop> result;
    for (FaceId id : sheet.faces()) {
        cancellationPoint();
        const auto &face = sheet.face(id);
        if (face.loops.size() < 2 || !holeCarrier(*face.surface)) continue;
        std::vector<HoleLoop> inside;
        int outer = 0;
        bool valid = true;
        for (LoopId loop : face.loops) {
            ProfileLoop profile;
            bool eligible = true;
            for (FinId id : sheet.loopFins(loop)) {
                const auto &fin = sheet.fin(id);
                const auto &edge = sheet.edge(fin.edge);
                if (!fin.pcurve || !edge.range.isFinite() || !(edge.range.length() > 0)) {
                    valid = false;
                    break;
                }
                const ProfileSegment piece{fin.pcurve, edge.range};
                profile.segments.push_back(fin.sense ? piece : reversed(piece));
                eligible = eligible && sheet.isLaminar(fin.edge) && holeCurve(edge);
            }
            const double area = (face.sense ? 1. : -1.) * signedArea(profile);
            if (!valid || !std::isfinite(area) || area == 0) { valid = false; break; }
            if (area > 0) ++outer;
            else if (eligible) inside.push_back({id, loop});
        }
        if (valid && outer == 1) result.insert(result.end(), inside.begin(), inside.end());
    }
    return result;
}

// Copy only the topology in use. Surfaces, edges, parameter curves and
// budgets of untouched fins are shared with the input layer.
struct SheetRecipe {
    std::vector<Vec3> points;
    std::vector<double> tolerances;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::map<int, int> faceSlots;
    using Origins = std::optional<std::vector<HistoryLink>>;
    std::array<std::vector<Origins>, 3> origins;

    int addPoint(const Vec3 &point, double tolerance, Origins origin) {
        points.push_back(point);
        tolerances.push_back(tolerance);
        origins[int(EntityKind::Vertex)].push_back(std::move(origin));
        return int(points.size() - 1);
    }
    int addEdge(Body::BuildEdge edge, Origins origin) {
        edges.push_back(std::move(edge));
        origins[int(EntityKind::Edge)].push_back(std::move(origin));
        return int(edges.size() - 1);
    }

    SheetRecipe(const Body &body, const BodyHistory &ancestry, const std::set<int> &omit = {}) {
        std::map<int, int> vertexSlots, edgeSlots;
        const auto origin = [&](EntityKind kind, int index) -> Origins {
            if (ancestry.isDeclared(kind, index)) return ancestry.origins(kind, index);
            return std::nullopt;
        };
        const auto point = [&](VertexId id) {
            const auto found = vertexSlots.find(id.index);
            if (found != vertexSlots.end()) return found->second;
            const auto &v = body.vertex(id);
            const int slot = addPoint(v.point, v.tolerance, origin(EntityKind::Vertex, id.index));
            return vertexSlots.emplace(id.index, slot).first->second;
        };
        for (FaceId id : body.faces()) {
            cancellationPoint();
            const auto &face = body.face(id);
            Body::BuildFace copied{face.surface, face.sense, {}};
            for (LoopId l : face.loops) {
                if (omit.count(l.index)) continue;
                const auto fins = body.loopFins(l);
                if (fins.empty()) continue;
                auto &loop = copied.loops.emplace_back();
                for (FinId fid : fins) {
                    const auto &fin = body.fin(fid);
                    auto found = edgeSlots.find(fin.edge.index);
                    if (found == edgeSlots.end()) {
                        const auto &e = body.edge(fin.edge);
                        const auto copy = addEdge({point(body.edgeStart(fin.edge)), point(body.edgeEnd(fin.edge)),
                            e.curve, e.range, e.tolerance}, origin(EntityKind::Edge, fin.edge.index));
                        found = edgeSlots.emplace(fin.edge.index, copy).first;
                    }
                    loop.push_back({found->second, fin.sense, fin.pcurve, fin.pcurveTolerance});
                }
            }
            faceSlots.emplace(id.index, int(faces.size()));
            faces.push_back(std::move(copied));
            origins[int(EntityKind::Face)].push_back(origin(EntityKind::Face, id.index));
        }
    }
    Layer build(const Body &root) const {
        auto body = Body::buildSheet(points, edges, faces);
        for (std::size_t i = 0; i < points.size(); ++i) body.vertex(VertexId{int(i)}).tolerance = tolerances[i];
        auto history = makeHistory({&root}, body);
        for (int kind = 0; kind < 3; ++kind)
            for (std::size_t i = 0; i < origins[kind].size(); ++i)
                if (origins[kind][i]) history.declareOrigins(EntityKind(kind), int(i), *origins[kind][i]);
        return {std::move(body), std::move(history), {}};
    }
};

void requireHoleFin(const CurvePtr<3> &curve, const SurfacePtr &surface, const CurvePtr<2> &pc,
                    Interval range, double connection) {
    const auto proof = surfaceCurveResidualBound(curve, surface, pc, range, connection);
    if (proof.status != SurfaceCurveBoundStatus::Certified)
        throw std::domain_error("spessore: contorno interno fuori dal budget di cucitura");
}

using HoleRails = std::map<int, std::shared_ptr<const BSplineCurve<3>>>;

Layer restoreOpenings(const Body &sheet, Layer layer, const std::vector<HoleLoop> &holes,
                      double distanceValue, double connection, HoleRails &rails) {
    if (distanceValue == 0.0) {
        for (const auto &hole : holes) for (FinId id : sheet.loopFins(hole.loop)) {
            const auto &fin = sheet.fin(id);
            const auto &edge = sheet.edge(fin.edge);
            rails.emplace(fin.edge.index, std::static_pointer_cast<const BSplineCurve<3>>(edge.curve));
        }
        return {sheet, indexHistory(sheet, sheet, HistoryRelation::Same), {}};
    }

    SheetRecipe recipe(layer.body, layer.history);
    std::map<int, int> vertices;
    for (const auto &hole : holes) {
        cancellationPoint();
        const auto children = layer.history.descendants(EntityKind::Face, 0, hole.face.index);
        if (children.size() != 1 || !holeCarrier(*layer.body.face(FaceId{children.front()}).surface))
            throw std::domain_error("spessore: il supporto del foro interno è cambiato");
        auto &face = recipe.faces.at(recipe.faceSlots.at(children.front()));
        const auto &original = sheet.face(hole.face);
        const FaceId target{children.front()};
        const FaceParameterClassifier classifier(layer.body, target, connection);
        const auto &trim = layer.body.face(target);
        const double uvTolerance = 1e-10 * std::max({1., trim.surface->uDomain().length(),
                                                       trim.surface->vDomain().length()});
        std::vector<Body::BuildFin> loop;
        const auto point = [&](VertexId id, const Vec3 &p) {
            const auto found = vertices.find(id.index);
            if (found == vertices.end())
                return vertices.emplace(id.index, recipe.addPoint(p, sheet.vertex(id).tolerance,
                    std::vector<HistoryLink>{{0, EntityKind::Vertex, id.index, HistoryRelation::Modified}})).first->second;
            const double gap = distance(recipe.points.at(std::size_t(found->second)), p);
            if (!(gap <= connection)) throw std::domain_error("spessore: estremi del foro interno disgiunti");
            auto &budget = recipe.tolerances.at(std::size_t(found->second));
            budget = std::max(budget, gap);
            return found->second;
        };
        for (FinId id : sheet.loopFins(hole.loop)) {
            const auto &fin = sheet.fin(id);
            const auto &edge = sheet.edge(fin.edge);
            const auto lift = [&](double t) {
                const Vec2 uv = fin.pcurve->point(t);
                return face.surface->point(uv.x(), uv.y());
            };
            for (double fraction : {0., .5, 1.}) {
                const double t = edge.range.lo + fraction * edge.range.length();
                const auto uv = fin.pcurve->point(t);
                if (classifier.classify(uv) != PointLocation::Inside)
                    throw std::domain_error("spessore: il foro non è interno al supporto rifilato");
                const double signedDistance = original.sense ? distanceValue : -distanceValue;
                const auto expected = original.surface->point(uv.x(), uv.y())
                    + signedDistance * original.surface->normal(uv.x(), uv.y());
                if (!(distance(expected, lift(t)) <= connection))
                    throw std::domain_error("spessore: la parametrizzazione del foro non corrisponde alla pelle a distanza");
            }
            // A continuous curve starting inside a nonperiodic face can
            // leave its trimmed region only through one of its boundaries.
            for (LoopId boundary : trim.loops) for (FinId borderId : layer.body.loopFins(boundary)) {
                cancellationPoint();
                const auto &border = layer.body.fin(borderId);
                if (!border.pcurve)
                    throw std::domain_error("spessore: bordo del supporto rifilato senza parametrizzazione");
                const auto hits = intersectCurves(*fin.pcurve, edge.range, *border.pcurve,
                    layer.body.edge(border.edge).range, uvTolerance);
                if (hits.overlap || !hits.points.empty())
                    throw std::domain_error("spessore: il foro incontra il bordo del supporto rifilato");
            }
            const auto rail = fitCurve(lift, edge.range, fin.pcurve->breakpoints(edge.range), .01 * connection);
            requireHoleFin(rail, face.surface, fin.pcurve, edge.range, connection);
            const auto a = point(sheet.edgeStart(fin.edge), rail->point(edge.range.lo));
            const auto b = point(sheet.edgeEnd(fin.edge), rail->point(edge.range.hi));
            const int slot = recipe.addEdge({a, b, rail, edge.range, 0},
                std::vector<HistoryLink>{{0, EntityKind::Edge, fin.edge.index, HistoryRelation::Modified}});
            rails.emplace(fin.edge.index, rail);
            loop.push_back({slot, fin.sense, fin.pcurve, connection});
        }
        if (face.sense != original.sense) {
            std::reverse(loop.begin(), loop.end());
            for (auto &fin : loop) fin.sense = !fin.sense;
        }
        face.loops.push_back(std::move(loop));
    }
    return recipe.build(sheet);
}

Body holeWall(const std::shared_ptr<const BSplineCurve<3>> &lowEdge,
              const std::shared_ptr<const BSplineCurve<3>> &highEdge, Interval range) {
    auto lower = lowEdge->clamped();
    auto upper = highEdge->clamped();
    auto carrier = cubicRuledWallSurface(lower, upper);
    std::vector<Vec3> points{lowEdge->point(range.lo),lowEdge->point(range.hi),
                             highEdge->point(range.lo),highEdge->point(range.hi)};
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFin> loop;
    const auto add = [&](int a, int b, CurvePtr<3> curve, Interval span, bool sense, Vec2 uvA, Vec2 uvB) {
        const auto pc = std::make_shared<BSplineCurve<2>>(1,
            std::vector<double>{span.lo, span.lo, span.hi, span.hi}, std::vector<Vec2>{uvA, uvB});
        loop.push_back({int(edges.size()), sense, pc, 0.});
        edges.push_back({a, b, std::move(curve), span, 0.});
    };
    const auto connect = [&](int a, int b, double t, bool sense) {
        const auto d = points[b] - points[a];
        const double length = norm(d);
        if (!(length > linearResolution())) throw std::domain_error("spessore: parete del foro collassata");
        add(a, b, std::make_shared<Line<3>>(points[a], d / length), {0., length}, sense, {t, 0.}, {t, 1.});
    };
    add(0, 1, lowEdge, range, true, {range.lo,0.}, {range.hi,0.});
    connect(1, 3, range.hi, true);
    add(2, 3, highEdge, range, false, {range.lo,1.}, {range.hi,1.});
    connect(0, 2, range.lo, false);
    return Body::buildSheet(points, edges, {{carrier,true,{loop}}});
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
    const auto closed = capped(sheet, {sheet, indexHistory(sheet, sheet, HistoryRelation::Same), {}}, connection);
    auto layer = offsetLayer(sheet, closed, distance, tolerance, connection);
    OffsetResult result;
    result.body = std::move(layer.body);
    result.history = std::move(layer.history);
    result.shells = int(result.body.shells().size());
    return result;
}
std::optional<Body> tryThickenOpenSheet(const Body &sheet, double lower, double upper, double tolerance,
                                        BodyHistory *history) {
    if (!sheet.isSheet()) return std::nullopt;
    const double offsetTolerance = std::min(tolerance, linearResolution());
    const double connection = std::max(offsetTolerance, booleanConnectionTolerance());
    Layer main{sheet, indexHistory(sheet, sheet, HistoryRelation::Same), {}};
    std::vector<HoleLoop> holes;
    // Le aperture interne non planari restano presenti sulle due pelli.
    // Le si esclude solo dalla costruzione delle giunzioni del bordo esterno.
    if (!canOffsetOpenSheet(sheet, tolerance)) {
        if (!sharp(sheet)) return std::nullopt;
        holes = internalOpenings(sheet);
        if (holes.empty()) return std::nullopt;
        std::set<int> omitted;
        for (const auto &hole : holes) omitted.insert(hole.loop.index);
        main = SheetRecipe(sheet, main.history, omitted).build(sheet);
        if (!canOffsetOpenSheet(main.body, tolerance)) return std::nullopt;
        for (const auto &hole : holes)
            for (FinId id : sheet.loopFins(hole.loop)) {
                const auto &fin = sheet.fin(id);
                const auto &edge = sheet.edge(fin.edge);
                requireHoleFin(edge.curve, sheet.face(hole.face).surface, fin.pcurve, edge.range, connection);
            }
    }
    const auto closed = capped(sheet, main, connection);
    const auto side = [&](double distance) {
        return distance == 0.0 ? main : offsetLayer(sheet, closed, distance, offsetTolerance, connection);
    };
    auto low = side(lower), high = side(upper);
    auto loops = boundaryLoops(low.body, low.history);
    const auto other = boundaryLoops(high.body, high.history);
    loops.insert(loops.end(), other.begin(), other.end());
    auto rims = planarPatches(loops, connection);
    HoleRails lowRails, highRails;
    std::vector<Body> walls;
    std::vector<FinId> wallOrigins;
    if (!holes.empty()) {
        low = restoreOpenings(sheet, std::move(low), holes, lower, connection, lowRails);
        high = restoreOpenings(sheet, std::move(high), holes, upper, connection, highRails);
        for (const auto &hole : holes) for (FinId id : sheet.loopFins(hole.loop)) {
            const EdgeId edge = sheet.fin(id).edge;
            const auto &a = lowRails.at(edge.index), &b = highRails.at(edge.index);
            walls.push_back(holeWall(a, b, sheet.edge(edge).range));
            wallOrigins.push_back(id);
        }
    }
    std::vector<BodyHistory> patchHistories;
    patchHistories.reserve(rims.size() + walls.size());
    std::vector<const Body *> parts{&high.body, &low.body};
    std::vector<const BodyHistory *> sources{&high.history, &low.history};
    for (const auto &rim : rims) {
        parts.push_back(&rim.body);
        patchHistories.push_back(generatedFaces(sheet, rim.body, rim.origins));
        sources.push_back(&patchHistories.back());
    }
    const int wallStart = int(parts.size());
    for (std::size_t i = 0; i < walls.size(); ++i) {
        parts.push_back(&walls[i]);
        const auto edge = sheet.fin(wallOrigins[i]).edge;
        patchHistories.push_back(generatedFaces(sheet, walls[i],
            {{0, EntityKind::Edge, edge.index, HistoryRelation::Generated}}));
        sources.push_back(&patchHistories.back());
    }
    BodyHistory stage;
    auto result = sewSheets(parts, connection, true, history || !holes.empty() ? &stage : nullptr);
    if (!result.solid)
        throw std::domain_error("spessore: le pareti non chiudono un solido");
    if (!holes.empty() && (result.shells != 1 || !checkBody(result.body, {true, true}).empty()))
        throw std::domain_error("spessore: i contorni ripristinati non formano un unico solido valido");
    for (std::size_t i = 0; i < walls.size(); ++i) {
        cancellationPoint();
        const auto faces = stage.descendants(EntityKind::Face, wallStart + int(i), walls[i].faces().front().index);
        if (faces.size() != 1) throw std::domain_error("spessore: parete del foro non conservata");
        const auto sourceFace = sheet.finFace(wallOrigins[i]);
        const auto sourceEdge = sheet.fin(wallOrigins[i]).edge;
        const auto &sourceRange = sheet.edge(sourceEdge).range;
        const std::array<std::vector<int>, 2> caps{
            high.history.descendants(EntityKind::Face, 0, sourceFace.index),
            low.history.descendants(EntityKind::Face, 0, sourceFace.index)};
        std::array<int, 2> rails{};
        for (LoopId loop : result.body.face(FaceId{faces.front()}).loops) for (FinId id : result.body.loopFins(loop)) {
            const FinId mate = result.body.otherFin(id);
            if (!mate.valid()) throw std::domain_error("spessore: parete del foro aperta");
            const FaceId face = result.body.finFace(mate);
            const auto &origins = stage.origins(EntityKind::Face, face.index);
            if (origins.size() != 1 || origins.front().operand > 1) continue;
            const auto &origin = origins.front();
            const auto &expectedCaps = caps.at(std::size_t(origin.operand));
            const auto &fin = result.body.fin(mate);
            const auto &edge = result.body.edge(fin.edge);
            if (expectedCaps.size() != 1 || expectedCaps.front() != origin.index)
                throw std::domain_error("spessore: una parete del foro incontra un supporto inatteso");
            const auto &expectedRail = (origin.operand == 0 ? highRails : lowRails).at(sourceEdge.index);
            if (edge.curve != expectedRail || edge.range.lo != sourceRange.lo || edge.range.hi != sourceRange.hi)
                throw std::domain_error("spessore: la cucitura ha cambiato un bordo del foro");
            requireHoleFin(edge.curve, result.body.face(face).surface, fin.pcurve, edge.range, connection);
            ++rails.at(std::size_t(origin.operand));
        }
        if (rails != std::array<int, 2>{1, 1})
            throw std::domain_error("spessore: una parete del foro non unisce le due pelli");
    }
    if (history)
        *history = transportHistory(stage, sources);
    return std::move(result.body);
}
} // namespace ForgeCad::Kernel::detail
