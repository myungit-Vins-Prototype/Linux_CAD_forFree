#include "fk_body_check.h"

#include <algorithm>
#include <exception>
#include <set>
#include <sstream>

#include "fk_parallel.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"

namespace ForgeCad::Kernel {
namespace {

class Report {
public:
    explicit Report(std::vector<CheckIssue> &issues) : issues_(issues) {}

    template <class... Parts>
    void add(CheckCode code, const Parts &...parts) {
        std::ostringstream stream;
        stream.precision(12);
        (stream << ... << parts);
        issues_.push_back({code, stream.str()});
    }

private:
    std::vector<CheckIssue> &issues_;
};

void checkTopology(const Body &body, Report &report) {
    const std::vector<FinId> fins = body.fins();
    std::set<int> finsInLoops;

    for (LoopId l : body.loops()) {
        const Loop &lp = body.loop(l);
        if (!body.contains(lp.face)) {
            report.add(CheckCode::BadReference, "loop ", l.index, ": faccia inesistente");
            continue;
        }
        const std::vector<LoopId> &owner = body.face(lp.face).loops;
        if (std::find(owner.begin(), owner.end(), l) == owner.end())
            report.add(CheckCode::BadOwnership, "loop ", l.index, " non registrato nella faccia ", lp.face.index);
        if (!lp.first.valid()) {
            if (!body.contains(lp.isolatedVertex))
                report.add(CheckCode::BrokenLoop, "loop ", l.index, " senza fin e senza vertice isolato");
            continue;
        }
        if (lp.isolatedVertex.valid())
            report.add(CheckCode::BrokenLoop, "loop ", l.index, " con fin e vertice isolato insieme");
        // Percorso del ciclo: deve richiudersi senza passare due volte dalla stessa fin.
        FinId f = lp.first;
        std::set<int> seen;
        while (true) {
            if (!body.contains(f)) {
                report.add(CheckCode::BadReference, "loop ", l.index, ": fin inesistente nel ciclo");
                break;
            }
            if (!seen.insert(f.index).second) {
                report.add(CheckCode::BrokenLoop, "loop ", l.index, ": il ciclo non torna alla prima fin");
                break;
            }
            finsInLoops.insert(f.index);
            if (body.fin(f).loop != l)
                report.add(CheckCode::BrokenLoop, "fin ", f.index, " nel ciclo del loop ", l.index, " ma assegnata al loop ",
                           body.fin(f).loop.index);
            f = body.fin(f).next;
            if (f == lp.first) break;
        }
    }

    for (FinId f : fins) {
        const Fin &fn = body.fin(f);
        if (!body.contains(fn.loop) || !body.contains(fn.edge) || !body.contains(fn.next) || !body.contains(fn.previous)
            || !body.contains(fn.vertex)) {
            report.add(CheckCode::BadReference, "fin ", f.index, ": riferimento inesistente");
            continue;
        }
        if (body.fin(fn.next).previous != f || body.fin(fn.previous).next != f)
            report.add(CheckCode::BrokenLoop, "fin ", f.index, ": next/previous non reciproci");
        if (!finsInLoops.count(f.index)) report.add(CheckCode::BrokenLoop, "fin ", f.index, " fuori dal ciclo del suo loop");
        const Edge &e = body.edge(fn.edge);
        if ((fn.sense && e.forward != f) || (!fn.sense && e.backward != f))
            report.add(CheckCode::BadEdge, "fin ", f.index, ": verso incoerente con l'edge ", fn.edge.index);
    }

    for (EdgeId e : body.edges()) {
        const Edge &ed = body.edge(e);
        // Edge di bordo di una lamina: una sola fin, in una shell non solida.
        if (ed.forward.valid() != ed.backward.valid()) {
            const FinId only = ed.forward.valid() ? ed.forward : ed.backward;
            if (!body.contains(only) || body.fin(only).edge != e || body.fin(only).sense != ed.forward.valid()) {
                report.add(CheckCode::BadEdge, "edge ", e.index, ": fin di bordo non collegata correttamente");
                continue;
            }
            const Fin &fn = body.fin(only);
            if (body.contains(fn.loop) && body.contains(body.loop(fn.loop).face)) {
                const Face &face = body.face(body.loop(fn.loop).face);
                if (body.contains(face.shell) && body.contains(body.shell(face.shell).region) && body.region(body.shell(face.shell).region).solid)
                    report.add(CheckCode::BadEdge, "edge ", e.index, ": una sola fin in un solido");
            }
            continue;
        }
        if (!body.contains(ed.forward) || !body.contains(ed.backward) || ed.forward == ed.backward) {
            report.add(CheckCode::BadEdge, "edge ", e.index, ": servono due fin distinte");
            continue;
        }
        const Fin &a = body.fin(ed.forward), &b = body.fin(ed.backward);
        if (a.edge != e || b.edge != e || !a.sense || b.sense) {
            report.add(CheckCode::BadEdge, "edge ", e.index, ": fin non collegate correttamente");
            continue;
        }
        if (!body.contains(a.next) || !body.contains(b.next)) continue;  // gia' segnalato
        // La fin forward finisce dove parte la backward e viceversa.
        if (body.fin(a.next).vertex != b.vertex || body.fin(b.next).vertex != a.vertex)
            report.add(CheckCode::BadEdge, "edge ", e.index, ": le due fin non hanno gli stessi estremi");
        if (body.contains(a.loop) && body.contains(b.loop) && body.contains(body.loop(a.loop).face)
            && body.contains(body.loop(b.loop).face)
            && body.face(body.loop(a.loop).face).shell != body.face(body.loop(b.loop).face).shell)
            report.add(CheckCode::BadEdge, "edge ", e.index, ": facce adiacenti in shell diverse");
    }

    for (FaceId f : body.faces()) {
        const Face &fc = body.face(f);
        if (!body.contains(fc.shell)) {
            report.add(CheckCode::BadReference, "faccia ", f.index, ": shell inesistente");
            continue;
        }
        const std::vector<FaceId> &owner = body.shell(fc.shell).faces;
        if (std::find(owner.begin(), owner.end(), f) == owner.end())
            report.add(CheckCode::BadOwnership, "faccia ", f.index, " non registrata nella shell ", fc.shell.index);
        if (fc.loops.empty()) report.add(CheckCode::BadOwnership, "faccia ", f.index, " senza loop");
        for (LoopId l : fc.loops)
            if (!body.contains(l) || body.loop(l).face != f)
                report.add(CheckCode::BadOwnership, "faccia ", f.index, ": loop ", l.index, " non suo");
    }

    for (ShellId s : body.shells()) {
        const Shell &sh = body.shell(s);
        if (!body.contains(sh.region)) {
            report.add(CheckCode::BadReference, "shell ", s.index, ": region inesistente");
            continue;
        }
        const std::vector<ShellId> &owner = body.region(sh.region).shells;
        if (std::find(owner.begin(), owner.end(), s) == owner.end())
            report.add(CheckCode::BadOwnership, "shell ", s.index, " non registrata nella region ", sh.region.index);
        if (sh.faces.empty()) report.add(CheckCode::BadOwnership, "shell ", s.index, " senza facce");
        for (FaceId f : sh.faces)
            if (!body.contains(f) || body.face(f).shell != s)
                report.add(CheckCode::BadOwnership, "shell ", s.index, ": faccia ", f.index, " non sua");
    }

    std::set<int> usedVertices;
    for (FinId f : fins) usedVertices.insert(body.fin(f).vertex.index);
    for (LoopId l : body.loops())
        if (body.loop(l).isolatedVertex.valid()) usedVertices.insert(body.loop(l).isolatedVertex.index);
    for (VertexId v : body.vertices())
        if (!usedVertices.count(v.index)) report.add(CheckCode::DanglingVertex, "vertice ", v.index, " non usato");
}

void checkEuler(const Body &body, Report &report) {
    for (ShellId s : body.shells())
        if (body.region(body.shell(s).region).solid && shellGenus(body, s) < 0)
            report.add(CheckCode::EulerViolation, "shell ", s.index, ": la formula di Eulero-Poincare' non torna");
}

double vertexTolerance(const Body &body, VertexId v, double edgeTolerance) {
    return std::max({kLinearResolution, body.vertex(v).tolerance, edgeTolerance});
}

// Poligono campionato del loop (ogni fin nel suo verso, senza ripetere i vertici).
std::vector<Vec3> sampleLoop(const Body &body, LoopId l, int samplesPerFin = 16) {
    std::vector<Vec3> points;
    for (FinId f : body.loopFins(l))
        for (int i = 0; i < samplesPerFin; ++i) points.push_back(body.finPoint(f, double(i) / samplesPerFin));
    return points;
}

void checkEdge(const Body &body, EdgeId e, Report &report) {
    const Edge &ed = body.edge(e);
    if (!ed.curve) {
        report.add(CheckCode::MissingGeometry, "edge ", e.index, " senza curva");
        return;
    }
    if (!ed.range.isFinite() || !(ed.range.lo < ed.range.hi)) {
        report.add(CheckCode::BadRange, "edge ", e.index, ": range non valido");
        return;
    }
    const VertexId start = body.edgeStart(e), end = body.edgeEnd(e);
    const double startGap = distance(ed.curve->point(ed.range.lo), body.vertex(start).point);
    const double endGap = distance(ed.curve->point(ed.range.hi), body.vertex(end).point);
    if (startGap > vertexTolerance(body, start, ed.tolerance))
        report.add(CheckCode::VertexOffEdge, "edge ", e.index, ": inizio a ", startGap, " dal vertice ", start.index);
    if (endGap > vertexTolerance(body, end, ed.tolerance))
        report.add(CheckCode::VertexOffEdge, "edge ", e.index, ": fine a ", endGap, " dal vertice ", end.index);

    // La curva deve giacere sulle superfici delle due facce adiacenti.
    const double tolerance = std::max(kLinearResolution, ed.tolerance);
    for (FinId f : {ed.forward, ed.backward}) {
        if (!f.valid()) continue;  // bordo di una lamina
        const FaceId faceId = body.finFace(f);
        const Face &fc = body.face(faceId);
        if (!fc.surface) continue;  // segnalato sotto
        // SP-curve: S(p(t)) deve coincidere con C(t) entro la tolleranza
        // dell'edge piu' lo scarto dichiarato dell'approssimazione.
        const Fin &fn = body.fin(f);
        double gap = -1.0;
        if (fn.pcurve) {
            gap = pcurveDeviation(*fc.surface, *ed.curve, *fn.pcurve, ed.range, 8);
            if (!(gap <= tolerance + fn.pcurveTolerance))
                report.add(CheckCode::PCurveOffEdge, "fin ", f.index, " (edge ", e.index, "): SP-curve fino a ", gap,
                           " dalla curva");
        }
        // La distanza dalla superficie non supera |C(t) - S(p(t))|: se l'SP-curve
        // sta gia' entro la tolleranza le proiezioni (lente sulle B-spline con
        // migliaia di pezze) non servono.
        if (gap >= 0.0 && gap <= tolerance) continue;
        double worst = 0.0;
        for (int i = 0; i <= 8; ++i) {
            const Vec3 p = ed.curve->point(ed.range.lo + ed.range.length() * i / 8.0);
            worst = std::max(worst, projectPoint(*fc.surface, p).distance);
        }
        if (worst > tolerance)
            report.add(CheckCode::EdgeOffFace, "edge ", e.index, ": fino a ", worst, " dalla superficie della faccia ",
                       faceId.index);
    }
}

void checkGeometry(const Body &body, Report &report) {
    // Gli edge si controllano in parallelo (proiezioni sulle superfici: e' la
    // parte lunga sui corpi importati); i messaggi restano nell'ordine degli edge.
    std::vector<EdgeId> edges;
    for (EdgeId e : body.edges()) edges.push_back(e);
    std::vector<std::vector<CheckIssue>> found(edges.size());
    std::vector<std::exception_ptr> failures(edges.size());
    parallelFor(edges.size(), edges.size() >= 256 ? threadCount(0) : 1u, [&](std::size_t k) {
        try {
            Report local(found[k]);
            checkEdge(body, edges[k], local);
        } catch (...) {
            failures[k] = std::current_exception();  // si rilancia dopo, come in sequenza
        }
    });
    for (const std::exception_ptr &failure : failures)
        if (failure) std::rethrow_exception(failure);
    for (const std::vector<CheckIssue> &issues : found)
        for (const CheckIssue &issue : issues) report.add(issue.code, issue.message);

    for (FaceId f : body.faces()) {
        const Face &fc = body.face(f);
        if (!fc.surface) {
            report.add(CheckCode::MissingGeometry, "faccia ", f.index, " senza superficie");
            continue;
        }
        if (fc.surface->type() != SurfaceType::Plane) continue;
        // Faccia piana: area con segno di ogni loop rispetto alla normale della
        // faccia. Il loop esterno deve essere l'unico positivo e il piu' grande.
        const Frame3 &frame = static_cast<const Plane &>(*fc.surface).frame();
        const double sign = fc.sense ? 1.0 : -1.0;
        std::vector<double> areas;
        for (LoopId l : fc.loops) {
            if (!body.loop(l).first.valid()) continue;
            const std::vector<Vec3> points = sampleLoop(body, l);
            double area = 0.0;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const Vec3 a = frame.toLocal(points[i]), b = frame.toLocal(points[(i + 1) % points.size()]);
                area += a.x() * b.y() - b.x() * a.y();
            }
            areas.push_back(0.5 * sign * area);
        }
        if (areas.empty()) continue;
        const auto largest = std::max_element(areas.begin(), areas.end(),
                                              [](double a, double b) { return std::fabs(a) < std::fabs(b); });
        const int positive = int(std::count_if(areas.begin(), areas.end(), [](double a) { return a > 0.0; }));
        if (*largest <= 0.0 || positive != 1)
            report.add(CheckCode::LoopOrientation, "faccia ", f.index, ": loop orientati male (esterno antiorario, fori orari)");
    }
}

}

