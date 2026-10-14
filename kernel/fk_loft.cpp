#include "fk_loft.h"

#include "fk_cancel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline_basis.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_hermite.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_unify.h"

namespace ForgeCad::Kernel {
namespace {

struct Piece {
    CurvePtr<3> curve;
    Interval range;
};

struct Section {
    std::vector<Piece> pieces;
    Vec3 centroid, normal;
    Frame3 frame;
    bool spatial = false;
};

struct GuideHit {
    Vec3 point;
    double progress = 0.0;
    Vec3 tangent;
    Vec3 curvature;
    std::size_t segment = 0;
    double parameter = 0.0;
};

std::vector<double> spatialGuideParameters(const PathSegment &guide, const Section &section, double tolerance) {
    std::vector<double> result;
    const auto add = [&](double t) {
        const Vec3 p = guide.curve->point(t);
        double nearest = 1e300;
        for (const Piece &piece : section.pieces)
            nearest = std::min(nearest, projectPoint(*piece.curve, p, piece.range).distance);
        if (nearest <= tolerance) result.push_back(t);
    };
    add(guide.range.lo); add(guide.range.hi);
    // Ogni intersezione spaziale compare nelle immagini piane; la verifica
    // 3D scarta le intersezioni apparenti. Tre direzioni evitano immagini
    // degeneri o sovrapposte. Nessuna discretizzazione della curva finale.
    for (const Piece &piece : section.pieces) {
        cancellationPoint();
        bool isolated = false;
        for (const Vec3 &normal : {Vec3(0,0,1), Vec3(0,1,0), Vec3(1,0,0)}) {
            cancellationPoint();
            const Frame3 frame = normalFrame(normal, Vec3());
            const auto a = planarImage(*guide.curve, guide.range, frame);
            const auto b = planarImage(*piece.curve, piece.range, frame);
            if (!a.curve || !b.curve) continue;
            const auto hits = intersectCurves(*a.curve, a.range, *b.curve, b.range, tolerance, true);
            if (hits.overlap) continue;
            isolated = true;
            for (const auto &hit : hits.points) {
                const double t = a.toCurve(hit.s), u = b.toCurve(hit.t);
                if (distance(guide.curve->point(t), piece.curve->point(u)) <= tolerance) add(t);
            }
            break;
        }
        if (!isolated) {
            // Una guida sovrapposta al profilo non determina un attraversamento.
            int matching = 0;
            for (int k=0; k<=8; ++k)
                if (projectPoint(*piece.curve, guide.curve->point(guide.range.lo+guide.range.length()*k/8.0), piece.range).distance <= tolerance) ++matching;
            if (matching > 2) throw std::domain_error("loft: una curva guida si sovrappone a una sezione");
            // Casi degeneri delle immagini: estremi della sezione sul tratto guida.
            for (double u : {piece.range.lo, piece.range.hi}) {
                const auto q = projectPoint(*guide.curve, piece.curve->point(u), guide.range);
                if (q.distance <= tolerance) add(q.parameter);
            }
        }
    }
    return result;
}

GuideHit guideHit(const std::vector<PathSegment> &guide, const Section &section, double tolerance) {
    std::vector<GuideHit> hits;
    double before = 0.0, total = 0.0;
    std::vector<double> lengths;
    for (const PathSegment &segment : guide) {
        cancellationPoint();
        if (!segment.curve) throw std::domain_error("loft: curva guida non valida");
        lengths.push_back(arcLength(*segment.curve, segment.range, 1e-11));
        total += lengths.back();
    }
    if (!(total > tolerance)) throw std::domain_error("loft: curva guida troppo corta");
    for (std::size_t segmentIndex = 0; segmentIndex < guide.size(); ++segmentIndex) {
        cancellationPoint();
        const PathSegment &segment = guide[segmentIndex];
        const PlaneRoots<3> roots = section.spatial ? PlaneRoots<3>{} : planeRoots<3>(*segment.curve, segment.range, section.normal,
                                                   dot(section.normal, section.frame.origin()), tolerance);
        if (!roots.coincident.empty()) throw std::domain_error("loft: una curva guida giace nel piano di una sezione");
        std::vector<double> parameters = section.spatial ? spatialGuideParameters(segment, section, tolerance) : roots.parameters;
        // Una guida termina spesso sulla sezione. Il residuo di un estremo
        // puo' essere positivo di pochi ulp senza attraversare lo zero: in
        // quel caso il cercatore di radici non restituisce l'estremo.
        for (double t : {segment.range.lo, segment.range.hi})
            if (!section.spatial && std::fabs(dot(section.normal, segment.curve->point(t) - section.frame.origin())) <= tolerance)
                parameters.push_back(t);
        for (double t : parameters) {
            cancellationPoint();
            const Vec3 p = segment.curve->point(t);
            double nearest = 1e300;
            for (const Piece &piece : section.pieces)
                nearest = std::min(nearest, projectPoint(*piece.curve, p, piece.range).distance);
            if (nearest > tolerance) continue;
            bool duplicate = false;
            for (const GuideHit &q : hits) duplicate = duplicate || distance(p, q.point) <= tolerance;
            if (!duplicate) {
                const Vec3 d1 = segment.curve->derivative(t), d2 = segment.curve->derivative(t, 2);
                const double speed = norm(d1);
                if (!(speed > 1e-12)) throw std::domain_error("loft: tangente nulla su una curva guida");
                const Vec3 tangent = d1 / speed;
                const Vec3 curvature = (d2 - dot(d2, tangent) * tangent) / (speed * speed);
                hits.push_back({p, (before + arcLength(*segment.curve, {segment.range.lo, t}, 1e-12)) / total, tangent, curvature, segmentIndex, t});
            }
        }
        before += lengths[segmentIndex];
    }
    if (hits.empty()) throw std::domain_error("loft: una curva guida non incontra una sezione");
    if (hits.size() != 1) throw std::domain_error("loft: una curva guida incontra piu' volte la stessa sezione");
    return hits.front();
}

void startAtGuide(Section &section, const Vec3 &hit, double tolerance) {
    std::size_t bestPiece = 0;
    CurveProjection<3> best;
    best.distance = 1e300;
    for (std::size_t k = 0; k < section.pieces.size(); ++k) {
        cancellationPoint();
        const CurveProjection<3> q = projectPoint(*section.pieces[k].curve, hit, section.pieces[k].range);
        if (q.distance < best.distance) best = q, bestPiece = k;
    }
    if (best.distance > tolerance) throw std::domain_error("loft: l'intersezione della guida non appartiene al contorno della sezione");
    const Piece selected = section.pieces[bestPiece];
    const double epsilon = 1e-9 * std::max(1.0, selected.range.length());
    std::vector<Piece> ordered;
    if (best.parameter > selected.range.lo + epsilon && best.parameter < selected.range.hi - epsilon) {
        ordered.push_back({selected.curve, {best.parameter, selected.range.hi}});
        for (std::size_t offset = 1; offset < section.pieces.size(); ++offset)
            ordered.push_back(section.pieces[(bestPiece + offset) % section.pieces.size()]);
        ordered.push_back({selected.curve, {selected.range.lo, best.parameter}});
    } else {
        if (best.parameter >= selected.range.hi - epsilon) bestPiece = (bestPiece + 1) % section.pieces.size();
        for (std::size_t offset = 0; offset < section.pieces.size(); ++offset)
            ordered.push_back(section.pieces[(bestPiece + offset) % section.pieces.size()]);
    }
    section.pieces = std::move(ordered);
}

// Ascissa curvilinea normalizzata del punto sul contorno, dopo che la prima
// guida ne ha fissato la cucitura. Serve a trasformare anche le altre guide in
// vere linee longitudinali della superficie, anziche' usarle soltanto per il
// parametro tra le sezioni.
double sectionProgress(const Section &section, const Vec3 &point, double tolerance, bool closed = true) {
    std::size_t bestPiece = 0;
    CurveProjection<3> best;
    best.distance = 1e300;
    std::vector<double> lengths;
    double total = 0.0;
    for (std::size_t k = 0; k < section.pieces.size(); ++k) {
        cancellationPoint();
        const Piece &piece = section.pieces[k];
        const CurveProjection<3> projection = projectPoint(*piece.curve, point, piece.range);
        if (projection.distance < best.distance) best = projection, bestPiece = k;
        lengths.push_back(arcLength(*piece.curve, piece.range, 1e-13));
        total += lengths.back();
    }
    if (best.distance > tolerance || !(total > tolerance))
        throw std::domain_error("loft: l'intersezione della guida non appartiene al contorno della sezione");
    double before = 0.0;
    for (std::size_t k = 0; k < bestPiece; ++k) before += lengths[k];
    double progress = (before + arcLength(*section.pieces[bestPiece].curve,
                                          {section.pieces[bestPiece].range.lo, best.parameter}, 1e-13)) / total;
    if (closed && progress >= 1.0 - 1e-9) progress = 0.0;
    return progress;
}

Vec3 sectionCentroid(const std::vector<Piece> &pieces) {
    Vec3 sum;
    double weight = 0.0;
    for (const Piece &p : pieces)
        for (int i = 0; i < 32; ++i) {
            cancellationPoint();
            const double t0 = p.range.lo + p.range.length() * i / 32.0, t1 = p.range.lo + p.range.length() * (i + 1) / 32.0;
            const Vec3 a = p.curve->point(t0), b = p.curve->point(t1);
            const double w = distance(a, b);
            sum += (0.5 * w) * (a + b);
            weight += w;
        }
    return sum / weight;
}

// Parametro del tratto a cui l'ascissa curvilinea dall'inizio vale `target`.
double parameterAtLength(const Piece &p, double target) {
    const double total = arcLength(*p.curve, p.range, 1e-13);
    double lo = p.range.lo, hi = p.range.hi, t = p.range.lo + p.range.length() * target / total;
    for (int iteration = 0; iteration < 100; ++iteration) {
        cancellationPoint();
        const double s = arcLength(*p.curve, {p.range.lo, t}, 1e-14) - target;
        if (std::fabs(s) <= 1e-13 * std::max(1.0, total)) break;
        if (s > 0.0) hi = t;
        else lo = t;
        const double speed = norm(p.curve->derivative(t));
        double next = speed > 0.0 ? t - s / speed : 0.5 * (lo + hi);
        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        t = next;
    }
    return t;
}

// Soluzione di A X = B (n x n densa, pivot parziale) per piu' termini noti.
void solveDense(std::vector<std::vector<double>> a, std::vector<std::vector<double>> &b) {
    const std::size_t n = a.size();
    for (std::size_t c = 0; c < n; ++c) {
        cancellationPoint();
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        if (!(std::fabs(a[pivot][c]) > 1e-300)) throw std::domain_error("loft: sistema d'interpolazione singolare");
        std::swap(a[c], a[pivot]);
        std::swap(b[c], b[pivot]);
        for (std::size_t r = c + 1; r < n; ++r) {
            cancellationPoint();
            const double f = a[r][c] / a[c][c];
            if (f == 0.0) continue;
            for (std::size_t k = c; k < n; ++k) a[r][k] -= f * a[c][k];
            for (std::size_t k = 0; k < b[r].size(); ++k) b[r][k] -= f * b[c][k];
        }
    }
    for (std::size_t c = n; c-- > 0;) {
        cancellationPoint();
        for (std::size_t r = c + 1; r < n; ++r)
            for (std::size_t k = 0; k < b[c].size(); ++k) b[c][k] -= a[c][r] * b[r][k];
        for (std::size_t k = 0; k < b[c].size(); ++k) b[c][k] /= a[c][c];
    }
}

using Homogeneous = std::array<double, 4>;

Homogeneous add(const Homogeneous &a, const Homogeneous &b, double factor = 1.0) {
    Homogeneous result;
    for (int k = 0; k < 4; ++k) result[k] = a[k] + factor * b[k];
    return result;
}
Homogeneous scaled(const Homogeneous &a, double factor) {
    Homogeneous result;
    for (int k = 0; k < 4; ++k) result[k] = factor * a[k];
    return result;
}

Vec3 contactNormal(const std::vector<LoftContact> &contacts, const Vec3 &point, double tolerance) {
    Vec3 normal;
    bool found = false;
    for (const LoftContact &contact : contacts) {
        cancellationPoint();
        if (!contact.body || !contact.face.valid()) throw std::domain_error("loft: faccia adiacente non valida");
        const Face &face = contact.body->face(contact.face);
        double nearest = 1e300;
        for (LoopId loop : face.loops)
            for (FinId fin : contact.body->loopFins(loop)) {
                const Edge &edge = contact.body->edge(contact.body->fin(fin).edge);
                nearest = std::min(nearest, projectPoint(*edge.curve, point, edge.range).distance);
            }
        if (nearest > tolerance) continue;
        const auto uv = projectPoint(*face.surface, point);
        if (uv.distance > tolerance) continue;
        const Vec3 candidate = normalAt(*face.surface, uv.u, uv.v);
        if (!(norm(candidate) > 1e-12)) throw std::domain_error("loft: normale nulla sulla faccia adiacente");
        if (found && norm(cross(normal, candidate)) > 1e-4)
            throw std::domain_error("loft: facce adiacenti con tangenti incompatibili sullo stesso bordo");
        normal = normalized(candidate);
        found = true;
    }
    if (!found) throw std::domain_error("loft: il profilo deve coincidere con il bordo delle facce adiacenti scelte");
    return normal;
}

LoftGuideCondition guideCondition(const LoftOptions &options, std::size_t index) {
    return options.guideConditions.empty() ? LoftGuideCondition{options.guideContinuity, options.guideInfluence}
                                          : options.guideConditions.at(index);
}

bool activeGuide(const LoftOptions &options, std::size_t index) {
    const auto condition = guideCondition(options, index);
    return condition.continuity > 0 && condition.influence > 0.0;
}

bool anyActiveGuide(const LoftOptions &options) {
    for (std::size_t i = 0; i < options.guides.size(); ++i) if (activeGuide(options, i)) return true;
    return false;
}

// Interpola la derivata trasversale nello stesso spazio B-spline del bordo,
// mantenendo esatte posizione e pesi del profilo. La tangenza e' verificata
// anche fra i punti di collocazione: non si accetta una faccia incompatibile.
std::vector<Homogeneous> contactDerivatives(const std::vector<BSplineCurve<3>> &curves,
                                          const std::vector<LoftContact> &contacts, bool atEnd,
                                          double span, double influence, double scale,
                                          const std::vector<std::vector<GuideHit>> &guides, const LoftOptions &options) {
    if (contacts.empty() || influence <= 0.0) return {};
    const auto &boundary = atEnd ? curves.back() : curves.front();
    const auto &neighbor = atEnd ? curves[curves.size() - 2] : curves[1];
    const int count = boundary.poleCount(), degree = boundary.degree();
    // Le guide agiscono su un campo continuo, non soltanto nelle ascisse
    // di collocazione vicine al loro punto: quelle correzioni isolate
    // creavano creste e oscillazioni della derivata di contatto.
    struct LocalGuide { double first; GuideHit second; double influence; };
    std::vector<LocalGuide> localGuides;
    for (std::size_t i = 0; i < guides.size(); ++i) {
        cancellationPoint();
            if (!activeGuide(options, i)) continue;
            const auto &guide = guides[i];
            const auto &hit=atEnd?guide.back():guide.front();
            const auto projected=projectPoint(boundary,hit.point,boundary.domain());
            if(projected.distance<=1e-6*scale)
                localGuides.push_back({(projected.parameter-boundary.domain().lo)/boundary.domain().length(),hit,guideCondition(options, i).influence});
        }
    std::sort(localGuides.begin(),localGuides.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    std::array<bool, 2> collapsedEndpoints{false, false};
    std::vector<std::vector<double>> matrix(count, std::vector<double>(count));
    std::vector<std::vector<double>> rhs(count, std::vector<double>(4));
    for (int j = 0; j < count; ++j) {
        cancellationPoint();
        // Media rispetto al primo nodo: sommando direttamente nodi vicini
        // a 1 si possono ottenere due ascisse uguali su una campata di pochi
        // ulp, rendendo singolare una collocazione altrimenti valida.
        const double anchor = boundary.knots()[j + 1];
        long double delta = 0.0L;
        for (int k = 1; k <= degree; ++k) delta += static_cast<long double>(boundary.knots()[j + k]) - anchor;
        const long double location = static_cast<long double>(anchor) + delta / degree;
        const double t = static_cast<double>(location);
        // Anche la base deve usare l'ascissa non arrotondata: una campata
        // di un solo ulp non contiene abbastanza double per degree+1 righe.
        int knotSpan = degree;
        while (knotSpan + 1 < count && boundary.knots()[knotSpan + 1] <= location) ++knotSpan;
        std::vector<long double> precise(degree+1),left(degree+1),right(degree+1);
        precise[0]=1;
        for(int jBasis=1; jBasis<=degree; ++jBasis) {
            left[jBasis]=location-boundary.knots()[knotSpan+1-jBasis];
            right[jBasis]=boundary.knots()[knotSpan+jBasis]-location;
            long double saved=0;
            for(int r=0;r<jBasis;++r) {
                const long double denominator=right[r+1]+left[jBasis-r];
                const long double term=denominator!=0?precise[r]/denominator:0;
                precise[r]=saved+right[r+1]*term;
                saved=left[jBasis-r]*term;
            }
            precise[jBasis]=saved;
        }
        std::vector<double> basis(precise.begin(),precise.end());
        Vec3 homogeneousPoint, homogeneousNeighbor;
        double w = 0.0, wn = 0.0;
        for (int k = 0; k <= degree; ++k) {
            cancellationPoint();
            const int pole = knotSpan - degree + k;
            matrix[j][pole] = basis[k];
            w += basis[k] * boundary.weight(pole);
            wn += basis[k] * neighbor.weight(pole);
            homogeneousPoint += basis[k] * boundary.weight(pole) * boundary.poles()[pole];
            homogeneousNeighbor += basis[k] * neighbor.weight(pole) * neighbor.poles()[pole];
        }
        const Vec3 p = boundary.point(t), normal = contactNormal(contacts, p, 1e-6 * scale);
        const double sign = atEnd ? -1.0 : 1.0;
        const double dw = sign * (wn - w) / span;
        Vec3 natural = (sign * (homogeneousNeighbor - homogeneousPoint) / span - dw * p) / w;
        const double speed = norm(natural);
        for (const auto &entry:localGuides) {
            cancellationPoint();
            const auto &hit=entry.second;
            if(distance(p,hit.point)<=1e-6*scale && std::fabs(dot(normal,hit.tangent))>1e-4)
                throw std::domain_error("loft: tangente della guida incompatibile con la faccia adiacente");
        }
        if(!localGuides.empty()) {
            const double u=std::clamp((t-boundary.domain().lo)/boundary.domain().length(),0.0,1.0);
            const double right=u*u*(3-2*u);
            const Vec3 original=natural;
            for(const auto &entry:localGuides) {
                Vec3 tangent=entry.second.tangent;
                if(dot(tangent,original)<0) tangent=-tangent;
                const double weight=entry.first<0.5?1-right:right;
                natural+=entry.influence*weight*(speed*tangent-original);
            }
        }
        Vec3 direction = natural - dot(natural, normal) * normal;
        const Vec3 tangent = boundary.derivative(t);
        if (!(norm(cross(direction, tangent)) > 1e-10 * std::max(1.0, norm(natural) * norm(tangent)))) {
            direction = cross(normal, tangent);
            if (dot(direction, natural) < 0.0) direction = -direction;
        }
        const bool endpoint = j == 0 || j + 1 == count;
        const bool collapsed = endpoint && speed <= 1e-12
            && distance(p, neighbor.point(t)) <= 1e-12 * scale;
        if (!(norm(direction) > 1e-12) || (!(norm(natural) > 1e-12) && !collapsed))
            throw std::domain_error("loft: derivata trasversale non determinabile sul bordo");
        if (collapsed) collapsedEndpoints[j == 0 ? 0 : 1] = true;
        // Un vertice comune ai due profili ha derivata trasversale nulla:
        // la tangenza geometrica si verifica tramite il limite sul bordo.
        const Vec3 derivative = collapsed ? Vec3() : influence * speed * normalized(direction);
        const Vec3 h = w * derivative + dw * p;
        rhs[j] = {h.x(), h.y(), h.z(), dw};
    }
    solveDense(matrix, rhs);
    std::vector<Homogeneous> result;
    for (const auto &r : rhs) result.push_back({r[0], r[1], r[2], r[3]});
    const auto verifyTangent = [&](double t) {
        const int knotSpan = detail::findSpan(boundary.knots(), degree, count, t);
        std::vector<double> basis(degree + 1);
        detail::basisFunctionDerivatives(boundary.knots(), knotSpan, t, degree, 0, basis.data());
        Homogeneous h{};
        for (int k = 0; k <= degree; ++k) h = add(h, result[knotSpan - degree + k], basis[k]);
        const Vec3 p = boundary.point(t), normal = contactNormal(contacts, p, 1e-6 * scale);
        const Vec3 d(h[0] - h[3] * p.x(), h[1] - h[3] * p.y(), h[2] - h[3] * p.z());
        const Vec3 faceNormal = cross(boundary.derivative(t), d);
        if (!(norm(faceNormal) > 1e-12) || norm(cross(normalized(faceNormal), normal)) > 1e-4)
            throw std::domain_error("loft: tangenza alla faccia non risolta entro la tolleranza angolare");
    };
    for (int sample = 0; sample <= 128; ++sample) {
        cancellationPoint();
        if ((sample == 0 && collapsedEndpoints[0]) || (sample == 128 && collapsedEndpoints[1])) continue;
        verifyTangent(boundary.domain().lo + boundary.domain().length() * sample / 128.0);
    }
    // Non saltare semplicemente il vertice singolare: controllare il limite
    // da dentro la prima/ultima campata, con la stessa tolleranza angolare.
    for (int end = 0; end < 2; ++end) {
        cancellationPoint();
        if (!collapsedEndpoints[end]) continue;
        const double t = end == 0 ? boundary.domain().lo : boundary.domain().hi;
        double adjacent = end == 0 ? boundary.domain().hi : boundary.domain().lo;
        for (double knot : boundary.knots()) {
            cancellationPoint();
            if (end == 0 && knot > t) adjacent = std::min(adjacent, knot);
            if (end == 1 && knot < t) adjacent = std::max(adjacent, knot);
        }
        for (double fraction : {1e-2, 1e-3, 1e-4, 1e-5, 1e-6})
            verifyTangent(t + fraction * (adjacent - t));
    }
    return result;
}

// Quintiche di Hermite per campata: valori, prima e seconda derivata sono
// condivisi nei nodi, quindi la geometria e' C2. G1 forza la direzione di
// uscita normale al piano della sezione; G2 forza anche curvatura nulla nella
// direzione longitudinale. L'influenza miscela la condizione con la forma
// libera stimata dalle sezioni vicine.
std::vector<Homogeneous> hermiteRow(const std::vector<Homogeneous> &value, const std::vector<double> &v,
                                   const Vec3 &startDirection, const Vec3 &endDirection,
                                   int startContinuity, int endContinuity, double startInfluence, double endInfluence,
                                   const std::vector<GuideHit> *guide = nullptr, double guideInfluence = 0.0,
                                   int guideContinuity = 1, const Homogeneous *startDerivative = nullptr,
                                   const Homogeneous *endDerivative = nullptr) {
    const std::size_t n = value.size();
    std::vector<Homogeneous> first(n), second(n);
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        if (i == 0) first[i] = scaled(add(value[1], value[0], -1.0), 1.0 / (v[1] - v[0]));
        else if (i + 1 == n) first[i] = scaled(add(value[i], value[i - 1], -1.0), 1.0 / (v[i] - v[i - 1]));
        else first[i] = scaled(add(value[i + 1], value[i - 1], -1.0), 1.0 / (v[i + 1] - v[i - 1]));
    }
    if (n > 2) {
        for (std::size_t i = 1; i + 1 < n; ++i) {
            cancellationPoint();
            const double before = v[i] - v[i - 1], after = v[i + 1] - v[i];
            second[i] = scaled(add(scaled(add(value[i + 1], value[i], -1.0), 1.0 / after),
                                   scaled(add(value[i], value[i - 1], -1.0), 1.0 / before), -1.0),
                               2.0 / (before + after));
        }
        second.front() = second[1];
        second.back() = second[n - 2];
    }
    if (guide && guide->size() == n && guideInfluence > 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
            cancellationPoint();
            const double w = value[i][3], dw = first[i][3], ddw = second[i][3];
            const Vec3 p(value[i][0] / w, value[i][1] / w, value[i][2] / w);
            Vec3 dp((first[i][0] - dw * p.x()) / w, (first[i][1] - dw * p.y()) / w, (first[i][2] - dw * p.z()) / w);
            Vec3 tangent = (*guide)[i].tangent;
            if (dot(dp, tangent) < 0.0) tangent = -tangent;
            const double speed = norm(dp);
            const Vec3 wantedFirst = speed * tangent;
            Vec3 ddp((second[i][0] - ddw * p.x() - 2.0 * dw * dp.x()) / w,
                     (second[i][1] - ddw * p.y() - 2.0 * dw * dp.y()) / w,
                     (second[i][2] - ddw * p.z() - 2.0 * dw * dp.z()) / w);
            const Vec3 wantedSecond = speed * speed * (*guide)[i].curvature + dot(ddp, tangent) * tangent;
            if (guideContinuity >= 1) dp = (1.0 - guideInfluence) * dp + guideInfluence * wantedFirst;
            if (guideContinuity >= 2) ddp = (1.0 - guideInfluence) * ddp + guideInfluence * wantedSecond;
            first[i][0] = w * dp.x() + dw * p.x();
            first[i][1] = w * dp.y() + dw * p.y();
            first[i][2] = w * dp.z() + dw * p.z();
            second[i][0] = w * ddp.x() + 2.0 * dw * dp.x() + ddw * p.x();
            second[i][1] = w * ddp.y() + 2.0 * dw * dp.y() + ddw * p.y();
            second[i][2] = w * ddp.z() + 2.0 * dw * dp.z() + ddw * p.z();
        }
    }
    const auto constrain = [&](std::size_t i, const Vec3 &direction, int continuity, double influence) {
        if (continuity == 0 || influence <= 0.0) return;
        const double w = value[i][3], dw = first[i][3], ddw = second[i][3];
        const Vec3 p(value[i][0] / w, value[i][1] / w, value[i][2] / w);
        Vec3 dp((first[i][0] - dw * p.x()) / w, (first[i][1] - dw * p.y()) / w, (first[i][2] - dw * p.z()) / w);
        Vec3 axis = normalized(direction);
        if (dot(dp, axis) < 0.0) axis = -axis;
        const Vec3 wanted = norm(dp) * axis;
        dp = (1.0 - influence) * dp + influence * wanted;
        first[i][0] = w * dp.x() + dw * p.x();
        first[i][1] = w * dp.y() + dw * p.y();
        first[i][2] = w * dp.z() + dw * p.z();
        if (continuity < 2) return;
        Vec3 ddp((second[i][0] - ddw * p.x() - 2.0 * dw * dp.x()) / w,
                 (second[i][1] - ddw * p.y() - 2.0 * dw * dp.y()) / w,
                 (second[i][2] - ddw * p.z() - 2.0 * dw * dp.z()) / w);
        ddp *= 1.0 - influence;
        second[i][0] = w * ddp.x() + 2.0 * dw * dp.x() + ddw * p.x();
        second[i][1] = w * ddp.y() + 2.0 * dw * dp.y() + ddw * p.y();
        second[i][2] = w * ddp.z() + 2.0 * dw * dp.z() + ddw * p.z();
    };
    constrain(0, startDirection, startContinuity, startInfluence);
    constrain(n - 1, endDirection, endContinuity, endInfluence);
    if (startDerivative) first.front() = *startDerivative;
    if (endDerivative) first.back() = *endDerivative;
    std::vector<Homogeneous> poles;
    poles.push_back(value.front());
    for (std::size_t i = 0; i + 1 < n; ++i) {
        cancellationPoint();
        const double h = v[i + 1] - v[i];
        poles.push_back(add(value[i], first[i], h / 5.0));
        poles.push_back(add(add(value[i], first[i], 2.0 * h / 5.0), second[i], h * h / 20.0));
        poles.push_back(add(add(value[i + 1], first[i + 1], -2.0 * h / 5.0), second[i + 1], h * h / 20.0));
        poles.push_back(add(value[i + 1], first[i + 1], -h / 5.0));
        poles.push_back(value[i + 1]);
    }
    return poles;
}

// Segue l'intera guida entro la precisione del kernel, conservando il jet
// tangente del loft nei punti di sezione. Il parametro della guida viene
// rimappato con quintiche; i suoi nodi sono sempre punti di suddivisione.
BSplineCurve<3> followingGuide(const std::vector<PathSegment> &path, const std::vector<GuideHit> &hits,
                              const std::vector<double> &v, const BSplineCurve<3> &base, double influence, double tolerance) {
    std::vector<double> offsets{0.0};
    for (const auto &piece : path) offsets.push_back(offsets.back() + piece.range.length());
    const auto coordinate = [&](const GuideHit &hit) { return offsets[hit.segment] + hit.parameter - path[hit.segment].range.lo; };
    struct SourceNode {double at; std::size_t piece; double parameter;};
    std::vector<SourceNode> sourceNodes;
    for(std::size_t k=0;k<path.size();++k) {
        auto nodes=path[k].curve->breakpoints(path[k].range);
        nodes.push_back(path[k].range.lo);nodes.push_back(path[k].range.hi);
        for(double u:nodes)sourceNodes.push_back({offsets[k]+u-path[k].range.lo,k,u});
    }
    std::sort(sourceNodes.begin(),sourceNodes.end(),[](const SourceNode&a,const SourceNode&b){return a.at<b.at;});
    const auto segmentAt = [&](double t,bool left) {
        std::size_t k=0;
        while(k+1<path.size() && (t>offsets[k+1] || (!left && t==offsets[k+1])))++k;
        return k;
    };
    const auto evaluate = [&](double t, bool left, Vec3 *out) {
        std::size_t k=segmentAt(t,left);
        double u=std::clamp(path[k].range.lo+t-offsets[k],path[k].range.lo,path[k].range.hi);
        const double eps=64*std::numeric_limits<double>::epsilon()*std::max(1.0,std::fabs(offsets.back()));
        auto node=std::lower_bound(sourceNodes.begin(),sourceNodes.end(),t,
            [](const SourceNode&n,double at){return n.at<at;});
        if(node==sourceNodes.end() || (node!=sourceNodes.begin() && std::fabs(std::prev(node)->at-t)<std::fabs(node->at-t)))
            if(node!=sourceNodes.begin())--node;
        // La bisezione puo' finire a pochi ulp dal nodo. Conservare il nodo
        // originale e il suo lato, senza cancellare dettagli geometrici.
        if(node!=sourceNodes.end() && std::fabs(node->at-t)<=eps) {
            const double at=node->at;
            const std::size_t piece=segmentAt(at,left);
            auto exact=std::lower_bound(sourceNodes.begin(),sourceNodes.end(),at,
                [](const SourceNode&n,double value){return n.at<value;});
            for(;exact!=sourceNodes.end() && exact->at==at;++exact)if(exact->piece==piece) {
                if(distance(path[k].curve->point(u),path[piece].curve->point(exact->parameter))<=tolerance/16) {
                    k=piece;u=exact->parameter;
                }
                break;
            }
        }
        const auto &piece=path[k];
        if(left && u>piece.range.lo) {
            if(piece.curve->type()==CurveType::BSpline)piece.curve->evaluateLeft(u,2,out);
            else piece.curve->evaluate(std::nextafter(u,piece.range.lo),2,out);
        } else piece.curve->evaluate(u,2,out);
    };
    std::vector<BSplineCurve<3>> maps;
    std::vector<double> breaks = v;
    for (std::size_t i = 0; i + 1 < hits.size(); ++i) {
        cancellationPoint();
        const double a = coordinate(hits[i]), b = coordinate(hits[i + 1]), h = v[i + 1] - v[i];
        Vec3 source[3], target[3];
        double speed[2], acceleration[2];
        for (int end = 0; end < 2; ++end) {
            cancellationPoint();
            const double t = end ? b : a;
            evaluate(t, (b > a) == bool(end), source);
            if (end) base.evaluateLeft(v[i + 1], 2, target);
            else base.evaluate(v[i], 2, target);
            const double square = dot(source[1], source[1]);
            if (!(square > 1e-24)) throw std::domain_error("loft: tangente nulla lungo una guida");
            speed[end] = dot(target[1], source[1]) / square;
            acceleration[end] = dot(target[2] - speed[end] * speed[end] * source[2], source[1]) / square;
        }
        const auto mapPiece=[&](double lo,double hi,double t0,double t1,double d0,double d1,double dd0,double dd1) {
            const double span=hi-lo;
            std::vector<Vec3> poles{{t0,0,0},{t0+span*d0/5,0,0},
                {t0+2*span*d0/5+span*span*dd0/20,0,0},
                {t1-2*span*d1/5+span*span*dd1/20,0,0},{t1-span*d1/5,0,0},{t1,0,0}};
            std::vector<double> knots(6,lo);knots.insert(knots.end(),6,hi);
            return BSplineCurve<3>(5,knots,poles);
        };
        const auto monotone=[&](const BSplineCurve<3> &curve) {
            for(int j=1;j<curve.poleCount();++j)
                if((curve.poles()[j].x()-curve.poles()[j-1].x())*(b-a)<0) return false;
            return true;
        };
        auto map=mapPiece(v[i],v[i+1],a,b,speed[0],speed[1],acceleration[0],acceleration[1]);
        if(!monotone(map)) {
            // Jet molto diversi possono richiedere transizioni piu' corte.
            // Accorciare la zona di raccordo conserva velocita'/accelerazione
            // negli estremi senza lasciare tornare indietro il parametro.
            const double slope=(b-a)/h;
            bool done=false;
            for(double delta=h/4;delta>1e-8*h;delta*=0.5) {
                const double c=a+delta*(speed[0]+slope)/2;
                const double d=b-delta*(speed[1]+slope)/2;
                const auto first=mapPiece(v[i],v[i]+delta,a,c,speed[0],slope,acceleration[0],0);
                const auto middle=mapPiece(v[i]+delta,v[i+1]-delta,c,d,slope,slope,0,0);
                const auto last=mapPiece(v[i+1]-delta,v[i+1],d,b,slope,speed[1],0,acceleration[1]);
                if(!monotone(first)||!monotone(middle)||!monotone(last))continue;
                map=joinBezierPieces({first,middle,last},{v[i],v[i]+delta,v[i+1]-delta,v[i+1]});done=true;break;
            }
            if(!done)throw std::domain_error("loft: parametrizzazione della guida incompatibile con le sezioni");
        }
        for(double knot:map.knots()) if(knot>v[i] && knot<v[i+1]) breaks.push_back(knot);
        maps.push_back(std::move(map));
        for (std::size_t k=0;k<path.size();++k) {
            cancellationPoint();
            auto nodes=path[k].curve->breakpoints(path[k].range);
            nodes.push_back(path[k].range.lo);nodes.push_back(path[k].range.hi);
            for(double node:nodes) {
                const double t=offsets[k]+node-path[k].range.lo;
                if (!(t>std::min(a,b) && t<std::max(a,b))) continue;
                double lo=v[i],hi=v[i+1];
                for(int step=0;step<60;++step) {
                    const double mid=(lo+hi)/2, at=maps.back().point(mid).x();
                    if ((at<t)==(b>a)) lo=mid; else hi=mid;
                }
                const double at=(lo+hi)/2;
                if(at>v[i] && at<v[i+1]) breaks.push_back(at);
            }
        }
    }
    // L'inversione del parametro guida puo' lasciare nodi a pochi ulp dalla
    // sezione, destabilizzando le derivate della superficie raffinata.
    // Conservare le sezioni esatte e verificare i dettagli eliminati sotto.
    auto originalBreaks = breaks;
    std::sort(originalBreaks.begin(),originalBreaks.end());
    originalBreaks.erase(std::unique(originalBreaks.begin(),originalBreaks.end()),originalBreaks.end());
    const double parameterTolerance = std::max(1e-12 * (v.back()-v.front()),
        64*std::numeric_limits<double>::epsilon()*std::max(std::fabs(v.front()),std::fabs(v.back())));
    for(double &knot : breaks)
        for(double section : v)
            if(std::fabs(knot-section)<=parameterTolerance) { knot=section; break; }
    std::sort(breaks.begin(),breaks.end());
    std::vector<double> stableBreaks;
    for(double knot : breaks) {
        const bool anchor = std::find(v.begin(),v.end(),knot)!=v.end();
        if(stableBreaks.empty() || knot-stableBreaks.back()>parameterTolerance)
            stableBreaks.push_back(knot);
        else if(knot!=stableBreaks.back() && anchor) {
            if(std::find(v.begin(),v.end(),stableBreaks.back())!=v.end())
                stableBreaks.push_back(knot); // Non eliminare mai una sezione distinta.
            else stableBreaks.back()=knot;
        }
    }
    breaks=std::move(stableBreaks);
    const auto sample = [&](double at, bool left, Vec3 *out) {
        std::size_t i=0;
        while(i+1<maps.size() && (at>v[i+1] || (!left && at==v[i+1]))) ++i;
        Vec3 map[3],source[3],original[3];
        maps[i].evaluate(at,2,map);
        const bool increasing=coordinate(hits[i+1])>coordinate(hits[i]);
        evaluate(map[0].x(), left==increasing, source);
        if(left) base.evaluateLeft(at,2,original); else base.evaluate(at,2,original);
        const Vec3 d1=source[1]*map[1].x();
        const Vec3 d2=source[2]*map[1].x()*map[1].x()+source[1]*map[2].x();
        out[0]=original[0]+influence*(source[0]-original[0]);
        out[1]=original[1]+influence*(d1-original[1]);
        out[2]=original[2]+influence*(d2-original[2]);
        // I profili di sezione restano esatti anche se l'incontro guida /
        // sezione e' stato accettato con una tolleranza numerica.
        if(std::find(v.begin(),v.end(),at)!=v.end()) out[0]=original[0];
    };
    const auto fit=detail::fitQuinticRows(sample,1,breaks,tolerance,(v.back()-v.front())/16);
    if(fit.error>tolerance) throw std::domain_error("loft: approssimazione della guida fuori tolleranza");
    auto result=detail::rowCurve(fit,0);
    // Controllo indipendente dai punti usati dalla suddivisione adattiva.
    for(std::size_t i=0;i+1<fit.parameters.size();++i)
        for(double fraction:{0.07,0.23,0.43,0.61,0.79,0.93}) {
            const double at=fit.parameters[i]+fraction*(fit.parameters[i+1]-fit.parameters[i]);
            Vec3 exact[3];sample(at,false,exact);
            if(distance(result.point(at),exact[0])>4*tolerance)
                throw std::domain_error("loft: verifica della guida fuori tolleranza");
        }
    // Un piccolo intervallo parametrico puo' contenere dettagli geometrici
    // significativi. Verificare entrambi i lati dei nodi originali.
    const auto verifyDetail = [&](double at,bool left) {
        Vec3 exact[3];sample(at,left,exact);
        if(distance(result.point(at),exact[0])>4*tolerance)
            throw std::domain_error("loft: dettaglio della guida non rappresentabile alla precisione numerica");
    };
    for(double at : originalBreaks) for(bool left : {false,true}) verifyDetail(at,left);
    for(std::size_t i=1;i<originalBreaks.size();++i)
        for(double fraction : {0.25,0.5,0.75})
            verifyDetail(originalBreaks[i-1]+fraction*(originalBreaks[i]-originalBreaks[i-1]),false);
    return result;
}

BSplineCurve<3> refineLoftRow(BSplineCurve<3> curve, const std::vector<double> &knots) {
    for (double knot : knots) {
        cancellationPoint();
        if (knot<=curve.domain().lo || knot>=curve.domain().hi) continue;
        const int have=int(std::count(curve.knots().begin(),curve.knots().end(),knot));
        if (have<5) curve=curve.insertKnot(knot,5-have);
    }
    return curve;
}

Body loftCore(const std::vector<Section> &sections, const LoftOptions &options, bool closed, const std::vector<Frame3> &capPlanes,
              double scale, const std::vector<std::vector<double>> &guideParameters, const std::vector<std::vector<GuideHit>> &guideHits);

// Loft di sezioni piane: corrispondenza (verso, partenza, guide), poi loftCore.
// `closed`: loop chiusi; `caps`: con le facce di testa (solido), altrimenti
// lamina (tubo aperto alle estremita' se i loop sono chiusi).
// Partenza di un loop chiuso allineata a `reference` (direzione dal
// baricentro della partenza della sezione precedente), proiettata sul piano
// del loop: il vertice con il coseno massimo (il loop ruota, senza dividere
// tratti) o, su un loop di una curva sola, il suo punto con il coseno massimo.
void alignStart(Section &s, const Vec3 &reference) {
    const auto planar = [&](const Vec3 &v) {
        const Vec3 w = v - dot(v, s.normal) * s.normal;
        return norm(w) > 0.0 ? w / norm(w) : w;
    };
    const Vec3 want = planar(reference);
    if (s.pieces.size() == 1) {
        const Piece p = s.pieces.front();
        double bestT = p.range.lo, best = -2.0;
        for (int k = 0; k < 720; ++k) {
            cancellationPoint();
            const double t = p.range.lo + p.range.length() * k / 720.0;
            const double c = dot(planar(p.curve->point(t) - s.centroid), want);
            if (c > best) best = c, bestT = t;
        }
        // Raffinamento: massimo del coseno con la bisezione aurea attorno al campione.
        double a = bestT - p.range.length() / 720.0, b = bestT + p.range.length() / 720.0;
        for (int k = 0; k < 80; ++k) {
            cancellationPoint();
            const double m1 = a + 0.381966 * (b - a), m2 = a + 0.618034 * (b - a);
            if (dot(planar(p.curve->point(m1) - s.centroid), want) > dot(planar(p.curve->point(m2) - s.centroid), want)) b = m2;
            else a = m1;
        }
        double t = 0.5 * (a + b);
        if (p.curve->isPeriodic()) {
            s.pieces = {{p.curve, {t, t + p.range.length()}}};
        } else {
            t = std::clamp(t, p.range.lo, p.range.hi);
            if (t - p.range.lo > 1e-9 * p.range.length() && p.range.hi - t > 1e-9 * p.range.length())
                s.pieces = {{p.curve, {t, p.range.hi}}, {p.curve, {p.range.lo, t}}};
        }
    } else {
        std::size_t bestIndex = 0;
        double best = -2.0;
        for (std::size_t k = 0; k < s.pieces.size(); ++k) {
            cancellationPoint();
            const double c = dot(planar(s.pieces[k].curve->point(s.pieces[k].range.lo) - s.centroid), want);
            if (c > best) best = c, bestIndex = k;
        }
        std::rotate(s.pieces.begin(), s.pieces.begin() + std::ptrdiff_t(bestIndex), s.pieces.end());
    }
}

std::vector<Piece> chainPieces(const std::vector<PathSegment> &chain, double tolerance);
Vec3 newellNormal(const std::vector<Piece> &pieces, const Vec3 &centroid);

Body loft(const std::vector<LoftSection> &input, const LoftOptions &options, bool closed, bool caps) {
    const std::size_t n = input.size();
    if (n < 2) throw std::domain_error("loft: servono almeno due sezioni");
    std::vector<Section> sections(n);
    // Sezioni nello spazio, baricentri.
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        const LoftSection &s = input[i];
        if (s.loop.segments.empty() && s.path.empty()) throw std::domain_error("loft: sezione vuota");
        if (!s.path.empty()) {
            sections[i].pieces = chainPieces(s.path, 1e-6);
            sections[i].spatial = true;
        } else for (const ProfileSegment &segment : s.loop.segments) sections[i].pieces.push_back({embedCurve(segment.curve, s.frame), segment.range});
        sections[i].centroid = sectionCentroid(sections[i].pieces);
        sections[i].frame = s.frame;
    }
    double size = 0.0;
    for (std::size_t i = 1; i < n; ++i) size = std::max(size, distance(sections[i].centroid, sections[0].centroid));
    for (const Section &s : sections)
        for (const Piece &p : s.pieces) size = std::max(size, distance(p.curve->point(p.range.lo), s.centroid));
    const double scale = std::max(1.0, size);
    for (std::size_t i = 0; i + 1 < n; ++i)
        if (distance(sections[i].centroid, sections[i + 1].centroid) <= 1e-9 * scale)
            throw std::domain_error("loft: due sezioni consecutive hanno lo stesso baricentro");
    if (options.startContinuity < 0 || options.startContinuity > 2 || options.endContinuity < 0 || options.endContinuity > 2
        || options.guideContinuity < 0 || options.guideContinuity > 2
        || options.guideInfluence < 0.0 || options.guideInfluence > 1.0 || options.startInfluence < 0.0 || options.startInfluence > 1.0
        || options.endInfluence < 0.0 || options.endInfluence > 1.0)
        throw std::domain_error("loft: opzioni di continuita' non valide");

