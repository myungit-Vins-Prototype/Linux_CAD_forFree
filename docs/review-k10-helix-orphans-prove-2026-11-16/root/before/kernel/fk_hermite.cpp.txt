#include "fk_hermite.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

#include "fk_bspline_basis.h"

namespace ForgeCad::Kernel::detail {
namespace {

constexpr int kDegree = 5;

// Poli di Bezier del quintico di Hermite su [a, a + h].
void hermiteBezier(const Vec3 *p0, const Vec3 *p1, double h, Vec3 *b) {
    b[0] = p0[0];
    b[1] = p0[0] + (h / 5.0) * p0[1];
    b[2] = p0[0] + (2.0 * h / 5.0) * p0[1] + (h * h / 20.0) * p0[2];
    b[3] = p1[0] - (2.0 * h / 5.0) * p1[1] + (h * h / 20.0) * p1[2];
    b[4] = p1[0] - (h / 5.0) * p1[1];
    b[5] = p1[0];
}

Vec3 bezierPoint(const Vec3 *b, double f) {
    Vec3 q[6];
    for (int i = 0; i < 6; ++i) q[i] = b[i];
    for (int r = 1; r < 6; ++r)
        for (int i = 0; i < 6 - r; ++i) q[i] = (1.0 - f) * q[i] + f * q[i + 1];
    return q[0];
}

bool solve3(double (&m)[3][3], double (&d)[3]) {
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::fabs(m[r][c]) > std::fabs(m[pivot][c])) pivot = r;
        if (!(std::fabs(m[pivot][c]) > 0.0)) return false;
        std::swap(m[c], m[pivot]);
        std::swap(d[c], d[pivot]);
        for (int r = c + 1; r < 3; ++r) {
            const double f = m[r][c] / m[c][c];
            for (int k = c; k < 3; ++k) m[r][k] -= f * m[c][k];
            d[r] -= f * d[c];
        }
    }
    for (int c = 2; c >= 0; --c) {
        for (int k = c + 1; k < 3; ++k) d[c] -= m[c][k] * d[k];
        d[c] /= m[c][c];
    }
    return true;
}

// Una parte liscia: nodi distinti e dati (valore, derivate) in ciascuno.
struct Part {
    std::vector<double> u;
    std::vector<std::vector<Vec3>> data;  // per nodo: 3 * rows
};

// B-spline C2 (nodi interni tripli) della parte: i tre poli di ogni nodo
// dalle tre condizioni in quel nodo (le altre funzioni di base vi si annullano
// con le derivate prima e seconda).
void partSpline(const Part &part, int rows, std::vector<double> &knots, std::vector<std::vector<Vec3>> &poles) {
    const int n = int(part.u.size()) - 1;
    knots.clear();
    for (int j = 0; j <= kDegree; ++j) knots.push_back(part.u.front());
    for (int i = 1; i < n; ++i)
        for (int j = 0; j < 3; ++j) knots.push_back(part.u[std::size_t(i)]);
    for (int j = 0; j <= kDegree; ++j) knots.push_back(part.u.back());
    poles.assign(std::size_t(rows), std::vector<Vec3>(std::size_t(3 * n + 3)));
    double ders[3 * (kDegree + 1)];
    for (int i = 0; i <= n; ++i) {
        const int span = i < n ? 3 * i + 5 : 3 * n + 2;
        basisFunctionDerivatives(knots, span, part.u[std::size_t(i)], kDegree, 2, ders);
        const int offset = 3 * i - (span - kDegree);
        for (int r = 0; r < rows; ++r)
            for (int coordinate = 0; coordinate < 3; ++coordinate) {
                double m[3][3], d[3];
                for (int k = 0; k < 3; ++k) {
                    for (int j = 0; j < 3; ++j) m[k][j] = ders[k * (kDegree + 1) + offset + j];
                    d[k] = part.data[std::size_t(i)][std::size_t(3 * r + k)][coordinate];
                }
                if (!solve3(m, d)) throw std::domain_error("fitQuinticRows: nodi degeneri");
                for (int j = 0; j < 3; ++j) poles[std::size_t(r)][std::size_t(3 * i + j)][coordinate] = d[j];
            }
    }
}

}

