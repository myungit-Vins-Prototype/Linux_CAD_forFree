#include "fk_fill.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "fk_bspline_basis.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_exchange.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

constexpr int kDegree = 3;
// Peso dell'energia di flessione rispetto ai dati (tracce normalizzate):
// regola solo le zone senza campioni.
constexpr double kSmoothing = 1e-6;
// Pesi del contorno e della tangenza (le guide hanno guideWeight). Pesi molto
// diversi tra loro rendono il sistema mal condizionato: le priorita' tra i
// vincoli incompatibili si ottengono con la zona di raccordo delle guide.
constexpr double kBoundaryWeight = 1.0;
constexpr double kTangentWeight = 1.0;
// Vicino ai tratti in tangenza le guide pesano meno (fino a zero sul bordo),
// in una fascia larga questa frazione del contorno: la superficie passa dalla
// tangenza della faccia adiacente alla guida.
constexpr double kGuideBlend = 0.15;

// Un tratto del contorno nel verso del contorno: s in [0, 1] dall'inizio alla fine.
struct Piece {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true;
    double length = 0.0;

    double parameter(double s) const { return forward ? range.lo + s * range.length() : range.hi - s * range.length(); }
    Vec3 at(double s) const { return curve->point(parameter(s)); }
    Vec3 start() const { return at(0.0); }
    Vec3 end() const { return at(1.0); }
    // Tangente nel verso del contorno (derivata rispetto al parametro della curva).
    Vec3 tangent(double s) const {
        Vec3 d[2];
        curve->evaluate(parameter(s), 1, d);
        return forward ? d[1] : -d[1];
    }
};

std::vector<Piece> chainLoop(const std::vector<PathSegment> &input, double tolerance) {
    std::vector<Piece> pieces;
    for (const PathSegment &segment : input) {
        if (!segment.curve || !(segment.range.length() > 0.0)) throw std::domain_error("riempimento: tratto del contorno non valido");
        Piece piece{segment.curve, segment.range, true, arcLength(*segment.curve, segment.range)};
        if (!(piece.length > tolerance)) throw std::domain_error("riempimento: tratto del contorno di lunghezza nulla");
        pieces.push_back(piece);
    }
    if (pieces.empty()) throw std::domain_error("riempimento: nessuna curva del contorno");
    std::vector<bool> used(pieces.size(), false);
    std::vector<Piece> loop{pieces.front()};
    used[0] = true;
    while (loop.size() < pieces.size()) {
        const Vec3 end = loop.back().end();
        int found = -1;
        bool forward = true;
        for (std::size_t j = 0; j < pieces.size(); ++j) {
            if (used[j]) continue;
            const bool head = distance(pieces[j].start(), end) <= tolerance, tail = distance(pieces[j].end(), end) <= tolerance;
            if (!head && !tail) continue;
            if (found >= 0) throw std::domain_error("riempimento: piu' di due curve del contorno in un estremo");
            found = int(j);
            forward = head;
        }
        if (found < 0) throw std::domain_error("riempimento: le curve del contorno non formano un contorno chiuso");
        Piece next = pieces[std::size_t(found)];
        next.forward = forward;
        used[std::size_t(found)] = true;
        loop.push_back(next);
    }
    if (distance(loop.back().end(), loop.front().start()) > tolerance)
        throw std::domain_error("riempimento: le curve del contorno non formano un contorno chiuso");
    return loop;
}

// Faccia adiacente in un punto del suo bordo.
struct ContactPoint {
    bool valid = false;
    Vec3 normal;        // normale uscente della faccia
    Vec3 continuation;  // nel piano tangente, normale al bordo, verso l'esterno della faccia
    Vec3 finTangent;    // verso della fin della faccia
    double curvature = 0.0;  // curvatura normale in `continuation`
};

class ContactFace {
public:
    ContactFace(std::shared_ptr<const Body> body, FaceId face) : body_(std::move(body)), face_(face) {
        for (LoopId loop : body_->face(face_).loops)
            for (FinId fin : body_->loopFins(loop)) fins_.push_back(fin);
    }

    // Distanza del punto dal bordo della faccia, e la fin con il parametro dell'edge.
    double nearest(const Vec3 &p, FinId &fin, double &t) const {
        double best = std::numeric_limits<double>::infinity();
        for (FinId f : fins_) {
            const Edge &edge = body_->edge(body_->fin(f).edge);
            const CurveProjection<3> projection = projectPoint(*edge.curve, p, edge.range);
            if (projection.distance < best) best = projection.distance, fin = f, t = projection.parameter;
        }
        return best;
    }