    if (!options.guideConditions.empty() && options.guideConditions.size() != options.guides.size())
        throw std::domain_error("loft: numero di condizioni diverso dal numero di guide");
    for (const auto &condition : options.guideConditions)
        if (condition.continuity < 0 || condition.continuity > 2 || !std::isfinite(condition.influence)
            || condition.influence < 0.0 || condition.influence > 1.0)
            throw std::domain_error("loft: condizione di una guida non valida");

    // Verso dei loop: attorno alla direzione del loft. Catene: inizio dalla stessa parte.
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        const Vec3 direction = i + 1 < n ? sections[i + 1].centroid - sections[i].centroid : sections[i].centroid - sections[i - 1].centroid;
        Section &s = sections[i];
        Vec3 z = input[i].frame.zDir();
        if (s.spatial) {
            z = newellNormal(s.pieces, s.centroid);
            if (norm(z) <= 1e-12 * scale * scale) z = direction;
            z = normalized(z);
            s.frame = normalFrame(z, s.centroid);
        }
        s.normal = dot(direction, z) >= 0.0 ? z : -z;
        if (closed) {
            if (!s.spatial && std::fabs(dot(normalized(direction), z)) < 1e-6) throw std::domain_error("loft: il piano di una sezione contiene la direzione del loft");
            s.normal = dot(direction, z) > 0.0 ? z : -z;
            const bool counterclockwise = s.spatial ? dot(newellNormal(s.pieces, s.centroid), s.normal) > 0.0
                : (signedArea(input[i].loop) > 0.0) == (dot(s.normal, z) > 0.0);
            if (!counterclockwise) {
                std::vector<Piece> reversedPieces;
                for (auto it = s.pieces.rbegin(); it != s.pieces.rend(); ++it)
                    reversedPieces.push_back({reversedCurve(it->curve), {-it->range.hi, -it->range.lo}});
                s.pieces = std::move(reversedPieces);
            }
        } else if (i > 0) {
            const Section &previous = sections[i - 1];
            const Vec3 a = s.pieces.front().curve->point(s.pieces.front().range.lo), b = s.pieces.back().curve->point(s.pieces.back().range.hi);
            const Vec3 pa = previous.pieces.front().curve->point(previous.pieces.front().range.lo),
                       pb = previous.pieces.back().curve->point(previous.pieces.back().range.hi);
            if (distance(a - s.centroid, pa - previous.centroid) + distance(b - s.centroid, pb - previous.centroid)
                > distance(a - s.centroid, pb - previous.centroid) + distance(b - s.centroid, pa - previous.centroid)) {
                std::vector<Piece> reversedPieces;
                for (auto it = s.pieces.rbegin(); it != s.pieces.rend(); ++it)
                    reversedPieces.push_back({reversedCurve(it->curve), {-it->range.hi, -it->range.lo}});
                s.pieces = std::move(reversedPieces);
            }
        }
    }
    // Le guide devono attraversare ogni sezione esattamente una volta. La
    // prima fissa la cucitura, anche quando cade nel mezzo di una curva.
    std::vector<std::vector<double>> guideParameters;
    std::vector<std::vector<GuideHit>> guideHits;
    if (!options.guides.empty()) {
        for (std::size_t guideIndex = 0; guideIndex < options.guides.size(); ++guideIndex) {
            cancellationPoint();
            const std::vector<PathSegment> &guide = options.guides[guideIndex];
            if (guide.empty()) throw std::domain_error("loft: curva guida vuota");
            std::vector<double> parameters;
            std::vector<GuideHit> hits;
            for (std::size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
                try {
                    hits.push_back(guideHit(guide, sections[sectionIndex], 1e-6 * scale));
                } catch (const std::domain_error &failure) {
                    throw std::domain_error(std::string(failure.what()) + " [[loft-section=" + std::to_string(sectionIndex)
                                            + "]] [[loft-guide=" + std::to_string(guideIndex) + "]]");
                }
                parameters.push_back(hits.back().progress);
            }
            const bool increasing = parameters.back() > parameters.front();
            for (std::size_t i = 1; i < parameters.size(); ++i)
                if ((increasing && parameters[i] <= parameters[i - 1] + 1e-9) || (!increasing && parameters[i] >= parameters[i - 1] - 1e-9))
                    throw std::domain_error("loft: una curva guida non attraversa le sezioni nel loro ordine");
            if (!increasing) {
                for (double &parameter : parameters) parameter = 1.0 - parameter;
                for (GuideHit &hit : hits) hit.tangent = -hit.tangent;
            }
            const double first = parameters.front(), span = parameters.back() - first;
            for (double &parameter : parameters) parameter = (parameter - first) / span;
            parameters.front() = 0.0;
            parameters.back() = 1.0;
            guideParameters.push_back(std::move(parameters));
            guideHits.push_back(std::move(hits));
        }
        if (closed)
            for (std::size_t i = 0; i < sections.size(); ++i)
                startAtGuide(sections[i], guideHits.front()[i].point, 1e-6 * scale);
    }
    // Senza guida, punto di partenza dei loop: il piu' vicino (in direzione
    // dal baricentro) alla partenza della sezione precedente.
    if (closed && options.guides.empty())
        for (std::size_t i = 1; i < n; ++i) {
            cancellationPoint();
            const Section &previous = sections[i - 1];
            alignStart(sections[i], previous.pieces.front().curve->point(previous.pieces.front().range.lo) - previous.centroid);
        }

    std::vector<Frame3> capPlanes;
    if (closed && caps)
        for (std::size_t i : {std::size_t(0), n - 1})
        {
            const Frame3 frame = sections[i].frame;
            if (sections[i].spatial)
                for (const Piece &piece : sections[i].pieces)
                    for (const auto &bezier : rationalBezierPieces(*piece.curve,piece.range))
                        for(const Vec3 &pole : bezier.poles())
                            if (std::fabs(dot(sections[i].normal,pole-frame.origin())) > 1e-6*scale)
                                throw std::domain_error("loft: una sezione di testa non e' piana; scegli Solo superficie");
            capPlanes.push_back(Frame3(frame.origin(), sections[i].normal, frame.xDir()));
        }
    return loftCore(sections, options, closed, capPlanes, scale, guideParameters, guideHits);
}

