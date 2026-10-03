#include "fk_boundary.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_exchange.h"
#include "fk_offset.h"

namespace ForgeCad::Kernel {
namespace {

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
    // Tangente nel verso del contorno.
    Vec3 tangent(double s) const {
        Vec3 d[2];
        curve->evaluate(parameter(s), 1, d);
        return forward ? d[1] : -d[1];
    }
    // Lunghezza da s = 0 a s.
    double lengthTo(double s) const {
        if (s <= 0.0) return 0.0;
        if (s >= 1.0) return length;
        const double t = parameter(s);
        return arcLength(*curve, forward ? Interval{range.lo, t} : Interval{t, range.hi});
    }
};

// Punto del contorno: tratto e frazione s nel tratto.
struct Mark {
    int piece = 0;
    double s = 0.0;
};

// Pezzo di un lato: il tratto `piece` da s0 a s1 (s0 < s1).
struct SubPiece {
    int piece = 0;
    double s0 = 0.0, s1 = 1.0;
};

std::vector<Piece> chainLoop(const std::vector<PathSegment> &input, double tolerance) {
    std::vector<Piece> pieces;
    for (const PathSegment &segment : input) {
        if (!segment.curve || !(segment.range.length() > 0.0)) throw std::domain_error("superficie tra curve: tratto non valido");
        Piece piece{segment.curve, segment.range, true, arcLength(*segment.curve, segment.range)};
        if (!(piece.length > tolerance)) throw std::domain_error("superficie tra curve: tratto di lunghezza nulla");
        pieces.push_back(piece);
    }
    if (pieces.empty()) throw std::domain_error("superficie tra curve: nessuna curva");
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
            if (found >= 0) throw std::domain_error("superficie tra curve: piu' di due curve in un estremo");
            found = int(j);
            forward = head;
        }
        if (found < 0) throw std::domain_error("superficie tra curve: le curve non formano un contorno chiuso");
        Piece next = pieces[std::size_t(found)];
        next.forward = forward;
        used[std::size_t(found)] = true;
        loop.push_back(next);
    }
    if (distance(loop.back().end(), loop.front().start()) > tolerance)
        throw std::domain_error("superficie tra curve: le curve non formano un contorno chiuso");
    return loop;
}

// Coppia di curve resa compatibile: stessi nodi (stesso grado 3, stesso dominio).
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

std::vector<double> greville(const BSplineCurve<3> &c) {
    const std::vector<double> &k = c.knots();
    const int p = c.degree();
    std::vector<double> result(std::size_t(c.poleCount()));
    for (int i = 0; i < c.poleCount(); ++i) {
        double sum = 0.0;
        for (int j = 1; j <= p; ++j) sum += k[std::size_t(i + j)];
        result[std::size_t(i)] = sum / p;
    }
    return result;
}

}

