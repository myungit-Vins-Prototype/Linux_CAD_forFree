#include "fk_quilt_mapping.h"
#include "fk_mapping_fit.h"
#include "fk_bspline_surface.h"
#include "fk_classify_uv.h"
#include "fk_curve_ops.h"
#include "fk_curve_algo.h"
#include "fk_cancel.h"
#include "fk_precision.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_body_check.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
void require(bool b, const char *m) {
    if (!b)
        throw std::domain_error(m);
}
std::pair<Interval, Interval> window(const Body &body, FaceId id) {
    Box box;
    for (auto loop : body.face(id).loops)
        for (auto use : body.loopFins(loop)) {
            const auto &fin = body.fin(use);
            require(bool(fin.pcurve), "Insieme di superfici: SP-curve assente.");
            box.add(curveBox(*embedCurve(fin.pcurve, Frame3()), body.edge(fin.edge).range));
        }
    require(!box.isEmpty() && box.hi.x() > box.lo.x() && box.hi.y() > box.lo.y(), "Insieme di superfici: dominio non limitato.");
    return { { box.lo.x(), box.hi.x() }, { box.lo.y(), box.hi.y() } };
}
std::vector<double> uniform(Interval range, int n) {
    std::vector<double> out;
    for (int i = 0; i <= n; ++i)
        out.push_back(i == n ? range.hi : range.lo + (range.hi - range.lo) * i / n);
    return out;
}
// Lunghezze integrate sulle isocurve esatte, con prefissi per campata.
// Nessuna derivata mista viene integrata: sulle NURBS molto fitte puo'
// essere dominata dalla cancellazione numerica senza migliorare la mappa.
struct ArcSlice {
    CurvePtr<3> curve;
    std::vector<double> cuts, lengths;
    double origin = 0;
    ArcSlice(const Surface &surface, Interval range, double v, double u0)
        : curve(surface.vIso(v)) {
        require(bool(curve), "Insieme di superfici: isocurva trasversale singolare.");
        cuts = curve->breakpoints(range);
        cuts.push_back(u0);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        long double sum = 0;
        lengths.push_back(0);
        for (std::size_t i = 1; i < cuts.size(); ++i) {
            cancellationPoint();
            sum += arcLength(*curve, { cuts[i - 1], cuts[i] }, 1e-11);
            lengths.push_back(double(sum));
        }
        origin = at(u0);
    }
    double at(double u) const {
        auto it = std::upper_bound(cuts.begin(), cuts.end(), u);
        const auto i = std::min(std::size_t(it == cuts.begin() ? 0 : it - cuts.begin() - 1), cuts.size() - 2);
        return lengths[i]
            + (u >= cuts[i] ? arcLength(*curve, { cuts[i], u }, 1e-11) : -arcLength(*curve, { u, cuts[i] }, 1e-11));
    }
    double distanceAt(double u) const { return at(u) - origin; }
};
struct Meridian {
    CurvePtr<3> curve;
    std::vector<double> cuts, lengths;
    double zero;
    Meridian(CurvePtr<3> c, Interval range, double anchor)
        : curve(std::move(c)) {
        require(bool(curve), "Insieme di superfici: meridiana singolare.");
        cuts = curve->breakpoints(range);
        cuts.push_back(anchor);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        long double sum = 0;
        lengths.push_back(0);
        for (std::size_t i = 1; i < cuts.size(); ++i) {
            cancellationPoint();
            sum += arcLength(*curve, { cuts[i - 1], cuts[i] }, 1e-11);
            lengths.push_back(double(sum));
        }
        require(std::isfinite(lengths.back()) && lengths.back() > kLinearResolution, "Insieme di superfici: meridiana degenere.");
        zero = lengthAt(anchor);
    }
    double lengthAt(double t) const {
        auto it = std::upper_bound(cuts.begin(), cuts.end(), t);
        const auto i = std::min(std::size_t(it == cuts.begin() ? 0 : it - cuts.begin() - 1), cuts.size() - 2);
        return lengths[i]
            + (t >= cuts[i] ? arcLength(*curve, { cuts[i], t }, 1e-11) : -arcLength(*curve, { t, cuts[i] }, 1e-11));
    }
    double parameter(double y) const {
        const double value = y + zero;
        require(value >= -kLinearResolution && value <= lengths.back() + kLinearResolution,
            "Insieme di superfici: punto oltre lo sviluppo longitudinale.");
        const double target = std::clamp(value, 0., lengths.back());
        auto it = std::upper_bound(lengths.begin(), lengths.end(), target);
        const auto i = std::min(std::size_t(it == lengths.begin() ? 0 : it - lengths.begin() - 1), cuts.size() - 2);
        double lo = cuts[i], hi = cuts[i + 1],
               t = lo + (hi - lo) * (target - lengths[i]) / (lengths[i + 1] - lengths[i]);
        for (int n = 0; n < 35; ++n) {
            const double residual = lengths[i] + arcLength(*curve, { cuts[i], t }, 1e-11) - target;
            if (std::fabs(residual) <= 1e-12)
                return t;
            if (residual > 0)
                hi = t;
            else
                lo = t;
            Vec3 d[2];
            curve->evaluate(t, 1, d);
            const double next = t - residual / norm(d[1]);
            t = next > lo && next < hi ? next : .5 * (lo + hi);
        }
        throw std::domain_error("Insieme di superfici: inversione della meridiana non convergente.");
    }
};
struct Chart {
    SurfaceTargetFace target;
    Interval u, v;
    double u0 = 0, sign = 1, vScale = 1, vOffset = 0;
    SurfacePtr flat;
    FaceParameterClassifier classifier;
    Chart(SurfaceTargetFace t, Interval uw, Interval vw, double origin)
        : target(t)
        , u(uw)
        , v(vw)
        , u0(origin)
        , classifier(*t.body, t.face, kLinearResolution * .1) {
        const auto &surface = *t.body->face(t.face).surface;
        std::map<double, std::shared_ptr<ArcSlice>> slices;
        std::size_t cachedIntervals = 0;
        const auto slice = [&](double value) {
            auto it = slices.find(value);
            if (it != slices.end())
                return it->second;
            auto result = std::make_shared<ArcSlice>(surface, u, value, u0);
            cachedIntervals += result->cuts.size();
            require(cachedIntervals <= 4000000, "Insieme di superfici: cache della parametrizzazione oltre il budget.");
            slices.emplace(value, result);
            return result;
        };
        const auto f = [&](double a, double b) { return Vec3(slice(b)->distanceAt(a), b, 0); };
        // Budget di approssimazione delle coordinate del piano (non un
        // allentamento della connessione del B-rep piegato). La mappa finale
        // e' la NURBS qui costruita; il contatto usa sempre il carrier esatto.
        const double chartBudget = 1e-6 * std::max(1., faceBox(*t.body, t.face).diagonal());
        double deviation = 0;
        flat = detail::fitMappingSurface(uniform(u, 4), uniform(v, 8), f, { }, chartBudget, deviation);
        // Per una B-spline polinomiale la derivata e' combinazione convessa
        // delle differenze dei poli. Positivita' di tutte le differenze X
        // certifica la monotonia trasversale, non soltanto lungo Newton.
        const auto &spline = static_cast<const BSplineSurface &>(*flat);
        for (int i = 0; i + 1 < spline.uPoleCount(); ++i)
            for (int j = 0; j < spline.vPoleCount(); ++j)
                require(spline.pole(i + 1, j).x() > spline.pole(i, j).x(),
                    "Insieme di superfici: mappa piana non monotona; parametrizzazione non supportata.");
    }
    std::pair<double, double> originAndScale(double b) const {
        Vec3 d[4], s[4];
        flat->evaluate(u0, b, 1, d);
        target.body->face(target.face).surface->evaluate(u0, b, 1, s);
        require(d[2].x() > 1e-12, "Insieme di superfici: metrica trasversale degenere.");
        return { d[0].x(), norm(s[2]) / d[2].x() };
    }
    double xAt(double a, double b) const {
        const auto at = originAndScale(b);
        return sign * at.second * (flat->point(a, b).x() - at.first);
    }
    double parameter(double x, double b) const {
        if (x == 0)
            return u0;
        double lo = u.lo, hi = u.hi;
        const auto at = originAndScale(b);
        const double a = flat->point(lo, b).x(), z = flat->point(hi, b).x(), value = sign * x / at.second + at.first;
        require(value >= a - kLinearResolution && value <= z + kLinearResolution,
            "Insieme di superfici: punto oltre lo sviluppo trasversale.");
        const double target = std::clamp(value, a, z);
        double t = lo + (hi - lo) * (target - a) / (z - a);
        for (int n = 0; n < 40; ++n) {
            Vec3 d[4];
            flat->evaluate(t, b, 1, d);
            const double residual = d[0].x() - target;
            require(d[2].x() > 1e-12, "Insieme di superfici: parametrizzazione ripiegata.");
            if (std::fabs(residual) <= 1e-12)
                return t;
            if (residual > 0)
                hi = t;
            else
                lo = t;
            const double next = t - residual / d[2].x();
            t = next > lo && next < hi ? next : .5 * (lo + hi);
        }
        throw std::domain_error("Insieme di superfici: inversione trasversale non convergente.");
    }
};
}
struct SurfaceQuiltMap::Data {
    std::vector<Chart> charts;
    std::shared_ptr<Meridian> meridian;
    double sense = 1, primarySide = 1;
    Vec2 flatPoint(std::size_t patch, const Vec2 &uv) const {
        const auto &c = charts[patch];
        return Vec2(c.xAt(uv.x(), uv.y()),
            sense * (meridian->lengthAt((uv.y() - c.vOffset) / c.vScale) - meridian->zero));
    }
    struct Contact {
        Vec3 point, normal;
        Vec2 uv;
        std::size_t patch;
    };
    Contact contact(const Vec2 &flat, bool carrierOnly, std::map<double, double> *parameters = nullptr) const {
        cancellationPoint();
        require(isFinite(flat), "Insieme di superfici: punto non finito.");
        double v;
        const auto found = parameters ? parameters->find(flat.y()) : std::map<double, double>::iterator { };
        if (parameters && found != parameters->end())
            v = found->second;
        else {
            v = meridian->parameter(sense * flat.y());
            if (parameters) {
                if (parameters->size() >= 65536)
                    parameters->clear();
                parameters->emplace(flat.y(), v);
            }
        }
        const auto index = charts.size() > 1 && flat.x() * primarySide < 0 ? 1 : 0;
        const auto &c = charts[index];
        const double localV = c.vScale * v + c.vOffset, u = c.parameter(flat.x(), localV);
        require(carrierOnly || c.classifier.classify(Vec2(u, localV)) != PointLocation::Outside,
            "Insieme di superfici: punto oltre il contorno appiattito delle facce destinazione.");
        const auto &face = c.target.body->face(c.target.face);
        Vec3 d[4];
        face.surface->evaluate(u, localV, 1, d);
        return { d[0], (face.sense ? 1. : -1.) * normalized(cross(d[2], d[1])), Vec2(u,localV),std::size_t(index) };
    }
};
Vec2 surfaceQuiltAnchor(const std::vector<SurfaceTargetFace> &targets) {
    require(!targets.empty() && targets.size() <= 2, "Insieme di superfici: scegliere una o due facce.");
    for (const auto &t : targets)
        require(t.body && t.body->contains(t.face), "Insieme di superfici: faccia assente.");
    const auto &first = *targets.front().body;
    if (targets.size() == 2) {
        require(targets[1].body == &first && targets[0].face != targets[1].face,
            "Insieme di superfici: scegliere due facce distinte dello stesso corpo.");
        for (auto loop : first.face(targets[0].face).loops)
            for (auto use : first.loopFins(loop)) {
                const auto &a = first.fin(use);
                if (!a.pcurve || a.pcurve->type() != CurveType::Line)
                    continue;
                for (auto otherLoop : first.face(targets[1].face).loops)
                    for (auto otherUse : first.loopFins(otherLoop)) {
                        const auto &b = first.fin(otherUse);
                        if (a.edge != b.edge || !b.pcurve || b.pcurve->type() != CurveType::Line)
                            continue;
                        const auto &pa = static_cast<const Line<2> &>(*a.pcurve);
                        const auto &pb = static_cast<const Line<2> &>(*b.pcurve);
                        if (std::fabs(pa.direction().x()) <= 1e-12 && std::fabs(pb.direction().x()) <= 1e-12) {
                            const auto r = first.edge(a.edge).range;
                            return a.pcurve->point(.5 * r.lo + .5 * r.hi);
                        }
                    }
            }
        throw std::domain_error("Insieme di superfici: giunzione U isoparametrica assente.");
    }
    Vec3 point;
    Vec2 uv;
    require(faceInteriorPoint(first, targets[0].face, point, &uv), "Insieme di superfici: faccia senza punto interno.");
    return uv;
}
SurfaceQuiltMap::SurfaceQuiltMap(const std::vector<SurfaceTargetFace> &targets, const Vec2 &anchor) {
    require(!targets.empty() && targets.size() <= 2 && isFinite(anchor),
        "Insieme di superfici: scegliere una faccia o due facce tangenti con giunzione isoparametrica.");
    for (const auto &t : targets)
        require(t.body && t.body->contains(t.face), "Insieme di superfici: faccia non disponibile.");
    auto data = std::make_shared<Data>();
    const auto &first = *targets[0].body;
    const auto &a = first.face(targets[0].face);
    const auto wa = window(first, targets[0].face);
    double originA = anchor.x(), originB = 0, scale = 1, offset = 0, sign = 1;
    data->sense = a.sense ? 1. : -1.;
    if (targets.size() == 2) {
        require(targets[0].body == targets[1].body, "Insieme di superfici: le facce devono condividere una giunzione topologica.");
        const auto &b = first.face(targets[1].face);
        const auto wb = window(first, targets[1].face);
        bool found = false;
        for (auto l : a.loops)
            for (auto f : first.loopFins(l))
                for (auto m : b.loops)
                    for (auto g : first.loopFins(m)) {
                        const auto &fa = first.fin(f), &fb = first.fin(g);
                        if (fa.edge != fb.edge || !fa.pcurve || !fb.pcurve || fa.pcurve->type() != CurveType::Line
                            || fb.pcurve->type() != CurveType::Line)
                            continue;
                        const auto &pa = static_cast<const Line<2> &>(*fa.pcurve),
                                   &pb = static_cast<const Line<2> &>(*fb.pcurve);
                        if (std::fabs(pa.direction().x()) > 1e-12 || std::fabs(pb.direction().x()) > 1e-12)
                            continue;
                        require(!found, "Insieme di superfici: piu' giunzioni comuni, scegliere un insieme di superfici aperto.");
                        found = true;
                        originA = pa.origin().x();
                        originB = pb.origin().x();
                        scale = pb.direction().y() / pa.direction().y();
                        offset = pb.origin().y() - scale * pa.origin().y();
                        const auto range = first.edge(fa.edge).range;
                        for (int i = 0; i <= 16; ++i) {
                            const double t = range.lo + (range.hi - range.lo) * i / 16.;
                            const auto ua = pa.point(t), ub = pb.point(t);
                            require(distance(a.surface->point(ua.x(), ua.y()), b.surface->point(ub.x(), ub.y()))
                                    <= kLinearResolution,
                                "Insieme di superfici: giunzione non coincidente.");
                            require(distance(data->sense * a.surface->normal(ua.x(), ua.y()),
                                        (b.sense ? 1. : -1.) * b.surface->normal(ub.x(), ub.y()))
                                    <= 1e-4,
                                "Insieme di superfici: giunzione non tangente.");
                        }
                        Vec3 da[4], db[4];
                        a.surface->evaluate(originA, anchor.y(), 1, da);
                        b.surface->evaluate(originB, scale * anchor.y() + offset, 1, db);
                        sign = dot(da[2], db[2]) >= 0 ? 1. : -1.;
                        data->primarySide = .5 * (wa.first.lo + wa.first.hi) > originA ? 1. : -1.;
                        const double sideB = .5 * (wb.first.lo + wb.first.hi) > originB ? 1. : -1.;
                        require(data->primarySide * sideB * sign < 0, "Insieme di superfici: facce sovrapposte nello sviluppo.");
                    }
        require(found, "Insieme di superfici: giunzione U isoparametrica non disponibile.");
        require(std::fabs(anchor.x() - originA) <= 1e-10,
            "Insieme di superfici: il punto U deve essere sulla giunzione comune; usare il pulsante di centraggio.");
    }
    require(
        wa.first.contains(originA, 1e-12) && wa.second.contains(anchor.y(), 1e-12), "Insieme di superfici: ancoraggio fuori dominio.");
    data->meridian = std::make_shared<Meridian>(a.surface->uIso(originA), wa.second, anchor.y());
    data->charts.emplace_back(targets[0], wa.first, wa.second, originA);
    if (targets.size() == 2) {
        const auto w = window(first, targets[1].face);
        data->charts.emplace_back(targets[1], w.first, w.second, originB);
        auto &c = data->charts.back();
        c.sign = sign;
        c.vScale = scale;
        c.vOffset = offset;
    }
    data_ = std::move(data);
}
Vec2 SurfaceQuiltMap::flatten(std::size_t patch, const Vec2 &uv) const {
    require(patch < data_->charts.size() && isFinite(uv), "Insieme di superfici: coordinate non valide.");
    const auto &c = data_->charts[patch];
    require(c.classifier.classify(uv) != PointLocation::Outside, "Insieme di superfici: punto fuori dal contorno rifilato.");
    return data_->flatPoint(patch, uv);
}
Vec3 SurfaceQuiltMap::fold(const Vec3 &flat, bool carrierOnly) const {
    require(isFinite(flat), "Insieme di superfici: punto non finito.");
    const auto hit = data_->contact(Vec2(flat.x(), flat.y()), carrierOnly);
    return hit.point + flat.z() * hit.normal;
}
Body SurfaceQuiltMap::flattenSheet(const Frame3 &plane, double tolerance,
    SurfaceFlattenStats *stats, BodyHistory *history) const {
    require(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= .01,
        "Appiattimento: budget di fit non valido.");
    const double connection = std::min(tolerance, kLinearResolution), budget = .2 * connection;
    const auto &source = *data_->charts.front().target.body;
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::map<int, int> vertexIndices, edgeIndices;
    std::vector<int> vertexOrigins, edgeOrigins, faceOrigins;
    SurfaceFlattenStats measured;
    measured.minimumStretch = measured.minimumAreaRatio = INFINITY;
    measured.maximumStretch = measured.maximumAreaRatio = 0;
    for (std::size_t patch = 0; patch < data_->charts.size(); ++patch) {
        const auto &chart = data_->charts[patch];
        const auto &face = source.face(chart.target.face);
        Body::BuildFace flat;
        flat.surface = std::make_shared<Plane>(plane);
        flat.sense = true;
        for (auto loop : face.loops) {
            std::vector<Body::BuildFin> fins;
            for (auto use : source.loopFins(loop)) {
                cancellationPoint();
                const auto &fin = source.fin(use);
                const auto &edge = source.edge(fin.edge);
                require(bool(fin.pcurve), "Appiattimento: curva di contorno assente.");
                const auto mapped = [&](double t) {
                    // Questo punto appartiene al contorno per provenienza:
                    // e' la SP-curve del suo uso topologico, non un punto
                    // libero da riclassificare vicino al bordo tollerante.
                    const auto q = data_->flatPoint(patch, fin.pcurve->point(t));
                    return plane.toGlobal(Vec3(q.x(), q.y(), 0));
                };
                const auto vertex = [&](VertexId id, double t) {
                    const auto p = mapped(t);
                    const auto old = vertexIndices.find(id.index);
                    if (old != vertexIndices.end()) {
                        require(distance(points[old->second], p) <= connection,
                            "Appiattimento: vertici incompatibili; dividere prima le superfici periodiche.");
                        return old->second;
                    }
                    const int index = int(points.size());
                    vertexIndices.emplace(id.index, index);
                    vertexOrigins.push_back(id.index);
                    points.push_back(p);
                    return index;
                };
                const int start = vertex(source.edgeStart(fin.edge), edge.range.lo);
                const int end = vertex(source.edgeEnd(fin.edge), edge.range.hi);
                auto old = edgeIndices.find(fin.edge.index);
                if (old == edgeIndices.end()) {
                    double deviation = 0;
                    auto curve = fitCurve(mapped, edge.range, fin.pcurve->breakpoints(edge.range), budget, &deviation);
                    require(deviation <= budget, "Appiattimento: approssimazione del contorno fuori dal budget.");
                    measured.boundaryDeviation = std::max(measured.boundaryDeviation, deviation);
                    const int index = int(edges.size());
                    edges.push_back({start, end, curve, edge.range, connection});
                    edgeOrigins.push_back(fin.edge.index);
                    old = edgeIndices.emplace(fin.edge.index, index).first;
                } else {
                    // Il bordo comune e' unico per identita' topologica.
                    // Il secondo uso deve concordare con la stessa curva.
                    const auto &common = edges[old->second];
                    for (int k = 0; k <= 32; ++k) {
                        const double t = edge.range.lo + (edge.range.hi - edge.range.lo) * k / 32.;
                        require(distance(common.curve->point(t), mapped(t)) <= connection,
                            "Appiattimento: contorni incompatibili sulla giunzione comune.");
                    }
                }
                fins.push_back({old->second, fin.sense, nullptr, 0});
            }
            flat.loops.push_back(std::move(fins));
        }
        faces.push_back(std::move(flat));
        faceOrigins.push_back(chart.target.face.index);
        // Derivate della mappa, non della tessellazione di visualizzazione.
        const auto measure = [&](const Vec2 &uv) {
            Vec3 a[9], origin[9], h[9], h0[9];
            face.surface->evaluate(uv.x(), uv.y(), 2, a);
            face.surface->evaluate(chart.u0, uv.y(), 2, origin);
            chart.flat->evaluate(uv.x(), uv.y(), 2, h);
            chart.flat->evaluate(chart.u0, uv.y(), 2, h0);
            const double speed = norm(origin[3]), scale = speed / h0[3].x();
            const double scaleV = dot(origin[3], origin[4]) / (speed * h0[3].x())
                - speed * h0[4].x() / (h0[3].x() * h0[3].x());
            const double xu = chart.sign * scale * h[3].x();
            const double xv = chart.sign * (scaleV * (h[0].x() - h0[0].x()) + scale * (h[1].x() - h0[1].x()));
            Vec3 meridian[2];
            data_->meridian->curve->evaluate((uv.y() - chart.vOffset) / chart.vScale, 1, meridian);
            const double yv = data_->sense * norm(meridian[1]) / chart.vScale;
            const double E = dot(a[3], a[3]), F = dot(a[3], a[1]), G = dot(a[1], a[1]);
            const double determinant = dot(cross(a[3], a[1]), cross(a[3], a[1]));
            require(determinant > 0 && std::isfinite(determinant), "Appiattimento: metrica sorgente degenere.");
            const double trace = (G * xu * xu - 2 * F * xu * xv + E * (xv * xv + yv * yv)) / determinant;
            const double area = std::fabs(xu * yv) / std::sqrt(determinant);
            const double largest = .5 * (trace + std::sqrt(std::max(0., trace * trace - 4 * area * area)));
            const double maximum = std::sqrt(largest), minimum = area / maximum;
            require(minimum > 0 && std::isfinite(minimum) && std::isfinite(maximum),
                "Appiattimento: mappa piana singolare.");
            measured.minimumStretch = std::min(measured.minimumStretch, minimum);
            measured.maximumStretch = std::max(measured.maximumStretch, maximum);
            measured.minimumAreaRatio = std::min(measured.minimumAreaRatio, area);
            measured.maximumAreaRatio = std::max(measured.maximumAreaRatio, area);
            ++measured.samples;
        };
        const int before = measured.samples;
        for (int i = 0; i < 17; ++i)
            for (int j = 0; j < 17; ++j) {
                cancellationPoint();
                const Vec2 uv(chart.u.lo + (chart.u.hi - chart.u.lo) * (i + .5) / 17.,
                    chart.v.lo + (chart.v.hi - chart.v.lo) * (j + .5) / 17.);
                if (chart.classifier.classify(uv) == PointLocation::Inside)
                    measure(uv);
            }
        if (before == measured.samples) {
            Vec3 point;
            Vec2 uv;
            require(faceInteriorPoint(source, chart.target.face, point, &uv), "Appiattimento: faccia degenere.");
            measure(uv);
        }
    }
    auto result = Body::buildSheet(points, edges, faces);
    require(computePCurves(result) == 0, "Appiattimento: contorni piani non rappresentabili.");
    const auto issues = checkBody(result, {true, true});
    if (!issues.empty())
        throw std::domain_error("Appiattimento: " + issues.front().message);
    if (history) {
        *history = makeHistory({&source}, result);
        const auto declare = [&](EntityKind kind, const auto &ids, const std::vector<int> &origins) {
            require(ids.size() == origins.size(), "Appiattimento: genealogia incoerente.");
            for (std::size_t i = 0; i < ids.size(); ++i)
                history->declareOrigins(kind, ids[i].index, {{0, kind, origins[i], HistoryRelation::Modified}});
        };
        declare(EntityKind::Vertex, result.vertices(), vertexOrigins);
        declare(EntityKind::Edge, result.edges(), edgeOrigins);
        declare(EntityKind::Face, result.faces(), faceOrigins);
    }
    if (stats)
        *stats = measured;
    return result;
}
Body SurfaceQuiltMap::bend(
    const Body &source, const SurfaceWrapOptions &options, BodyHistory *history, double *deviation) const {
    require(std::isfinite(options.tolerance) && options.tolerance >= 1e-9 && options.tolerance <= .01,
        "Insieme di superfici: budget di fit non valido.");
    require(!options.rigid && std::isfinite(options.angle) && isFinite(options.shift)
            && std::isfinite(options.normalOffset),
        "Insieme di superfici: parametri di piegatura non validi.");
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const auto flat = [&](const Vec3 &p) {
        const auto q = options.sourcePlane.toLocal(p);
        double x = c * q.x() - s * q.y() + options.shift.x();
        // Solo arrotondamento della trasformazione affine sulla giunzione:
        // nessuno snap geometrico o ampliamento del trim.
        const double roundoff = 64 * kMachineEpsilon
            * std::max(1., norm(p) + norm(options.sourcePlane.origin()) + std::fabs(options.shift.x()));
        if (data_->charts.size() > 1 && std::fabs(x) <= roundoff)
            x = 0;
        return Vec3(x, s * q.x() + c * q.y() + options.shift.y(), q.z() + options.normalOffset);
    };
    // Cache locale alla singola piegatura: nessuno stato condiviso mutabile,
    // nessun riuso dopo modifiche di riferimenti o parametri.
    std::map<double, double> parameters;
    std::map<std::tuple<double, double, bool>, Data::Contact> contacts;
    const auto mapped = [&](const Vec3 &p, bool carrier) {
        cancellationPoint();
        const auto q = flat(p);
        const auto key = std::make_tuple(q.x(), q.y(), carrier);
        auto it = contacts.find(key);
        Data::Contact hit;
        if (it != contacts.end())
            hit = it->second;
        else {
            hit = data_->contact(Vec2(q.x(), q.y()), carrier, &parameters);
            if (contacts.size() >= 200000)
                contacts.clear();
            contacts.emplace(key, hit);
        }
        return hit.point + q.z() * hit.normal;
    };
    std::vector<Plane> cuts;
    if (data_->charts.size() > 1) {
        const auto n = c * options.sourcePlane.xDir() - s * options.sourcePlane.yDir();
        cuts.emplace_back(Frame3(options.sourcePlane.origin() - options.shift.x() * n, n, options.sourcePlane.zDir()));
    }
    // La coordinata longitudinale e' indipendente da X: i nodi dei
    // carrier e della mappa appiattita hanno controimmagini piane esatte.
    // Il fit deve conservarle; raffinare attraverso tali discontinuita'
    // di derivata produce griglie enormi senza raggiungere il budget.
    std::vector<double> longitudinal=data_->meridian->cuts;
    for(const auto &chart:data_->charts)for(double v:chart.flat->vBreakpoints(chart.v))
        longitudinal.push_back((v-chart.vOffset)/chart.vScale);
    std::sort(longitudinal.begin(),longitudinal.end());
    longitudinal.erase(std::unique(longitudinal.begin(),longitudinal.end()),longitudinal.end());
    const auto along=s*options.sourcePlane.xDir()+c*options.sourcePlane.yDir();
    for(double v:longitudinal) {
        if(v<data_->meridian->cuts.front()||v>data_->meridian->cuts.back())continue;
        const double y=data_->sense*(data_->meridian->lengthAt(v)-data_->meridian->zero);
        cuts.emplace_back(Frame3(options.sourcePlane.origin()+(y-options.shift.y())*along,along,options.sourcePlane.zDir()));
    }
    const auto faceImage=[&](FaceId id) {
        detail::MappingFaceImage image;
        const auto &face=source.face(id);
        if(face.surface->type()!=SurfaceType::Plane)return image;
        const auto &plane=static_cast<const Plane &>(*face.surface).frame();
        if(norm(cross(plane.zDir(),options.sourcePlane.zDir()))>kAngularResolution)return image;
        const double roundoff=64*kMachineEpsilon*std::max(1.,norm(plane.origin())+norm(options.sourcePlane.origin()));
        const double height=flat(plane.origin()).z();
        Box bounds;
        const auto coordinates=Transform3::fromFrame(Frame3(options.sourcePlane.origin(),options.sourcePlane.zDir(),
            c*options.sourcePlane.xDir()-s*options.sourcePlane.yDir())).inverted();
        for(auto loop:face.loops)for(auto use:source.loopFins(loop)) {
            const auto &edge=source.edge(source.fin(use).edge);bounds.add(curveBox(TransformedCurve(edge.curve,coordinates),edge.range));
        }
        if(bounds.isEmpty())return image;
        bounds.lo[0]+=options.shift.x();bounds.hi[0]+=options.shift.x();
        if(data_->charts.size()>1&&bounds.lo.x()<-roundoff&&bounds.hi.x()>roundoff)return image;
        const std::size_t patch=data_->charts.size()>1&&(.5*bounds.lo.x()+.5*bounds.hi.x())*data_->primarySide<0?1:0;
        const auto &target=data_->charts[patch].target;const auto &carrier=target.body->face(target.face);
        image.surface=carrier.surface;
        image.sense=dot((face.sense?1.:-1.)*plane.zDir(),options.sourcePlane.zDir())>0?carrier.sense:!carrier.sense;
        image.parameters=[&,patch](const Vec3 &p) {
            const auto q=flat(p);const auto hit=data_->contact(Vec2(q.x(),q.y()),false,&parameters);
            require(hit.patch==patch||q.x()==0,"Insieme di superfici: faccia attraversa la giunzione.");
            if(hit.patch==patch)return hit.uv;
            const auto &from=data_->charts[hit.patch];const auto &to=data_->charts[patch];
            return Vec2(to.u0,to.vScale*(hit.uv.y()-from.vOffset)/from.vScale+to.vOffset);
        };
        if(std::fabs(height)>roundoff) {
            Box uvBounds;
            const double budget=.1*std::min(options.tolerance,kLinearResolution);
            for(auto loop:face.loops)for(auto use:source.loopFins(loop)) {
                const auto &edge=source.edge(source.fin(use).edge);
                const auto uvCurve=fitCurve([&](double t){const auto uv=image.parameters(edge.curve->point(t));return Vec3(uv.x(),uv.y(),0);},
                    edge.range,edge.curve->breakpoints(edge.range),1e-12);
                uvBounds.add(curveBox(*uvCurve,edge.range));
            }
            image.surface=offsetSurface(*carrier.surface,(carrier.sense?1.:-1.)*height,
                {uvBounds.lo.x(),uvBounds.hi.x()},{uvBounds.lo.y(),uvBounds.hi.y()},budget);
        }
        return image;
    };
    return detail::mapBodyByFunction(
        source, [&](const Vec3 &p) { return mapped(p, false); }, [&](const Vec3 &p) { return mapped(p, true); },
        options.tolerance, history, deviation, cuts, .01, 5, faceImage);
}
}