// Parte comune del loft e della superficie rigata: sezioni gia' nello spazio,
// orientate e con la partenza scelta (pieces 3D qualsiasi: `normal` e
// `centroid` servono solo al loft liscio). Divisione nelle frazioni comuni,
// NURBS compatibili, superfici tra le righe dei poli, topologia. `capPlanes`
// vuoto: lamina; altrimenti i piani delle facce di testa della prima e
// dell'ultima sezione (solido, solo con `closed`).
Body loftCore(const std::vector<Section> &sections, const LoftOptions &options, bool closed, const std::vector<Frame3> &capPlanes,
              double scale, const std::vector<std::vector<double>> &guideParameters, const std::vector<std::vector<GuideHit>> &guideHits) {
    const bool ruled = options.ruled && options.startContinuity == 0 && options.endContinuity == 0;
    const std::size_t n = sections.size();
    // Sezioni con lo stesso numero di tratti (piu' di uno): tratto con tratto,
    // cosi' gli spigoli vivi (i vertici di due poligoni) si corrispondono. Altrimenti
    // si dividono alle stesse ascisse curvilinee normalizzate.
    bool sameCount = sections.front().pieces.size() > 1;
    for (const auto &section : sections) sameCount = sameCount && section.pieces.size() == sections.front().pieces.size();
    std::vector<std::vector<double>> fractions(n);
    std::vector<double> all;
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        double total = 0.0;
        std::vector<double> lengths;
        for (const Piece &p : sections[i].pieces) lengths.push_back(arcLength(*p.curve, p.range, 1e-13)), total += lengths.back();
        double cumulative = 0.0;
        for (double l : lengths) {
            cancellationPoint();
            fractions[i].push_back(cumulative / total);
            cumulative += l;
        }
        fractions[i].push_back(1.0);
        all.insert(all.end(), fractions[i].begin(), fractions[i].end());
    }
    // Con piu' guide la loro posizione relativa sul perimetro puo' cambiare
    // tra una sezione e l'altra. Si riparametrizza quindi ogni contorno a
    // tratti: la stessa guida riceve la stessa ascissa comune in tutte le
    // sezioni. In questo modo diventa davvero una riga longitudinale anche su
    // profili di forma o dimensione diversa.
    std::vector<std::vector<double>> localGuidePosition;
    std::vector<std::size_t> guideOrder;
    std::vector<double> commonGuidePosition;
    const bool guidedSplit = !guideHits.empty() && (!closed || guideHits.size() > 1);
    std::vector<std::vector<double>> guideAnchors(n);
    if (guidedSplit) {
        localGuidePosition.assign(guideHits.size(), std::vector<double>(n));
        for (std::size_t guide = 0; guide < guideHits.size(); ++guide)
            for (std::size_t i = 0; i < n; ++i)
                localGuidePosition[guide][i] = sectionProgress(sections[i], guideHits[guide][i].point, 1e-6 * scale, closed);
        for (std::size_t guide = 0; guide < guideHits.size(); ++guide) guideOrder.push_back(guide);
        std::sort(guideOrder.begin(), guideOrder.end(), [&](std::size_t a, std::size_t b) {
            return localGuidePosition[a][0] < localGuidePosition[b][0];
        });
        for (std::size_t i = 0; i < n; ++i) {
            cancellationPoint();
            double previous = -1.0;
            for (std::size_t guide : guideOrder) {
                const double value = localGuidePosition[guide][i];
                if (value <= previous + 1e-7)
                    throw std::domain_error("loft: le curve guida si incrociano o cambiano ordine lungo le sezioni");
                previous = value;
                // Una guida di bordo deve restare sullo stesso bordo in tutte le sezioni.
                for (double endpoint : {0.0, 1.0})
                    if ((std::fabs(localGuidePosition[guide][0] - endpoint) < 1e-7)
                        != (std::fabs(value - endpoint) < 1e-7))
                        throw std::domain_error("loft: una guida passa dal bordo all'interno di una sezione");
            }
        }
        for (auto &anchors : guideAnchors) anchors.push_back(0.0);
        commonGuidePosition.push_back(0.0);
        for (std::size_t guide : guideOrder) {
            cancellationPoint();
            if (localGuidePosition[guide][0] < 1e-7 || localGuidePosition[guide][0] > 1.0 - 1e-7) continue;
            double average = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                guideAnchors[i].push_back(localGuidePosition[guide][i]);
                average += localGuidePosition[guide][i];
            }
            commonGuidePosition.push_back(average / double(n));
        }
        for (auto &anchors : guideAnchors) anchors.push_back(1.0);
        commonGuidePosition.push_back(1.0);
        sameCount = false;
        all.clear();
        const auto remap = [](double value, const std::vector<double> &from, const std::vector<double> &to) {
            std::size_t interval = 0;
            while (interval + 2 < from.size() && value > from[interval + 1] + 1e-12) ++interval;
            const double ratio = (value - from[interval]) / (from[interval + 1] - from[interval]);
            return to[interval] + std::clamp(ratio, 0.0, 1.0) * (to[interval + 1] - to[interval]);
        };
        for (std::size_t i = 0; i < n; ++i)
            for (double position : fractions[i]) all.push_back(remap(position, guideAnchors[i], commonGuidePosition));
        all.insert(all.end(), commonGuidePosition.begin(), commonGuidePosition.end());
    }
    std::sort(all.begin(), all.end());
    std::vector<double> cuts;
    for (double f : all)
        if (cuts.empty() || f - cuts.back() > 1e-7) cuts.push_back(f);
    cuts.back() = 1.0;
    // Un loop di un solo tratto si divide a meta' (facce aperte, senza cucitura).
    if (closed && cuts.size() == 2) cuts = {0.0, 0.5, 1.0};
    const std::size_t M = sameCount ? sections.front().pieces.size() : cuts.size() - 1;  // tratti per sezione
    std::vector<std::vector<Piece>> split(n);
    for (std::size_t i = 0; i < n && sameCount; ++i) split[i] = sections[i].pieces;
    for (std::size_t i = 0; i < n && !sameCount; ++i) {
        cancellationPoint();
        const std::vector<Piece> &pieces = sections[i].pieces;
        const std::vector<double> &f = fractions[i];
        std::vector<double> localCuts = cuts;
        if (guidedSplit) {
            const std::vector<double> &local = guideAnchors[i];
            const auto inverse = [&](double value) {
                std::size_t interval = 0;
                while (interval + 2 < commonGuidePosition.size() && value > commonGuidePosition[interval + 1] + 1e-12) ++interval;
                const double span = commonGuidePosition[interval + 1] - commonGuidePosition[interval];
                const double ratio = span > 0.0 ? (value - commonGuidePosition[interval]) / span : 0.0;
                return local[interval] + std::clamp(ratio, 0.0, 1.0) * (local[interval + 1] - local[interval]);
            };
            for (double &cut : localCuts) cut = inverse(cut);
            localCuts.front() = 0.0;
            localCuts.back() = 1.0;
        }
        double total = 0.0;
        std::vector<double> lengths;
        for (const Piece &p : pieces) lengths.push_back(arcLength(*p.curve, p.range, 1e-13)), total += lengths.back();
        // Per ogni taglio: tratto e parametro.
        std::vector<std::pair<std::size_t, double>> at;
        for (double c : localCuts) {
            cancellationPoint();
            std::size_t k = 0;
            while (k + 1 < pieces.size() && f[k + 1] <= c + 1e-7) ++k;
            if (c >= 1.0 - 1e-12) at.push_back({pieces.size() - 1, pieces.back().range.hi});
            else if (std::fabs(c - f[k]) <= 1e-7) at.push_back({k, pieces[k].range.lo});
            else at.push_back({k, parameterAtLength(pieces[k], (c - f[k]) * total)});
        }
        for (std::size_t m = 0; m < M; ++m) {
            cancellationPoint();
            const auto [k0, t0] = at[m];
            const auto [k1, t1] = at[m + 1];
            if (k0 == k1) split[i].push_back({pieces[k0].curve, {t0, t1}});
            else if (k1 == k0 + 1 && t1 == pieces[k1].range.lo) split[i].push_back({pieces[k0].curve, {t0, pieces[k0].range.hi}});
            else throw std::logic_error("loft: divisione delle sezioni non riuscita");
        }
    }

    // NURBS compatibili per ogni tratto m: stesso grado, stessi nodi.
    // nurbs[m][i]
    std::vector<std::vector<BSplineCurve<3>>> nurbs(M);
    for (std::size_t m = 0; m < M; ++m) {
        cancellationPoint();
        const bool spreadGuides=anyActiveGuide(options);
        int degree = options.startFaces.empty() && options.endFaces.empty() && !spreadGuides ? 1 : 3;
        for (std::size_t i = 0; i < n; ++i)
            for (const BSplineCurve<3> &b : rationalBezierPieces(*split[i][m].curve, split[i][m].range)) degree = std::max(degree, b.degree());
        std::vector<std::vector<BSplineCurve<3>>> bezier(n);
        std::vector<std::vector<double>> breaks(n);
        for (std::size_t i = 0; i < n; ++i) {
            cancellationPoint();
            bezier[i] = standardBezierPieces(*split[i][m].curve, split[i][m].range, degree);
            // Il taglio nel punto di una guida puo' lasciare una pezza di
            // pochi ulp con tutti i poli coincidenti alla precisione macchina.
            // Non e' una campata geometrica: rende nulla la tangente al bordo.
            // Elimina solo residui terminali minuscoli sia in parametro sia
            // in coordinate; conserva campate strette con geometria distinta.
            const double originalSpan = bezier[i].back().domain().hi - bezier[i].front().domain().lo;
            const auto roundoffPiece = [&](const BSplineCurve<3> &piece) {
                const double eps = std::numeric_limits<double>::epsilon();
                if (piece.domain().length() > 128*eps*originalSpan) return false;
                const Vec3 first = piece.poles().front();
                const double tolerance = 8*eps*std::max(1.0,norm(first));
                for (const Vec3 &pole : piece.poles()) if (distance(pole,first) > tolerance) return false;
                return true;
            };
            while (bezier[i].size()>1 && roundoffPiece(bezier[i].front())) bezier[i].erase(bezier[i].begin());
            while (bezier[i].size()>1 && roundoffPiece(bezier[i].back())) bezier[i].pop_back();

            // Conserva il parametro delle sezioni: ripartire i nodi secondo
            // la lunghezza delle pezze cambia le velocita' ai loro confini.
            // Su cerchi/ellissi cio' crea salti di normale nel loft anche
            // quando ogni sezione e' geometricamente liscia.
            const double start = bezier[i].front().domain().lo;
            const double span = bezier[i].back().domain().hi - start;
            breaks[i].push_back(0.0);
            for (std::size_t k = 0; k < bezier[i].size(); ++k)
                breaks[i].push_back(k + 1 == bezier[i].size() ? 1.0 :
                    (bezier[i][k].domain().hi - start) / span);
            // La normalizzazione puo' arrotondare un nodo interno su 0/1
            // o su quello adiacente. Mantieni campate strettamente positive,
            // muovendo soltanto il parametro normalizzato di pochi ulp;
            // i poli/pesi delle pezze, e quindi la geometria, restano esatti.
            for (std::size_t k=1;k+1<breaks[i].size();++k)
                breaks[i][k]=std::max(breaks[i][k],std::nextafter(breaks[i][k-1],1.0));
            for (std::size_t k=breaks[i].size()-2;k>0;--k)
                breaks[i][k]=std::min(breaks[i][k],std::nextafter(breaks[i][k+1],0.0));
        }
        // Allinea gli arrotondamenti fra sezioni diverse, senza mai
        // accorpare due nodi distinti appartenenti alla stessa sezione.
        // L'unione di nodi quasi uguali senza allineamento introdurrebbe
        // campate spurie e salti di normale sui profili conici lisci.
        struct Break { double value; std::size_t section, index; };
        std::vector<Break> entries;
        for(std::size_t i=0;i<n;++i)
            for(std::size_t k=1;k+1<breaks[i].size();++k) entries.push_back({breaks[i][k],i,k});
        std::sort(entries.begin(),entries.end(),[](const Break &a,const Break &b) {
            if(a.value!=b.value) return a.value<b.value;
            return a.section<b.section;
        });
        std::vector<double> knots;
        std::vector<bool> used(n,false);
        for(const auto &entry:entries) {
            if(knots.empty() || entry.value-knots.back()>1e-9 || used[entry.section]) {
                if(knots.empty() || entry.value!=knots.back()) {
                    knots.push_back(entry.value); std::fill(used.begin(),used.end(),false);
                }
            }
            breaks[entry.section][entry.index]=knots.back();
            used[entry.section]=true;
        }
        for (std::size_t i = 0; i < n; ++i) {
            cancellationPoint();
            BSplineCurve<3> curve = joinBezierPieces(bezier[i], breaks[i]);
            for (double k : knots)
                if (curve.multiplicity(k) < degree) curve = curve.insertKnot(k, degree - curve.multiplicity(k));
            if (!options.startFaces.empty() || !options.endFaces.empty())
                for (int sample = 1; sample < 16; ++sample) {
                    const double knot = sample / 16.0;
                    if (curve.multiplicity(knot) == 0) curve = curve.insertKnot(knot);
                }
            nurbs[m].push_back(std::move(curve));
        }
        for (std::size_t i = 1; i < n; ++i)
            if (nurbs[m][i].knots() != nurbs[m][0].knots()) throw std::logic_error("loft: nodi delle sezioni non compatibili");
    }

    // Vertici (inizio di ogni tratto; le catene anche la fine) e loro poli.
    const std::size_t V = closed ? M : M + 1;
    std::vector<std::vector<Vec3>> vertexPoint(n, std::vector<Vec3>(V));
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        for (std::size_t m = 0; m < M; ++m) vertexPoint[i][m] = split[i][m].curve->point(split[i][m].range.lo);
        if (!closed) vertexPoint[i][M] = split[i][M - 1].curve->point(split[i][M - 1].range.hi);
    }
    const auto nextVertex = [&](std::size_t m) { return closed ? (m + 1) % M : m + 1; };

    // Associa ogni guida alla riga longitudinale che attraversa davvero i
    // suoi punti sulle sezioni. Questo comprende le guide aggiuntive; prima
    // soltanto la guida usata come cucitura riceveva le condizioni G1/G2.
    std::vector<const std::vector<GuideHit> *> vertexGuide(V, nullptr);
    for (std::size_t guide = 0; guide < guideHits.size(); ++guide) {
        cancellationPoint();
        std::size_t bestVertex = 0;
        double bestError = 1e300;
        for (std::size_t m = 0; m < V; ++m) {
            cancellationPoint();
            double error = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                error = std::max(error, distance(vertexPoint[i][m], guideHits[guide][i].point));
            if (error < bestError) bestError = error, bestVertex = m;
        }
        if (bestError <= 2e-6 * scale) vertexGuide[bestVertex] = &guideHits[guide];
        else if (activeGuide(options, guide))
            throw std::domain_error("loft: la posizione relativa di una curva guida cambia tra le sezioni; impossibile applicarne la tangenza");
    }

    // Parametri delle sezioni lungo il loft (distanze tra i baricentri).
    std::vector<double> v(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) v[i] = v[i - 1] + distance(sections[i].centroid, sections[i - 1].centroid);
    if (v.back() > 0.0) {
        for (double &value : v) value /= v.back();
        v.back() = 1.0;
    } else if (!ruled) {
        throw std::domain_error("loft: due sezioni consecutive hanno lo stesso baricentro");
    }
    if (!guideParameters.empty()) {
        // La riparametrizzazione eredita esattamente il comportamento storico
        // con condizioni uniformi, ma pesa ogni guida con la propria influenza.
        double total = 0.0;
        for (std::size_t g = 0; g < guideParameters.size(); ++g) total += guideCondition(options, g).influence;
        const double blend = total / double(guideParameters.size());
        if (total > 0.0) for (std::size_t i = 1; i + 1 < n; ++i) {
            double guided = 0.0;
            for (std::size_t g = 0; g < guideParameters.size(); ++g) guided += guideCondition(options, g).influence * guideParameters[g][i];
            v[i] = (1.0 - blend) * v[i] + blend * guided / total;
        }
        for (std::size_t i = 1; i < n; ++i)
            if (v[i] <= v[i - 1] + 1e-8) throw std::domain_error("loft: l'influenza delle guide comprime due sezioni nello stesso parametro");
    }

    // Vertici: in tutte le sezioni se rigato, nella prima e nell'ultima se liscio.
    std::vector<Vec3> vertices;
    for (std::size_t i = 0; i < n; ++i)
        if (ruled || i == 0 || i + 1 == n)
            for (std::size_t m = 0; m < V; ++m) vertices.push_back(vertexPoint[i][m]);
    const auto vertexIndex = [&](std::size_t i, std::size_t m) { return int((ruled ? i : (i == 0 ? 0 : 1)) * V + m); };
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    // Spigoli trasversali: le curve delle sezioni (tutte se rigato, prima e ultima se liscio).
    std::map<std::pair<std::size_t, std::size_t>, int> sectionEdge;
    for (std::size_t i = 0; i < n; ++i) {
        cancellationPoint();
        if (!ruled && i != 0 && i + 1 != n) continue;
        for (std::size_t m = 0; m < M; ++m) {
            cancellationPoint();
            Body::BuildEdge edge;
            edge.start = vertexIndex(i, m);
            edge.end = vertexIndex(i, nextVertex(m));
            edge.curve = split[i][m].curve;
            edge.range = split[i][m].range;
            const double gap = distance(edge.curve->point(edge.range.hi), vertexPoint[i][nextVertex(m)]);
            if (gap > kLinearResolution) edge.tolerance = 1.01 * gap;
            sectionEdge[{i, m}] = int(edges.size());
            edges.push_back(edge);
        }
    }
    // Poli (omogenei) del tratto m nella sezione i; gli estremi sono i vertici.
    const auto homogeneous = [&](std::size_t m, std::size_t i, int j, Vec3 &p, double &w) {
        const BSplineCurve<3> &c = nurbs[m][i];
        w = c.weight(j);
        p = c.poles()[std::size_t(j)];
        if (j == 0) p = vertexPoint[i][m], w = 1.0;
        if (j + 1 == c.poleCount()) p = vertexPoint[i][nextVertex(m)], w = 1.0;
    };
    if (ruled) {
        // Spigoli longitudinali rettilinei, una faccia per campata e tratto.
        std::vector<std::vector<int>> longitudinal(n - 1, std::vector<int>(V));
        for (std::size_t i = 0; i + 1 < n; ++i)
            for (std::size_t m = 0; m < V; ++m) {
                const Vec3 a = vertexPoint[i][m], b = vertexPoint[i + 1][m];
                if (distance(a, b) <= 1e-9 * scale) throw std::domain_error("loft: due sezioni si toccano");
                Body::BuildEdge edge;
                edge.start = vertexIndex(i, m);
                edge.end = vertexIndex(i + 1, m);
                edge.curve = std::make_shared<Line<3>>(a, normalized(b - a));
                edge.range = {0.0, distance(a, b)};
                longitudinal[i][m] = int(edges.size());
                edges.push_back(edge);
            }
        for (std::size_t i = 0; i + 1 < n; ++i)
            for (std::size_t m = 0; m < M; ++m) {
                const BSplineCurve<3> &c = nurbs[m][i];
                const int uCount = c.poleCount();
                std::vector<Vec3> poles(std::size_t(2 * uCount));
                std::vector<double> weights(poles.size());
                bool rational = false;
                for (int j = 0; j < uCount; ++j)
                    for (int s = 0; s < 2; ++s) {
                        homogeneous(m, i + std::size_t(s), j, poles[std::size_t(2 * j + s)], weights[std::size_t(2 * j + s)]);
                        rational = rational || std::fabs(weights[std::size_t(2 * j + s)] - 1.0) > 1e-15;
                    }
                Body::BuildFace face;
                face.surface = std::make_shared<BSplineSurface>(c.degree(), 1, c.knots(), std::vector<double>{0, 0, 1, 1}, uCount, 2, poles,
                                                                rational ? weights : std::vector<double>{});
                // Pezze piane (lati di tronchi di piramide): il piano esatto.
                if (SurfacePtr plane = planarEquivalent(*face.surface)) face.surface = plane;
                face.sense = true;
                face.loops.push_back({{sectionEdge.at({i, m}), true, nullptr, 0.0}, {longitudinal[i][nextVertex(m)], true, nullptr, 0.0},
                                      {sectionEdge.at({i + 1, m}), false, nullptr, 0.0}, {longitudinal[i][m], false, nullptr, 0.0}});
                faces.push_back(std::move(face));
            }
    } else {
        // G0 usa la normale interpolazione del loft. Attivare qui le quintiche
        // anche senza alcun vincolo di derivata era la causa dei rientri vicino
        // alla cucitura mostrati dai loft guidati.
        const bool endpointControlled = options.startContinuity != 0 || options.endContinuity != 0
                                     || anyActiveGuide(options);
        if (endpointControlled) {
            constexpr int q = 5;
            std::vector<double> vKnots(6, 0.0);
            for (std::size_t i = 1; i + 1 < n; ++i)
                vKnots.insert(vKnots.end(), 5, v[i]);
            vKnots.insert(vKnots.end(), 6, 1.0);
            int vCount = int(5 * (n - 1) + 1);
            const std::vector<double> originalVKnots = vKnots;
            std::vector<std::vector<Homogeneous>> startDerivatives(M), endDerivatives(M);
            for (std::size_t m = 0; m < M; ++m) {
                if (options.startContinuity == 1)
                    startDerivatives[m] = contactDerivatives(nurbs[m], options.startFaces, false, v[1] - v[0], options.startInfluence, scale,
                                                            guideHits, options);
                if (options.endContinuity == 1)
                    endDerivatives[m] = contactDerivatives(nurbs[m], options.endFaces, true, v[n - 1] - v[n - 2], options.endInfluence, scale,
                                                          guideHits, options);
            }
            const auto interpolate = [&](std::size_t m, int j, std::vector<Vec3> &poles, std::vector<double> &weights, bool useGuide=true) {
                std::vector<Homogeneous> values(n);
                for (std::size_t i = 0; i < n; ++i) {
                    Vec3 p;
                    double w;
                    homogeneous(m, i, j, p, w);
                    values[i] = {w * p.x(), w * p.y(), w * p.z(), w};
                }
                const bool lastPole = j + 1 == nurbs[m][0].poleCount();
                const auto *guide = j == 0 ? vertexGuide[m] : lastPole ? vertexGuide[nextVertex(m)] : nullptr;
                // Sulle sezioni aperte una guida sola governa la forma anche
                // tra le righe dei vertici: limitarla alla riga di passaggio
                // creerebbe una cresta artificiale su un profilo liscio.
                if (!closed && guideHits.size() == 1) guide = &guideHits.front();
                if(!useGuide) guide=nullptr;
                const auto condition = guide ? guideCondition(options, std::size_t(guide - guideHits.data())) : LoftGuideCondition{0, 0.0};
                if (guide && condition.continuity > 0 && condition.influence > 0.0) {
                    for (bool atEnd : {false, true}) {
                        const auto &contacts = atEnd ? options.endFaces : options.startFaces;
                        if (contacts.empty()) continue;
                        const auto &hit = atEnd ? guide->back() : guide->front();
                        if (std::fabs(dot(contactNormal(contacts, hit.point, 1e-6 * scale), hit.tangent)) > 1e-4)
                            throw std::domain_error("loft: tangente della guida incompatibile con la faccia adiacente");
                    }
                }
                const std::vector<Homogeneous> row = hermiteRow(values, v, sections.front().normal, sections.back().normal,
                                                                 options.startFaces.empty() ? options.startContinuity : 0,
                                                                 options.endFaces.empty() ? options.endContinuity : 0,
                                                                 options.startInfluence, options.endInfluence,
                                                                 guide, condition.influence, condition.continuity,
                                                                 startDerivatives[m].empty() ? nullptr : &startDerivatives[m][j],
                                                                 endDerivatives[m].empty() ? nullptr : &endDerivatives[m][j]);
                poles.clear();
                weights.clear();
                for (const Homogeneous &h : row) {
                    if (!(h[3] > 1e-12)) throw std::domain_error("loft: pesi non positivi con le condizioni alle estremita'");
                    poles.push_back(Vec3(h[0], h[1], h[2]) / h[3]);
                    weights.push_back(h[3]);
                }
                if(vKnots!=originalVKnots) {
                    const auto refined=refineLoftRow(BSplineCurve<3>(q,originalVKnots,poles,weights),vKnots);
                    poles=refined.poles();weights=refined.weights();
                }
            };
            std::vector<std::vector<Vec3>> vertexRows(V);
            std::vector<std::vector<double>> vertexWeights(V);
            std::vector<BSplineCurvePtr<3>> followed(V);
            std::vector<double> commonBreaks = v;
            for (std::size_t m=0;m<V;++m) if (vertexGuide[m] && activeGuide(options, std::size_t(vertexGuide[m] - guideHits.data()))) {
                std::vector<Vec3> row;std::vector<double> weights;
                if(m<M) interpolate(m,0,row,weights);
                else interpolate(M-1,nurbs[M-1][0].poleCount()-1,row,weights);
                const BSplineCurve<3> base(q,originalVKnots,row,weights);
                const std::size_t guide=std::size_t(vertexGuide[m]-guideHits.data());
                auto curve=followingGuide(options.guides[guide],guideHits[guide],v,base,guideCondition(options, guide).influence,1e-9);
                for(double knot:curve.knots()) commonBreaks.push_back(knot);
                followed[m]=std::make_shared<BSplineCurve<3>>(std::move(curve));
            }
            std::sort(commonBreaks.begin(),commonBreaks.end());
            commonBreaks.erase(std::unique(commonBreaks.begin(),commonBreaks.end()),commonBreaks.end());
            vKnots.assign(6,0.0);
            for(double knot:commonBreaks) if(knot>0 && knot<1) vKnots.insert(vKnots.end(),5,knot);
            vKnots.insert(vKnots.end(),6,1.0);vCount=int(vKnots.size())-q-1;
            for(auto &curve:followed) if(curve) curve=std::make_shared<BSplineCurve<3>>(refineLoftRow(*curve,commonBreaks));
            std::vector<int> longitudinal(V);
            for (std::size_t m = 0; m < V; ++m) {
                if (m < M) interpolate(m, 0, vertexRows[m], vertexWeights[m]);
                else interpolate(M - 1, nurbs[M - 1][0].poleCount() - 1, vertexRows[m], vertexWeights[m]);
                if(followed[m]) {
                    vertexRows[m]=followed[m]->poles();
                    vertexWeights[m].assign(vertexRows[m].size(),1.0);
                }
                bool rational = false;
                for (double weight : vertexWeights[m]) rational = rational || std::fabs(weight - 1.0) > 1e-15;
                Body::BuildEdge edge;
                edge.start = vertexIndex(0, m);
                edge.end = vertexIndex(n - 1, m);
                edge.curve = std::make_shared<BSplineCurve<3>>(q, vKnots, vertexRows[m], rational ? vertexWeights[m] : std::vector<double>{});
                edge.range = {0.0, 1.0};
                longitudinal[m] = int(edges.size());
                edges.push_back(edge);
            }
            for (std::size_t m = 0; m < M; ++m) {
                const BSplineCurve<3> &c = nurbs[m][0];
                const int uCount = c.poleCount();
                const bool spreadGuides=anyActiveGuide(options)
                    && (guideHits.size()>1 || followed[m] || followed[nextVertex(m)]);
                const bool originalSpread=guideHits.size()>1;
                std::vector<Vec3> baseLeft,baseRight;std::vector<double> weightLeft,weightRight;
                if(spreadGuides) {
                    interpolate(m,0,baseLeft,weightLeft,!originalSpread);
                    interpolate(m,uCount-1,baseRight,weightRight,!originalSpread);
                }
                std::vector<Vec3> poles(std::size_t(uCount * vCount));
                std::vector<double> weights(poles.size());
                bool rational = false;
                for (int j = 0; j < uCount; ++j) {
                    std::vector<Vec3> row;
                    std::vector<double> rowWeights;
                    if (j == 0) row = vertexRows[m], rowWeights = vertexWeights[m];
                    else if (j + 1 == uCount) row = vertexRows[nextVertex(m)], rowWeights = vertexWeights[nextVertex(m)];
                    else {
                        interpolate(m,j,row,rowWeights,!originalSpread);
                        if(spreadGuides) {
                            // Coefficienti esatti del raccordo cubico 3*u^2-2*u^3
                            // nella base B-spline. La correzione ha derivata U
                            // nulla sulle guide; conserva i profili e distribuisce
                            // l’influenza su tutta la pezza senza alette locali.
                            long double sum=0,square=0,cube=0;
                            for(int k=1;k<=c.degree();++k) {
                                const long double u=(c.knots()[j+k]-c.domain().lo)/c.domain().length();
                                sum+=u;square+=u*u;cube+=u*u*u;
                            }
                            const long double p=c.degree();
                            const double right=double(3*(sum*sum-square)/(p*(p-1))
                                -2*(sum*sum*sum-3*sum*square+2*cube)/(p*(p-1)*(p-2)));
                            for(int i=0;i<vCount;++i) {
                                const Vec3 left=vertexWeights[m][i]*vertexRows[m][i]-weightLeft[i]*baseLeft[i];
                                const Vec3 delta=vertexWeights[nextVertex(m)][i]*vertexRows[nextVertex(m)][i]-weightRight[i]*baseRight[i];
                                row[i]+=((1-right)*left+right*delta)/rowWeights[i];
                            }
                        }
                    }
                    for (int i = 0; i < vCount; ++i) {
                        poles[std::size_t(j * vCount + i)] = row[std::size_t(i)];
                        weights[std::size_t(j * vCount + i)] = rowWeights[std::size_t(i)];
                        rational = rational || std::fabs(rowWeights[std::size_t(i)] - 1.0) > 1e-15;
                    }
                }
                Body::BuildFace face;
                face.surface = std::make_shared<BSplineSurface>(c.degree(), q, c.knots(), vKnots, uCount, vCount, poles,
                                                                rational ? weights : std::vector<double>{});
                // Verificare la superficie assemblata: inseguimento e
                // raffinamento delle guide possono alterare la G1 iniziale.
                for(bool end : {false,true}) {
                    const auto &contacts=end?options.endFaces:options.startFaces;
                    if(contacts.empty())continue;
                    const double at=end?v.back():v.front();
                    for(int sample=0;sample<=128;++sample) {
                        const double u=c.domain().lo+c.domain().length()*sample/128.0;
                        Vec3 d[4];face.surface->evaluate(u,at,1,d);
                        const Vec3 normal=cross(d[Surface::derivativeIndex(1,0,1)],d[Surface::derivativeIndex(0,1,1)]);
                        // Ai vertici collassati il piano tangente e' un
                        // limite; si verificano i campioni regolari vicini.
                        if(norm(normal)<=1e-12 && (sample==0 || sample==128))continue;
                        const Vec3 target=contactNormal(contacts,d[0],1e-6*scale);
                        if(!(norm(normal)>1e-12) || norm(cross(normalized(normal),target))>1e-4)
                            throw std::domain_error("loft: tangenza alla faccia non conservata dopo l'inseguimento delle guide");
                    }
                }
                if (SurfacePtr plane = planarEquivalent(*face.surface)) face.surface = plane;
                face.sense = true;
                // Le righe longitudinali condividono il parametro v della
                // superficie: conservarlo evita una riproiezione ambigua su
                // bordi che ripiegano o hanno una tangente quasi nulla.
                const bool splineSurface = face.surface->type() == SurfaceType::BSpline;
                const CurvePtr<2> right = splineSurface ? std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)) : nullptr;
                const CurvePtr<2> left = splineSurface ? std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)) : nullptr;
                face.loops.push_back({{sectionEdge.at({0, m}), true, nullptr, 0.0}, {longitudinal[nextVertex(m)], true, right, 0.0},
                                          {sectionEdge.at({n - 1, m}), false, nullptr, 0.0}, {longitudinal[m], false, left, 0.0}});
                faces.push_back(std::move(face));
            }
        } else {
        // Interpolazione di grado q con i nodi per media (NURBS Book, A9.1), in coordinate omogenee.
        const int q = std::min<int>(3, int(n) - 1);
        std::vector<double> vKnots(std::size_t(q + 1), 0.0);
        for (std::size_t j = 1; j + q < n; ++j) {
            cancellationPoint();
            double sum = 0.0;
            for (std::size_t k = j; k < j + std::size_t(q); ++k) sum += v[k];
            vKnots.push_back(sum / q);
        }
        vKnots.insert(vKnots.end(), std::size_t(q + 1), 1.0);
        std::vector<std::vector<double>> basis(n, std::vector<double>(n, 0.0));
        std::vector<double> ders(std::size_t(q + 1));
        for (std::size_t k = 0; k < n; ++k) {
            cancellationPoint();
            const int span = detail::findSpan(vKnots, q, int(n), v[k]);
            detail::basisFunctionDerivatives(vKnots, span, v[k], q, 0, ders.data());
            for (int j = 0; j <= q; ++j) basis[k][std::size_t(span - q + j)] = ders[std::size_t(j)];
        }
        const auto interpolate = [&](std::size_t m, int j, std::vector<Vec3> &poles, std::vector<double> &weights) {
            std::vector<std::vector<double>> rhs(n, std::vector<double>(4));
            for (std::size_t i = 0; i < n; ++i) {
                Vec3 p;
                double w;
                homogeneous(m, i, j, p, w);
                rhs[i] = {w * p.x(), w * p.y(), w * p.z(), w};
            }
            solveDense(basis, rhs);
            poles.clear();
            weights.clear();
            for (std::size_t i = 0; i < n; ++i) {
                if (!(rhs[i][3] > 1e-12)) throw std::domain_error("loft: pesi non positivi tra le sezioni (sezioni troppo diverse)");
                poles.push_back(Vec3(rhs[i][0], rhs[i][1], rhs[i][2]) / rhs[i][3]);
                weights.push_back(rhs[i][3]);
            }
        };
        // Righe dei vertici (una sola volta: condivise dai due tratti vicini).
        std::vector<std::vector<Vec3>> vertexRows(V);
        std::vector<int> longitudinal(V);
        for (std::size_t m = 0; m < V; ++m) {
            cancellationPoint();
            std::vector<double> weights;
            if (m < M) interpolate(m, 0, vertexRows[m], weights);
            else interpolate(M - 1, nurbs[M - 1][0].poleCount() - 1, vertexRows[m], weights);
            Body::BuildEdge edge;
            edge.start = vertexIndex(0, m);
            edge.end = vertexIndex(n - 1, m);
            edge.curve = std::make_shared<BSplineCurve<3>>(q, vKnots, vertexRows[m]);
            edge.range = {0.0, 1.0};
            longitudinal[m] = int(edges.size());
            edges.push_back(edge);
        }
        for (std::size_t m = 0; m < M; ++m) {
            cancellationPoint();
            const BSplineCurve<3> &c = nurbs[m][0];
            const int uCount = c.poleCount();
            std::vector<Vec3> poles(std::size_t(uCount) * n);
            std::vector<double> weights(poles.size());
            bool rational = false;
            for (int j = 0; j < uCount; ++j) {
                std::vector<Vec3> row;
                std::vector<double> rowWeights;
                if (j == 0) row = vertexRows[m], rowWeights.assign(n, 1.0);
                else if (j + 1 == uCount) row = vertexRows[nextVertex(m)], rowWeights.assign(n, 1.0);
                else interpolate(m, j, row, rowWeights);
                for (std::size_t i = 0; i < n; ++i) {
                    poles[std::size_t(j) * n + i] = row[i];
                    weights[std::size_t(j) * n + i] = rowWeights[i];
                    rational = rational || std::fabs(rowWeights[i] - 1.0) > 1e-15;
                }
            }
            Body::BuildFace face;
            face.surface = std::make_shared<BSplineSurface>(c.degree(), q, c.knots(), vKnots, uCount, int(n), poles, rational ? weights : std::vector<double>{});
            if (SurfacePtr plane = planarEquivalent(*face.surface)) face.surface = plane;
            face.sense = true;
            // Le righe longitudinali condividono il parametro v della
            // superficie: conservarlo evita una riproiezione ambigua su
            // bordi che ripiegano o hanno una tangente quasi nulla.
            const bool splineSurface = face.surface->type() == SurfaceType::BSpline;
            const CurvePtr<2> right = splineSurface ? std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)) : nullptr;
            const CurvePtr<2> left = splineSurface ? std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)) : nullptr;
            face.loops.push_back({{sectionEdge.at({0, m}), true, nullptr, 0.0}, {longitudinal[nextVertex(m)], true, right, 0.0},
                                  {sectionEdge.at({n - 1, m}), false, nullptr, 0.0}, {longitudinal[m], false, left, 0.0}});
            faces.push_back(std::move(face));
        }
        }
    }
    // Facce di testa: uscenti verso -n nella prima sezione, +n nell'ultima (i loop girano attorno a n).
    if (closed && capPlanes.size() == 2) {
        for (int cap = 0; cap < 2; ++cap) {
            cancellationPoint();
            const std::size_t i = cap == 0 ? 0 : n - 1;
            Body::BuildFace face;
            face.surface = std::make_shared<Plane>(capPlanes[std::size_t(cap)]);
            face.sense = cap == 1;
            std::vector<Body::BuildFin> fins;
            if (cap == 1)
                for (std::size_t m = 0; m < M; ++m) fins.push_back({sectionEdge.at({i, m}), true, nullptr, 0.0});
            else
                for (std::size_t m = M; m-- > 0;) fins.push_back({sectionEdge.at({i, m}), false, nullptr, 0.0});
            face.loops.push_back(std::move(fins));
            faces.push_back(std::move(face));
        }
    }
    Body body = closed && capPlanes.size() == 2 ? Body::build(vertices, edges, faces) : Body::buildSheet(vertices, edges, faces);
    computePCurves(body);
    const std::vector<CheckIssue> issues = checkBody(body);
    if (!issues.empty()) throw std::domain_error("loft: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    // Pezze sulla stessa superficie (i lati piani di un loft rigato divisi dalle
    // frazioni di lunghezza delle altre sezioni) e spigoli allineati: una faccia e
    // un edge (come dopo le booleane).
    return unifySameDomain(body);
}