int shellGenus(const Body &body, ShellId s) {
    std::set<int> vertices, edges;
    int faces = 0, loops = 0;
    for (FaceId f : body.shell(s).faces) {
        ++faces;
        for (LoopId l : body.face(f).loops) {
            ++loops;
            const Loop &lp = body.loop(l);
            if (lp.isolatedVertex.valid()) vertices.insert(lp.isolatedVertex.index);
            for (FinId fin : body.loopFins(l)) {
                vertices.insert(body.fin(fin).vertex.index);
                edges.insert(body.fin(fin).edge.index);
            }
        }
    }
    const int chi = int(vertices.size()) - int(edges.size()) + faces - (loops - faces);
    if (chi > 2 || (2 - chi) % 2 != 0) return -1;
    return (2 - chi) / 2;
}

namespace {

// Loop della stessa faccia che si toccano o si incrociano: SP-curve di loop
// diversi intersecate nello spazio (u, v), anche spostate di un periodo.
void checkLoopCrossings(const Body &body, Report &report) {
    for (FaceId f : body.faces()) {
        const Face &face = body.face(f);
        if (face.loops.size() < 2 || !face.surface) continue;
        struct Piece {
            CurvePtr<2> curve;
            Interval range;
            Box box;
            std::vector<VertexId> ends;
        };
        std::vector<std::vector<Piece>> loops;
        double size = 0.0;
        for (LoopId l : face.loops) {
            std::vector<Piece> pieces;
            for (FinId fin : body.loopFins(l)) {
                const Fin &data = body.fin(fin);
                const Edge &edge = body.edge(data.edge);
                if (!data.pcurve || !(edge.range.length() > 0.0)) continue;
                Piece piece{data.pcurve, edge.range, Box(), {body.edgeStart(data.edge), body.edgeEnd(data.edge)}};
                for (int k = 0; k <= 16; ++k) {
                    const Vec2 q = data.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
                    piece.box.add(Vec3(q.x(), q.y(), 0.0));
                }
                size = std::max(size, piece.box.diagonal());
                pieces.push_back(std::move(piece));
            }
            loops.push_back(std::move(pieces));
        }
        const double margin = 0.05 * size + 1e-9;
        const double uPeriod = face.surface->isUPeriodic() ? face.surface->uPeriod() : 0.0;
        const double vPeriod = face.surface->isVPeriodic() ? face.surface->vPeriod() : 0.0;
        bool found = false;
        for (std::size_t a = 0; a < loops.size() && !found; ++a)
            for (std::size_t b = a + 1; b < loops.size() && !found; ++b)
                for (const Piece &p : loops[a])
                    for (const Piece &q : loops[b]) {
                        if (found) break;
                        for (int i = uPeriod > 0.0 ? -1 : 0; i <= (uPeriod > 0.0 ? 1 : 0) && !found; ++i)
                            for (int j = vPeriod > 0.0 ? -1 : 0; j <= (vPeriod > 0.0 ? 1 : 0) && !found; ++j) {
                                const Vec2 shift(i * uPeriod, j * vPeriod);
                                Box moved;
                                moved.add(q.box.lo + Vec3(shift.x(), shift.y(), 0.0));
                                moved.add(q.box.hi + Vec3(shift.x(), shift.y(), 0.0));
                                if (!p.box.padded(margin).overlaps(moved.padded(margin))) continue;
                                const CurvePtr<2> other = i == 0 && j == 0 ? q.curve : translatedCurve(q.curve, shift);
                                CurveCurveIntersection hits;
                                try {
                                    hits = intersectCurves(*p.curve, p.range, *other, q.range, 1e-10 * std::max(1.0, size));
                                } catch (const std::exception &) {
                                    continue;
                                }
                                for (const CurveCurvePoint &hit : hits.points) {
                                    const Vec3 point = face.surface->point(hit.point.x(), hit.point.y());
                                    // Un vertice comune ai due loop (loop che si toccano in un vertice) non conta.
                                    bool shared = false;
                                    for (VertexId v : p.ends)
                                        if (std::find(q.ends.begin(), q.ends.end(), v) != q.ends.end()
                                            && distance(body.vertex(v).point, point) <= std::max(body.vertex(v).tolerance, kLinearResolution) * 10.0)
                                            shared = true;
                                    if (shared) continue;
                                    report.add(CheckCode::LoopsCross, "faccia ", f.index, ": i loop ", a, " e ", b, " si incontrano in (", point.x(), ", ",
                                               point.y(), ", ", point.z(), ")");
                                    found = true;
                                    break;
                                }
                            }
                    }
    }
}

}