    ContactPoint at(const Vec3 &p, double scale) const {
        ContactPoint result;
        FinId finId;
        double t = 0.0;
        if (!std::isfinite(nearest(p, finId, t))) return result;
        const Fin &fin = body_->fin(finId);
        const Edge &edge = body_->edge(fin.edge);
        const Face &face = body_->face(face_);
        const Surface &surface = *face.surface;
        Vec2 uv;
        bool located = false;
        if (fin.pcurve) {
            uv = fin.pcurve->point(t);
            located = true;
        } else {
            const SurfaceProjection guess = projectPoint(surface, edge.curve->point(t));
            uv = Vec2(guess.u, guess.v);
            located = invertPoint(surface, edge.curve->point(t), uv, 1e-6 * scale, scale);
        }
        if (!located) return result;
        Vec3 d[9];
        surface.evaluate(uv.x(), uv.y(), 2, d);
        const Vec3 su = d[Surface::derivativeIndex(1, 0, 2)], sv = d[Surface::derivativeIndex(0, 1, 2)];
        const Vec3 suu = d[Surface::derivativeIndex(2, 0, 2)], suv = d[Surface::derivativeIndex(1, 1, 2)], svv = d[Surface::derivativeIndex(0, 2, 2)];
        const Vec3 n = cross(su, sv);
        Vec3 c[2];
        edge.curve->evaluate(t, 1, c);
        if (!(norm(n) > 1e-12 * squaredNorm(su) + 1e-300) || !(norm(c[1]) > 0.0)) return result;
        result.normal = normalized(n) * (face.sense ? 1.0 : -1.0);
        result.finTangent = normalized(fin.sense ? c[1] : -c[1]);
        // La faccia sta a sinistra della fin: dentro = n x t, la continuazione e' l'opposto.
        result.continuation = normalized(cross(result.finTangent, result.normal));
        // Curvatura normale in D: D = a Su + b Sv.
        const double g11 = dot(su, su), g12 = dot(su, sv), g22 = dot(sv, sv);
        const double r1 = dot(su, result.continuation), r2 = dot(sv, result.continuation);
        const double det = g11 * g22 - g12 * g12;
        if (!(std::fabs(det) > 0.0)) return result;
        const double a = (r1 * g22 - r2 * g12) / det, b = (g11 * r2 - g12 * r1) / det;
        result.curvature = dot(result.normal, a * a * suu + 2.0 * a * b * suv + b * b * svv);
        result.valid = true;
        return result;
    }

private:
    std::shared_ptr<const Body> body_;
    FaceId face_;
    std::vector<FinId> fins_;
};

// Matrice simmetrica definita positiva a banda (solo la parte bassa).
class BandMatrix {
public:
    BandMatrix(int n, int band) : n_(n), b_(band), a_(std::size_t(n) * std::size_t(band + 1), 0.0) {}
    double &at(int i, int j) { return a_[std::size_t(i) * std::size_t(b_ + 1) + std::size_t(i - j)]; }  // i >= j, i - j <= b
    double get(int i, int j) const { return a_[std::size_t(i) * std::size_t(b_ + 1) + std::size_t(i - j)]; }
    int size() const { return n_; }
    // y = A x (A simmetrica, prima della fattorizzazione).
    std::vector<double> multiply(const std::vector<double> &x) const {
        std::vector<double> y(std::size_t(n_), 0.0);
        for (int i = 0; i < n_; ++i)
            for (int j = std::max(0, i - b_); j <= i; ++j) {
                const double a = get(i, j);
                y[std::size_t(i)] += a * x[std::size_t(j)];
                if (j != i) y[std::size_t(j)] += a * x[std::size_t(i)];
            }
        return y;
    }
    int band() const { return b_; }

    // Cholesky in place; falso se la matrice non e' definita positiva.
    bool factor() {
        for (int i = 0; i < n_; ++i) {
            const int first = std::max(0, i - b_);
            for (int j = first; j <= i; ++j) {
                double sum = at(i, j);
                for (int k = std::max(first, j - b_); k < j; ++k) sum -= at(i, k) * at(j, k);
                if (i == j) {
                    if (!(sum > 0.0)) return false;
                    at(i, i) = std::sqrt(sum);
                } else {
                    at(i, j) = sum / at(j, j);
                }
            }
        }
        return true;
    }
    void solve(std::vector<double> &x) {
        for (int i = 0; i < n_; ++i) {
            double sum = x[std::size_t(i)];
            for (int k = std::max(0, i - b_); k < i; ++k) sum -= at(i, k) * x[std::size_t(k)];
            x[std::size_t(i)] = sum / at(i, i);
        }
        for (int i = n_ - 1; i >= 0; --i) {
            double sum = x[std::size_t(i)];
            for (int k = i + 1; k <= std::min(n_ - 1, i + b_); ++k) sum -= at(k, i) * x[std::size_t(k)];
            x[std::size_t(i)] = sum / at(i, i);
        }
    }

private:
    int n_, b_;
    std::vector<double> a_;
};

