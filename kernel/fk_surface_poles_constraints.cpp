#include "fk_surface_poles_constraints.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include <functional>
#include "fk_pcurve.h"
#include "fk_surface_refit.h"
#include "fk_surface_poles_jets.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
namespace ForgeCad::Kernel {
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::domain_error(text);
}
struct Pole {
    int face, u, v;
    bool operator<(const Pole &p) const { return std::tie(face, u, v) < std::tie(p.face, p.u, p.v); }
};
struct Side {
    int axis = -1;
    bool high = false, reversed = false;
    int count(const BSplineSurface &s) const { return axis == 0 ? s.vPoleCount() : s.uPoleCount(); }
    int degree(const BSplineSurface &s) const { return axis == 0 ? s.vDegree() : s.uDegree(); }
    const std::vector<double> &knots(const BSplineSurface &s) const { return axis == 0 ? s.vKnots() : s.uKnots(); }
    Pole pole(int f, const BSplineSurface &s, int i, int layer = 0) const {
        int transverse = high ? (axis == 0 ? s.uPoleCount() : s.vPoleCount()) - 1 - layer : layer;
        return axis == 0 ? Pole{f, transverse, i} : Pole{f, i, transverse};
    }
    double factor(const BSplineSurface &s) const {
        const auto &k = axis == 0 ? s.uKnots() : s.vKnots();
        int n = axis == 0 ? s.uPoleCount() : s.vPoleCount(), p = axis == 0 ? s.uDegree() : s.vDegree();
        return p / (high ? k[n] - k[n - 1] : k[p + 1] - k[p]);
    }
};
Side side(const BSplineSurface &s, const Curve<2> &uv, Interval range) {
    auto a = uv.point(range.lo), b = uv.point(range.hi), m = uv.point((range.lo + range.hi) * .5);
    const auto u = s.uDomain(), v = s.vDomain();
    a = {(a.x() - u.lo) / u.length(), (a.y() - v.lo) / v.length()};
    b = {(b.x() - u.lo) / u.length(), (b.y() - v.lo) / v.length()};
    m = {(m.x() - u.lo) / u.length(), (m.y() - v.lo) / v.length()};
    for (int axis = 0; axis < 2; ++axis)
        for (int h = 0; h < 2; ++h)
            if (std::abs(a[axis] - h) < 1e-8 && std::abs(b[axis] - h) < 1e-8 && std::abs(m[axis] - h) < 1e-8 &&
                std::abs(std::min(a[1 - axis], b[1 - axis])) < 1e-8 &&
                std::abs(std::max(a[1 - axis], b[1 - axis]) - 1) < 1e-8) {
                const auto &k = axis == 0 ? s.uKnots() : s.vKnots();
                int p = axis == 0 ? s.uDegree() : s.vDegree(), n = axis == 0 ? s.uPoleCount() : s.vPoleCount();
                if (k[p] != k.front() || k[n] != k.back())
                    return {};
                return {axis, bool(h), a[1 - axis] > b[1 - axis]};
            }
    return {};
}
bool compatible(const BSplineSurface &a, Side sa, const BSplineSurface &b, Side sb) {
    if (sa.axis < 0 || sb.axis < 0 || sa.count(a) != sb.count(b) || sa.degree(a) != sb.degree(b))
        return false;
    const auto &ka = sa.knots(a), &kb = sb.knots(b);
    const int n = ka.size();
    for (int i = 0; i < n; ++i) {
        double x = (ka[i] - ka.front()) / (ka.back() - ka.front());
        double y = (kb[sa.reversed == sb.reversed ? i : n - 1 - i] - kb.front()) / (kb.back() - kb.front());
        if (sa.reversed != sb.reversed)
            y = 1 - y;
        if (std::abs(x - y) > 1e-10)
            return false;
    }
    double ratio = 0;
    for (int i = 0; i < sa.count(a); ++i) {
        auto p = sa.pole(0, a, i), q = sb.pole(0, b, sa.reversed == sb.reversed ? i : sb.count(b) - 1 - i);
        double r = a.weight(p.u, p.v) / b.weight(q.u, q.v);
        if (i == 0)
            ratio = r;
        if (std::abs(r / ratio - 1) > 1e-10)
            return false;
    }
    return true;
}
double rowRatio(const BSplineSurface &s, Side side) {
    double ratio = 0;
    for (int i = 0; i < side.count(s); ++i) {
        auto p = side.pole(0, s, i), q = side.pole(0, s, i, 1);
        double r = s.weight(q.u, q.v) / s.weight(p.u, p.v);
        if (i == 0)
            ratio = r;
        if (std::abs(r / ratio - 1) > 1e-10)
            return 0;
    }
    return ratio;
}
struct JointMeasure {
    double gap = 0, angle = 0, ratio = 0;
    bool regular = true, strong = true;
};
JointMeasure measure(const Surface &a, const Curve<2> &ca, const Surface &b, const Curve<2> &cb, Interval range,
                     Side sa = {}, Side sb = {}) {
    JointMeasure m;
    double ratio = 0;
    for (int i = 0; i <= 32; ++i) {
        double t = range.lo + range.length() * i / 32.;
        auto ua = ca.point(t), ub = cb.point(t);
        Vec3 da[4], db[4];
        a.evaluate(ua.x(), ua.y(), 1, da);
        b.evaluate(ub.x(), ub.y(), 1, db);
        m.gap = std::max(m.gap, distance(da[0], db[0]));
        auto na = cross(da[1], da[2]), nb = cross(db[1], db[2]);
        if (norm(na) < 1e-12 || norm(nb) < 1e-12) {
            m.regular = false;
            continue;
        }
        const double sine = std::min(1., norm(cross(na, nb)) / (norm(na) * norm(nb)));
        m.angle = std::max(m.angle, std::asin(sine));
        if (sa.axis >= 0 && sb.axis >= 0) {
            auto x = da[sa.axis == 0 ? 2 : 1] * (sa.high ? -1. : 1.),
                 y = db[sb.axis == 0 ? 2 : 1] * (sb.high ? -1. : 1.);
            const double q = norm(x) / norm(y);
            if (i == 0)
                ratio = q;
            if (!std::isfinite(q) || dot(x, y) >= 0 || norm(x + ratio * y) > 1e-6 * std::max(norm(x), norm(y)))
                m.strong = false;
        } else
            m.strong = false;
    }
    m.ratio = ratio;
    return m;
}
} // namespace
struct SurfacePoleConstraints::Data {
    struct Row {
        std::vector<std::pair<int, double>> terms;
        Vec3 rhs;
    };
    std::map<int, BSplineSurfacePtr> surfaces;
    std::map<int, BSplineSurfacePtr> replacements;
    std::map<Pole, int> indices;
    std::vector<Pole> poles;
    std::vector<Row> equations;
    std::set<Pole> protectedPoles;
    SurfacePoleContinuityReport info;
    double residualTolerance = 1e-8, refitDeviation = 0;
    Vec3 point(Pole p) const { return surfaces.at(p.face)->pole(p.u, p.v); }
    int index(Pole p) {
        auto found = indices.find(p);
        if (found != indices.end())
            return found->second;
        require(poles.size() < 1000000, "Continuita': rete troppo grande; semplificare la griglia.");
        int i = poles.size();
        indices.emplace(p, i);
        poles.push_back(p);
        return i;
    }
    void row(const std::vector<std::pair<Pole, double>> &terms, bool relative = false) {
        Row equation;
        // Senza refit i vincoli agiscono sugli spostamenti: il corpo neutro
        // conserva le connessioni tolleranti gia' dichiarate dal B-rep.
        // Non si tenta di guarirle spostando poli protetti prima del gesto.
        const bool refitted = std::any_of(terms.begin(), terms.end(), [&](const auto &term) {
            return replacements.count(term.first.face) != 0;
        });
        std::map<int, double> coefficients;
        for (const auto &term : terms) {
            if(term.second==0)continue;
            coefficients[index(term.first)] += term.second;
            if (!relative && refitted)
                equation.rhs -= point(term.first) * term.second;
        }
        double scale = 0;
        for (const auto &c : coefficients)
            scale += c.second * c.second;
        scale = std::sqrt(scale);
        double largest=0;for(const auto &term:terms)largest=std::max(largest,std::abs(term.second));
        if (scale <= 1e-12*largest)
            return;
        for (const auto &c : coefficients)
            if (std::abs(c.second) > 1e-15)
                equation.terms.push_back({c.first, c.second / scale});
        equation.rhs /= scale;
        equations.push_back(std::move(equation));
    }
    void protect(int face, Side side, int order, const CurvePtr<2> &trim, Interval range) {
        const auto &s = *surfaces.at(face);
        const auto freeze = [&](int u, int v) {
            const Pole p{face, u, v};
            if (protectedPoles.insert(p).second) row({{p, 1}}, true);
        };
        if (side.axis >= 0) {
            for (int i = 0; i < side.count(s); ++i)
                for (int layer = 0; layer < (order + 1); ++layer) {
                    const auto p = side.pole(face, s, i, layer);
                    freeze(p.u, p.v);
                }
            return;
        }
        // Una base B-spline ha supporto locale [k_i,k_{i+p+1}]. Fissare
        // solo le basi che toccano il trim conserva esattamente bordo e
        // derivate, lasciando liberi i poli interni anche sulle griglie grandi.
        const auto intervals = [](Interval box, Interval domain, bool periodic) {
            if (!periodic) return std::vector<Interval>{box};
            if (box.length() >= domain.length()) return std::vector<Interval>{domain};
            const double shift = std::floor((box.lo - domain.lo) / domain.length()) * domain.length();
            box.lo -= shift; box.hi -= shift;
            if (box.hi <= domain.hi) return std::vector<Interval>{box};
            return std::vector<Interval>{{box.lo, domain.hi}, {domain.lo, box.hi - domain.length()}};
        };
        const auto indices = [](const std::vector<double> &knots, int degree, int count, Interval box) {
            const double epsilon = 1e-12 * std::max({1., std::abs(box.lo), std::abs(box.hi)});
            const int first = std::max(0, int(std::lower_bound(knots.begin(), knots.end(), box.lo - epsilon) - knots.begin()) - degree - 1);
            const int end = std::min(count, int(std::upper_bound(knots.begin(), knots.end(), box.hi + epsilon) - knots.begin()));
            return std::pair<int, int>{first, end};
        };
        const auto curve = embedCurve(trim, Frame3());
        std::function<void(const BSplineCurve<3> &, int)> protectPiece;
        protectPiece = [&](const BSplineCurve<3> &piece, int depth) {
            cancellationPoint();
            Box box;
            for (const auto &p : piece.poles()) box.add(p);
            std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> blocks;
            std::size_t count = 0;
            for (auto u : intervals({box.lo.x(), box.hi.x()}, s.uDomain(), s.isUPeriodic()))
                for (auto v : intervals({box.lo.y(), box.hi.y()}, s.vDomain(), s.isVPeriodic())) {
                    const auto us = indices(s.uKnots(), s.uDegree(), s.uPoleCount(), u);
                    const auto vs = indices(s.vKnots(), s.vDegree(), s.vPoleCount(), v);
                    count += std::size_t(std::max(0, us.second - us.first)) * std::max(0, vs.second - vs.first);
                    blocks.push_back({us, vs});
                }
            if (count > 128 && depth < 12) {
                const auto d = piece.domain();
                const auto halves = piece.insertKnot((d.lo + d.hi) * .5, piece.degree()).bezierSegments();
                for (const auto &half : halves) protectPiece(half, depth + 1);
                return;
            }
            for (const auto &block : blocks)
                for (int i = block.first.first; i < block.first.second; ++i)
                    for (int j = block.second.first; j < block.second.second; ++j) freeze(i, j);
        };
        for (const auto &piece : rationalBezierPieces(*curve, range)) protectPiece(piece, 0);
    }
};
namespace {
std::vector<FaceId> connectedPoleFaces(const Body &body, const std::vector<FaceId> &seeds,bool analytic) {
    std::set<int> included;
    for (auto f : seeds)
        included.insert(f.index);
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto e : body.edges()) {
            const auto &edge = body.edge(e);
            if (!edge.forward.valid() || !edge.backward.valid())
                continue;
            const auto a = body.finFace(edge.forward), b = body.finFace(edge.backward);
            if (included.count(a.index) == included.count(b.index))
                continue;
            auto target = included.count(a.index) ? b : a;
            if (body.face(target).surface && (analytic || body.face(target).surface->type()==SurfaceType::BSpline)) {
                included.insert(target.index);
                changed = true;
            }
        }
    }
    require(included.size() <= 256, "Continuita': oltre 256 facce collegate.");
    std::vector<FaceId> result;
    for (auto f : body.faces())
        if (included.count(f.index))
            result.push_back(f);
    return result;
}
} // namespace
std::vector<FaceId> connectedSurfacePoleFaces(const Body &body,const std::vector<FaceId> &seeds) {
    return connectedPoleFaces(body,seeds,true);
}
SurfacePoleConstraints::SurfacePoleConstraints(const Body &body, const std::vector<SurfaceFacePoleChange> &bases,
                                               int mode, double tolerance) {
    require(mode >= 1 && mode <= 4, "Continuita': opzione non valida.");
    require(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= .01, "Continuita': tolleranza non valida.");
    auto data = std::make_shared<Data>();
    data->residualTolerance = std::max(1e-12, tolerance * .01);
    std::vector<FaceId> selected;
    for (const auto &base : bases) {
        require(body.contains(base.face), "Continuita': faccia assente.");
        selected.push_back(base.face);
        if (base.replacement)
            data->replacements[base.face.index] = base.replacement;
    }
    auto connected = connectedPoleFaces(body, selected,false);
    Body uv = body;
    require(computePCurves(uv) == 0, "Continuita': rifilature UV non disponibili.");
    for (auto face : connected) {
        auto original = std::dynamic_pointer_cast<const BSplineSurface>(body.face(face).surface);
        require(bool(original), "Continuita': supporto non NURBS.");
        data->surfaces[face.index] =
            data->replacements.count(face.index) ? data->replacements.at(face.index) : original;
    }
    for (auto e : uv.edges()) {
        cancellationPoint();
        const auto &edge = uv.edge(e);
        if (!edge.forward.valid() || !edge.backward.valid())
            continue;
        auto a = uv.finFace(edge.forward), b = uv.finFace(edge.backward);
        if (!data->surfaces.count(a.index) && !data->surfaces.count(b.index))
            continue;
        const auto ca = uv.fin(edge.forward).pcurve, cb = uv.fin(edge.backward).pcurve;
        require(bool(ca) && bool(cb), "Continuita': giunto senza UV.");
        const auto oa = std::dynamic_pointer_cast<const BSplineSurface>(body.face(a).surface),
                   ob = std::dynamic_pointer_cast<const BSplineSurface>(body.face(b).surface);
        Side sa = oa ? side(*oa, *ca, edge.range) : Side{}, sb = ob ? side(*ob, *cb, edge.range) : Side{};
        const auto original = measure(*body.face(a).surface, *ca, *body.face(b).surface, *cb, edge.range, sa, sb);
        ++data->info.connected;
        data->info.gap = std::max(data->info.gap, original.gap);
        const bool smooth = original.regular && original.angle <= 1e-5;
        if (smooth) {
            ++data->info.tangent;
            data->info.angle = std::max(data->info.angle, original.angle);
        } else
            ++data->info.sharp;
        auto fa = data->surfaces.find(a.index), fb = data->surfaces.find(b.index);
        const auto protectRefit = [&](bool tangency) {
            if (!data->replacements.count(a.index) && !data->replacements.count(b.index))
                return;
            const Surface &as = fa == data->surfaces.end() ? *body.face(a).surface : *fa->second;
            const Surface &bs = fb == data->surfaces.end() ? *body.face(b).surface : *fb->second;
            const auto fit = measure(as, *ca, bs, *cb, edge.range);
            const double allowed = std::max({tolerance, edge.tolerance, uv.fin(edge.forward).pcurveTolerance,
                                             uv.fin(edge.backward).pcurveTolerance});
            require(fit.gap <= allowed && (!tangency || fit.angle <= 1e-5),
                    "Continuita': griglia approssimata incompatibile sul giunto protetto; disattivare Semplifica la "
                    "griglia.");
        };
        if (fa == data->surfaces.end() || fb == data->surfaces.end() || !compatible(*fa->second, sa, *fb->second, sb)) {
            ++data->info.protectedJoints;
            protectRefit(mode >= 2 && smooth);
            if (fa != data->surfaces.end())
                data->protect(a.index, sa, smooth ? (mode == 4 ? 2 : int(mode >= 2)) : 0, ca, edge.range);
            if (fb != data->surfaces.end())
                data->protect(b.index, sb, smooth ? (mode == 4 ? 2 : int(mode >= 2)) : 0, cb, edge.range);
            continue;
        }
        const auto &as = *fa->second, &bs = *fb->second;
        const double ka = sa.factor(as) * rowRatio(as, sa), kb = sb.factor(bs) * rowRatio(bs, sb) * original.ratio;
        const bool tangent = mode >= 2 && smooth;
        if (tangent && (!original.strong || ka <= 0 || kb <= 0)) {
            ++data->info.protectedJoints;
            protectRefit(true);
            data->protect(a.index, sa, mode==4?2:1, ca, edge.range);
            data->protect(b.index, sb, mode==4?2:1, cb, edge.range);
            continue;
        }
        if(mode==4 && tangent) {
            const auto parameter=[](const BSplineSurface &s,Side side){const auto d=side.axis==0?s.uDomain():s.vDomain();return side.high?d.hi:d.lo;};
            const auto link=surfacePoleJetLink(as,{sa.axis,parameter(as,sa),sa.high,sa.reversed},bs,{sb.axis,parameter(bs,sb),sb.high,sb.reversed},2);
            if(!link.compatible || !link.tangent || !link.curvature) {
                ++data->info.protectedJoints;
                protectRefit(true);
                data->protect(a.index,sa,2,ca,edge.range);data->protect(b.index,sb,2,cb,edge.range);
            } else {
                ++data->info.curvature;
                for(const auto &jet:link.rows) {
                    std::vector<std::pair<Pole,double>> terms;
                    for(const auto &term:jet.terms)terms.push_back({{term.side==0?a.index:b.index,term.u,term.v},term.coefficient});
                    data->row(terms);
                }
            }
        }
        for (int i = 0; i < sa.count(as); ++i) {
            const int j = sa.reversed == sb.reversed ? i : sb.count(bs) - 1 - i;
            auto pa = sa.pole(a.index, as, i), pb = sb.pole(b.index, bs, j);
            data->row({{pa, 1}, {pb, -1}});
            if (tangent)
                data->row({{sa.pole(a.index, as, i, 1), ka},
                           {sb.pole(b.index, bs, j, 1), kb},
                           {pa, -(ka + kb) * .5},
                           {pb, -(ka + kb) * .5}});
        }
    }
    require(data->poles.size() <= 1000000, "Continuita': rete troppo grande; semplificare la griglia.");
    // Le cuciture implicite delle NURBS chiuse e i lati degeneri (poli di
    // sfere/coni) appartengono allo stesso supporto, senza un edge di seam.
    for(const auto &entry:data->surfaces) {
        const int f=entry.first;const auto &s=*entry.second;
        if(mode>=3)for(const auto &link:surfacePoleInternalJets(s,mode==4?2:1)) {
            ++data->info.internal;
            if(mode==4&&link.curvature)++data->info.curvature;
            if(link.protectedBand) {
                require(!data->replacements.count(f),"Continuita' G1/G2: la griglia approssimata non conserva un passaggio interno o una cucitura; disattivare Semplifica la griglia.");
                ++data->info.protectedJoints;
                for(const auto &term:link.support) {
                    const Pole p{f,term.u,term.v};
                    if(data->protectedPoles.insert(p).second)data->row({{p,1}},true);
                }
            }

            // Spigoli preesistenti non vengono spianati implicitamente.
            for(const auto &jet:link.rows) {
                std::vector<std::pair<Pole,double>> terms;
                for(const auto &term:jet.terms)terms.push_back({{f,term.u,term.v},term.coefficient});
                data->row(terms);
            }
        }

        const auto &neutral=static_cast<const BSplineSurface &>(*body.face(FaceId(f)).surface);
        const auto neutralPoint=[&](Pole p){return neutral.pole(p.u,p.v);};
        for(int axis=0;axis<2;++axis) {
            Side low{axis,false,false}, high{axis,true,false};
            const int count=low.count(s);
            bool closed=compatible(s,low,s,high);
            for(int i=0;i<low.count(neutral);++i)closed=closed && distance(neutralPoint(low.pole(f,neutral,i)),neutralPoint(high.pole(f,neutral,i)))<=1e-10;
            if(closed) {
                ++data->info.connected;
                const double ka=low.factor(s)*rowRatio(s,low),kb=high.factor(s)*rowRatio(s,high);
                bool smooth=ka>0 && kb>0;
                for(int i=0;i<low.count(neutral);++i){const auto a=low.pole(f,neutral,i),b=high.pole(f,neutral,i);
                    const auto x=(neutralPoint(low.pole(f,neutral,i,1))-neutralPoint(a))*low.factor(neutral)*rowRatio(neutral,low);
                    const auto y=(neutralPoint(high.pole(f,neutral,i,1))-neutralPoint(b))*high.factor(neutral)*rowRatio(neutral,high);
                    smooth=smooth && norm(x+y)<=1e-8*std::max({1.,norm(x),norm(y)});
                }
                if(smooth)++data->info.tangent;else ++data->info.sharp;
                for(int i=0;i<count;++i){const auto a=low.pole(f,s,i),b=high.pole(f,s,i);
                    data->row({{a,1},{b,-1}});
                    if(mode>=2 && smooth)data->row({{low.pole(f,s,i,1),ka},{high.pole(f,s,i,1),kb},{a,-(ka+kb)*.5},{b,-(ka+kb)*.5}});
                }
            }
            for(auto side:{low,high}) {
                const auto first=side.pole(f,s,0);bool collapsed=true;
                for(int i=1;i<side.count(neutral);++i)collapsed=collapsed && distance(neutralPoint(side.pole(f,neutral,0)),neutralPoint(side.pole(f,neutral,i)))<=1e-10;
                if(collapsed)for(int i=1;i<count;++i)data->row({{first,1},{side.pole(f,s,i),-1}});
            }
        }
    }
    data_ = data;
    if (!data->replacements.empty())
        for (const auto &face : propagate({})) {
            const auto &s = *data->surfaces.at(face.face.index);
            std::vector<Vec3> points;
            std::vector<double> weights;
            for (int u = 0; u < s.uPoleCount(); ++u)
                for (int v = 0; v < s.vPoleCount(); ++v) {
                    points.push_back(s.pole(u, v));
                    weights.push_back(s.weight(u, v));
                }
            for (const auto &edit : face.changes) {
                points[edit.u * s.vPoleCount() + edit.v] += edit.delta;
                weights[edit.u * s.vPoleCount() + edit.v] *= edit.weightFactor;
            }
            BSplineSurface neutral(s.uDegree(), s.vDegree(), s.uKnots(), s.vKnots(), s.uPoleCount(), s.vPoleCount(),
                                   std::move(points), std::move(weights), s.isUPeriodic(), s.isVPeriodic());
            const auto original = std::dynamic_pointer_cast<const BSplineSurface>(body.face(face.face).surface);
            data->refitDeviation = std::max(data->refitDeviation, sampledSurfacePoleDeviation(*original, neutral));
        }
}
double SurfacePoleConstraints::refitDeviation() const { return data_->refitDeviation; }
SurfacePoleContinuityReport SurfacePoleConstraints::report() const { return data_->info; }
std::vector<SurfaceFacePoleChange>
SurfacePoleConstraints::propagate(const std::vector<SurfaceFacePoleChange> &drivers,
                                  SurfacePoleContinuityReport *report,
                                  const std::vector<std::array<int, 3>> *active) const {
    const auto &d = *data_;
    std::map<Pole, SurfacePoleChange> edits;
    std::map<int, Vec3> fixed, preferred;
    std::set<Pole> selected;
    if (active)
        for (const auto &p : *active)
            selected.insert({p[0], p[1], p[2]});
    for (const auto &face : drivers)
        for (const auto &edit : face.changes) {
            auto found = d.surfaces.find(face.face.index);
            require(found != d.surfaces.end(), "Continuita': faccia fuori rete.");
            const auto &s = *found->second;
            require(edit.u >= 0 && edit.v >= 0 && edit.u < s.uPoleCount() && edit.v < s.vPoleCount() &&
                        isFinite(edit.delta) && std::isfinite(edit.weightFactor) && edit.weightFactor > 0,
                    "Continuita': polo non valido.");
            const Pole pole{face.face.index, edit.u, edit.v};
            require(edits.emplace(pole, edit).second, "Continuita': polo duplicato.");
            auto index = d.indices.find(pole);
            if (index != d.indices.end()) {
                require(std::abs(edit.weightFactor - 1) < 1e-12,
                        "Continuita': conservare i pesi dei poli sul giunto e sulla fascia tangente.");
                if (!active || selected.count(pole))
                    fixed[index->second] = edit.delta;
                else
                    preferred[index->second] = edit.delta;
            }
        }
    const int n = d.poles.size();
    std::vector<Data::Row> rows;
    for (auto row : d.equations) {
        std::vector<std::pair<int, double>> free;
        for (const auto &term : row.terms) {
            auto f = fixed.find(term.first);
            if (f != fixed.end())
                row.rhs -= f->second * term.second;
            else {
                free.push_back(term);
                const auto preference = preferred.find(term.first);
                if (preference != preferred.end())
                    row.rhs -= preference->second * term.second;
            }
        }
        row.terms = std::move(free);
        if (row.terms.empty()) {
            require(norm(row.rhs) < d.residualTolerance,
                    "Continuita': poli selezionati incompatibili o giunto protetto.");
            continue;
        }
        double scale = 0;
        for (auto c : row.terms)
            scale += c.second * c.second;
        scale = std::sqrt(scale);
        for (auto &c : row.terms)
            c.second /= scale;
        row.rhs /= scale;
        rows.push_back(std::move(row));
    }
    const int m = rows.size();
    std::vector<Vec3> r(m), p(m), y(m), ap(m), work(n);
    const auto dotAll = [](const auto &a, const auto &b) {
        double sum = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            sum += dot(a[i], b[i]);
        return sum;
    };
    for (int i = 0; i < m; ++i)
        r[i] = p[i] = rows[i].rhs;
    const auto multiply = [&](const std::vector<Vec3> &x, std::vector<Vec3> &out) {
        std::fill(work.begin(), work.end(), Vec3());
        for (int i = 0; i < m; ++i)
            for (auto c : rows[i].terms)
                work[c.first] += x[i] * c.second;
        for (int i = 0; i < m; ++i) {
            out[i] = {};
            for (auto c : rows[i].terms)
                out[i] += work[c.first] * c.second;
        }
    };
    double rr = dotAll(r, r);
    const double threshold = std::max(1e-24, rr * 1e-24);
    for (int step = 0; step < 4096 && rr > threshold; ++step) {
        cancellationPoint();
        multiply(p, ap);
        double denominator = dotAll(p, ap);
        if (!(denominator > 1e-30))
            break;
        double alpha = rr / denominator;
        for (int i = 0; i < m; ++i) {
            y[i] += p[i] * alpha;
            r[i] -= ap[i] * alpha;
        }
        double next = dotAll(r, r);
        double beta = next / rr;
        for (int i = 0; i < m; ++i)
            p[i] = r[i] + p[i] * beta;
        rr = next;
    }
    std::vector<Vec3> solution(n);
    for (int i = 0; i < m; ++i)
        for (auto c : rows[i].terms)
            solution[c.first] += y[i] * c.second;
    for (const auto &row : rows) {
        Vec3 residual = -row.rhs;
        for (auto c : row.terms)
            residual += solution[c.first] * c.second;
        require(norm(residual) < d.residualTolerance,
                "Continuita': vincoli incompatibili; ridurre la selezione o lo spostamento.");
    }
    for (const auto &preference : preferred)
        solution[preference.first] += preference.second;
    int followers = 0;
    for (int i = 0; i < n; ++i)
        if (!fixed.count(i)) {
            const auto pole = d.poles[i];
            auto prior = edits.find(pole);
            if (prior != edits.end()) {
                if (norm(prior->second.delta - solution[i]) > 1e-11)
                    ++followers;
                prior->second.delta = solution[i];
            } else if (norm(solution[i]) > 1e-11) {
                edits[pole] = {pole.u, pole.v, solution[i], 1};
                ++followers;
            }
        }
    require(edits.size() <= 4096, "Continuita': oltre 4096 poli mossi; ridurre la selezione.");
    std::map<int, SurfaceFacePoleChange> output;
    for (const auto &fit : d.replacements)
        output[fit.first] = {FaceId(fit.first), {}, fit.second};
    for (const auto &edit : edits) {
        auto &face = output[edit.first.face];
        face.face = FaceId(edit.first.face);
        face.changes.push_back(edit.second);
    }
    std::vector<SurfaceFacePoleChange> result;
    for (auto &face : output)
        result.push_back(std::move(face.second));
    if (report) {
        *report = d.info;
        report->followers = followers;
    }
    return result;
}
SurfacePoleContinuityReport checkSurfacePoleContinuity(const Body &reference, const Body &result,
                                                       const std::vector<FaceId> &faces, double tolerance) {
    SurfacePoleContinuityReport report;
    std::set<int> selected;
    for (auto f : faces)
        selected.insert(f.index);
    Body uv = reference;
    require(computePCurves(uv) == 0, "Continuita': controllo senza UV.");
    for (auto e : reference.edges()) {
        const auto &edge = reference.edge(e);
        if (!edge.forward.valid() || !edge.backward.valid())
            continue;
        auto a = reference.finFace(edge.forward), b = reference.finFace(edge.backward);
        if (!selected.count(a.index) && !selected.count(b.index))
            continue;
        const auto ca = uv.fin(edge.forward).pcurve, cb = uv.fin(edge.backward).pcurve;
        const auto before = measure(*reference.face(a).surface, *ca, *reference.face(b).surface, *cb, edge.range);
        const auto after = measure(*result.face(a).surface, *ca, *result.face(b).surface, *cb, edge.range);
        const auto oa=std::dynamic_pointer_cast<const BSplineSurface>(reference.face(a).surface),ob=std::dynamic_pointer_cast<const BSplineSurface>(reference.face(b).surface);
        const auto ra=std::dynamic_pointer_cast<const BSplineSurface>(result.face(a).surface),rb=std::dynamic_pointer_cast<const BSplineSurface>(result.face(b).surface);
        if(oa&&ob&&ra&&rb&&oa->uKnots()==ra->uKnots()&&oa->vKnots()==ra->vKnots()&&ob->uKnots()==rb->uKnots()&&ob->vKnots()==rb->vKnots()) {
            const auto sa=side(*oa,*ca,edge.range),sb=side(*ob,*cb,edge.range);
            if(compatible(*oa,sa,*ob,sb)) {
                const auto parameter=[](const BSplineSurface &s,Side side){const auto d=side.axis==0?s.uDomain():s.vDomain();return side.high?d.hi:d.lo;};
                const auto link=surfacePoleJetLink(*oa,{sa.axis,parameter(*oa,sa),sa.high,sa.reversed},*ob,{sb.axis,parameter(*ob,sb),sb.high,sb.reversed},2);
                if(link.curvature){++report.curvature;report.curvatureError=std::max(report.curvatureError,surfacePoleJetResidual(link,*ra,*rb,2));}
            }
        }
        ++report.connected;
        report.gap = std::max(report.gap, after.gap);
        (void)tolerance;
        if (before.regular && before.angle <= 1e-5) {
            ++report.tangent;
            report.angle = std::max(report.angle, after.angle);
        } else
            ++report.sharp;
    }
    for(auto face:faces) {
        const auto a=std::dynamic_pointer_cast<const BSplineSurface>(reference.face(face).surface);
        const auto b=std::dynamic_pointer_cast<const BSplineSurface>(result.face(face).surface);
        if(!a || !b || a->uKnots()!=b->uKnots() || a->vKnots()!=b->vKnots())continue;
        for(const auto &link:surfacePoleInternalJets(*a,2)) {
            ++report.internal;
            report.internalTangentError=std::max(report.internalTangentError,surfacePoleJetResidual(link,*b,*b,1));
            if(link.curvature) {
                ++report.curvature;
                report.curvatureError=std::max(report.curvatureError,surfacePoleJetResidual(link,*b,*b,2));
            }
        }
    }
    return report;
}
} // namespace ForgeCad::Kernel