// Chiusura di una sezione piana (stessa regola di loftSolid).
bool sectionClosed(const LoftSection &s) {
    if (!s.path.empty()) {
        const auto pieces = chainPieces(s.path, 1e-6);
        const Vec3 a = pieces.front().curve->point(pieces.front().range.lo), b = pieces.back().curve->point(pieces.back().range.hi);
        return distance(a,b) <= 1e-6 * std::max(1.0,norm(a));
    }
    if (s.loop.segments.empty()) throw std::domain_error("loft: sezione vuota");
    const ProfileSegment &a = s.loop.segments.front(), &b = s.loop.segments.back();
    return distance(a.start(), b.end()) <= 1e-6 * std::max(1.0, norm(a.start()));
}

Piece reversedPiece(const Piece &p) { return {reversedCurve(p.curve), {-p.range.hi, -p.range.lo}}; }

void reverseChain(std::vector<Piece> &pieces) {
    std::vector<Piece> result;
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it) result.push_back(reversedPiece(*it));
    pieces = std::move(result);
}

Vec3 chainStart(const std::vector<Piece> &pieces) { return pieces.front().curve->point(pieces.front().range.lo); }
Vec3 chainEnd(const std::vector<Piece> &pieces) { return pieces.back().curve->point(pieces.back().range.hi); }