// Griglia uniforme di una B-spline bicubica sul rettangolo dei parametri.
struct Grid {
    std::vector<double> uKnots, vKnots;
    int nu = 0, nv = 0;  // poli
    double h = 0.0;      // lato della cella piu' piccola

    Grid(double u0, double u1, int uSpans, double v0, double v1, int vSpans) {
        const auto knots = [](double lo, double hi, int spans) {
            std::vector<double> k(std::size_t(kDegree), lo);
            for (int i = 0; i <= spans; ++i) k.push_back(i == spans ? hi : lo + (hi - lo) * i / spans);
            for (int i = 0; i < kDegree; ++i) k.push_back(hi);
            return k;
        };
        uKnots = knots(u0, u1, uSpans);
        vKnots = knots(v0, v1, vSpans);
        nu = uSpans + kDegree;
        nv = vSpans + kDegree;
        h = std::min((u1 - u0) / uSpans, (v1 - v0) / vSpans);
    }
};

// Riga di vincolo: combinazione lineare dei poli.
struct Row {
    int index[(kDegree + 1) * (kDegree + 1)];
    double value[(kDegree + 1) * (kDegree + 1)];
};

// Derivate delle funzioni di base: b[k][a], k = 0..2.
struct Basis {
    int span = 0;
    double d[3][kDegree + 1];
};

Basis basisAt(const std::vector<double> &knots, int poles, double t) {
    Basis result;
    t = std::clamp(t, knots.front(), knots.back());
    result.span = detail::findSpan(knots, kDegree, poles, t);
    double ders[3 * (kDegree + 1)];
    detail::basisFunctionDerivatives(knots, result.span, t, kDegree, 2, ders);
    for (int k = 0; k < 3; ++k)
        for (int a = 0; a <= kDegree; ++a) result.d[k][a] = ders[k * (kDegree + 1) + a];
    return result;
}

// Riga della derivata sum_c coefficients[c] d^(ku_c + kv_c) S / du^ku_c dv^kv_c.
Row rowAt(const Grid &grid, double u, double v, std::initializer_list<std::tuple<int, int, double>> terms) {
    const Basis bu = basisAt(grid.uKnots, grid.nu, u), bv = basisAt(grid.vKnots, grid.nv, v);
    Row row;
    for (int a = 0; a <= kDegree; ++a)
        for (int b = 0; b <= kDegree; ++b) {
            const int k = a * (kDegree + 1) + b;
            row.index[k] = (bu.span - kDegree + a) * grid.nv + (bv.span - kDegree + b);
            double sum = 0.0;
            for (const auto &[ku, kv, c] : terms) sum += c * bu.d[ku][a] * bv.d[kv][b];
            row.value[k] = sum;
        }
    return row;
}

// Sistema normale: matrice a banda e tre termini noti.
struct NormalSystem {
    BandMatrix matrix;
    std::vector<double> rhs[3];

    explicit NormalSystem(const Grid &grid) : matrix(grid.nu * grid.nv, kDegree * grid.nv + kDegree) {
        for (auto &r : rhs) r.assign(std::size_t(grid.nu * grid.nv), 0.0);
    }
    double positionTrace = 0.0;  // traccia delle righe di posizione con peso 1: scala dell'energia

    void add(const Row &row, const Vec3 &target, double weight, bool position = false) {
        constexpr int m = (kDegree + 1) * (kDegree + 1);
        if (position)
            for (int p = 0; p < m; ++p) positionTrace += row.value[p] * row.value[p];
        for (int p = 0; p < m; ++p) {
            const int i = row.index[p];
            const double wi = weight * row.value[p];
            for (int c = 0; c < 3; ++c) rhs[c][std::size_t(i)] += wi * target[c];
            for (int q = 0; q < m; ++q) {
                const int j = row.index[q];
                if (j <= i) matrix.at(i, j) += wi * row.value[q];
            }
        }
    }
};

