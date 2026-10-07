#include "fk_loft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline_basis.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
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
};

struct GuideHit {
    Vec3 point;
    double progress = 0.0;
    Vec3 tangent;
    Vec3 curvature;
};

GuideHit guideHit(const std::vector<PathSegment> &guide, const Section &section, double tolerance) {
    std::vector<GuideHit> hits;
    double before = 0.0, total = 0.0;
    std::vector<double> lengths;
    for (const PathSegment &segment : guide) {
        if (!segment.curve) throw std::domain_error("loft: curva guida non valida");
        lengths.push_back(arcLength(*segment.curve, segment.range, 1e-11));
        total += lengths.back();
    }
    if (!(total > tolerance)) throw std::domain_error("loft: curva guida troppo corta");
    for (std::size_t segmentIndex = 0; segmentIndex < guide.size(); ++segmentIndex) {
        const PathSegment &segment = guide[segmentIndex];
        const PlaneRoots<3> roots = planeRoots<3>(*segment.curve, segment.range, section.normal,
                                                   dot(section.normal, section.frame.origin()), tolerance);
        if (!roots.coincident.empty()) throw std::domain_error("loft: una curva guida giace nel piano di una sezione");
        for (double t : roots.parameters) {
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
                hits.push_back({p, (before + arcLength(*segment.curve, {segment.range.lo, t}, 1e-12)) / total, tangent, curvature});
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
double sectionProgress(const Section &section, const Vec3 &point, double tolerance) {
    std::size_t bestPiece = 0;
    CurveProjection<3> best;
    best.distance = 1e300;
    std::vector<double> lengths;
    double total = 0.0;
    for (std::size_t k = 0; k < section.pieces.size(); ++k) {
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
    if (progress >= 1.0 - 1e-9) progress = 0.0;
    return progress;
}

Vec3 sectionCentroid(const std::vector<Piece> &pieces) {
    Vec3 sum;
    double weight = 0.0;
    for (const Piece &p : pieces)
        for (int i = 0; i < 32; ++i) {
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
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        if (!(std::fabs(a[pivot][c]) > 1e-300)) throw std::domain_error("loft: sistema d'interpolazione singolare");
        std::swap(a[c], a[pivot]);
        std::swap(b[c], b[pivot]);
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = a[r][c] / a[c][c];
            if (f == 0.0) continue;
            for (std::size_t k = c; k < n; ++k) a[r][k] -= f * a[c][k];
            for (std::size_t k = 0; k < b[r].size(); ++k) b[r][k] -= f * b[c][k];
        }
    }
    for (std::size_t c = n; c-- > 0;) {
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

// Quintiche di Hermite per campata: valori, prima e seconda derivata sono
// condivisi nei nodi, quindi la geometria e' C2. G1 forza la direzione di
// uscita normale al piano della sezione; G2 forza anche curvatura nulla nella
// direzione longitudinale. L'influenza miscela la condizione con la forma
// libera stimata dalle sezioni vicine.
std::vector<Homogeneous> hermiteRow(const std::vector<Homogeneous> &value, const std::vector<double> &v,
                                   const Vec3 &startDirection, const Vec3 &endDirection,
                                   int startContinuity, int endContinuity, double startInfluence, double endInfluence,
                                   const std::vector<GuideHit> *guide = nullptr, double guideInfluence = 0.0,
                                   int guideContinuity = 1) {
    const std::size_t n = value.size();
    std::vector<Homogeneous> first(n), second(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) first[i] = scaled(add(value[1], value[0], -1.0), 1.0 / (v[1] - v[0]));
        else if (i + 1 == n) first[i] = scaled(add(value[i], value[i - 1], -1.0), 1.0 / (v[i] - v[i - 1]));
        else first[i] = scaled(add(value[i + 1], value[i - 1], -1.0), 1.0 / (v[i + 1] - v[i - 1]));
    }
    if (n > 2) {
        for (std::size_t i = 1; i + 1 < n; ++i) {
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
    std::vector<Homogeneous> poles;
    poles.push_back(value.front());
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double h = v[i + 1] - v[i];
        poles.push_back(add(value[i], first[i], h / 5.0));
        poles.push_back(add(add(value[i], first[i], 2.0 * h / 5.0), second[i], h * h / 20.0));
        poles.push_back(add(add(value[i + 1], first[i + 1], -2.0 * h / 5.0), second[i + 1], h * h / 20.0));
        poles.push_back(add(value[i + 1], first[i + 1], -h / 5.0));
        poles.push_back(value[i + 1]);
    }
    return poles;
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
            const double t = p.range.lo + p.range.length() * k / 720.0;
            const double c = dot(planar(p.curve->point(t) - s.centroid), want);
            if (c > best) best = c, bestT = t;
        }
        // Raffinamento: massimo del coseno con la bisezione aurea attorno al campione.
        double a = bestT - p.range.length() / 720.0, b = bestT + p.range.length() / 720.0;
        for (int k = 0; k < 80; ++k) {
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
            const double c = dot(planar(s.pieces[k].curve->point(s.pieces[k].range.lo) - s.centroid), want);
            if (c > best) best = c, bestIndex = k;
        }
        std::rotate(s.pieces.begin(), s.pieces.begin() + std::ptrdiff_t(bestIndex), s.pieces.end());
    }
}

Body loft(const std::vector<LoftSection> &input, const LoftOptions &options, bool closed, bool caps) {
    const std::size_t n = input.size();
    if (n < 2) throw std::domain_error("loft: servono almeno due sezioni");
    std::vector<Section> sections(n);
    // Sezioni nello spazio, baricentri.
    for (std::size_t i = 0; i < n; ++i) {
        const LoftSection &s = input[i];
        if (s.loop.segments.empty()) throw std::domain_error("loft: sezione vuota");
        for (const ProfileSegment &segment : s.loop.segments) sections[i].pieces.push_back({embedCurve(segment.curve, s.frame), segment.range});
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

    // Verso dei loop: attorno alla direzione del loft. Catene: inizio dalla stessa parte.
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 direction = i + 1 < n ? sections[i + 1].centroid - sections[i].centroid : sections[i].centroid - sections[i - 1].centroid;
        const Vec3 z = input[i].frame.zDir();
        Section &s = sections[i];
        if (closed) {
            if (std::fabs(dot(normalized(direction), z)) < 1e-6) throw std::domain_error("loft: il piano di una sezione contiene la direzione del loft");
            s.normal = dot(direction, z) > 0.0 ? z : -z;
            const bool counterclockwise = (signedArea(input[i].loop) > 0.0) == (dot(s.normal, z) > 0.0);
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
        if (!closed) throw std::domain_error("loft: le curve guida richiedono sezioni chiuse");
        for (std::size_t guideIndex = 0; guideIndex < options.guides.size(); ++guideIndex) {
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
        for (std::size_t i = 0; i < sections.size(); ++i)
            startAtGuide(sections[i], guideHits.front()[i].point, 1e-6 * scale);
    }
    // Senza guida, punto di partenza dei loop: il piu' vicino (in direzione
    // dal baricentro) alla partenza della sezione precedente.
    if (closed && options.guides.empty())
        for (std::size_t i = 1; i < n; ++i) {
            const Section &previous = sections[i - 1];
            alignStart(sections[i], previous.pieces.front().curve->point(previous.pieces.front().range.lo) - previous.centroid);
        }

    std::vector<Frame3> capPlanes;
    if (closed && caps)
        for (std::size_t i : {std::size_t(0), n - 1})
            capPlanes.push_back(Frame3(input[i].frame.origin(), sections[i].normal, input[i].frame.xDir()));
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
    const bool ruled = options.ruled;
    const std::size_t n = sections.size();
    // Sezioni con lo stesso numero di tratti (piu' di uno): tratto con tratto,
    // cosi' gli spigoli vivi (i vertici di due poligoni) si corrispondono. Altrimenti
    // si dividono alle stesse ascisse curvilinee normalizzate.
    bool sameCount = sections.front().pieces.size() > 1;
    for (const auto &section : sections) sameCount = sameCount && section.pieces.size() == sections.front().pieces.size();
    std::vector<std::vector<double>> fractions(n);
    std::vector<double> all;
    for (std::size_t i = 0; i < n; ++i) {
        double total = 0.0;
        std::vector<double> lengths;
        for (const Piece &p : sections[i].pieces) lengths.push_back(arcLength(*p.curve, p.range, 1e-13)), total += lengths.back();
        double cumulative = 0.0;
        for (double l : lengths) {
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
    if (guideHits.size() > 1) {
        localGuidePosition.assign(guideHits.size(), std::vector<double>(n));
        for (std::size_t guide = 0; guide < guideHits.size(); ++guide)
            for (std::size_t i = 0; i < n; ++i)
                localGuidePosition[guide][i] = sectionProgress(sections[i], guideHits[guide][i].point, 1e-6 * scale);
        guideOrder.resize(guideHits.size());
        for (std::size_t guide = 0; guide < guideOrder.size(); ++guide) guideOrder[guide] = guide;
        std::sort(guideOrder.begin() + 1, guideOrder.end(), [&](std::size_t a, std::size_t b) {
            return localGuidePosition[a][0] < localGuidePosition[b][0];
        });
        if (guideOrder.front() != 0) throw std::logic_error("loft: la prima guida non coincide con la cucitura");
        for (std::size_t i = 0; i < n; ++i) {
            double previous = -1.0;
            for (std::size_t position = 0; position < guideOrder.size(); ++position) {
                const double value = localGuidePosition[guideOrder[position]][i];
                if ((position == 0 && value > 1e-6) || (position > 0 && value <= previous + 1e-7))
                    throw std::domain_error("loft: le curve guida si incrociano o cambiano ordine attorno alle sezioni");
                previous = value;
            }
        }
        commonGuidePosition.push_back(0.0);
        for (std::size_t position = 1; position < guideOrder.size(); ++position) {
            double average = 0.0;
            for (std::size_t i = 0; i < n; ++i) average += localGuidePosition[guideOrder[position]][i];
            commonGuidePosition.push_back(average / double(n));
        }
        commonGuidePosition.push_back(1.0);
        sameCount = false;
        all.clear();
        const auto remap = [](double value, const std::vector<double> &from, const std::vector<double> &to) {
            std::size_t interval = 0;
            while (interval + 2 < from.size() && value > from[interval + 1] + 1e-12) ++interval;
            const double span = from[interval + 1] - from[interval];
            const double ratio = span > 0.0 ? (value - from[interval]) / span : 0.0;
            return to[interval] + std::clamp(ratio, 0.0, 1.0) * (to[interval + 1] - to[interval]);
        };
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<double> local;
            for (std::size_t guide : guideOrder) local.push_back(localGuidePosition[guide][i]);
            local.push_back(1.0);
            for (double position : fractions[i]) all.push_back(remap(position, local, commonGuidePosition));
        }
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
        const std::vector<Piece> &pieces = sections[i].pieces;
        const std::vector<double> &f = fractions[i];
        std::vector<double> localCuts = cuts;
        if (guideHits.size() > 1) {
            std::vector<double> local;
            for (std::size_t guide : guideOrder) local.push_back(localGuidePosition[guide][i]);
            local.push_back(1.0);
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
            std::size_t k = 0;
            while (k + 1 < pieces.size() && f[k + 1] <= c + 1e-7) ++k;
            if (c >= 1.0 - 1e-12) at.push_back({pieces.size() - 1, pieces.back().range.hi});
            else if (std::fabs(c - f[k]) <= 1e-7) at.push_back({k, pieces[k].range.lo});
            else at.push_back({k, parameterAtLength(pieces[k], (c - f[k]) * total)});
        }
        for (std::size_t m = 0; m < M; ++m) {
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
        int degree = 1;
        for (std::size_t i = 0; i < n; ++i)
            for (const BSplineCurve<3> &b : rationalBezierPieces(*split[i][m].curve, split[i][m].range)) degree = std::max(degree, b.degree());
        std::vector<std::vector<BSplineCurve<3>>> bezier(n);
        std::vector<std::vector<double>> breaks(n);
        std::vector<double> interior;
        for (std::size_t i = 0; i < n; ++i) {
            bezier[i] = standardBezierPieces(*split[i][m].curve, split[i][m].range, degree);
            // Conserva il parametro delle sezioni: ripartire i nodi secondo
            // la lunghezza delle pezze cambia le velocita' ai loro confini.
            // Su cerchi/ellissi cio' crea salti di normale nel loft anche
            // quando ogni sezione e' geometricamente liscia.
            const double start = bezier[i].front().domain().lo;
            const double span = bezier[i].back().domain().hi - start;
            breaks[i].push_back(0.0);
            for (std::size_t k = 0; k < bezier[i].size(); ++k) {
                breaks[i].push_back(k + 1 == bezier[i].size() ? 1.0 :
                    (bezier[i][k].domain().hi - start) / span);
                if (k + 1 < bezier[i].size()) interior.push_back(breaks[i].back());
            }
        }
        std::sort(interior.begin(), interior.end());
        std::vector<double> knots;
        for (double k : interior)
            if (knots.empty() || k - knots.back() > 1e-9) knots.push_back(k);
        const auto representative = [&](double k) {
            for (double r : knots)
                if (std::fabs(r - k) <= 1e-9) return r;
            return k;
        };
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = 1; k + 1 < breaks[i].size(); ++k) breaks[i][k] = representative(breaks[i][k]);
            BSplineCurve<3> curve = joinBezierPieces(bezier[i], breaks[i]);
            for (double k : knots)
                if (curve.multiplicity(k) < degree) curve = curve.insertKnot(k, degree - curve.multiplicity(k));
            nurbs[m].push_back(std::move(curve));
        }
        for (std::size_t i = 1; i < n; ++i)
            if (nurbs[m][i].knots() != nurbs[m][0].knots()) throw std::logic_error("loft: nodi delle sezioni non compatibili");
    }

    // Vertici (inizio di ogni tratto; le catene anche la fine) e loro poli.
    const std::size_t V = closed ? M : M + 1;
    std::vector<std::vector<Vec3>> vertexPoint(n, std::vector<Vec3>(V));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t m = 0; m < M; ++m) vertexPoint[i][m] = split[i][m].curve->point(split[i][m].range.lo);
        if (!closed) vertexPoint[i][M] = split[i][M - 1].curve->point(split[i][M - 1].range.hi);
    }
    const auto nextVertex = [&](std::size_t m) { return closed ? (m + 1) % M : m + 1; };

    // Associa ogni guida alla riga longitudinale che attraversa davvero i
    // suoi punti sulle sezioni. Questo comprende le guide aggiuntive; prima
    // soltanto la guida usata come cucitura riceveva le condizioni G1/G2.
    std::vector<const std::vector<GuideHit> *> vertexGuide(V, nullptr);
    for (std::size_t guide = 0; guide < guideHits.size(); ++guide) {
        std::size_t bestVertex = 0;
        double bestError = 1e300;
        for (std::size_t m = 0; m < V; ++m) {
            double error = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                error = std::max(error, distance(vertexPoint[i][m], guideHits[guide][i].point));
            if (error < bestError) bestError = error, bestVertex = m;
        }
        if (bestError <= 2e-6 * scale) vertexGuide[bestVertex] = &guideHits[guide];
        else if (options.guideContinuity > 0 && options.guideInfluence > 0.0)
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
    if (!guideParameters.empty() && options.guideInfluence > 0.0) {
        for (std::size_t i = 1; i + 1 < n; ++i) {
            double guided = 0.0;
            for (const std::vector<double> &parameters : guideParameters) guided += parameters[i];
            guided /= double(guideParameters.size());
            v[i] = (1.0 - options.guideInfluence) * v[i] + options.guideInfluence * guided;
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
        if (!ruled && i != 0 && i + 1 != n) continue;
        for (std::size_t m = 0; m < M; ++m) {
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
                                     || (!guideHits.empty() && options.guideContinuity > 0 && options.guideInfluence > 0.0);
        if (endpointControlled) {
            constexpr int q = 5;
            std::vector<double> vKnots(6, 0.0);
            for (std::size_t i = 1; i + 1 < n; ++i)
                vKnots.insert(vKnots.end(), 5, v[i]);
            vKnots.insert(vKnots.end(), 6, 1.0);
            const int vCount = int(5 * (n - 1) + 1);
            const auto interpolate = [&](std::size_t m, int j, std::vector<Vec3> &poles, std::vector<double> &weights) {
                std::vector<Homogeneous> values(n);
                for (std::size_t i = 0; i < n; ++i) {
                    Vec3 p;
                    double w;
                    homogeneous(m, i, j, p, w);
                    values[i] = {w * p.x(), w * p.y(), w * p.z(), w};
                }
                const std::vector<Homogeneous> row = hermiteRow(values, v, sections.front().normal, sections.back().normal,
                                                                 options.startContinuity, options.endContinuity,
                                                                 options.startInfluence, options.endInfluence,
                                                                 j == 0 && m < vertexGuide.size() ? vertexGuide[m] : nullptr, options.guideInfluence,
                                                                 options.guideContinuity);
                poles.clear();
                weights.clear();
                for (const Homogeneous &h : row) {
                    if (!(h[3] > 1e-12)) throw std::domain_error("loft: pesi non positivi con le condizioni alle estremita'");
                    poles.push_back(Vec3(h[0], h[1], h[2]) / h[3]);
                    weights.push_back(h[3]);
                }
            };
            std::vector<std::vector<Vec3>> vertexRows(V);
            std::vector<std::vector<double>> vertexWeights(V);
            std::vector<int> longitudinal(V);
            for (std::size_t m = 0; m < V; ++m) {
                if (m < M) interpolate(m, 0, vertexRows[m], vertexWeights[m]);
                else interpolate(M - 1, nurbs[M - 1][0].poleCount() - 1, vertexRows[m], vertexWeights[m]);
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
                std::vector<Vec3> poles(std::size_t(uCount * vCount));
                std::vector<double> weights(poles.size());
                bool rational = false;
                for (int j = 0; j < uCount; ++j) {
                    std::vector<Vec3> row;
                    std::vector<double> rowWeights;
                    if (j == 0) row = vertexRows[m], rowWeights = vertexWeights[m];
                    else if (j + 1 == uCount) row = vertexRows[nextVertex(m)], rowWeights = vertexWeights[nextVertex(m)];
                    else interpolate(m, j, row, rowWeights);
                    for (int i = 0; i < vCount; ++i) {
                        poles[std::size_t(j * vCount + i)] = row[std::size_t(i)];
                        weights[std::size_t(j * vCount + i)] = rowWeights[std::size_t(i)];
                        rational = rational || std::fabs(rowWeights[std::size_t(i)] - 1.0) > 1e-15;
                    }
                }
                Body::BuildFace face;
                face.surface = std::make_shared<BSplineSurface>(c.degree(), q, c.knots(), vKnots, uCount, vCount, poles,
                                                                rational ? weights : std::vector<double>{});
                if (SurfacePtr plane = planarEquivalent(*face.surface)) face.surface = plane;
                face.sense = true;
                face.loops.push_back({{sectionEdge.at({0, m}), true, nullptr, 0.0}, {longitudinal[nextVertex(m)], true, nullptr, 0.0},
                                      {sectionEdge.at({n - 1, m}), false, nullptr, 0.0}, {longitudinal[m], false, nullptr, 0.0}});
                faces.push_back(std::move(face));
            }
        } else {
        // Interpolazione di grado q con i nodi per media (NURBS Book, A9.1), in coordinate omogenee.
        const int q = std::min<int>(3, int(n) - 1);
        std::vector<double> vKnots(std::size_t(q + 1), 0.0);
        for (std::size_t j = 1; j + q < n; ++j) {
            double sum = 0.0;
            for (std::size_t k = j; k < j + std::size_t(q); ++k) sum += v[k];
            vKnots.push_back(sum / q);
        }
        vKnots.insert(vKnots.end(), std::size_t(q + 1), 1.0);
        std::vector<std::vector<double>> basis(n, std::vector<double>(n, 0.0));
        std::vector<double> ders(std::size_t(q + 1));
        for (std::size_t k = 0; k < n; ++k) {
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
            face.loops.push_back({{sectionEdge.at({0, m}), true, nullptr, 0.0}, {longitudinal[nextVertex(m)], true, nullptr, 0.0},
                                  {sectionEdge.at({n - 1, m}), false, nullptr, 0.0}, {longitudinal[m], false, nullptr, 0.0}});
            faces.push_back(std::move(face));
        }
        }
    }
    // Facce di testa: uscenti verso -n nella prima sezione, +n nell'ultima (i loop girano attorno a n).
    if (closed && capPlanes.size() == 2) {
        for (int cap = 0; cap < 2; ++cap) {
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
        const ProfileSegment &a = s.loop.segments.front(), &b = s.loop.segments.back();
        if (distance(a.start(), b.end()) > 1e-6 * std::max(1.0, norm(a.start()))) throw std::domain_error("loft: una sezione non e' un contorno chiuso");
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
    // Scala dai campioni delle due catene (prima di concatenarle: serve alla tolleranza).
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const std::vector<PathSegment> *chain : {&first, &second})
        for (const PathSegment &segment : *chain) {
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
    LoftOptions options;
    options.ruled = true;
    return loftCore(sections, options, closed, {}, scale, {}, {});
}

}