// Tratti di una catena 3D, nell'ordine dato e girati in modo che ognuno
// cominci dove finisce il precedente (estremi entro `tolerance`). Le eliche
// esatte (non tipi di edge) diventano la loro B-spline entro 1e-9.
std::vector<Piece> chainPieces(const std::vector<PathSegment> &chain, double tolerance) {
    if (chain.empty()) throw std::domain_error("superficie rigata: catena vuota");
    std::vector<Piece> pieces;
    for (const PathSegment &segment : chain) {
        cancellationPoint();
        if (!segment.curve || !(segment.range.length() > 0.0)) throw std::domain_error("superficie rigata: tratto non valido");
        CurvePtr<3> curve = segment.curve;
        if (curve->type() == CurveType::Other) {
            const auto *helix = dynamic_cast<const HelixCurve *>(curve.get());
            if (!helix) throw std::domain_error("superficie rigata: tipo di curva non gestito");
            curve = std::make_shared<BSplineCurve<3>>(helixBSpline(*helix, 1e-9));
        }
        pieces.push_back({curve, segment.range});
    }
    const auto start = [](const Piece &p) { return p.curve->point(p.range.lo); };
    const auto end = [](const Piece &p) { return p.curve->point(p.range.hi); };
    if (pieces.size() > 1) {
        const Piece &second = pieces[1];
        const double keep = std::min(distance(end(pieces[0]), start(second)), distance(end(pieces[0]), end(second)));
        const double flip = std::min(distance(start(pieces[0]), start(second)), distance(start(pieces[0]), end(second)));
        if (flip < keep) pieces[0] = reversedPiece(pieces[0]);
    }
    for (std::size_t k = 1; k < pieces.size(); ++k) {
        cancellationPoint();
        const Vec3 previous = end(pieces[k - 1]);
        if (distance(previous, start(pieces[k])) <= tolerance) continue;
        if (distance(previous, end(pieces[k])) <= tolerance) pieces[k] = reversedPiece(pieces[k]);
        else throw std::domain_error("superficie rigata: i tratti di una catena non sono consecutivi");
    }
    return pieces;
}