std::vector<CheckIssue> checkBody(const Body &body, const CheckOptions &options) {
    std::vector<CheckIssue> issues;
    Report report(issues);
    checkTopology(body, report);
    if (!issues.empty()) return issues;  // i controlli successivi presuppongono una topologia sana
    checkEuler(body, report);
    if (options.geometry) checkGeometry(body, report);
    if (options.geometry && options.loopCrossings && issues.empty()) checkLoopCrossings(body, report);
    return issues;
}

std::string describe(CheckCode code) {
    switch (code) {
    case CheckCode::BadReference: return "riferimento non valido";
    case CheckCode::BrokenLoop: return "loop rotto";
    case CheckCode::BadEdge: return "edge incoerente";
    case CheckCode::BadOwnership: return "appartenenza incoerente";
    case CheckCode::DanglingVertex: return "vertice non usato";
    case CheckCode::EulerViolation: return "formula di Eulero violata";
    case CheckCode::MissingGeometry: return "geometria mancante";
    case CheckCode::BadRange: return "range dell'edge non valido";
    case CheckCode::VertexOffEdge: return "vertice lontano dalla curva";
    case CheckCode::EdgeOffFace: return "edge lontano dalla superficie";
    case CheckCode::LoopOrientation: return "orientamento dei loop";
    case CheckCode::PCurveOffEdge: return "SP-curve lontana dall'edge";
    case CheckCode::LoopsCross: return "loop della faccia che si incrociano";
    }
    return "?";
}

}