Body boundarySheet(const std::vector<PathSegment> &input, double tolerance) {
    // Scala del contorno per le tolleranze di collegamento.
    double scale = 1.0;
    for (const PathSegment &segment : input)
        if (segment.curve) scale = std::max({scale, norm(segment.curve->point(segment.range.lo)), norm(segment.curve->point(segment.range.hi))});
    const double join = 1e-6 * scale;
    const std::vector<Piece> loop = chainLoop(input, join);
    const int n = int(loop.size());
    std::vector<double> cumulative(std::size_t(n) + 1, 0.0);
    for (int k = 0; k < n; ++k) cumulative[std::size_t(k) + 1] = cumulative[std::size_t(k)] + loop[std::size_t(k)].length;
    const double total = cumulative.back();
    const auto position = [&](const Mark &m) { return cumulative[std::size_t(m.piece)] + loop[std::size_t(m.piece)].lengthTo(m.s); };

    // Angoli vivi: dove la tangente non continua.
    std::vector<Mark> marks;
    for (int k = 0; k < n; ++k) {
        const Vec3 a = loop[std::size_t((k + n - 1) % n)].tangent(1.0), b = loop[std::size_t(k)].tangent(0.0);
        const double na = norm(a), nb = norm(b);
        if (!(na > 0.0) || !(nb > 0.0)) {
            marks.push_back({k, 0.0});
            continue;
        }
        const double angle = std::atan2(norm(cross(a, b)), dot(a, b));
        if (angle > 1e-2) marks.push_back({k, 0.0});
    }
    if (marks.size() > 4)
        throw std::domain_error("superficie tra curve: il contorno ha " + std::to_string(marks.size()) + " angoli vivi (al massimo 4 lati)");
    if (marks.empty()) marks.push_back({0, 0.0});
    // Meno di tre angoli: si divide a meta' il lato piu' lungo fino a quattro lati.
    while (marks.size() < 4) {
        std::size_t longest = 0;
        double best = -1.0;
        for (std::size_t i = 0; i < marks.size(); ++i) {
            const double from = position(marks[i]);
            double to = position(marks[(i + 1) % marks.size()]);
            if (to <= from) to += total;
            if (to - from > best) best = to - from, longest = i;
        }
        double target = position(marks[longest]) + 0.5 * best;
        if (target >= total) target -= total;
        int k = 0;
        while (k + 1 < n && cumulative[std::size_t(k) + 1] <= target) ++k;
        const Piece &piece = loop[std::size_t(k)];
        const double local = target - cumulative[std::size_t(k)];
        double lo = 0.0, hi = 1.0;
        for (int iteration = 0; iteration < 60; ++iteration) {
            const double mid = 0.5 * (lo + hi);
            (piece.lengthTo(mid) < local ? lo : hi) = mid;
        }
        marks.insert(marks.begin() + std::ptrdiff_t(longest) + 1, Mark{k, 0.5 * (lo + hi)});
        std::sort(marks.begin(), marks.end(), [](const Mark &x, const Mark &y) { return x.piece != y.piece ? x.piece < y.piece : x.s < y.s; });
    }

    // Lati: dal segno i al segno i + 1 lungo il contorno.
    const int sideCount = int(marks.size());
    std::vector<std::vector<SubPiece>> sides(static_cast<std::size_t>(sideCount));
    for (int i = 0; i < sideCount; ++i) {
        const Mark from = marks[std::size_t(i)], to = marks[std::size_t((i + 1) % sideCount)];
        Mark at = from;
        for (int guard = 0; guard <= n + 1; ++guard) {
            const bool last = at.piece == to.piece && to.s > at.s;
            const double end = last ? to.s : 1.0;
            if (end > at.s + 1e-12) sides[std::size_t(i)].push_back({at.piece, at.s, end});
            if (last) break;
            at = {(at.piece + 1) % n, 0.0};
            if (at.piece == to.piece && to.s <= 0.0) break;
        }
        if (sides[std::size_t(i)].empty()) throw std::domain_error("superficie tra curve: lato di lunghezza nulla");
    }

    // Lato come funzione di s in [0, 1] (proporzionale alla lunghezza dei pezzi).
    const auto sideFunction = [&](const std::vector<SubPiece> &side, bool reversed, std::vector<double> &breaks) {
        std::vector<double> bounds{0.0};
        for (const SubPiece &sub : side) {
            const Piece &piece = loop[std::size_t(sub.piece)];
            bounds.push_back(bounds.back() + piece.lengthTo(sub.s1) - piece.lengthTo(sub.s0));
        }
        const double length = bounds.back();
        if (!(length > join)) throw std::domain_error("superficie tra curve: lato di lunghezza nulla");
        for (double &b : bounds) b /= length;
        breaks.clear();
        for (std::size_t m = 1; m + 1 < bounds.size(); ++m) breaks.push_back(reversed ? 1.0 - bounds[m] : bounds[m]);
        return std::function<Vec3(double)>([&loop, side, bounds, reversed](double sigma) {
            if (reversed) sigma = 1.0 - sigma;
            std::size_t m = 0;
            while (m + 2 < bounds.size() && sigma > bounds[m + 1]) ++m;
            const double width = bounds[m + 1] - bounds[m];
            const double f = width > 0.0 ? std::clamp((sigma - bounds[m]) / width, 0.0, 1.0) : 0.0;
            const SubPiece &sub = side[m];
            return loop[std::size_t(sub.piece)].at(sub.s0 + f * (sub.s1 - sub.s0));
        });
    };
    const auto fit = [&](int side, bool reversed) {
        std::vector<double> breaks;
        const auto f = sideFunction(sides[std::size_t(side)], reversed, breaks);
        return *fitCurve(f, {0.0, 1.0}, breaks, tolerance);
    };
    // C0: lato 0 (v = 0), D1: lato 1 (u = 1), C1: lato 2 al contrario (v = 1),
    // D0: lato 3 al contrario (u = 0) o, con tre lati, il punto d'angolo.
    BSplineCurve<3> c0 = fit(0, false), d1 = fit(1, false), c1 = fit(2, true);
    makeCompatible(c0, c1);
    std::shared_ptr<BSplineCurve<3>> d0;
    if (sideCount == 4) {
        BSplineCurve<3> left = fit(3, true);
        makeCompatible(left, d1);
        d0 = std::make_shared<BSplineCurve<3>>(left);
    } else {
        d0 = std::make_shared<BSplineCurve<3>>(3, d1.knots(), std::vector<Vec3>(std::size_t(d1.poleCount()), c0.poles().front()));
    }
    const Vec3 p00 = c0.poles().front(), p10 = c0.poles().back(), p01 = c1.poles().front(), p11 = c1.poles().back();
    const std::vector<double> xi = greville(c0), eta = greville(d1);
    const int nu = c0.poleCount(), nv = d1.poleCount();
    std::vector<Vec3> poles(std::size_t(nu) * std::size_t(nv));
    for (int i = 0; i < nu; ++i) {
        const double u = xi[std::size_t(i)];
        for (int j = 0; j < nv; ++j) {
            const double v = eta[std::size_t(j)];
            const Vec3 bilinear = (1 - u) * (1 - v) * p00 + u * (1 - v) * p10 + (1 - u) * v * p01 + u * v * p11;
            poles[std::size_t(i) * std::size_t(nv) + std::size_t(j)] = (1 - v) * c0.poles()[std::size_t(i)] + v * c1.poles()[std::size_t(i)]
                + (1 - u) * d0->poles()[std::size_t(j)] + u * d1.poles()[std::size_t(j)] - bilinear;
        }
    }
    const auto surface = std::make_shared<BSplineSurface>(3, 3, c0.knots(), d1.knots(), nu, nv, std::move(poles));

    // Lamina: gli edge sono i pezzi delle curve date, nell'ordine del contorno.
    detail::RawModel model;
    detail::RawFace face;
    face.surface = surface;
    face.sense = true;
    std::vector<SubPiece> all;
    for (const auto &side : sides) all.insert(all.end(), side.begin(), side.end());
    const int m = int(all.size());
    for (const SubPiece &sub : all) model.points.push_back(loop[std::size_t(sub.piece)].at(sub.s0));
    std::vector<detail::RawFin> fins;
    for (int k = 0; k < m; ++k) {
        const SubPiece &sub = all[std::size_t(k)];
        const Piece &piece = loop[std::size_t(sub.piece)];
        detail::RawEdge edge;
        const int a = k, b = (k + 1) % m;
        edge.start = piece.forward ? a : b;
        edge.end = piece.forward ? b : a;
        edge.curve = piece.curve;
        edge.hasRange = true;
        const double t0 = piece.parameter(sub.s0), t1 = piece.parameter(sub.s1);
        edge.range = {std::min(t0, t1), std::max(t0, t1)};
        fins.push_back({int(model.edges.size()), piece.forward});
        model.edges.push_back(edge);
    }
    face.loops.push_back(fins);
    model.faces.push_back(face);
    return detail::assembleBody(model, false);
}

}