// Matrici di Gram 1D delle derivate k-esime: g[k][i][j - i + p], |i - j| <= p.
std::vector<double> gram(const std::vector<double> &knots, int poles, int k) {
    std::vector<double> g(std::size_t(poles) * std::size_t(2 * kDegree + 1), 0.0);
    static const double x[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
    static const double w[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    for (std::size_t s = std::size_t(kDegree); s + 1 + std::size_t(kDegree) < knots.size(); ++s) {
        const double a = knots[s], b = knots[s + 1];
        if (!(b > a)) continue;
        for (int q = 0; q < 4; ++q) {
            const double t = 0.5 * (a + b) + 0.5 * (b - a) * x[q], weight = 0.5 * (b - a) * w[q];
            const Basis basis = basisAt(knots, poles, t);
            for (int i = 0; i <= kDegree; ++i)
                for (int j = 0; j <= kDegree; ++j) {
                    const int gi = basis.span - kDegree + i, gj = basis.span - kDegree + j;
                    g[std::size_t(gi) * std::size_t(2 * kDegree + 1) + std::size_t(gj - gi + kDegree)] += weight * basis.d[k][i] * basis.d[k][j];
                }
        }
    }
    return g;
}

// Energia di flessione: sum P_ij P_kl [A2u M0v + 2 A1u A1v + M0u A2v], aggiunta con peso alpha.
void addEnergy(const Grid &grid, NormalSystem &system, double alpha) {
    std::vector<double> u[3], v[3];
    for (int k = 0; k < 3; ++k) u[k] = gram(grid.uKnots, grid.nu, k), v[k] = gram(grid.vKnots, grid.nv, k);
    const int width = 2 * kDegree + 1;
    for (int i = 0; i < grid.nu; ++i)
        for (int j = 0; j < grid.nv; ++j) {
            const int row = i * grid.nv + j;
            for (int di = -kDegree; di <= 0; ++di) {
                const int k = i + di;
                if (k < 0) continue;
                for (int dj = -kDegree; dj <= kDegree; ++dj) {
                    const int l = j + dj;
                    if (l < 0 || l >= grid.nv) continue;
                    const int col = k * grid.nv + l;
                    if (col > row) continue;
                    const auto g = [&](const std::vector<double> &m, int a, int b) { return m[std::size_t(a) * std::size_t(width) + std::size_t(b - a + kDegree)]; };
                    const double value = g(u[2], i, k) * g(v[0], j, l) + 2.0 * g(u[1], i, k) * g(v[1], j, l) + g(u[0], i, k) * g(v[2], j, l);
                    system.matrix.at(row, col) += alpha * value;
                }
            }
        }
}

// Campione di una curva vincolata.
struct Sample {
    Vec3 point;
    Vec2 uv;
    Vec2 inward;          // contorno: normale interna nel piano dei parametri (nulla se non definita)
    int piece = -1;       // contorno: tratto
    ContactPoint contact;
    double lambda = 0.0;  // lunghezza della derivata trasversale voluta
    double weight = 1.0;  // guide: peso del campione
};

bool segmentsCross(const Vec2 &a, const Vec2 &b, const Vec2 &c, const Vec2 &d) {
    const auto side = [](const Vec2 &p, const Vec2 &q, const Vec2 &r) { return cross(q - p, r - p); };
    const double d1 = side(c, d, a), d2 = side(c, d, b), d3 = side(a, b, c), d4 = side(a, b, d);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

int winding(const std::vector<Vec2> &polygon, const Vec2 &p) {
    int count = 0;
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const Vec2 &a = polygon[k], &b = polygon[(k + 1) % polygon.size()];
        if (a.y() <= p.y()) {
            if (b.y() > p.y() && cross(b - a, p - a) > 0) ++count;
        } else if (b.y() <= p.y() && cross(b - a, p - a) < 0) {
            --count;
        }
    }
    return count;
}

double polygonDistance(const std::vector<Vec2> &polygon, const Vec2 &p) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const Vec2 &a = polygon[k], &b = polygon[(k + 1) % polygon.size()];
        const Vec2 ab = b - a;
        const double t = std::clamp(dot(p - a, ab) / std::max(squaredNorm(ab), 1e-300), 0.0, 1.0);
        best = std::min(best, norm(p - (a + t * ab)));
    }
    return best;
}

// Distanza da segmenti dati a coppie.
double polylineDistance(const std::vector<Vec2> &segments, const Vec2 &p) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k + 1 < segments.size(); k += 2) {
        const Vec2 &a = segments[k], ab = segments[k + 1] - segments[k];
        const double t = std::clamp(dot(p - a, ab) / std::max(squaredNorm(ab), 1e-300), 0.0, 1.0);
        best = std::min(best, norm(p - (a + t * ab)));
    }
    return best;
}

std::string degrees(double radians) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", radians * 180.0 / kPi);
    return text;
}

}

