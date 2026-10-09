#include "fk_draft.h"
#include "fk_body_check.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_intersect.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace ForgeCad::Kernel {
Body draftFaces(const Body &input, const std::vector<FaceId> &selected,
                const Vec3 &origin, const Vec3 &direction, double angle) {
    if (input.isSheet() || input.shells().size() != 1 || input.faces().empty())
        throw std::domain_error("lo sformo richiede un unico solido convesso");
    if (selected.empty()) throw std::domain_error("scegli almeno una faccia da sformare");
    if (!isFinite(origin) || !isFinite(direction) || norm(direction) < 1e-12 || !std::isfinite(angle)
        || std::fabs(angle) < 1e-10 || std::fabs(angle) >= 89.0 * kPi / 180.0)
        throw std::domain_error("piano, direzione o angolo di sformo non validi (0 < |angolo| < 89 gradi)");
    const Vec3 pull = normalized(direction);
    Box bounds;
    for (VertexId v : input.vertices()) bounds.add(input.vertex(v).point);
    const double tolerance = std::max(kLinearResolution, bounds.diagonal() * 1e-9);
    std::set<int> chosen;
    for (FaceId f : selected) {
        if (!input.contains(f)) throw std::domain_error("una faccia scelta non esiste piu'");
        chosen.insert(f.index);
    }
    struct Support { Vec3 normal; double offset; };
    std::map<int, Support> supports;
    std::map<int, std::set<int>> incident;
    for (FaceId f : input.faces()) {
        const Face &face = input.face(f);
        if (!face.surface || face.surface->type() != SurfaceType::Plane)
            throw std::domain_error("per ora lo sformo supporta solidi convessi con sole facce piane");
        const Frame3 &frame = static_cast<const Plane &>(*face.surface).frame();
        Vec3 n = frame.zDir() * (face.sense ? 1.0 : -1.0);
        double offset = dot(n, frame.origin() - origin);
        for (VertexId v : input.vertices())
            if (dot(n, input.vertex(v).point - origin) - offset > tolerance)
                throw std::domain_error("per ora lo sformo richiede un solido convesso");
        if (chosen.count(f.index)) {
            const Vec3 transverse = n - pull * dot(n, pull);
            const double length = norm(transverse);
            if (length < 1e-8) throw std::domain_error("una faccia scelta e' parallela al piano neutro");
            // La traccia n.(x-origin)=offset sul piano neutro resta immobile.
            // L'angolo e' assoluto rispetto all'estrazione, non incrementale.
            n = transverse / length * std::cos(angle) + pull * std::sin(angle);
            offset *= std::cos(angle) / length;
        }
        supports.emplace(f.index, Support{n, offset});
        for (LoopId l : face.loops)
            for (FinId fin : input.loopFins(l)) incident[input.finStart(fin).index].insert(f.index);
    }
    for (EdgeId e : input.edges())
        if (!input.edge(e).curve || input.edge(e).curve->type() != CurveType::Line)
            throw std::domain_error("per ora lo sformo richiede bordi rettilinei");
    Body result = input;
    for (VertexId v : input.vertices()) {
        const auto &ids = incident.at(v.index);
        std::vector<Support> planes;
        for (int id : ids) planes.push_back(supports.at(id));
        Vec3 point;
        double best = 0.0;
        for (std::size_t i = 0; i < planes.size(); ++i)
            for (std::size_t j = i + 1; j < planes.size(); ++j)
                for (std::size_t k = j + 1; k < planes.size(); ++k) {
                    const auto &a = planes[i], &b = planes[j], &c = planes[k];
                    const double det = dot(a.normal, cross(b.normal, c.normal));
                    if (std::fabs(det) <= best) continue;
                    best = std::fabs(det);
                    point = (a.offset * cross(b.normal, c.normal) + b.offset * cross(c.normal, a.normal)
                             + c.offset * cross(a.normal, b.normal)) / det;
                }
        if (best < 1e-10 || !isFinite(point)) throw std::domain_error("lo sformo rende degeneri i vertici");
        for (const auto &plane : planes)
            if (std::fabs(dot(plane.normal, point) - plane.offset) > tolerance)
                throw std::domain_error("lo sformo richiede una modifica della topologia del vertice");
        for (const auto &entry : supports)
            if (dot(entry.second.normal, point) - entry.second.offset > tolerance)
                throw std::domain_error("angolo troppo grande: le facce si incrociano");
        result.vertex(v).point = origin + point;
        result.vertex(v).tolerance = 0.0;
    }
    for (FaceId f : result.faces()) {
        const auto &s = supports.at(f.index);
        const Vec3 x = std::fabs(s.normal.x()) < 0.8 ? Vec3(1,0,0) : Vec3(0,1,0);
        result.face(f).surface = std::make_shared<Plane>(Frame3(origin + s.normal * s.offset, s.normal, x));
        result.face(f).sense = true;
    }
    for (EdgeId e : result.edges()) {
        const Vec3 a = result.vertex(result.edgeStart(e)).point;
        const Vec3 b = result.vertex(result.edgeEnd(e)).point;
        const Vec3 old = input.vertex(input.edgeEnd(e)).point - input.vertex(input.edgeStart(e)).point;
        if (distance(a,b) <= tolerance || dot(b-a, old) <= 0.0)
            throw std::domain_error("angolo troppo grande: un bordo collassa o si inverte");
        result.edge(e).curve = std::make_shared<Line<3>>(a, b-a);
        result.edge(e).range = {0.0, distance(a,b)};
        result.edge(e).tolerance = 0.0;
    }
    for (FinId f : result.fins()) { result.fin(f).pcurve.reset(); result.fin(f).pcurveTolerance = 0.0; }
    if (computePCurves(result) != 0) throw std::domain_error("non e' possibile ricostruire i bordi dello sformo");
    CheckOptions check;
    check.loopCrossings = true;
    const auto issues = checkBody(result, check);
    if (!issues.empty()) throw std::domain_error("sformo non valido: " + issues.front().message);
    return result;
}
}
