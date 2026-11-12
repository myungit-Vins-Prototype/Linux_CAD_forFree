#include "fk_nurbs_join.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "fk_blend_section_curve.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel::detail {
namespace {

// Solo arrotondamento dei coefficienti double; non e' una tolleranza di
// cucitura. Le coordinate omogenee sono centrate e normalizzate prima dei
// confronti dei jet, per non dipendere dalla traslazione del modello.
constexpr double coefficientRoundoff = 128.0 * kMachineEpsilon;

BSplineSurface orient(const BSplineSurface &s, bool swap, bool reverseU, bool reverseV) {
    auto u = swap ? s.vKnots() : s.uKnots(), v = swap ? s.uKnots() : s.vKnots();
    const Interval ud = swap ? s.vDomain() : s.uDomain(), vd = swap ? s.uDomain() : s.vDomain();
    if (reverseU) {
        std::reverse(u.begin(), u.end());
        for (double &t : u) t = ud.lo + ud.hi - t;
    }
    if (reverseV) {
        std::reverse(v.begin(), v.end());
        for (double &t : v) t = vd.lo + vd.hi - t;
    }
    const int nu = swap ? s.vPoleCount() : s.uPoleCount(), nv = swap ? s.uPoleCount() : s.vPoleCount();
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
        const int a = reverseU ? nu - 1 - i : i, b = reverseV ? nv - 1 - j : j;
        poles.push_back(swap ? s.pole(b, a) : s.pole(a, b));
        weights.push_back(swap ? s.weight(b, a) : s.weight(a, b));
    }
    return BSplineSurface(swap ? s.vDegree() : s.uDegree(), swap ? s.uDegree() : s.vDegree(),
                          u, v, nu, nv, std::move(poles), std::move(weights));
}

// Elevazione omogenea: preserva anche il parametro. standardBezierPieces
// normalizza invece i pesi con una riparametrizzazione di Moebius.
BSplineSurface elevateU(const BSplineSurface &s, int degree) {
    if (s.uDegree() == degree) return s;
    std::vector<std::vector<Vec3>> columns;
    std::vector<std::vector<double>> columnWeights;
    std::vector<double> knots;
    for (int j = 0; j < s.vPoleCount(); ++j) {
        std::vector<Vec3> p;
        std::vector<double> w;
        for (int i = 0; i < s.uPoleCount(); ++i) { p.push_back(s.pole(i,j)); w.push_back(s.weight(i,j)); }
        const BSplineCurve<3> curve(s.uDegree(), s.uKnots(), p, w);
        p.clear(); w.clear();
        std::vector<double> ku;
        const auto segments = curve.bezierSegments();
        for (std::size_t k = 0; k < segments.size(); ++k) {
            const auto &segment = segments[k];
            std::vector<Vec3> hp;
            std::vector<double> hw;
            for (int i = 0; i < segment.poleCount(); ++i) {
                hp.push_back(segment.weight(i)*segment.poles()[std::size_t(i)]); hw.push_back(segment.weight(i));
            }
            for (int d = segment.degree(); d < degree; ++d) {
                std::vector<Vec3> np(std::size_t(d+2));
                std::vector<double> nw(std::size_t(d+2));
                for (int i = 0; i <= d+1; ++i) {
                    const double f = double(i)/double(d+1);
                    if (i) { np[std::size_t(i)] += f*hp[std::size_t(i-1)]; nw[std::size_t(i)] += f*hw[std::size_t(i-1)]; }
                    if (i <= d) { np[std::size_t(i)] += (1-f)*hp[std::size_t(i)]; nw[std::size_t(i)] += (1-f)*hw[std::size_t(i)]; }
                }
                hp = std::move(np); hw = std::move(nw);
            }
            for (int i = k ? 1 : 0; i <= degree; ++i) { p.push_back(hp[std::size_t(i)]/hw[std::size_t(i)]); w.push_back(hw[std::size_t(i)]); }
            if (!k) ku.insert(ku.end(), std::size_t(degree+1), segment.domain().lo);
            ku.insert(ku.end(), std::size_t(k+1 == segments.size() ? degree+1 : degree), segment.domain().hi);
        }
        columns.push_back(std::move(p)); columnWeights.push_back(std::move(w)); knots = std::move(ku);
    }
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (std::size_t i = 0; i < columns.front().size(); ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
        poles.push_back(columns[std::size_t(j)][i]); weights.push_back(columnWeights[std::size_t(j)][i]);
    }
    return BSplineSurface(degree, s.vDegree(), knots, s.vKnots(), int(columns.front().size()), s.vPoleCount(), poles, weights);
}

