#include "cad_topology_ref.h"

#include <algorithm>
#include <exception>
#include <limits>

#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

int edgeContext(const Body &body, EdgeId id) {
    const Edge &edge = body.edge(id);
    int a = -1, b = -1;
    if (edge.forward.valid()) a = int(body.face(body.finFace(edge.forward)).surface->type());
    if (edge.backward.valid()) b = int(body.face(body.finFace(edge.backward)).surface->type());
    if (a > b) std::swap(a, b);
    return (a + 1) | ((b + 1) << 8);
}

int faceContext(const Body &body, FaceId id) {
    const Face &face = body.face(id);
    int edges = 0;
    for (LoopId loop : face.loops) edges += int(body.loopFins(loop).size());
    return std::min(int(face.loops.size()), 0x7fff) | (std::min(edges, 0xffff) << 15);
}

int vertexContext(const Body &body, VertexId id) {
    int valence = 0;
    for (EdgeId edge : body.edges()) {
        const Edge &e = body.edge(edge);
        for (FinId fin : {e.forward, e.backward})
            if (fin.valid() && body.fin(fin).vertex == id) { ++valence; break; }
    }
    return valence;
}

template <class Id, class Range, class Matches, class Metric>
Id resolve(const EdgePoint &reference, const Range &range, Matches matches, Metric metric, double legacyTolerance) {
    const bool persistent = reference.subshape >= 0 && reference.geometry >= 0 && reference.context != -1;
    if (persistent) {
        const Id exact(reference.subshape);
        try {
            if (matches(exact)) return exact;
        } catch (const std::exception &) {
        }
    }
    Id best;
    double closest = persistent ? std::numeric_limits<double>::max() : legacyTolerance;
    for (Id candidate : range) {
        try {
            if (persistent && !matches(candidate)) continue;
            const double d = metric(candidate);
            if (d <= closest) closest = d, best = candidate;
        } catch (const std::exception &) {
        }
    }
    return best;
}

}

EdgePoint edgeReference(const Body &body, EdgeId edge, const Vec3 &point) {
    const Edge &e = body.edge(edge);
    return {point.x(), point.y(), point.z(), edge.index, int(e.curve->type()), edgeContext(body, edge)};
}

EdgePoint faceReference(const Body &body, FaceId face, const Vec3 &point) {
    return {point.x(), point.y(), point.z(), face.index, int(body.face(face).surface->type()), faceContext(body, face)};
}

EdgePoint vertexReference(const Body &body, VertexId vertex) {
    const Vec3 &point = body.vertex(vertex).point;
    return {point.x(), point.y(), point.z(), vertex.index, 0, vertexContext(body, vertex)};
}

QVector<EdgePoint> freeBoundaryLoop(const Body &body, EdgeId edge) {
    QVector<EdgePoint> result;
    if (!body.contains(edge) || !body.isLaminar(edge)) return result;
    // Edge di bordo per vertice.
    std::vector<std::vector<EdgeId>> around;
    for (EdgeId e : body.edges()) {
        if (!body.isLaminar(e)) continue;
        for (VertexId v : {body.edgeStart(e), body.edgeEnd(e)}) {
            if (std::size_t(v.index) >= around.size()) around.resize(std::size_t(v.index) + 1);
            around[std::size_t(v.index)].push_back(e);
        }
    }
    const auto other = [&](VertexId v, EdgeId from) {
        const std::vector<EdgeId> &list = around[std::size_t(v.index)];
        // Un edge chiuso (stesso vertice ai due estremi) compare due volte.
        if (list.size() != 2 || list[0] == list[1]) return EdgeId();
        return list[0] == from ? list[1] : list[0];
    };
    // Si cammina in avanti dalla fine dell'edge, poi all'indietro dall'inizio.
    std::vector<EdgeId> forward{edge}, backward;
    bool closed = false;
    for (int pass = 0; pass < 2 && !closed; ++pass) {
        EdgeId current = edge;
        VertexId vertex = pass == 0 ? body.edgeEnd(edge) : body.edgeStart(edge);
        while (true) {
            const EdgeId next = other(vertex, current);
            if (!next.valid()) break;
            if (next == edge) {
                closed = true;
                break;
            }
            (pass == 0 ? forward : backward).push_back(next);
            vertex = body.edgeStart(next) == vertex ? body.edgeEnd(next) : body.edgeStart(next);
            current = next;
            if (forward.size() + backward.size() > body.edges().size()) break;  // sicurezza
        }
    }
    std::vector<EdgeId> chain(backward.rbegin(), backward.rend());
    chain.insert(chain.end(), forward.begin(), forward.end());
    for (EdgeId e : chain) {
        const Edge &data = body.edge(e);
        result.append(edgeReference(body, e, data.curve->point(0.5 * (data.range.lo + data.range.hi))));
    }
    return result;
}

EdgeId resolveEdgeReference(const Body &body, const EdgePoint &reference, double legacyTolerance) {
    const Vec3 point(reference.x, reference.y, reference.z);
    return resolve<EdgeId>(reference, body.edges(), [&](EdgeId edge) {
        const Edge &e = body.edge(edge);
        return int(e.curve->type()) == reference.geometry && edgeContext(body, edge) == reference.context;
    }, [&](EdgeId edge) {
        const Edge &e = body.edge(edge);
        return projectPoint(*e.curve, point, e.range).distance;
    }, legacyTolerance);
}