RowSpline fitQuinticRows(const RowSampler &sample, int rows, const std::vector<double> &breaks, double tolerance, double maxStep) {
    if (rows <= 0 || breaks.size() < 2) throw std::invalid_argument("fitQuinticRows: dati non validi");
    for (std::size_t k = 1; k < breaks.size(); ++k)
        if (!(breaks[k] > breaks[k - 1])) throw std::invalid_argument("fitQuinticRows: punti di rottura non crescenti");
    const std::size_t width = std::size_t(3 * rows);
    std::vector<Vec3> buffer(width), exact(width);
    const auto evaluate = [&](double t, bool left) {
        std::vector<Vec3> values(width);
        sample(t, left, values.data());
        for (const Vec3 &v : values)
            if (!isFinite(v)) throw std::domain_error("fitQuinticRows: funzione non finita");
        return values;
    };
    RowSpline result;
    const double total = breaks.back() - breaks.front();
    int guard = 0;
    for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
        const double a = breaks[k], b = breaks[k + 1];
        const bool rightEndIsBreak = k + 2 < breaks.size();
        // Dati negli estremi della parte: da destra all'inizio, da sinistra alla fine.
        std::map<double, std::vector<Vec3>> data;
        data[a] = evaluate(a, false);
        std::vector<Vec3> endData = evaluate(b, rightEndIsBreak);
        const auto at = [&](double t) -> const std::vector<Vec3> & {
            if (t == b) return endData;
            auto found = data.find(t);
            if (found == data.end()) found = data.emplace(t, evaluate(t, false)).first;
            return found->second;
        };
        int pieces = 1;
        if (maxStep > 0.0) pieces = std::max(1, int(std::ceil((b - a) / maxStep - 1e-9)));
        std::vector<std::pair<double, double>> pending;
        for (int j = pieces; j > 0; --j) pending.push_back({a + (b - a) * (j - 1) / pieces, j == pieces ? b : a + (b - a) * j / pieces});
        Part part;
        part.u.push_back(a);
        part.data.push_back(data[a]);
        while (!pending.empty()) {
            if (++guard > 200000) throw std::domain_error("fitQuinticRows: funzione troppo complessa da approssimare");
            const auto [lo, hi] = pending.back();
            pending.pop_back();
            const std::vector<Vec3> &p0 = at(lo);
            const std::vector<Vec3> &p1 = at(hi);
            const double h = hi - lo;
            double error = 0.0;
            for (double f : {0.15, 0.35, 0.5, 0.65, 0.85}) {
                sample(lo + f * h, false, exact.data());
                for (int r = 0; r < rows; ++r) {
                    Vec3 bez[6];
                    hermiteBezier(&p0[std::size_t(3 * r)], &p1[std::size_t(3 * r)], h, bez);
                    error = std::max(error, distance(bezierPoint(bez, f), exact[std::size_t(3 * r)]));
                }
            }
            if (error > tolerance && h > 1e-7 * total) {
                const double m = 0.5 * (lo + hi);
                pending.push_back({m, hi});
                pending.push_back({lo, m});
                continue;
            }
            result.error = std::max(result.error, error);
            part.u.push_back(hi);
            part.data.push_back(p1);
        }
        std::vector<double> knots;
        std::vector<std::vector<Vec3>> poles;
        partSpline(part, rows, knots, poles);
        // Concatenazione: nel punto di rottura nodo di molteplicita' 5, polo comune.
        if (result.knots.empty()) {
            result.knots = std::move(knots);
            result.poles = std::move(poles);
        } else {
            result.knots.pop_back();
            result.knots.insert(result.knots.end(), knots.begin() + (kDegree + 1), knots.end());
            for (int r = 0; r < rows; ++r) {
                std::vector<Vec3> &target = result.poles[std::size_t(r)];
                target.insert(target.end(), poles[std::size_t(r)].begin() + 1, poles[std::size_t(r)].end());
            }
        }
        for (std::size_t i = result.parameters.empty() ? 0 : 1; i < part.u.size(); ++i) result.parameters.push_back(part.u[i]);
    }
    return result;
}

BSplineCurve<3> rowCurve(const RowSpline &spline, int row) {
    return BSplineCurve<3>(kDegree, spline.knots, spline.poles.at(std::size_t(row)));
}

}