using HPoint = std::array<double,4>;
HPoint homogeneous(const BSplineSurface &s, int i, int j, const Vec3 &origin, double scale, double factor=1.0) {
    const double w = factor*s.weight(i,j);
    const Vec3 p = (w/scale)*(s.pole(i,j)-origin);
    return {p.x(),p.y(),p.z(),w};
}

std::shared_ptr<const BSplineSurface> concatenate(const BSplineSurface &left, const BSplineSurface &right) {
    if (left.vDegree() != right.vDegree() || left.vKnots().size() != right.vKnots().size()) return {};
    const double ratioV = left.vDomain().length()/right.vDomain().length();
    if (!(ratioV > 0.0) || !std::isfinite(ratioV)) return {};
    for (std::size_t i=0;i<left.vKnots().size();++i) {
        const double mapped = left.vDomain().lo+ratioV*(right.vKnots()[i]-right.vDomain().lo);
        if (std::fabs(mapped-left.vKnots()[i]) > coefficientRoundoff*std::max({1.0,std::fabs(mapped),std::fabs(left.vKnots()[i])})) return {};
    }
    std::vector<Vec3> rightPoles;
    std::vector<double> rightWeights;
    for (int i=0;i<right.uPoleCount();++i) for (int j=0;j<right.vPoleCount();++j) {
        rightPoles.push_back(right.pole(i,j)); rightWeights.push_back(right.weight(i,j));
    }
    const BSplineSurface aligned(right.uDegree(),right.vDegree(),right.uKnots(),left.vKnots(),
        right.uPoleCount(),right.vPoleCount(),rightPoles,rightWeights);
    const int degree = std::max(left.uDegree(),right.uDegree());
    const auto a = elevateU(left,degree), b = elevateU(aligned,degree);
    const int na = a.uPoleCount(), nb = b.uPoleCount(), nv = a.vPoleCount();
    const double ratio = a.weight(na-1,0)/b.weight(0,0);
    if (!(ratio > 0.0) || !std::isfinite(ratio)) return {};
    Box bounds;
    double coordinateScale = 1.0;
    for (int j=0;j<nv;++j) for (const Vec3 &p : {a.pole(na-1,j),b.pole(0,j)}) {
        bounds.add(p); coordinateScale=std::max(coordinateScale,norm(p));
    }
    for (int j=0;j<nv;++j) {
        if (distance(a.pole(na-1,j),b.pole(0,j)) > coefficientRoundoff*coordinateScale
            || std::fabs(a.weight(na-1,j)-ratio*b.weight(0,j)) > coefficientRoundoff*std::max(a.weight(na-1,j),ratio*b.weight(0,j))) return {};
    }
    const Vec3 origin = a.pole(na-1,0);
    const double scale = std::max(bounds.diagonal(),1.0);
    const double ka = degree/(a.uDomain().hi-a.uKnots()[std::size_t(na-1)]);
    const double kb = degree/(b.uKnots()[std::size_t(degree+1)]-b.uDomain().lo);
    std::vector<HPoint> seam, da, db;
    double xx=0,xy=0,yy=0,xz=0,yz=0,coefficientMagnitude=1.0;
    for (int j=0;j<nv;++j) {
        const HPoint h=homogeneous(a,na-1,j,origin,scale), l=homogeneous(a,na-2,j,origin,scale);
        const HPoint r0=homogeneous(b,0,j,origin,scale,ratio), r1=homogeneous(b,1,j,origin,scale,ratio);
        for (int k=0;k<4;++k)
            coefficientMagnitude=std::max({coefficientMagnitude,std::fabs(h[k]),std::fabs(l[k]),std::fabs(r0[k]),std::fabs(r1[k])});
        HPoint u{},v{};
        for (int k=0;k<4;++k) { u[k]=ka*(h[k]-l[k]); v[k]=kb*(r1[k]-r0[k]);
            xx+=u[k]*u[k]; xy+=u[k]*h[k]; yy+=h[k]*h[k]; xz+=u[k]*v[k]; yz+=h[k]*v[k]; }
        seam.push_back(h); da.push_back(u); db.push_back(v);
    }
    const double determinant=xx*yy-xy*xy;
    if (!(determinant > 0.0)) return {};
    const double alpha=(xz*yy-yz*xy)/determinant, beta=(yz*xx-xz*xy)/determinant;
    if (!(alpha > 0.0) || !std::isfinite(alpha) || !std::isfinite(beta)) return {};
    double error=0.0;
    for (int j=0;j<nv;++j) for (int k=0;k<4;++k) {
        const double predicted=alpha*da[std::size_t(j)][k]+beta*seam[std::size_t(j)][k];
        error=std::max(error,std::fabs(predicted-db[std::size_t(j)][k]));
    }
    // Il limite e' sui coefficienti, non sui jet gia' differenziati.
    // Propaga lo stesso roundoff nelle sottrazioni dei poli e nei fattori
    // grado/campata: confrontare direttamente il jet con il limite dei poli
    // rendeva il certificato dipendente dal verso e dalla scala UV.
    const double jetGain=2.0*(std::fabs(alpha*ka)+std::fabs(kb))+std::fabs(beta);
    const double jetRoundoff=coefficientRoundoff*coefficientMagnitude*jetGain;
    if (error > jetRoundoff) return {};
    auto knots=a.uKnots(); knots.pop_back();
    const double shift=a.uDomain().hi-b.uDomain().lo;
    for (std::size_t i=std::size_t(degree+1);i<b.uKnots().size();++i) knots.push_back(b.uKnots()[i]+shift);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i=0;i<na+nb-1;++i) for (int j=0;j<nv;++j) {
        poles.push_back(i<na ? a.pole(i,j) : b.pole(i-na+1,j));
        weights.push_back(i<na ? a.weight(i,j) : ratio*b.weight(i-na+1,j));
    }
    return std::make_shared<BSplineSurface>(degree,a.vDegree(),knots,a.vKnots(),na+nb-1,nv,poles,weights);
}