FaceId resolveFaceReference(const Body &body, const EdgePoint &reference, double legacyTolerance) {
    const Vec3 point(reference.x, reference.y, reference.z);
    return resolve<FaceId>(reference, body.faces(), [&](FaceId face) {
        return int(body.face(face).surface->type()) == reference.geometry && faceContext(body, face) == reference.context;
    }, [&](FaceId face) {
        const SurfaceProjection projection = projectPoint(*body.face(face).surface, point);
        return classifyPointOnFace(body, face, projection.point, std::max(legacyTolerance, 1e-9)) == PointLocation::Outside
            ? std::numeric_limits<double>::max() : distance(projection.point, point);
    }, legacyTolerance);
}

VertexId resolveVertexReference(const Body &body, const EdgePoint &reference, double legacyTolerance) {
    const Vec3 point(reference.x, reference.y, reference.z);
    return resolve<VertexId>(reference, body.vertices(), [&](VertexId vertex) {
        return reference.geometry == 0 && vertexContext(body, vertex) == reference.context;
    }, [&](VertexId vertex) { return distance(body.vertex(vertex).point, point); }, legacyTolerance);
}

void upgradeTopologyReferences(QVector<ExtrusionObject> &features) {
    const auto owner = [&](const GeometryRef &ref) -> int {
        if (ref.featureId)
            for (int index = 0; index < features.size(); ++index)
                if (features.at(index).featureId == ref.featureId) return index;
        return ref.index;
    };
    const auto upgrade = [&](GeometryRef &ref) {
        if (ref.kind < 3 || ref.kind > 5 || ref.point.subshape >= 0) return;
        const int index = owner(ref);
        if (index < 0 || index >= features.size() || !features.at(index).forgeBody) return;
        const Body &body = *features.at(index).forgeBody;
        if (ref.kind == 3) {
            const VertexId vertex = resolveVertexReference(body, ref.point, std::numeric_limits<double>::max());
            if (vertex.valid()) ref.point = vertexReference(body, vertex);
        } else if (ref.kind == 4) {
            const EdgeId edge = resolveEdgeReference(body, ref.point, std::numeric_limits<double>::max());
            if (edge.valid()) ref.point = edgeReference(body, edge, Vec3(ref.point.x, ref.point.y, ref.point.z));
        } else {
            Box box;
            for (VertexId vertex : body.vertices()) box.add(body.vertex(vertex).point);
            const double tolerance = 1e-3 * std::max(1.0, box.diagonal());
            const FaceId face = resolveFaceReference(body, ref.point, tolerance);
            if (face.valid()) ref.point = faceReference(body, face, Vec3(ref.point.x, ref.point.y, ref.point.z));
        }
    };
    const auto upgradeEdges = [&](QVector<EdgePoint> &refs, int base) {
        if (base < 0 || base >= features.size() || !features.at(base).forgeBody) return;
        const Body &body = *features.at(base).forgeBody;
        Box box;
        for (VertexId vertex : body.vertices()) box.add(body.vertex(vertex).point);
        const double tolerance = 1e-3 * std::max(1.0, box.diagonal());
        for (EdgePoint &ref : refs) {
            if (ref.subshape >= 0) continue;
            const EdgeId edge = resolveEdgeReference(body, ref, tolerance);
            if (edge.valid()) ref = edgeReference(body, edge, Vec3(ref.x, ref.y, ref.z));
        }
    };
    for (ExtrusionObject &feature : features) {
        for (GeometryRef &ref : feature.datum.refs) upgrade(ref);
        for (GeometryRef &ref : feature.pattern.refs) upgrade(ref);
        for (QVector<GeometryRef> *refs : {&feature.ruledFirst, &feature.ruledSecond, &feature.planarRefs})
            for (GeometryRef &ref : *refs) upgrade(ref);
        upgrade(feature.extentRef);
        upgrade(feature.move.axis);
        if (feature.feature == BodyFeature::Blend || feature.feature == BodyFeature::SheetExtend)
            upgradeEdges(feature.blendEdges, feature.firstBody);
        if ((feature.feature == BodyFeature::SurfaceOffset || feature.feature == BodyFeature::DeleteFace || feature.feature == BodyFeature::Shell) && feature.firstBody >= 0 && feature.firstBody < features.size()
            && features.at(feature.firstBody).forgeBody) {
            const Body &body = *features.at(feature.firstBody).forgeBody;
            for (EdgePoint &ref : feature.offsetFaces) {
                if (ref.subshape >= 0) continue;
                const FaceId face = resolveFaceReference(body, ref, std::numeric_limits<double>::max());
                if (face.valid()) ref = faceReference(body, face, Vec3(ref.x, ref.y, ref.z));
            }
        }
        if (feature.feature == BodyFeature::Helix && feature.helix.source != 0 && feature.firstBody >= 0
            && feature.firstBody < features.size() && features.at(feature.firstBody).forgeBody && feature.helix.reference.subshape < 0) {
            const Body &body = *features.at(feature.firstBody).forgeBody;
            const Vec3 point(feature.helix.reference.x, feature.helix.reference.y, feature.helix.reference.z);
            if (feature.helix.source == 1) {
                const EdgeId edge = resolveEdgeReference(body, feature.helix.reference, std::numeric_limits<double>::max());
                if (edge.valid()) feature.helix.reference = edgeReference(body, edge, point);
            } else {
                const FaceId face = resolveFaceReference(body, feature.helix.reference, std::numeric_limits<double>::max());
                if (face.valid()) feature.helix.reference = faceReference(body, face, point);
            }
        }
    }
}

}
