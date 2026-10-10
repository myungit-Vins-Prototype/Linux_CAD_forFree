#include "fk_thicken.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_exchange.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"

namespace ForgeCad::Kernel {
namespace {

// Normale della faccia (con il suo verso) nel punto della superficie piu' vicino a p.
Vec3 faceNormalAt(const Body &body, FaceId f, const Vec3 &p) {
    const Face &face = body.face(f);
    const SurfaceProjection at = projectPoint(*face.surface, p);
    const Vec3 n = normalAt(*face.surface, at.u, at.v);
    return face.sense ? n : -n;
}

// Coppia di curve resa compatibile: stessi nodi (stesso grado, stesso dominio).
void makeCompatible(BSplineCurve<3> &a, BSplineCurve<3> &b) {
    std::vector<double> values;
    for (const BSplineCurve<3> *c : {&a, &b}) {
        const std::vector<double> &k = c->knots();
        for (std::size_t i = std::size_t(c->degree()) + 1; i < std::size_t(c->poleCount()); ++i) values.push_back(k[i]);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    for (double u : values) {
        const int target = std::max(a.multiplicity(u), b.multiplicity(u));
        if (a.multiplicity(u) < target) a = a.insertKnot(u, target - a.multiplicity(u));
        if (b.multiplicity(u) < target) b = b.insertKnot(u, target - b.multiplicity(u));
    }
}

// Bordo libero (edge con una sola fin tra le facce scelte) con la faccia a cui appartiene.
struct FreeEdge {
    EdgeId edge;
    FaceId face;
};

// Tratti dei bordi liberi: un edge chiuso in due meta' (ogni parete ha quattro lati).
std::vector<Interval> edgeRanges(const Edge &edge, double tolerance) {
    if (distance(edge.curve->point(edge.range.lo), edge.curve->point(edge.range.hi)) <= tolerance) {
        const double mid = 0.5 * (edge.range.lo + edge.range.hi);
        return {{edge.range.lo, mid}, {mid, edge.range.hi}};
    }
    return {edge.range};
}

// Parete a quattro lati tra due curve nello stesso parametro (a sotto, b sopra),
// con la superficie data; lati rettilinei agli estremi.
Body wall(const CurvePtr<3> &a, const CurvePtr<3> &b, const Interval &range, const SurfacePtr &surface) {
    detail::RawModel model;
    model.points = {a->point(range.lo), a->point(range.hi), b->point(range.lo), b->point(range.hi)};
    const auto add = [&](int start, int end, CurvePtr<3> curve, const Interval &interval) {
        detail::RawEdge e;
        e.start = start;
        e.end = end;
        e.curve = std::move(curve);
        e.hasRange = true;
        e.range = interval;
        model.edges.push_back(e);
    };
    const auto segment = [&](int i, int j) {
        return std::make_shared<Line<3>>(model.points[std::size_t(i)], model.points[std::size_t(j)] - model.points[std::size_t(i)]);
    };
    add(0, 1, a, range);
    add(1, 3, segment(1, 3), {0.0, distance(model.points[1], model.points[3])});
    add(2, 3, b, range);
    add(0, 2, segment(0, 2), {0.0, distance(model.points[0], model.points[2])});
    detail::RawFace face;
    face.surface = surface;
    face.sense = true;
    face.loops.push_back({{0, true}, {1, true}, {2, false}, {3, false}});
    model.faces.push_back(face);
    return detail::assembleBody(model, false);
}

// Punti di prova dentro le facce: una griglia nel rettangolo (u, v) delle
// SP-curve dei loop, tenuti i punti dentro la faccia, piu' punti dei bordi.
std::vector<std::pair<FaceId, Vec3>> facePoints(const Body &body, const std::vector<FaceId> &faces, int grid) {
    std::vector<std::pair<FaceId, Vec3>> result;
    for (FaceId f : faces) {
        const Face &face = body.face(f);
        double ulo = 1e300, uhi = -1e300, vlo = 1e300, vhi = -1e300;
        for (LoopId l : face.loops)
            for (FinId fin : body.loopFins(l)) {
                const Fin &data = body.fin(fin);
                const Edge &edge = body.edge(data.edge);
                for (int k = 0; k <= 8; ++k) {
                    const Vec3 p = edge.curve->point(edge.range.lo + edge.range.length() * k / 8.0);
                    result.emplace_back(f, p);
                    if (!data.pcurve) continue;
                    const Vec2 uv = data.pcurve->point(edge.range.lo + edge.range.length() * k / 8.0);
                    ulo = std::min(ulo, uv[0]), uhi = std::max(uhi, uv[0]), vlo = std::min(vlo, uv[1]), vhi = std::max(vhi, uv[1]);
                }
            }
        if (!(ulo < uhi && vlo < vhi)) continue;
        for (int i = 1; i < grid; ++i)
            for (int j = 1; j < grid; ++j) {
                const Vec3 p = face.surface->point(ulo + (uhi - ulo) * i / grid, vlo + (vhi - vlo) * j / grid);
                if (classifyPointOnFace(body, f, p, 1e-7) == PointLocation::Inside) result.emplace_back(f, p);
            }
    }
    return result;
}

Body sewSolid(std::vector<const Body *> sheets, double tolerance, const char *what) {
    SewResult sewn = sewSheets(sheets, tolerance, true);
    if (!sewn.solid)
        throw std::domain_error(std::string("spessore: ") + what + " non si chiude in un solido (" + std::to_string(sewn.freeEdges) + " bordi liberi)");
    return std::move(sewn.body);
}

// Parametri del tratto in cui la normale della faccia salta lungo il bordo
// (il bordo attraversa una piega della superficie fuori dai nodi della sua
// curva): rotture per l'approssimazione delle pareti.
std::vector<double> normalBreaks(const Body &sheet, FaceId face, const Edge &edge, const Interval &range, double *largest = nullptr) {
    constexpr int kSamples = 256;
    std::vector<Vec3> normals(kSamples + 1);
    for (int k = 0; k <= kSamples; ++k) normals[std::size_t(k)] = faceNormalAt(sheet, face, edge.curve->point(range.lo + range.length() * k / kSamples));
    const auto angle = [](const Vec3 &a, const Vec3 &b) { return std::asin(std::min(1.0, norm(cross(a, b)))); };
    std::vector<double> breaks;
    for (int k = 0; k < kSamples; ++k) {
        const double here = angle(normals[std::size_t(k)], normals[std::size_t(k + 1)]);
        const double before = k > 0 ? angle(normals[std::size_t(k - 1)], normals[std::size_t(k)]) : 0.0;
        const double after = k + 1 < kSamples ? angle(normals[std::size_t(k + 1)], normals[std::size_t(k + 2)]) : 0.0;
        // Un salto: molto piu' della variazione liscia dei tratti vicini.
        if (!(here > 1e-5 && here > 4.0 * std::max(before, after))) continue;
        double a = range.lo + range.length() * k / kSamples, b = range.lo + range.length() * (k + 1) / kSamples;
        Vec3 na = normals[std::size_t(k)], nb = normals[std::size_t(k + 1)];
        for (int i = 0; i < 50 && b - a > 1e-12 * range.length(); ++i) {
            const double m = 0.5 * (a + b);
            const Vec3 nm = faceNormalAt(sheet, face, edge.curve->point(m));
            if (angle(na, nm) > angle(nm, nb)) b = m, nb = nm;
            else a = m, na = nm;
        }
        breaks.push_back(0.5 * (a + b));
        if (largest) *largest = std::max(*largest, angle(na, nb));
    }
    return breaks;
}

// Lungo la normale: le due superfici a distanza s0 < s1 (0 = la superficie
// stessa) e le pareti rigate lungo la normale ai bordi liberi.
Body thickenAlongNormal(const Body &sheet, const std::vector<FreeEdge> &free, double s0, double s1, double tolerance, double offsetTolerance) {
    std::vector<FaceId> faces = sheet.faces();
    // Facce unite da spigoli tangenti o quasi: lungo una piega le superfici a
    // distanza si staccano o si attraversano di spessore x angolo. Fino a 2
    // gradi (le pieghe di uno sweep lungo una spline solo C1) lo scarto si
    // assorbe nella cucitura; oltre, errore.
    double crease = 0.0;
    for (EdgeId e : sheet.edges()) {
        if (sheet.isLaminar(e)) continue;
        const Edge &edge = sheet.edge(e);
        const FaceId a = sheet.finFace(edge.forward), b = sheet.finFace(edge.backward);
        double angle = 0.0;
        for (int k = 1; k <= 3; ++k) {
            const Vec3 p = edge.curve->point(edge.range.lo + edge.range.length() * k / 4.0);
            angle = std::max(angle, std::asin(std::min(1.0, norm(cross(faceNormalAt(sheet, a, p), faceNormalAt(sheet, b, p))))));
        }
        if (angle > 2.0 * M_PI / 180.0)
            throw std::domain_error("spessore lungo la normale: le facce formano uno spigolo vivo (E" + std::to_string(e.index)
                                    + "); usa lo spessore lungo una direzione o separa le facce");
        crease = std::max(crease, angle);
    }
    const auto side = [&](double s, double accuracy) -> Body {
        if (s == 0.0) return sheet;
        return offsetFaces(sheet, faces, s, accuracy, false, 2.0 * M_PI / 180.0).body;
    };
    Body lower, upper;
    double accuracy = 0.0;  // quella usata per le superfici a distanza
    // Il salto dei nodi solo G1 per lo spessore cresce con lo spessore: prima
    // tolleranze proporzionali (5e-5 e 2e-5 per mm), poi via via piu' strette.
    const double scale = std::max(1.0, std::max(std::fabs(s0), std::fabs(s1)));
    const std::vector<double> accuracies = offsetTolerance > 0.0 ? std::vector<double>{offsetTolerance}
                                                                 : std::vector<double>{5e-5 * scale, 2e-5 * scale, 1e-5, 1e-6, 1e-7};
    for (std::size_t k = 0; k < accuracies.size(); ++k) {
        try {
            lower = side(s0, accuracies[k]);
            upper = side(s1, accuracies[k]);
            accuracy = accuracies[k];
            break;
        } catch (const std::domain_error &) {
            if (k + 1 == accuracies.size()) throw;  // la causa vera (curvatura, spigoli) con la tolleranza piu' stretta
        }
    }
    std::vector<Body> walls;
    double wallAccuracy = 0.0;
    for (const FreeEdge &item : free) {
        const Edge &edge = sheet.edge(item.edge);
        // Tratti del bordo tra le pieghe che attraversa: una parete per tratto
        // (la normale vi e' continua; nelle pieghe resta lo scarto spessore x
        // angolo, assorbito dalla cucitura).
        std::vector<Interval> ranges;
        std::vector<double> creaseAt;  // estremi dei tratti su una piega
        for (const Interval &whole : edgeRanges(edge, tolerance)) {
            double start = whole.lo;
            for (double t : normalBreaks(sheet, item.face, edge, whole, &crease)) {
                ranges.push_back({start, t});
                creaseAt.push_back(t);
                start = t;
            }
            ranges.push_back({start, whole.hi});
        }
        for (const Interval &range : ranges) {
            // Agli estremi su una piega la normale si valuta appena dentro il
            // tratto: vale quella della parte giusta.
            const double inset = 1e-7 * range.length();
            const auto onCrease = [&](double t) { return std::find(creaseAt.begin(), creaseAt.end(), t) != creaseAt.end(); };
            const double from = onCrease(range.lo) ? range.lo + inset : range.lo, to = onCrease(range.hi) ? range.hi - inset : range.hi;
            const auto at = [&](double s) {
                return [&, s, from, to](double t) {
                    const Vec3 p = edge.curve->point(t);
                    return p + s * faceNormalAt(sheet, item.face, edge.curve->point(std::clamp(t, from, to)));
                };
            };
            std::vector<double> breaks = edge.curve->breakpoints(range);
            breaks.erase(std::remove_if(breaks.begin(), breaks.end(), [&](double t) { return !(t > range.lo && t < range.hi); }), breaks.end());
            // Entro 1e-9; se la normale ha micro-salti ai nodi della superficie
            // (raccordi G1 solo numericamente), entro la tolleranza delle
            // superfici a distanza.
            std::shared_ptr<BSplineCurve<3>> f0, f1;
            for (const double fit : {1e-9, accuracy}) {
                try {
                    f0 = fitCurve(at(s0), range, breaks, fit);
                    f1 = fitCurve(at(s1), range, breaks, fit);
                    wallAccuracy = std::max(wallAccuracy, fit);
                    break;
                } catch (const std::domain_error &failure) {
                    if (fit == accuracy || !(accuracy > 1e-9))
                        throw std::domain_error("spessore: parete lungo il bordo E" + std::to_string(item.edge.index) + " non approssimabile ("
                                                + failure.what() + ")");
                }
            }
            BSplineCurve<3> c0 = *f0, c1 = *f1;
            // Bordo di una retta con la normale costante: la parete e' un piano.
            if (edge.curve->type() == CurveType::Line) {
                const Vec3 pa = edge.curve->point(range.lo), pb = edge.curve->point(range.hi);
                const Vec3 na = faceNormalAt(sheet, item.face, pa), nb = faceNormalAt(sheet, item.face, pb);
                if (norm(cross(na, nb)) <= 1e-12) {
                    const auto shifted = [&](double s) -> CurvePtr<3> {
                        if (s == 0.0) return edge.curve;
                        return std::make_shared<Line<3>>(edge.curve->point(0.0) + s * na, edge.curve->derivative(0.0));
                    };
                    const Vec3 direction = normalized(pb - pa);
                    walls.push_back(wall(shifted(s0), shifted(s1), range,
                                         std::make_shared<Plane>(Frame3(pa, normalized(cross(direction, na)), direction))));
                    continue;
                }
            }
            makeCompatible(c0, c1);
            const int n = c0.poleCount();
            std::vector<Vec3> poles(std::size_t(2 * n));
            for (int i = 0; i < n; ++i) {
                poles[std::size_t(2 * i)] = c0.poles()[std::size_t(i)];
                poles[std::size_t(2 * i + 1)] = c1.poles()[std::size_t(i)];
            }
            const auto ruled = std::make_shared<BSplineSurface>(3, 1, c0.knots(), std::vector<double>{0.0, 0.0, 1.0, 1.0}, n, 2, std::move(poles));
            const CurvePtr<3> a = s0 == 0.0 ? edge.curve : CurvePtr<3>(std::make_shared<BSplineCurve<3>>(c0));
            const CurvePtr<3> b = s1 == 0.0 ? edge.curve : CurvePtr<3>(std::make_shared<BSplineCurve<3>>(c1));
            walls.push_back(wall(a, b, range, ruled));
        }
    }
    // Pieghe tra le facce o dentro una faccia (dove un bordo le attraversa):
    // le parti a distanza si scostano al piu' di spessore x angolo.
    const double sewTolerance = 10.0 * tolerance + 1.5 * std::max(std::fabs(s0), std::fabs(s1)) * crease + 2.0 * wallAccuracy;
    std::vector<const Body *> sheets{&lower, &upper};
    for (const Body &w : walls) sheets.push_back(&w);
    return sewSolid(sheets, sewTolerance, "la superficie ispessita lungo la normale");
}

// Lungo una direzione w: la superficie traslata di s0 w e di s1 w e le pareti
// estruse lungo w dai bordi liberi.
Body thickenAlongDirection(const Body &sheet, const std::vector<FreeEdge> &free, const Vec3 &w, double s0, double s1, double tolerance) {
    // Ogni retta parallela a w incontra la superficie una volta: n . w lontano
    // da zero e sempre dello stesso segno.
    double lo = 1e300, hi = -1e300;
    for (const auto &[face, point] : facePoints(sheet, sheet.faces(), 12)) {
        const double c = dot(faceNormalAt(sheet, face, point), w);
        lo = std::min(lo, c), hi = std::max(hi, c);
    }
    if (lo < 1e-3 && hi > -1e-3)
        throw std::domain_error(lo * hi < 0.0 ? "spessore lungo la direzione: la superficie si ripiega rispetto alla direzione (la normale cambia verso)"
                                              : "spessore lungo la direzione: la superficie e' quasi parallela alla direzione in qualche punto");
    const auto moved = [&](double s) -> Body { return s == 0.0 ? sheet : transformBody(sheet, Transform3::translation(s * w)); };
    const Body lower = moved(s0), upper = moved(s1);
    std::vector<Body> walls;
    for (const FreeEdge &item : free) {
        const Edge &edge = sheet.edge(item.edge);
        const auto shifted = [&](double s) -> CurvePtr<3> { return s == 0.0 ? edge.curve : transformCurve(edge.curve, Transform3::translation(s * w)); };
        const CurvePtr<3> base = shifted(s0), top = shifted(s1);
        for (const Interval &range : edgeRanges(edge, tolerance)) {
            SurfacePtr surface;
            if (edge.curve->type() == CurveType::Line) {
                const Vec3 pa = edge.curve->point(range.lo), pb = edge.curve->point(range.hi);
                const Vec3 direction = normalized(pb - pa);
                surface = std::make_shared<Plane>(Frame3(base->point(range.lo), normalized(cross(direction, w)), direction));
            } else {
                surface = std::make_shared<ExtrusionSurface>(base, w);
            }
            walls.push_back(wall(base, top, range, surface));
        }
    }
    std::vector<const Body *> sheets{&lower, &upper};
    for (const Body &w2 : walls) sheets.push_back(&w2);
    return sewSolid(sheets, 10.0 * tolerance, "la superficie ispessita lungo la direzione");
}

}

Body thickenSheet(const Body &body, const std::vector<FaceId> &faces, const ThickenOptions &options) {
    if (!(options.thickness > 0.0) || !std::isfinite(options.thickness)) throw std::domain_error("spessore: il valore deve essere positivo");
    std::vector<FaceId> chosen = faces.empty() ? body.faces() : faces;
    if (chosen.empty()) throw std::domain_error("spessore: nessuna faccia");
    // Tutta una lamina: il body com'e' (rimontarne le facce ricalcolerebbe le
    // SP-curve, che sugli edge tolleranti dei file possono non tornare).
    Body sheet = body.isSheet() && chosen.size() == body.faces().size() ? body : facesAsSheet(body, chosen);
    computePCurves(sheet);
    std::vector<FreeEdge> free;
    for (EdgeId e : sheet.edges())
        if (sheet.isLaminar(e)) {
            const Edge &edge = sheet.edge(e);
            free.push_back({e, sheet.finFace(edge.forward.valid() ? edge.forward : edge.backward)});
        }
    if (free.empty()) throw std::domain_error("spessore: la superficie e' chiusa (nessun bordo libero); usa il guscio o l'offset");
    const double t = options.thickness;
    double s0 = 0.0, s1 = t;
    if (options.side == ThickenSide::Backward) s0 = -t, s1 = 0.0;
    else if (options.side == ThickenSide::Both) s0 = -0.5 * t, s1 = 0.5 * t;
    if (options.useDirection) {
        if (!(norm(options.direction) > 0.0)) throw std::domain_error("spessore: direzione nulla");
        return thickenAlongDirection(sheet, free, normalized(options.direction), s0, s1, options.tolerance);
    }
    return thickenAlongNormal(sheet, free, s0, s1, options.tolerance, options.offsetTolerance);
}

}
