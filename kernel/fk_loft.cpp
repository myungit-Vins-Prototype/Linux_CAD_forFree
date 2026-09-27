#include "fk_loft.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline_basis.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
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

Body loft(const std::vector<LoftSection> &input, bool ruled, bool closed) {
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
    // Punto di partenza dei loop: il piu' vicino (in direzione dal baricentro) alla partenza della sezione precedente.
    if (closed)
        for (std::size_t i = 1; i < n; ++i) {
            Section &s = sections[i];
            const Section &previous = sections[i - 1];
            const Vec3 reference = previous.pieces.front().curve->point(previous.pieces.front().range.lo) - previous.centroid;
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
        double total = 0.0;
        std::vector<double> lengths;
        for (const Piece &p : pieces) lengths.push_back(arcLength(*p.curve, p.range, 1e-13)), total += lengths.back();
        // Per ogni taglio: tratto e parametro.
        std::vector<std::pair<std::size_t, double>> at;
        for (double c : cuts) {
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
            double total = 0.0;
            std::vector<double> lengths;
            for (const BSplineCurve<3> &b : bezier[i]) lengths.push_back(arcLength(b, b.domain(), 1e-13)), total += lengths.back();
            double cumulative = 0.0;
            breaks[i].push_back(0.0);
            for (std::size_t k = 0; k < lengths.size(); ++k) {
                cumulative += lengths[k];
                breaks[i].push_back(k + 1 == lengths.size() ? 1.0 : cumulative / total);
                if (k + 1 < lengths.size()) interior.push_back(breaks[i].back());
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

    // Parametri delle sezioni lungo il loft (distanze tra i baricentri).
    std::vector<double> v(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) v[i] = v[i - 1] + distance(sections[i].centroid, sections[i - 1].centroid);
    for (double &value : v) value /= v.back();
    v.back() = 1.0;

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
    // Facce di testa: uscenti verso -n nella prima sezione, +n nell'ultima (i loop girano attorno a n).
    if (closed) {
        for (int cap = 0; cap < 2; ++cap) {
            const std::size_t i = cap == 0 ? 0 : n - 1;
            Body::BuildFace face;
            face.surface = std::make_shared<Plane>(Frame3(input[i].frame.origin(), sections[i].normal, input[i].frame.xDir()));
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
    Body body = closed ? Body::build(vertices, edges, faces) : Body::buildSheet(vertices, edges, faces);
    computePCurves(body);
    const std::vector<CheckIssue> issues = checkBody(body);
    if (!issues.empty()) throw std::domain_error("loft: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    // Pezze sulla stessa superficie (i lati piani di un loft rigato divisi dalle
    // frazioni di lunghezza delle altre sezioni) e spigoli allineati: una faccia e
    // un edge (come dopo le booleane).
    return unifySameDomain(body);
}

}

Body loftSolid(const std::vector<LoftSection> &sections, bool ruled) {
    for (const LoftSection &s : sections) {
        const ProfileSegment &a = s.loop.segments.front(), &b = s.loop.segments.back();
        if (distance(a.start(), b.end()) > 1e-6 * std::max(1.0, norm(a.start()))) throw std::domain_error("loft: una sezione non e' un contorno chiuso");
    }
    return loft(sections, ruled, true);
}

Body loftSheet(const std::vector<LoftSection> &sections, bool ruled) { return loft(sections, ruled, false); }

}
