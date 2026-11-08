#include "fk_blend_polar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>

#include "fk_blend_surface.h"
#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_history.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"

namespace ForgeCad::Kernel::detail {
namespace {
double kPolarFitTolerance() { return scaledAccuracy(1e-9); } // mm, fit delle calotte comuni
using Origins = std::vector<HistoryLink>;
HistoryLink origin(EntityKind kind, int index, HistoryRelation relation = HistoryRelation::Generated) {
    return {0, kind, index, relation};
}

// Solo la parametrizzazione a meridiani: entrambi i lati v convergono allo
// stesso polo a ciascun estremo u. Il limite della normale usa il primo
// coefficiente non nullo di Su x Sv, non normalAt nel punto singolare.
Vec3 poleNormal(const Surface &s, bool sense, bool high, double v) {
    const double u = high ? s.uDomain().hi : s.uDomain().lo;
    Vec3 d[9]; s.evaluate(u, v, 2, d);
    const auto at = [&](int a, int b) { return d[Surface::derivativeIndex(a,b,2)]; };
    Vec3 n = cross(at(2,0),at(0,1)) + cross(at(1,0),at(1,1));
    if (!(norm(n) > 0)) throw std::domain_error("raccordo ai poli: limite della normale singolare");
    if (high) n = -n;
    return sense ? normalized(n) : -normalized(n);
}

struct Cap {
    bool smooth = false;
    Vec3 center;
    std::map<int, SurfaceProjection> contacts;
    std::map<int, int> vertices;
};

Cap makeCap(const Body &body, const std::vector<FaceId> &faces, VertexId pole,
            const Vec3 &inward, double length, double radius, bool high) {
    Cap cap;
    cap.smooth = true;
    for (auto f : faces) {
        const auto &face = body.face(f);
        const auto &s = *face.surface;
        for (double v : {s.vDomain().lo, .5*(s.vDomain().lo+s.vDomain().hi), s.vDomain().hi})
            if (norm(poleNormal(s,face.sense,high,v)+inward) > 1e-8) cap.smooth = false;
    }
    const Vec3 tip = body.vertex(pole).point;
    if (cap.smooth) { cap.center = tip + radius*inward; return cap; }
    const auto &support = *body.face(faces.front()).surface;
    double lo = 0, hi = 0;
    bool bracket = false;
    for (int i = 1; i <= 64; ++i) {
        hi = .5*length*i/64;
        if (projectPoint(support,tip+hi*inward).distance >= radius) { bracket = true; break; }
        lo = hi;
    }
    if (!bracket) throw std::domain_error("raccordo ai poli: raggio troppo grande per la calotta comune");
    for (int i = 0; i < 55; ++i) {
        const double mid = .5*(lo+hi);
        if (projectPoint(support,tip+mid*inward).distance < radius) lo = mid;
        else hi = mid;
    }
    cap.center = tip + .5*(lo+hi)*inward;
    for (auto f : faces) {
        const auto &face = body.face(f);
        const auto q = projectPoint(*face.surface,cap.center);
        Vec3 normal = face.surface->normal(q.u,q.v);
        if (!face.sense) normal = -normal;
        if (distance(q.point-radius*normal,cap.center) > kPolarFitTolerance()
            || classifyPointOnFace(body,f,q.point,linearResolution()) == PointLocation::Outside)
            throw std::domain_error("raccordo ai poli: le facce non ammettono una calotta sferica tangente comune");
        cap.contacts.emplace(f.index,q);
    }
    return cap;
}

// Funzione orientata, senza acos: lo zero e' la tangenza dei supporti.
// Le SP-curve originali mantengono il ramo esatto anche vicino a tale zero.
double turnAt(const Body &body, EdgeId id, double t) {
    const auto &edge = body.edge(id);
    Vec3 n[2]; int k = 0;
    for (auto fin : {edge.forward,edge.backward}) {
        const auto &face = body.face(body.finFace(fin));
        const auto uv = body.fin(fin).pcurve->point(t);
        n[k] = face.surface->normal(uv.x(),uv.y());
        if (!face.sense) n[k] = -n[k];
        ++k;
    }
    return dot(cross(n[1],-normalized(edge.curve->derivative(t))),n[0]);
}

std::vector<double> transitions(const Body &body, EdgeId id) {
    const auto &edge = body.edge(id);
    // Ricerca iniziale in parametri, integrata dai nodi della curva. Il fit
    // e i controlli dei contatti rifiutano i tratti non regolari a valle.
    auto samples = edge.curve->breakpoints(edge.range);
    for (int i = 1; i < 128; ++i) samples.push_back(edge.range.lo+edge.range.length()*i/128.);
    const double inset = 1e-6*edge.range.length();
    samples.push_back(edge.range.lo+inset); samples.push_back(edge.range.hi-inset);
    std::sort(samples.begin(),samples.end());
    samples.erase(std::unique(samples.begin(),samples.end()),samples.end());
    std::vector<double> roots;
    double previous = edge.range.lo+inset, value = turnAt(body,id,previous);
    for (double t : samples) {
        if (t <= previous || t >= edge.range.hi-inset) continue;
        const double next = turnAt(body,id,t);
        if (next*value < 0) {
            double lo = previous, hi = t, fl = value;
            for (int j = 0; j < 48; ++j) {
                const double mid = .5*(lo+hi), fm = turnAt(body,id,mid);
                if (fm*fl > 0) lo = mid;
                else hi = mid;
            }
            roots.push_back(.5*(lo+hi));
        }
        previous = t; value = next;
    }
    return roots;
}

double capParameter(const Body &body, EdgeId id, const Cap &cap, bool high) {
    const auto &edge = body.edge(id);
    if (cap.smooth) return high ? edge.range.hi : edge.range.lo;
    const auto fin = edge.backward;
    const auto &q = cap.contacts.at(body.finFace(fin).index);
    double lo = edge.range.lo, hi = edge.range.hi;
    for (int i = 0; i < 55; ++i) {
        const double mid = .5*(lo+hi);
        if (body.fin(fin).pcurve->point(mid).x() < q.u) lo = mid;
        else hi = mid;
    }
    return .5*(lo+hi);
}

// Un fit unico per copie circolari certificate dai coefficienti NURBS.
// Evita che l'arrotondamento in pose diverse cambi il ramo di Newton presso
// le tangenze. Non e' una ricerca di entita' per vicinanza: supporti, curva,
// parametrizzazione e verso devono corrispondere agli stessi coefficienti.
std::optional<Transform3> circularCopy(const Body &body, EdgeId source, EdgeId target,
                                      const Vec3 &axis, const Vec3 &pole, double scale) {
    const auto &a = body.edge(source), &b = body.edge(target);
    if (a.range.lo != b.range.lo || a.range.hi != b.range.hi) return {};
    const double mid = .5*(a.range.lo+a.range.hi);
    Vec3 ra = a.curve->point(mid)-pole, rb = b.curve->point(mid)-pole;
    ra -= dot(ra,axis)*axis; rb -= dot(rb,axis)*axis;
    if (!(norm(ra) > 0 && norm(rb) > 0)) return {};
    const double angle = std::atan2(dot(cross(ra,rb),axis),dot(ra,rb));
    const auto rotation = Transform3::rotation(pole,axis,angle);
    // Budget di solo arrotondamento delle trasformazioni, proporzionale
    // alla scala delle coordinate. Nessuna risoluzione/connessione allargata.
    const double rounding = 256*std::numeric_limits<double>::epsilon()*scale;
    const auto *ca = dynamic_cast<const BSplineCurve<3>*>(a.curve.get());
    const auto *cb = dynamic_cast<const BSplineCurve<3>*>(b.curve.get());
    if (!ca || !cb || ca->degree() != cb->degree() || ca->knots() != cb->knots()
        || ca->poleCount() != cb->poleCount() || ca->weights() != cb->weights()) return {};
    for (int i = 0; i < ca->poleCount(); ++i)
        if (distance(rotation.applyToPoint(ca->poles()[i]),cb->poles()[i]) > rounding) return {};
    for (const auto &pair : {std::make_pair(a.forward,b.forward),std::make_pair(a.backward,b.backward)}) {
        const auto &fa = body.face(body.finFace(pair.first)), &fb = body.face(body.finFace(pair.second));
        const auto &sa = static_cast<const BSplineSurface&>(*fa.surface), &sb = static_cast<const BSplineSurface&>(*fb.surface);
        if (fa.sense != fb.sense || sa.uDegree() != sb.uDegree() || sa.vDegree() != sb.vDegree()
            || sa.uKnots() != sb.uKnots() || sa.vKnots() != sb.vKnots()
            || sa.uPoleCount() != sb.uPoleCount() || sa.vPoleCount() != sb.vPoleCount()) return {};
        for (int i = 0; i < sa.uPoleCount(); ++i) for (int j = 0; j < sa.vPoleCount(); ++j)
            if (sa.weight(i,j) != sb.weight(i,j)
                || distance(rotation.applyToPoint(sa.pole(i,j)),sb.pole(i,j)) > rounding) return {};
        const auto *pa = dynamic_cast<const BSplineCurve<2>*>(body.fin(pair.first).pcurve.get());
        const auto *pb = dynamic_cast<const BSplineCurve<2>*>(body.fin(pair.second).pcurve.get());
        if (!pa || !pb || pa->degree() != pb->degree() || pa->knots() != pb->knots()
            || pa->poleCount() != pb->poleCount() || pa->weights() != pb->weights()) return {};
        for (int i = 0; i < pa->poleCount(); ++i)
            if (distance(pa->poles()[i],pb->poles()[i]) > 256*std::numeric_limits<double>::epsilon()
                *std::max({1.,norm(pa->poles()[i]),norm(pb->poles()[i])})) return {};
    }
    return rotation;
}

std::vector<Body::BuildFin> orderedLoop(std::vector<Body::BuildFin> pending,
                                      const std::vector<Body::BuildEdge> &edges) {
    std::vector<Body::BuildFin> loop;
    while (!pending.empty()) {
        auto it = pending.begin();
        if (!loop.empty()) {
            const auto &last = loop.back();
            const int end = last.sense ? edges[last.edge].end : edges[last.edge].start;
            it = std::find_if(pending.begin(),pending.end(),[&](const auto &f) {
                return (f.sense ? edges[f.edge].start : edges[f.edge].end) == end;
            });
        }
        if (it == pending.end()) throw std::domain_error("raccordo ai poli: contorno della calotta non chiuso");
        loop.push_back(*it); pending.erase(it);
    }
    return loop;
}
}

std::optional<Body> blendPolarEdges(const Body &input, const std::vector<EdgeId> &selected,
                                   double radius, BodyHistory *history) {
    const auto poles = input.vertices();
    const auto sourceEdges = input.edges();
    const auto sourceFaces = input.faces();
    if (input.isSheet() || poles.size() != 2 || sourceEdges.size() < 5
        || sourceFaces.size() != sourceEdges.size()) return {};
    // L'orientamento comune e' una scelta di costruzione, con indici e
    // genealogia originali conservati anche se un edge era stato invertito.
    Body body = input;
    for (auto e : sourceEdges) {
        if (body.isLaminar(e) || body.edgeStart(e) == body.edgeEnd(e)) return {};
        if (body.edgeStart(e) != poles.front()) body.reverseEdge(e);
        if (!body.fin(body.edge(e).forward).pcurve || !body.fin(body.edge(e).backward).pcurve) return {};
    }
    const Vec3 axis = normalized(body.vertex(poles[1]).point-body.vertex(poles[0]).point);
    const double length = distance(body.vertex(poles[1]).point,body.vertex(poles[0]).point);
    for (auto f : sourceFaces) {
        const auto &face = body.face(f);
        if (face.surface->type() != SurfaceType::BSpline || face.loops.size() != 1
            || body.loopFins(face.loops.front()).size() != 2) return {};
        const auto &s = *face.surface;
        for (bool high : {false,true}) for (int i = 0; i <= 4; ++i) {
            const double u = high ? s.uDomain().hi : s.uDomain().lo;
            const double v = s.vDomain().lo+s.vDomain().length()*i/4.;
            if (distance(s.point(u,v),body.vertex(poles[high ? 1 : 0]).point) > linearResolution()) return {};
        }
        for (auto fin : body.loopFins(face.loops.front())) {
            const auto &edge = body.edge(body.fin(fin).edge);
            const auto &pc = *body.fin(fin).pcurve;
            for (int i = 0; i <= 32; ++i) {
                const double t = edge.range.lo+edge.range.length()*i/32.;
                if (!(pc.derivative(t).x() > 0)) return {};
            }
        }
    }
    std::set<int> chosen;
    for (auto e : selected) chosen.insert(e.index);
    if (chosen.size() != sourceEdges.size())
        throw std::domain_error("raccordo ai poli: seleziona tutti i "+std::to_string(sourceEdges.size())
                                +" giunti convergenti per chiudere insieme i raccordi alle punte");
    Cap caps[2] = {makeCap(body,sourceFaces,poles[0],axis,length,radius,false),
                   makeCap(body,sourceFaces,poles[1],-axis,length,radius,true)};
    std::vector<Vec3> vertices;
    std::vector<Origins> vertexOrigins, edgeOrigins, faceOrigins;
    for (int p = 0; p < 2; ++p) {
        if (caps[p].smooth) {
            const int vertex = int(vertices.size()); vertices.push_back(body.vertex(poles[p]).point);
            vertexOrigins.push_back({origin(EntityKind::Vertex,poles[p].index,HistoryRelation::Same)});
            for (auto f : sourceFaces) caps[p].vertices.emplace(f.index,vertex);
        } else for (auto f : sourceFaces) {
            caps[p].vertices.emplace(f.index,int(vertices.size()));
            vertices.push_back(caps[p].contacts.at(f.index).point);
            vertexOrigins.push_back({origin(EntityKind::Face,f.index),origin(EntityKind::Vertex,poles[p].index)});
        }
    }
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::map<int,int> faceIndices;
    for (auto f : sourceFaces) {
        faceIndices.emplace(f.index,int(faces.size()));
        faces.push_back({body.face(f).surface,body.face(f).sense,{{}}});
        faceOrigins.push_back({origin(EntityKind::Face,f.index,HistoryRelation::Modified)});
    }
    std::vector<Body::BuildFin> capFins[2];
    std::map<std::pair<int,int>,std::vector<Body::BuildFin>> contactFins;
    std::vector<double> referenceCuts;
    std::vector<SurfacePtr> referencePatches;
    const double coordinateScale = std::max({1.,length,norm(body.vertex(poles[0]).point),norm(body.vertex(poles[1]).point)});
    for (auto e : sourceEdges) {
        const auto copy = e == sourceEdges.front() ? std::optional<Transform3>{}
            : circularCopy(body,sourceEdges.front(),e,axis,body.vertex(poles[0]).point,coordinateScale);
        const auto &edge = body.edge(e);
        const auto a = body.finFace(edge.forward), b = body.finFace(edge.backward);
        auto cuts = copy ? std::vector<double>{} : transitions(body,e);
        const double from = capParameter(body,e,caps[0],false), to = capParameter(body,e,caps[1],true);
        if (!(from < to) || (!cuts.empty() && (cuts.front() <= from || cuts.back() >= to)))
            throw std::domain_error("raccordo ai poli: raggio troppo grande, la calotta invade un cambio di convessita'");
        cuts.insert(cuts.begin(),from); cuts.push_back(to);
        if (copy) cuts = referenceCuts;
        if (e == sourceEdges.front()) referenceCuts = cuts;
        std::vector<int> va{caps[0].vertices.at(a.index)}, vb{caps[0].vertices.at(b.index)};
        for (std::size_t k = 1; k+1 < cuts.size(); ++k) {
            const int vertex = int(vertices.size()); vertices.push_back(edge.curve->point(cuts[k]));
            va.push_back(vertex); vb.push_back(vertex);
            vertexOrigins.push_back({origin(EntityKind::Edge,e.index)});
        }
        va.push_back(caps[1].vertices.at(a.index)); vb.push_back(caps[1].vertices.at(b.index));
        for (std::size_t k = 0; k+1 < cuts.size(); ++k) {
            const Interval range{cuts[k],cuts[k+1]};
            std::array<std::optional<Vec3>,2> collapsed, centers;
            if (k == 0 && !caps[0].smooth) centers[0] = caps[0].center;
            if (k+2 == cuts.size() && !caps[1].smooth) centers[1] = caps[1].center;
            if (va[k] == vb[k]) collapsed[0] = vertices[va[k]];
            if (va[k+1] == vb[k+1]) collapsed[1] = vertices[va[k+1]];
            const auto surface = copy ? transformSurface(referencePatches.at(k),*copy)
                : polarBlendPatch(body,e,range,radius,turnAt(body,e,.5*(range.lo+range.hi)) < 0,collapsed,centers);
            if (e == sourceEdges.front()) referencePatches.push_back(surface);
            const int ca = int(edges.size()); edges.push_back({va[k],va[k+1],surface->vIso(0),range});
            edgeOrigins.push_back({origin(EntityKind::Edge,e.index),origin(EntityKind::Face,a.index)});
            const int cb = int(edges.size()); edges.push_back({vb[k],vb[k+1],surface->vIso(1),range});
            edgeOrigins.push_back({origin(EntityKind::Edge,e.index),origin(EntityKind::Face,b.index)});
            contactFins[{a.index,e.index}].push_back({ca,true,{}});
            contactFins[{b.index,e.index}].push_back({cb,true,{}});
            std::vector<Body::BuildFin> loop{{ca,false,{}}};
            const auto terminal = [&](int p, double t) {
                const int ce = int(edges.size());
                edges.push_back({caps[p].vertices.at(a.index),caps[p].vertices.at(b.index),surface->uIso(t),{0,1}});
                edgeOrigins.push_back({origin(EntityKind::Edge,e.index),origin(EntityKind::Vertex,poles[p].index)});
                loop.push_back({ce,p == 0,{}}); capFins[p].push_back({ce,p != 0,{}});
                const auto &cap = caps[p];
                for (double v : {0.,.25,.5,.75,1.})
                    if (std::abs(distance(surface->point(t,v),cap.center)-radius) > kPolarFitTolerance())
                        throw std::domain_error("raccordo ai poli: arco fuori dalla calotta comune");
            };
            if (k == 0 && !caps[0].smooth) terminal(0,range.lo);
            loop.push_back({cb,true,{}});
            if (k+2 == cuts.size() && !caps[1].smooth) terminal(1,range.hi);
            faces.push_back({surface,false,{loop}});
            faceOrigins.push_back({origin(EntityKind::Edge,e.index)});
        }
    }
    for (auto f : sourceFaces) for (auto old : body.loopFins(body.face(f).loops.front())) {
        auto fins = contactFins.at({f.index,body.fin(old).edge.index});
        if (!body.fin(old).sense) {
            std::reverse(fins.begin(),fins.end()); for (auto &fin : fins) fin.sense = false;
        }
        auto &loop = faces[faceIndices.at(f.index)].loops.front(); loop.insert(loop.end(),fins.begin(),fins.end());
    }
    for (int p = 0; p < 2; ++p) if (!caps[p].smooth) {
        const Vec3 outward = p ? axis : -axis;
        const Vec3 reference = std::abs(outward.x()) < .9 ? Vec3{1,0,0} : Vec3{0,1,0};
        faces.push_back({std::make_shared<SphericalSurface>(Frame3(caps[p].center,outward,reference),radius),true,
                         {orderedLoop(capFins[p],edges)}});
        faceOrigins.push_back({origin(EntityKind::Vertex,poles[p].index)});
    }
    Body result = Body::build(vertices,edges,faces);
    if (computePCurves(result) != 0) throw std::domain_error("raccordo ai poli: contorni non ricostruibili entro tolleranza");
    const auto issues = checkBody(result,{true,true});
    if (!issues.empty()) throw std::domain_error("raccordo ai poli non valido: "+issues.front().message);
    if (history) {
        auto declared = makeHistory({&input},result);
        for (int i = 0; i < int(faces.size()); ++i) declared.declareOrigins(EntityKind::Face,i,faceOrigins[i]);
        for (int i = 0; i < int(edges.size()); ++i) declared.declareOrigins(EntityKind::Edge,i,edgeOrigins[i]);
        for (int i = 0; i < int(vertices.size()); ++i) declared.declareOrigins(EntityKind::Vertex,i,vertexOrigins[i]);
        *history = std::move(declared);
    }
    return result;
}
}