Body fillSurface(const std::vector<PathSegment> &boundary, const std::vector<PathSegment> &guides,
                 const std::vector<FillContact> &contacts, const FillOptions &options, FillReport *report) {
    FillReport local;
    FillReport &out = report ? *report : local;
    out = FillReport();
    double scale = 1.0;
    for (const PathSegment &segment : boundary)
        if (segment.curve) scale = std::max({scale, norm(segment.curve->point(segment.range.lo)), norm(segment.curve->point(segment.range.hi))});
    const double join = 1e-6 * scale;
    const std::vector<Piece> loop = chainLoop(boundary, join);
    const int pieceCount = int(loop.size());
    const double tolerance = std::max(options.tolerance, 1e-9 * scale);

    // Piano dei parametri: normale di Newell del contorno (il contorno vi gira in senso antiorario).
    std::vector<Vec3> polygon3;
    const int perPiece = std::max(16, 480 / pieceCount);
    for (const Piece &piece : loop)
        for (int k = 0; k < perPiece; ++k) polygon3.push_back(piece.at(double(k) / perPiece));
    Vec3 center, newell;
    for (const Vec3 &p : polygon3) center = center + p / double(polygon3.size());
    for (std::size_t k = 0; k < polygon3.size(); ++k) newell = newell + cross(polygon3[k] - center, polygon3[(k + 1) % polygon3.size()] - center);
    double size = 0.0;
    for (const Vec3 &p : polygon3) size = std::max(size, norm(p - center));
    if (!(norm(newell) > 1e-9 * size * size)) throw std::domain_error("riempimento: il contorno non racchiude un'area (visto da ogni direzione)");
    const Vec3 normal = normalized(newell);
    const Frame3 frame(center, normal, std::fabs(normal.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0));
    const auto project = [&](const Vec3 &p) {
        const Vec3 q = frame.toLocal(p);
        return Vec2(q.x(), q.y());
    };
    std::vector<Vec2> polygon;
    for (const Vec3 &p : polygon3) polygon.push_back(project(p));
    for (std::size_t a = 0; a < polygon.size(); ++a)
        for (std::size_t b = a + 2; b < polygon.size(); ++b) {
            if (a == 0 && b + 1 == polygon.size()) continue;
            if (segmentsCross(polygon[a], polygon[a + 1], polygon[b], polygon[(b + 1) % polygon.size()]))
                throw std::domain_error("riempimento: il contorno, visto dalla sua normale media, si sovrappone a se stesso");
        }
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    for (const Vec2 &q : polygon) u0 = std::min(u0, q.x()), u1 = std::max(u1, q.x()), v0 = std::min(v0, q.y()), v1 = std::max(v1, q.y());
    const double width = std::max(u1 - u0, v1 - v0);
    u0 -= 0.03 * width, u1 += 0.03 * width, v0 -= 0.03 * width, v1 += 0.03 * width;

    // Guide: dentro il contorno.
    for (std::size_t g = 0; g < guides.size(); ++g) {
        const PathSegment &guide = guides[g];
        if (!guide.curve || !(guide.range.length() > 0.0)) throw std::domain_error("riempimento: curva guida non valida");
        for (int k = 0; k <= 32; ++k) {
            const Vec2 q = project(guide.curve->point(guide.range.lo + guide.range.length() * k / 32.0));
            if (polygonDistance(polygon, q) > 1e-3 * width && winding(polygon, q) == 0)
                throw std::domain_error("riempimento: la curva guida " + std::to_string(g + 1) + " esce dal contorno");
        }
    }

    // Facce adiacenti: per ogni tratto la faccia che ha il tratto sul bordo.
    std::vector<ContactFace> faces;
    for (const FillContact &contact : contacts)
        if (contact.body && contact.face.valid()) faces.emplace_back(contact.body, contact.face);
    const double match = 10.0 * join;
    std::vector<int> pieceContact(std::size_t(pieceCount), -1);
    for (int k = 0; k < pieceCount && options.continuity > 0; ++k)
        for (std::size_t f = 0; f < faces.size() && pieceContact[std::size_t(k)] < 0; ++f) {
            bool all = true;
            for (double s : {0.2, 0.5, 0.8}) {
                FinId fin;
                double t;
                if (faces[f].nearest(loop[std::size_t(k)].at(s), fin, t) > match) {
                    all = false;
                    break;
                }
            }
            if (all) pieceContact[std::size_t(k)] = int(f);
        }
    for (int c : pieceContact) out.contactPieces += c >= 0 ? 1 : 0;
    // Tratti in tangenza proiettati: coppie di punti (segmenti).
    std::vector<Vec2> contactPolygon;
    for (int k = 0; k < pieceCount; ++k)
        if (pieceContact[std::size_t(k)] >= 0)
            for (int i = 0; i < 64; ++i) {
                contactPolygon.push_back(project(loop[std::size_t(k)].at(i / 64.0)));
                contactPolygon.push_back(project(loop[std::size_t(k)].at((i + 1) / 64.0)));
            }

    // Peso delle guide vicino ai tratti in tangenza: nullo nel primo terzo
    // della fascia, poi sale con continuita' C1 fino a 1.
    const auto guideFade = [&](const Vec2 &uv) {
        if (contactPolygon.empty()) return 1.0;
        const double f = std::clamp((polylineDistance(contactPolygon, uv) / (kGuideBlend * width) - 1.0 / 3.0) * 1.5, 0.0, 1.0);
        return f * f * (3.0 - 2.0 * f);
    };

    bool foldsBack = false;  // una faccia adiacente prosegue verso l'esterno del contorno proiettato

    // Campioni di un tratto del contorno.
    const auto boundarySample = [&](int k, double s) {
        const Piece &piece = loop[std::size_t(k)];
        Sample sample;
        sample.piece = k;
        sample.point = piece.at(s);
        sample.uv = project(sample.point);
        const Vec3 t = piece.tangent(s);
        const Vec2 q(dot(t, frame.xDir()), dot(t, frame.yDir()));
        if (norm(q) > 1e-6 * norm(t)) sample.inward = normalized(Vec2(-q.y(), q.x()));
        const int c = pieceContact[std::size_t(k)];
        if (c >= 0 && norm(sample.inward) > 0.0) {
            sample.contact = faces[std::size_t(c)].at(sample.point, scale);
            if (sample.contact.valid) {
                // Velocita' unitaria: 1 mm nello spazio per 1 mm nel piano dei
                // parametri. Con la velocita' coerente con la proiezione
                // (1 / coseno della pendenza) le facce adiacenti ripide
                // spingevano la superficie troppo in alto, con pieghe tra le guide.
                sample.lambda = options.influence;
                const Vec3 d3 = sample.inward.x() * frame.xDir() + sample.inward.y() * frame.yDir();
                if (dot(sample.contact.continuation, d3) < 0.0) foldsBack = true;
            }
        }
        return sample;
    };

    // Verso della faccia: la normale come quella delle facce adiacenti (le
    // loro fin percorrono il bordo comune al contrario).
    int agree = 0, disagree = 0;
    for (int k = 0; k < pieceCount; ++k) {
        if (pieceContact[std::size_t(k)] < 0) continue;
        const Sample sample = boundarySample(k, 0.5);
        if (!sample.contact.valid) continue;
        (dot(sample.contact.finTangent, loop[std::size_t(k)].tangent(0.5)) < 0.0 ? agree : disagree) += 1;
    }
    const bool flip = disagree > agree;
    const double sign = flip ? -1.0 : 1.0;

    // Conflitti tra le guide e la tangenza nei punti in cui arrivano sul bordo.
    if (options.continuity > 0)
        for (std::size_t g = 0; g < guides.size(); ++g) {
            double worst = 0.0;
            for (int end = 0; end < 2; ++end) {
                const PathSegment &guide = guides[g];
                const double t = end == 0 ? guide.range.lo : guide.range.hi;
                Vec3 d[2];
                guide.curve->evaluate(t, 1, d);
                for (int k = 0; k < pieceCount; ++k) {
                    const int c = pieceContact[std::size_t(k)];
                    if (c < 0) continue;
                    const Piece &piece = loop[std::size_t(k)];
                    if (projectPoint(*piece.curve, d[0], piece.range).distance > match) continue;
                    const ContactPoint contact = faces[std::size_t(c)].at(d[0], scale);
                    if (!contact.valid || !(norm(d[1]) > 0.0)) continue;
                    worst = std::max(worst, std::asin(std::min(1.0, std::fabs(dot(normalized(d[1]), contact.normal)))));
                    break;
                }
            }
            if (worst > 0.5 * kPi / 180.0)
                out.notes.push_back("la curva guida " + std::to_string(g + 1) + " arriva sul bordo a " + degrees(worst)
                                    + " gradi dal piano tangente della faccia adiacente: guida e tangenza non sono compatibili");
        }

    // Soluzione su griglie sempre piu' fitte.
    std::shared_ptr<BSplineSurface> surface;
    int spans = 8;
    for (;;) {
        const int uSpans = std::max(4, int(std::lround(spans * (u1 - u0) / width))), vSpans = std::max(4, int(std::lround(spans * (v1 - v0) / width)));
        const Grid grid(u0, u1, uSpans, v0, v1, vSpans);
        const double h = grid.h;
        std::vector<Sample> edgeSamples, guideSamples;
        for (int k = 0; k < pieceCount; ++k) {
            const int count = std::max(8, int(std::ceil(4.0 * loop[std::size_t(k)].length / h)));
            for (int i = 0; i <= count; ++i) edgeSamples.push_back(boundarySample(k, double(i) / count));
        }
        if (options.guideWeight > 0.0)
            for (const PathSegment &guide : guides) {
                const int count = std::max(8, int(std::ceil(4.0 * arcLength(*guide.curve, guide.range) / h)));
                for (int i = 0; i <= count; ++i) {
                    Sample sample;
                    sample.point = guide.curve->point(guide.range.lo + guide.range.length() * i / count);
                    sample.uv = project(sample.point);
                    sample.weight = options.guideWeight * guideFade(sample.uv);
                    guideSamples.push_back(sample);
                }
            }

        // Dati: contorno (peso 1), guide, derivate trasversali.
        const auto assemble = [&](const std::vector<Vec3> *second) {
            NormalSystem system(grid);
            for (const Sample &s : edgeSamples) system.add(rowAt(grid, s.uv.x(), s.uv.y(), {{0, 0, 1.0}}), s.point, kBoundaryWeight, true);
            for (const Sample &s : guideSamples) system.add(rowAt(grid, s.uv.x(), s.uv.y(), {{0, 0, 1.0}}), s.point, s.weight, true);
            for (std::size_t i = 0; i < edgeSamples.size(); ++i) {
                const Sample &s = edgeSamples[i];
                if (!s.contact.valid) continue;
                const double du = s.inward.x(), dv = s.inward.y();
                system.add(rowAt(grid, s.uv.x(), s.uv.y(), {{1, 0, du}, {0, 1, dv}}), s.lambda * s.contact.continuation, kTangentWeight * h * h);
                if (second)
                    system.add(rowAt(grid, s.uv.x(), s.uv.y(), {{2, 0, du * du}, {1, 1, 2.0 * du * dv}, {0, 2, dv * dv}}), (*second)[i], kTangentWeight * h * h * h * h);
            }
            const double dataTrace = system.positionTrace;
            NormalSystem energy(grid);
            addEnergy(grid, energy, 1.0);
            double energyTrace = 0.0;
            for (int i = 0; i < energy.matrix.size(); ++i) energyTrace += energy.matrix.at(i, i);
            const double alpha = kSmoothing * dataTrace / energyTrace;
            addEnergy(grid, system, alpha);
            for (int i = 0; i < system.matrix.size(); ++i) system.matrix.at(i, i) += 1e-12 * alpha * energyTrace / system.matrix.size();
            const BandMatrix original = system.matrix;
            if (!system.matrix.factor()) throw std::domain_error("riempimento: sistema dei vincoli non risolvibile");
            std::vector<Vec3> poles(std::size_t(grid.nu * grid.nv));
            for (int c = 0; c < 3; ++c) {
                // Pesi molto diversi (contorno ed energia): due passi di raffinamento iterativo.
                std::vector<double> x = system.rhs[c];
                system.matrix.solve(x);
                for (int step = 0; step < 2; ++step) {
                    std::vector<double> r = original.multiply(x);
                    for (std::size_t i = 0; i < r.size(); ++i) r[i] = system.rhs[c][i] - r[i];
                    system.matrix.solve(r);
                    for (std::size_t i = 0; i < r.size(); ++i) x[i] += r[i];
                }
                for (std::size_t i = 0; i < poles.size(); ++i) poles[i][c] = x[i];
            }
            return std::make_shared<BSplineSurface>(kDegree, kDegree, grid.uKnots, grid.vKnots, grid.nu, grid.nv, std::move(poles));
        };
        surface = assemble(nullptr);
        if (options.continuity >= 2 && out.contactPieces > 0) {
            // G2: la parte normale della derivata seconda trasversale e' fissata,
            // quella tangente segue la soluzione corrente.
            for (int iteration = 0; iteration < 4; ++iteration) {
                std::vector<Vec3> second(edgeSamples.size());
                for (std::size_t i = 0; i < edgeSamples.size(); ++i) {
                    const Sample &s = edgeSamples[i];
                    if (!s.contact.valid) continue;
                    Vec3 d[9];
                    surface->evaluate(s.uv.x(), s.uv.y(), 2, d);
                    const double du = s.inward.x(), dv = s.inward.y();
                    const Vec3 current = du * du * d[Surface::derivativeIndex(2, 0, 2)] + 2.0 * du * dv * d[Surface::derivativeIndex(1, 1, 2)]
                                       + dv * dv * d[Surface::derivativeIndex(0, 2, 2)];
                    const Vec3 n = s.contact.normal;
                    second[i] = current - dot(current, n) * n + s.contact.curvature * s.lambda * s.lambda * n;
                }
                surface = assemble(&second);
            }
        }

        // Scarti sui punti di mezzo dei campioni.
        double boundaryError = 0.0, guideError = 0.0, blendError = 0.0;
        for (int k = 0; k < pieceCount; ++k) {
            const int count = 2 * std::max(8, int(std::ceil(4.0 * loop[std::size_t(k)].length / h)));
            for (int i = 0; i < count; ++i) {
                const Vec3 p = loop[std::size_t(k)].at((i + 0.5) / count);
                const Vec2 q = project(p);
                boundaryError = std::max(boundaryError, distance(surface->point(q.x(), q.y()), p));
            }
        }
        for (const PathSegment &guide : guides) {
            const int count = 2 * std::max(8, int(std::ceil(4.0 * arcLength(*guide.curve, guide.range) / h)));
            for (int i = 0; i < count; ++i) {
                const Vec3 p = guide.curve->point(guide.range.lo + guide.range.length() * (i + 0.5) / count);
                const Vec2 q = project(p);
                // Distanza dalla superficie: il punto con gli stessi parametri e' solo un limite superiore.
                const SurfaceProjection near = projectPoint(*surface, p);
                const double gap = std::min(near.distance, distance(surface->point(q.x(), q.y()), p));
                if (guideFade(q) >= 1.0) guideError = std::max(guideError, gap);
                else blendError = std::max(blendError, gap);
            }
        }
        out.boundaryDeviation = boundaryError;
        out.guideDeviation = guideError;
        out.guideBlendDeviation = blendError;
        out.spans = spans;
        const bool guidesOk = options.guideWeight <= 0.0 || guides.empty() || guideError <= tolerance;
        if ((boundaryError <= tolerance && guidesOk) || spans >= options.maxSpans) break;
        spans = std::min(options.maxSpans, 2 * spans);
    }

    // Tangenza e curvatura ottenute.
    for (int k = 0; k < pieceCount; ++k) {
        if (pieceContact[std::size_t(k)] < 0) continue;
        for (int i = 0; i <= 64; ++i) {
            const Sample s = boundarySample(k, i / 64.0);
            if (!s.contact.valid) continue;
            Vec3 d[9];
            surface->evaluate(s.uv.x(), s.uv.y(), 2, d);
            const Vec3 su = d[Surface::derivativeIndex(1, 0, 2)], sv = d[Surface::derivativeIndex(0, 1, 2)];
            const Vec3 n = cross(su, sv);
            if (!(norm(n) > 0.0)) continue;
            const Vec3 nf = sign * normalized(n);
            out.tangentAngle = std::max(out.tangentAngle, std::acos(std::clamp(dot(nf, s.contact.normal), -1.0, 1.0)));
            if (options.continuity >= 2) {
                const double du = s.inward.x(), dv = s.inward.y();
                const Vec3 first = du * su + dv * sv;
                const Vec3 second = du * du * d[Surface::derivativeIndex(2, 0, 2)] + 2.0 * du * dv * d[Surface::derivativeIndex(1, 1, 2)]
                                  + dv * dv * d[Surface::derivativeIndex(0, 2, 2)];
                if (squaredNorm(first) > 0.0)
                    out.curvatureDeviation = std::max(out.curvatureDeviation, std::fabs(dot(second, nf) / squaredNorm(first) - s.contact.curvature));
            }
        }
    }
    if (foldsBack)
        out.notes.push_back("in qualche punto la faccia adiacente prosegue verso l'esterno del contorno (visto dalla sua normale media): "
                            "la tangenza obbliga la superficie a ripiegarsi");
    if (out.boundaryDeviation > tolerance)
        out.notes.push_back("il contorno resta a " + std::to_string(out.boundaryDeviation) + " mm dalla superficie (edge tolleranti)");

    // Lamina: gli edge sono le curve del contorno, nell'ordine del contorno.
    detail::RawModel model;
    detail::RawFace face;
    face.surface = surface;
    face.sense = !flip;
    std::vector<detail::RawFin> fins;
    for (int k = 0; k < pieceCount; ++k) model.points.push_back(loop[std::size_t(k)].start());
    for (int k = 0; k < pieceCount; ++k) {
        const Piece &piece = loop[std::size_t(k)];
        detail::RawEdge edge;
        const int a = k, b = (k + 1) % pieceCount;
        edge.start = piece.forward ? a : b;
        edge.end = piece.forward ? b : a;
        edge.curve = piece.curve;
        edge.hasRange = true;
        edge.range = piece.range;
        // SP-curve: la proiezione della curva sul piano dei parametri, nello
        // stesso parametro (i campioni sono stati presi cosi'). Proiettare i
        // punti sulla superficie potrebbe trovare la parte prolungata oltre il bordo.
        std::vector<double> breaks = piece.curve->breakpoints(piece.range);
        breaks.erase(std::remove_if(breaks.begin(), breaks.end(), [&](double t) { return t <= piece.range.lo || t >= piece.range.hi; }), breaks.end());
        const auto planar = fitCurve([&](double t) {
            const Vec2 q = project(piece.curve->point(t));
            return Vec3(q.x(), q.y(), 0.0);
        }, piece.range, breaks, 1e-10 * scale);
        std::vector<Vec2> poles2;
        for (const Vec3 &pole : planar->poles()) poles2.emplace_back(pole.x(), pole.y());
        const auto pcurve = std::make_shared<BSplineCurve<2>>(planar->degree(), planar->knots(), std::move(poles2));
        const double gap = pcurveDeviation(*surface, *piece.curve, *pcurve, piece.range);
        fins.push_back(detail::RawFin(int(model.edges.size()), piece.forward, pcurve, std::max(gap, 1e-12)));
        model.edges.push_back(edge);
    }
    if (flip) {
        // Normale della faccia opposta a quella della superficie: il loop gira al contrario.
        std::reverse(fins.begin(), fins.end());
        for (detail::RawFin &fin : fins) fin.sense = !fin.sense;
    }
    face.loops.push_back(fins);
    model.faces.push_back(face);
    return detail::assembleBody(model, false, &out.notes);
}

}
