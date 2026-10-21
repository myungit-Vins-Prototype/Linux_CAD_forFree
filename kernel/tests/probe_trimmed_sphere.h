#ifndef FORGECAD_PROBE_TRIMMED_SPHERE_H
#define FORGECAD_PROBE_TRIMMED_SPHERE_H

// Solo diagnostica: limiti dai poli razionali, suddivisione di de Casteljau
// e dominio rifilato dalle pcurve. Il limite di lavoro restituisce Unknown.
#include <algorithm>
#include <cmath>
#include <vector>
#include "fk_bspline_surface.h"
#include "fk_classify.h"

namespace Probe {
using namespace ForgeCad::Kernel;
struct Cell {
    int nu, nv, depth = 0;
    Interval u, v;
    std::vector<Vec3> h;
    std::vector<double> w;
};
struct UVBox { double u0, u1, v0, v1; };
struct Boundary { UVBox box; std::vector<Vec2> poles; std::vector<double> weights; };
inline bool overlaps(const UVBox &a, const Cell &b) {
    return a.u0 <= b.u.hi && a.u1 >= b.u.lo && a.v0 <= b.v.hi && a.v1 >= b.v.lo;
}
inline bool crosses(const Boundary &boundary, const Cell &cell, int depth=0) {
    if (!overlaps(boundary.box,cell)) return false;
    for (const auto &p : {boundary.poles.front(),boundary.poles.back()})
        if(p.x()>=cell.u.lo && p.x()<=cell.u.hi && p.y()>=cell.v.lo && p.y()<=cell.v.hi) return true;
    if(depth>=36) return true;
    Boundary a,b;
    const int n=int(boundary.poles.size());
    a.poles.resize(n); b.poles.resize(n); a.weights.resize(n); b.weights.resize(n);
    std::vector<Vec2> h(n); auto w=boundary.weights;
    for(int k=0;k<n;++k) h[k]=w[k]*boundary.poles[k];
    for(int level=0;level<n;++level) {
        a.poles[level]=h[0]/w[0]; a.weights[level]=w[0];
        b.poles[n-1-level]=h[n-1-level]/w[n-1-level]; b.weights[n-1-level]=w[n-1-level];
        for(int k=0;k<n-1-level;++k) { h[k]=0.5*(h[k]+h[k+1]); w[k]=0.5*(w[k]+w[k+1]); }
    }
    for(auto *c:{&a,&b}) {
        c->box={1e300,-1e300,1e300,-1e300};
        for(const auto &p:c->poles) {
            c->box.u0=std::min(c->box.u0,p.x()); c->box.u1=std::max(c->box.u1,p.x());
            c->box.v0=std::min(c->box.v0,p.y()); c->box.v1=std::max(c->box.v1,p.y());
        }
    }
    return crosses(a,cell,depth+1)||crosses(b,cell,depth+1);
}
inline void split(const Cell &a, bool alongU, Cell &left, Cell &right) {
    left = right = a;
    left.depth = right.depth = a.depth + 1;
    const int n = alongU ? a.nu : a.nv, lines = alongU ? a.nv : a.nu;
    const auto index = [&](int k, int j) { return alongU ? k * a.nv + j : j * a.nv + k; };
    for (int j = 0; j < lines; ++j) {
        std::vector<Vec3> h(n);
        std::vector<double> w(n);
        for (int k = 0; k < n; ++k) { h[k] = a.h[index(k,j)]; w[k] = a.w[index(k,j)]; }
        for (int level = 0; level < n; ++level) {
            left.h[index(level,j)] = h[0]; left.w[index(level,j)] = w[0];
            right.h[index(n-1-level,j)] = h[n-1-level]; right.w[index(n-1-level,j)] = w[n-1-level];
            for (int k = 0; k < n-1-level; ++k) { h[k] = 0.5*(h[k]+h[k+1]); w[k] = 0.5*(w[k]+w[k+1]); }
        }
    }
    Interval &l = alongU ? left.u : left.v, &r = alongU ? right.u : right.v;
    l.hi = r.lo = 0.5*(l.lo+l.hi);
}

// 1: nessuna penetrazione oltre tolerance nel dominio rifilato;
// 0: collisione trovata; -1: limite raggiunto/tipo non supportato.
inline int sphereOutsideFace(const Body &body, FaceId id, const Vec3 &center,
                             double radius, double tolerance, double &penetration) {
    if (!isFinite(center) || !std::isfinite(radius) || !std::isfinite(tolerance)
        || !(radius > tolerance && tolerance > 0)) return -1;
    const auto &face = body.face(id);
    if (!face.surface || face.surface->type() != SurfaceType::BSpline || face.loops.empty()) return -1;
    std::vector<Boundary> boundaries;
    const auto addBounds = [&](const std::vector<Vec2> &poles, std::vector<double> weights = {}) {
        if(weights.empty()) weights.assign(poles.size(),1.0);
        UVBox b{1e300,-1e300,1e300,-1e300};
        for (const auto &p : poles) {
            b.u0 = std::min(b.u0,p.x()); b.u1 = std::max(b.u1,p.x());
            b.v0 = std::min(b.v0,p.y()); b.v1 = std::max(b.v1,p.y());
        }
        boundaries.push_back({b,poles,std::move(weights)});
    };
    for (auto loop : face.loops) for (auto fin : body.loopFins(loop)) {
        const auto &f = body.fin(fin);
        if (!f.pcurve) return -1;
        if (f.pcurve->type() == CurveType::Line) {
            const auto range = body.edge(f.edge).range;
            addBounds({f.pcurve->point(range.lo),f.pcurve->point(range.hi)});
        } else if (f.pcurve->type() == CurveType::BSpline) {
            const auto &curve = static_cast<const BSplineCurve<2>&>(*f.pcurve);
            for (const auto &b : *curve.cachedBezierSegments()) {
                if (b.domain().hi < body.edge(f.edge).range.lo || b.domain().lo > body.edge(f.edge).range.hi) continue;
                for (int i = 0; i < b.poleCount(); ++i) if (!(b.weight(i) > 0)) return -1;
                addBounds(b.poles(),b.weights());
            }
        } else return -1;
    }
    if (boundaries.empty()) return -1;
    std::vector<Cell> pending;
    const auto &surface = static_cast<const BSplineSurface&>(*face.surface);
    for (const auto &b : *surface.cachedBezierPatches()) {
        Cell c{b.uPoleCount(),b.vPoleCount(),0,b.uDomain(),b.vDomain(),{}, {}};
        for (int i=0;i<c.nu;++i) for(int j=0;j<c.nv;++j) {
            const double w=b.weight(i,j);
            if (!(w>0)) return -1;
            c.h.push_back(w*b.pole(i,j)); c.w.push_back(w);
        }
        pending.push_back(std::move(c));
    }
    int visits=0;
    while (!pending.empty()) {
        if (++visits>200000) return -1;
        Cell c=std::move(pending.back()); pending.pop_back();
        const Vec3 middle=surface.point(0.5*(c.u.lo+c.u.hi),0.5*(c.v.lo+c.v.hi));
        const double distance=norm(middle-center);
        if (!(distance>0)) return -1;
        const Vec3 direction=(middle-center)/distance;
        double lower=1e300, uSize=0, vSize=0;
        for(int i=0;i<c.nu;++i) for(int j=0;j<c.nv;++j) {
            const int k=i*c.nv+j;
            const Vec3 p=c.h[k]/c.w[k];
            lower=std::min(lower,dot(p-center,direction));
            if(i) uSize=std::max(uSize,norm(p-c.h[k-c.nv]/c.w[k-c.nv]));
            if(j) vSize=std::max(vSize,norm(p-c.h[k-1]/c.w[k-1]));
        }
        if(lower-1e-12*(1+norm(center))>=radius-tolerance) continue;
        bool boundary=false;
        for(const auto &b:boundaries) if(crosses(b,c)) { boundary=true; break; }
        const auto location=classifyPointOnFace(body,id,middle,tolerance*0.1);
        if(!boundary && location==PointLocation::Outside) continue;
        if(location!=PointLocation::Outside && distance<radius-tolerance) {
            penetration=std::max(penetration,radius-distance); return 0;
        }
        if(c.depth>=60) return -1;
        Cell a,b;
        split(c,uSize>=vSize,a,b);
        pending.push_back(std::move(a)); pending.push_back(std::move(b));
    }
    return 1;
}
}
#endif