// Normale di Newell del loop campionato (meta' dell'area vettoriale).
Vec3 newellNormal(const std::vector<Piece> &pieces, const Vec3 &centroid) {
    std::vector<Vec3> samples;
    for (const Piece &p : pieces)
        for (int k = 0; k < 64; ++k) samples.push_back(p.curve->point(p.range.lo + p.range.length() * k / 64.0) - centroid);
    Vec3 sum;
    for (std::size_t k = 0; k < samples.size(); ++k) sum += cross(samples[k], samples[(k + 1) % samples.size()]);
    return 0.5 * sum;
}


}

Body loftSolid(const std::vector<LoftSection> &sections, bool ruled) {
    LoftOptions options;
    options.ruled = ruled;
    return loftSolid(sections, options);
}

Body loftSolid(const std::vector<LoftSection> &sections, const LoftOptions &options) {
    for (const LoftSection &s : sections) {
        cancellationPoint();
        if (!sectionClosed(s)) throw std::domain_error("loft: una sezione non e' un contorno chiuso");
    }
    return loft(sections, options, true, true);
}

Body loftSheet(const std::vector<LoftSection> &sections, bool ruled) {
    LoftOptions options;
    options.ruled = ruled;
    return loftSheet(sections, options);
}

Body loftSheet(const std::vector<LoftSection> &sections, const LoftOptions &options) {
    if (sections.empty()) throw std::domain_error("loft: servono almeno due sezioni");
    const bool closed = sectionClosed(sections.front());
    for (const LoftSection &s : sections)
        if (sectionClosed(s) != closed)
            throw std::domain_error("loft: le sezioni di una lamina devono essere tutte chiuse (tubo) o tutte aperte");
    return loft(sections, options, closed, false);
}