bool withinPolynomialBudget(const Curve<3> &original, const Curve<3> &replacement, const Interval &range, double budget) {
    auto breaks=original.breakpoints(range);
    const auto extra=replacement.breakpoints(range);
    breaks.insert(breaks.end(),extra.begin(),extra.end());
    std::sort(breaks.begin(),breaks.end()); breaks.erase(std::unique(breaks.begin(),breaks.end()),breaks.end());
    for (std::size_t k=1;k<breaks.size();++k) {
        const Interval span{breaks[k-1],breaks[k]};
        auto a=standardBezierPieces(original,span), b=standardBezierPieces(replacement,span);
        if (a.size()!=1 || b.size()!=1) return false;
        const int degree=std::max(a.front().degree(),b.front().degree());
        a=standardBezierPieces(original,span,degree); b=standardBezierPieces(replacement,span,degree);
        if (a.front().isRational() || b.front().isRational()) return false;
        for (int i=0;i<=degree;++i) if (distance(a.front().poles()[std::size_t(i)],b.front().poles()[std::size_t(i)])>budget) return false;
    }
    return true;
}

CurvePtr<3> certifiedTrim(const CurvePtr<3> &curve, const Interval &range, double budget) {
    const auto candidate=blendSectionCurve(curve,range,0.0);
    if (candidate==curve || withinPolynomialBudget(*curve,*candidate,range,budget)) return candidate;
    return curve;
}
}

std::optional<JoinedNurbsSurface> joinTangentNurbs(const BSplineSurface &a, const BSplineSurface &b) {
    if (a.isUPeriodic() || a.isVPeriodic() || b.isUPeriodic() || b.isVPeriodic()) return {};
    for (bool swapA : {false,true}) for (bool swapB : {false,true})
        for (bool reverseA : {false,true}) for (bool reverseB : {false,true}) for (bool reverseV : {false,true}) {
            const auto left=orient(a,swapA,reverseA,false), right=orient(b,swapB,reverseB,reverseV);
            if (auto joined=concatenate(left,right)) {
                const Interval da=swapA?a.vDomain():a.uDomain(), db=swapB?b.vDomain():b.uDomain();
                return JoinedNurbsSurface{joined,swapA!=reverseA,(swapB!=reverseB)!=reverseV,
                    swapA?1:0,swapB?1:0,reverseA?da.lo:da.hi,reverseB?db.hi:db.lo};
            }
        }
    return {};
}

CurvePtr<3> joinTangentSplineCurves(const CurvePtr<3> &a, const Interval &rangeA, bool forwardA,
                                  const CurvePtr<3> &b, const Interval &rangeB, bool forwardB, double budget) {
    const auto *sa=dynamic_cast<const BSplineCurve<3>*>(a.get()), *sb=dynamic_cast<const BSplineCurve<3>*>(b.get());
    if (!sa || !sb || sa->isRational() || sb->isRational()) return {};
    const double ta=forwardA?rangeA.hi:rangeA.lo, tb=forwardB?rangeB.lo:rangeB.hi;
    const auto tangent = [](const Curve<3> &curve, double t, bool left) {
        Vec3 jet[2];
        if (left) curve.evaluateLeft(t,1,jet); else curve.evaluate(t,1,jet);
        return jet[1];
    };
    const Vec3 da=(forwardA?1.:-1.)*tangent(*a,ta,forwardA), db=(forwardB?1.:-1.)*tangent(*b,tb,!forwardB);
    if (!(norm(da)>0 && norm(db)>0) || dot(da,db)<=0.0 || distance(a->point(ta),b->point(tb))>0.5*budget
        || norm(cross(normalized(da),normalized(db)))>std::sqrt(2e-9)) return {};
    const int degree=std::max(sa->degree(),sb->degree());
    std::vector<BSplineCurve<3>> pieces;
    std::vector<double> breaks{0.0};
    const CurvePtr<3> curves[2]={certifiedTrim(a,rangeA,0.5*budget),certifiedTrim(b,rangeB,0.5*budget)};
    const Interval ranges[2]={rangeA,rangeB};
    const bool forward[2]={forwardA,forwardB};
    const double speeds[2]={norm(da),norm(db)};
    for (int k=0;k<2;++k) {
        auto segments=standardBezierPieces(*curves[k],ranges[k],degree);
        if (!forward[k]) std::reverse(segments.begin(),segments.end());
        for (const auto &segment:segments) {
            auto poles=segment.poles();
            if (!forward[k]) std::reverse(poles.begin(),poles.end());
            pieces.emplace_back(degree,segment.knots(),poles);
            // Il parametro e' una lunghezza di riferimento, non un dato
            // geometrico. Riserva almeno il budget per campata, affinche'
            // nodi distinti non collassino dopo la concatenazione.
            const double step=std::max(budget,segment.domain().length()*speeds[k]);
            if (!(breaks.back()+step>breaks.back())) return {};
            breaks.push_back(breaks.back()+step);
        }
    }
    return std::make_shared<BSplineCurve<3>>(joinBezierPieces(pieces,breaks));
}
}