Body ruledSurface(const std::vector<PathSegment> &first, const std::vector<PathSegment> &second) {
    LoftOptions options;
    options.ruled = true;
    return ruledSurface(first, second, options);
}

Body ruledSurface(const std::vector<PathSegment> &first, const std::vector<PathSegment> &second, const LoftOptions &inputOptions) {
    // Scala dai campioni delle due catene (prima di concatenarle: serve alla tolleranza).
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const std::vector<PathSegment> *chain : {&first, &second})
        for (const PathSegment &segment : *chain) {
            cancellationPoint();
            if (!segment.curve) throw std::domain_error("superficie rigata: tratto non valido");
            for (int k = 0; k <= 16; ++k) {
                const Vec3 p = segment.curve->point(segment.range.lo + segment.range.length() * k / 16.0);
                lo = Vec3(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
                hi = Vec3(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
            }
        }
    const double scale = std::max(1.0, distance(lo, hi));
    const double tolerance = 1e-6 * scale;
    std::vector<Section> sections(2);
    sections[0].pieces = chainPieces(first, tolerance);
    sections[1].pieces = chainPieces(second, tolerance);
    const bool closed = distance(chainStart(sections[0].pieces), chainEnd(sections[0].pieces)) <= tolerance;
    if (closed != (distance(chainStart(sections[1].pieces), chainEnd(sections[1].pieces)) <= tolerance))
        throw std::domain_error("superficie rigata: le due catene devono essere entrambe aperte o entrambe chiuse");
    for (Section &s : sections) s.centroid = sectionCentroid(s.pieces);
    if (closed) {
        // Stesso verso di rotazione (normali di Newell), partenza della seconda
        // nel punto piu' vicino alla partenza della prima.
        const Vec3 a = newellNormal(sections[0].pieces, sections[0].centroid), b = newellNormal(sections[1].pieces, sections[1].centroid);
        if (!(norm(a) > 1e-12 * scale * scale) || !(norm(b) > 1e-12 * scale * scale))
            throw std::domain_error("superficie rigata: verso di rotazione di una catena chiusa non determinabile");
        if (dot(a, b) < 0.0) reverseChain(sections[1].pieces);
        sections[0].normal = normalized(a);
        sections[1].normal = normalized(dot(a, b) < 0.0 ? -b : b);
        // Partenza come nel loft rigato: il vertice della seconda nella
        // direzione (dal baricentro) della partenza della prima; il punto piu'
        // vicino in 3D cadeva a meta' di un lato con sezioni di misura diversa
        // o spostate, e la superficie si torceva.
        alignStart(sections[1], chainStart(sections[0].pieces) - sections[0].centroid);
    } else {
        // L'inizio della seconda dalla parte dell'inizio della prima.
        const Vec3 a0 = chainStart(sections[0].pieces), a1 = chainEnd(sections[0].pieces);
        const Vec3 b0 = chainStart(sections[1].pieces), b1 = chainEnd(sections[1].pieces);
        if (distance(a0, b0) + distance(a1, b1) > distance(a0, b1) + distance(a1, b0)) reverseChain(sections[1].pieces);
    }
    LoftOptions options = inputOptions;
    options.ruled = true;
    for (std::size_t i = 0; i < 2; ++i) {
        cancellationPoint();
        const int continuity = i == 0 ? options.startContinuity : options.endContinuity;
        const auto &contacts = i == 0 ? options.startFaces : options.endFaces;
        if (continuity == 0 || !contacts.empty()) continue;
        Vec3 normal = newellNormal(sections[i].pieces, sections[i].centroid);
        if (!(norm(normal) > 1e-12 * scale * scale)) normal = sections[1].centroid - sections[0].centroid;
        if (!(norm(normal) > 1e-12 * scale)) throw std::domain_error("superficie rigata: direzione tra i profili non determinabile");
        if (dot(normal, sections[1].centroid - sections[0].centroid) < 0.0) normal = -normal;
        sections[i].normal = normalized(normal);
    }
    return loftCore(sections, options, closed, {}, scale, {}, {});
}

}
