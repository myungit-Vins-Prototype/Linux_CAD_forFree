// Diagnostica sperimentale, separata dall'app e dalla suite automatica.
// c++ -std=c++17 -O2 -I kernel kernel/tests/probe_blend_candidates.cpp \
//   forgecad-cuda-build/kernel/libforgekernel.a -o /tmp/probe_blend_candidates
// /tmp/probe_blend_candidates file.body capFace radius seedsPerFace randomSeed [seamHalfSamples] [sampleIndex] [transitionFace transitionEdge]
// CSV su stdout, riepilogo su stderr. Non costruisce ne' modifica un B-rep.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>

#include "fk_body_io.h"
#include "fk_classify.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"
#include "probe_trimmed_sphere.h"

using namespace ForgeCad::Kernel;

namespace {
struct Contact {
    Vec3 point, center, du, dv;
};

Contact contact(const Face &face, double u, double v, double radius) {
    Vec3 d[9];
    face.surface->evaluate(u, v, 2, d);
    const auto at = [&](int a, int b) { return d[Surface::derivativeIndex(a, b, 2)]; };
    const Vec3 su = at(1, 0), sv = at(0, 1), m = cross(su, sv);
    const double length = norm(m);
    if (!(length > 1e-15)) throw std::domain_error("singular surface");
    const Vec3 n = m / length;
    const Vec3 mu = cross(at(2, 0), sv) + cross(su, at(1, 1));
    const Vec3 mv = cross(at(1, 1), sv) + cross(su, at(0, 2));
    const double r = face.sense ? radius : -radius;
    return {at(0, 0), at(0, 0) - r * n,
            su - r * (mu - dot(n, mu) * n) / length,
            sv - r * (mv - dot(n, mv) * n) / length};
}

// I semi casuali cercano radici diverse. Il residuo e' sempre calcolato
// sulle superfici originali: nessuna tangente viene inventata o rilassata.
bool solve(const Face &face, double radius, const Vec3 &origin, const Vec3 &normal,
           const Vec3 &side, Vec2 &uv, Contact &out) {
    const Interval ur = face.surface->uDomain(), vr = face.surface->vDomain();
    for (int iteration = 0; iteration < 60; ++iteration) {
        const Contact c = contact(face, uv.x(), uv.y(), radius);
        const Vec3 delta = c.center - origin;
        const double f = dot(delta, normal), g = dot(delta, side);
        const double error = std::hypot(f, g);
        if (error < 1e-8) { out = c; return true; }
        const double a = dot(c.du, normal), b = dot(c.dv, normal);
        const double e = dot(c.du, side), d = dot(c.dv, side);
        const double det = a * d - b * e;
        if (std::fabs(det) <= 1e-14 * (std::fabs(a * d) + std::fabs(b * e) + 1e-30)) return false;
        const double du = (-f * d + b * g) / det, dv = (e * f - a * g) / det;
        bool improved = false;
        for (double step = 1.0; step >= 1.0 / 1024.0; step *= 0.5) {
            const Vec2 trial(std::clamp(uv.x() + step * du, ur.lo, ur.hi),
                             std::clamp(uv.y() + step * dv, vr.lo, vr.hi));
            const Vec3 q = contact(face, trial.x(), trial.y(), radius).center - origin;
            if (std::hypot(dot(q, normal), dot(q, side)) < error) {
                uv = trial; improved = true; break;
            }
        }
        if (!improved) return false;
    }
    return false;
}

// Centro C=origin+rho*direction, punto Q=E(t): |C-Q|=R e
// (C-Q).T=0. La seconda equazione impone il contatto con l'interno
// dello spigolo, non una tangenza inventata alle facce adiacenti.
bool solveEdge(const Curve<3> &curve, const Interval &range, double radius,
               const Vec3 &origin, const Vec3 &direction, double &t, double &rho,
               Contact &out) {
    const auto values = [&](double parameter, double radial, double &f, double &g,
                            Vec3 &v, Vec3 &tangent, Vec3 &dt, double &speed) {
        Vec3 d[3];
        curve.evaluate(parameter, 2, d);
        speed = norm(d[1]);
        if (!(speed > 1e-15)) return false;
        tangent = d[1] / speed;
        dt = (d[2] - dot(tangent, d[2]) * tangent) / speed;
        v = origin + radial * direction - d[0];
        f = norm(v) - radius;
        g = dot(v, tangent);
        return isFinite(v) && std::isfinite(f) && std::isfinite(g);
    };
    for (int iteration = 0; iteration < 60; ++iteration) {
        double f, g, speed;
        Vec3 v, tangent, dt;
        if (!values(t, rho, f, g, v, tangent, dt, speed)) return false;
        const double error = std::hypot(f, g), length = norm(v);
        if (error < 1e-8) {
            if (!(rho > 0) || !(t > range.lo && t < range.hi)) return false;
            out.point = curve.point(t);
            out.center = origin + rho * direction;
            return true;
        }
        if (!(length > 1e-15)) return false;
        const double a = -dot(v, tangent) * speed / length, b = dot(v, direction) / length;
        const double c = -speed + dot(v, dt), d = dot(direction, tangent);
        const double det = a * d - b * c;
        if (std::fabs(det) <= 1e-14 * (std::fabs(a * d) + std::fabs(b * c) + 1e-30)) return false;
        const double stepT = (-f * d + b * g) / det, stepR = (c * f - a * g) / det;
        bool improved = false;
        for (double step = 1; step >= 1.0 / 1024; step *= 0.5) {
            const double trialT = std::clamp(t + step * stepT, range.lo, range.hi);
            const double trialR = std::max(0.0, rho + step * stepR);
            double tf, tg, ts;
            Vec3 tv, tt, td;
            if (values(trialT, trialR, tf, tg, tv, tt, td, ts) && std::hypot(tf, tg) < error) {
                t = trialT; rho = trialR; improved = true; break;
            }
        }
        if (!improved) return false;
    }
    return false;
}

// Un minimo sul supporto esteso fuori dal trim richiede un controllo separato
// del dominio rifilato. Se la suddivisione non lo risolve, rimane Unknown.
enum class SphereCheck { Clear, Collision, Unknown };
SphereCheck checkSphere(const Body &body, const SolidClassifier &classifier,
                       const Vec3 &center, double radius, double &penetration, int &hitFace) {
    constexpr double tolerance = 1e-7;
    if (classifier.classify(center) != PointLocation::Inside) return SphereCheck::Collision;
    bool unknown = false;
    for (FaceId id : body.faces()) {
        const auto p = projectPoint(*body.face(id).surface, center);
        if (p.distance >= radius - tolerance) continue;
        double distance = p.distance;
        if (classifyPointOnFace(body, id, p.point, tolerance) == PointLocation::Outside) {
            distance = distanceToFaceBoundary(body, id, center);
            if (distance >= radius - tolerance) {
                double depth = 0;
                const int checked = Probe::sphereOutsideFace(body, id, center, radius, tolerance, depth);
                if (checked == 1) continue;
                if (checked == 0 && depth > penetration) { penetration = depth; hitFace = id.index; }
                unknown |= checked < 0;
            }
        }
        if (radius - distance > std::max(tolerance, penetration)) {
            penetration = radius - distance;
            hitFace = id.index;
        }
    }
    if (penetration > tolerance) return SphereCheck::Collision;
    return unknown ? SphereCheck::Unknown : SphereCheck::Clear;
}
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            const Line<3> edge(Vec3(5, 0, 0), Vec3(0, 1, 0));
            for (double expected : {4.0, 6.0}) {
                double t = 0.2, rho = expected + 0.15;
                Contact c;
                if (!solveEdge(edge, {0, 3}, 1, Vec3(0, 1, 0), Vec3(1, 0, 0), t, rho, c)
                    || std::fabs(t - 1) > 1e-7 || std::fabs(rho - expected) > 1e-7)
                    throw std::runtime_error("edge contact self-test failed");
            }
            double t = 0.2, rho = 4;
            Contact c;
            if (solveEdge(edge, {0, 0.5}, 1, Vec3(0, 1, 0), Vec3(1, 0, 0), t, rho, c))
                throw std::runtime_error("contact outside trimmed edge accepted");
            const Circle<3> circle(Vec3(5, 1, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5);
            t = 3.0; rho = 3.4;
            if (!solveEdge(circle, {0, kTwoPi}, 1, Vec3(0, 1, 0), Vec3(1, 0, 0), t, rho, c)
                || std::fabs(t - kPi) > 1e-7 || std::fabs(rho - 3.5) > 1e-7)
                throw std::runtime_error("curved edge contact self-test failed");
            const std::vector<Vec3> corners{Vec3(0,0,0),Vec3(1,0,0),Vec3(1,1,0),Vec3(0,1,0)};
            std::vector<Body::BuildEdge> edges;
            Body::BuildFace face;
            face.surface = std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,1,1},
                std::vector<double>{0,0,1,1},2,2,
                std::vector<Vec3>{Vec3(-2,-2,0),Vec3(-2,2,0),Vec3(2,-2,0),Vec3(2,2,0)});
            face.sense=true; face.loops.resize(1);
            for(int i=0;i<4;++i) {
                const int next=(i+1)%4;
                edges.push_back({i,next,std::make_shared<Line<3>>(corners[i],corners[next]-corners[i]),{0,1},0});
                face.loops[0].push_back({i,true,nullptr,0});
            }
            Body sheet=Body::buildSheet(corners,edges,{face});
            computePCurves(sheet);
            double penetration=0;
            if(Probe::sphereOutsideFace(sheet,FaceId(0),Vec3(-0.1,0.5,0.2),0.2,1e-7,penetration)!=1)
                throw std::runtime_error("extended support mistaken for trimmed face");
            if(Probe::sphereOutsideFace(sheet,FaceId(0),Vec3(-0.1,0.5,0.2),0.3,1e-7,penetration)!=0)
                throw std::runtime_error("trimmed face collision missed");
            penetration=0;
            if(Probe::sphereOutsideFace(sheet,FaceId(0),Vec3(-0.1,0.5,0.2),std::sqrt(0.05),1e-7,penetration)!=1)
                throw std::runtime_error("tangent sphere mistaken for penetration");
            // Foro nel dominio: il minimo sul piano cade nel vuoto, ma una
            // sfera piu' grande deve comunque trovare il materiale circostante.
            std::vector<Vec3> holedCorners=corners;
            holedCorners.insert(holedCorners.end(),{Vec3(0.25,0.25,0),Vec3(0.25,0.75,0),
                Vec3(0.75,0.75,0),Vec3(0.75,0.25,0)});
            face.loops.resize(2);
            for(int i=4;i<8;++i) {
                const int next=4+(i-3)%4;
                edges.push_back({i,next,std::make_shared<Line<3>>(holedCorners[i],holedCorners[next]-holedCorners[i]),{0,1},0});
                face.loops[1].push_back({i,true,nullptr,0});
            }
            Body holed=Body::buildSheet(holedCorners,edges,{face});
            computePCurves(holed);
            if(Probe::sphereOutsideFace(holed,FaceId(0),Vec3(0.5,0.5,0.1),0.2,1e-7,penetration)!=1)
                throw std::runtime_error("hole in trimmed face ignored");
            if(Probe::sphereOutsideFace(holed,FaceId(0),Vec3(0.5,0.5,0.1),0.3,1e-7,penetration)!=0)
                throw std::runtime_error("collision around hole missed");
            if(Probe::sphereOutsideFace(sheet,FaceId(0),Vec3(),-1,1e-7,penetration)!=-1)
                throw std::runtime_error("invalid sphere accepted");
            std::cout << "PASS: analytic line/circle contacts, finite-edge rejection, trimmed-surface distance bounds, tangency and holes\n";
            return 0;
        }
        if (argc < 6 || (argc > 8 && argc != 10)) throw std::invalid_argument("usage: probe file.body capFace radius seedsPerFace randomSeed [seamHalfSamples] [sampleIndex] [transitionFace transitionEdge]");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::invalid_argument("cannot open body");
        std::stringstream data; data << input.rdbuf();
        Body body = readBodyBinary(data.str());
        computePCurves(body);
        const FaceId cap(std::stoi(argv[2]));
        const double radius = std::stod(argv[3]);
        const int attempts = std::stoi(argv[4]);
        const int refinement = argc >= 7 ? std::stoi(argv[6]) : 0;
        const int onlySample = argc >= 8 ? std::stoi(argv[7]) : -1;
        if (onlySample < -1) throw std::invalid_argument("invalid sample index");
        if (refinement < 0 || refinement > 1000) throw std::invalid_argument("invalid refinement");
        if (!(radius > 0) || !std::isfinite(radius) || attempts < 1 || attempts > 1024)
            throw std::invalid_argument("invalid radius or seed count");
        const auto ids = body.faces();
        if (std::find(ids.begin(), ids.end(), cap) == ids.end()) throw std::invalid_argument("invalid face");
        const Face &capFace = body.face(cap);
        if (capFace.surface->type() != SurfaceType::Plane) throw std::invalid_argument("cap must be planar");
        const auto &frame = static_cast<const Plane &>(*capFace.surface).frame();
        const Vec3 normal = capFace.sense ? frame.zDir() : -frame.zDir();
        std::set<int> adjacent;
        Vec3 average;
        int vertices = 0;
        for (LoopId loop : capFace.loops) for (FinId fin : body.loopFins(loop)) {
            average += body.vertex(body.finStart(fin)).point;
            ++vertices;
            const FinId other = body.otherFin(fin);
            if (other.valid()) adjacent.insert(body.finFace(other).index);
        }
        if (!vertices) throw std::invalid_argument("empty cap");
        // Origine interna alla calotta solo come riferimento per le sezioni radiali.
        // La diagnostica non presume che ogni calotta sia stellata rispetto ad essa.
        const Vec3 origin = average / double(vertices) - radius * normal;
        std::mt19937 random(std::stoul(argv[5]));
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        std::map<int, std::vector<Vec2>> starts;
        for (int id : adjacent) {
            const Surface &s = *body.face(FaceId(id)).surface;
            if (!s.uDomain().isFinite() || !s.vDomain().isFinite())
                throw std::invalid_argument("probe requires finite side domains");
            for (int k = 0; k < attempts; ++k)
                starts[id].emplace_back(s.uDomain().lo + uniform(random) * s.uDomain().length(),
                                        s.vDomain().lo + uniform(random) * s.vDomain().length());
        }
        // Giro uniforme e campioni fitti attorno alle direzioni delle cuciture.
        std::vector<double> angles;
        for (int i = 0; i < 64; ++i) angles.push_back(kTwoPi * i / 64.0);
        for (LoopId loop : capFace.loops) for (FinId fin : body.loopFins(loop)) {
            const Vec3 p = body.vertex(body.finStart(fin)).point - origin;
            const double angle = std::atan2(dot(p, frame.yDir()), dot(p, frame.xDir()));
            if (refinement) {
                for (int k = -refinement; k <= refinement; ++k)
                    angles.push_back(angle + 0.06 * k / refinement);
            } else {
                for (double offset : {-0.02, -0.005, -0.001, 0.0, 0.001, 0.005, 0.02})
                    angles.push_back(angle + offset);
            }
        }
        if (argc == 10) {
            if (onlySample < 0 || onlySample + 1 >= int(angles.size())) throw std::invalid_argument("invalid transition bracket");
            const FaceId support(std::stoi(argv[8]));
            const EdgeId seam(std::stoi(argv[9]));
            if (!adjacent.count(support.index)) throw std::invalid_argument("transition face not adjacent to cap");
            const auto allEdges=body.edges();
            if(std::find(allEdges.begin(),allEdges.end(),seam)==allEdges.end()) throw std::invalid_argument("invalid transition edge");
            const Edge &edge=body.edge(seam);
            Contact onFace,onEdge;
            double parameter=0;
            Vec2 uv;
            const auto gapAt=[&](double angle) {
                const Vec3 direction=std::cos(angle)*frame.xDir()+std::sin(angle)*frame.yDir();
                const Vec3 side=cross(normal,direction);
                bool have=false;
                for(Vec2 seed:starts.at(support.index)) {
                    Contact candidate;
                    if(solve(body.face(support),radius,origin,normal,side,seed,candidate)
                       && dot(candidate.center-origin,direction)>0) { onFace=candidate; uv=seed; have=true; break; }
                }
                if(!have) throw std::domain_error("transition face branch not found");
                double best=1e300;
                for(int k=0;k<32;++k) for(double sign:{-1.0,1.0}) {
                    double t=edge.range.lo+edge.range.length()*(k+0.5)/32;
                    double rho=std::max(0.0,dot(edge.curve->point(t)-origin,direction)+sign*radius);
                    Contact candidate;
                    if(solveEdge(*edge.curve,edge.range,radius,origin,direction,t,rho,candidate)
                       && distance(candidate.center,onFace.center)<best) {
                        best=distance(candidate.center,onFace.center); onEdge=candidate; parameter=t;
                    }
                }
                if(best==1e300) throw std::domain_error("transition edge branch not found");
                return dot(onFace.center-onEdge.center,direction);
            };
            double a=angles[onlySample], b=angles[onlySample+1];
            double fa=gapAt(a),fb=gapAt(b);
            std::cerr<<std::setprecision(17)<<"bracket "<<a<<' '<<b<<" gap "<<fa<<' '<<fb<<'\n';
            if(fa*fb>0) throw std::domain_error("no sign change: transition not bracketed");
            for(int i=0;i<60;++i) {
                const double m=0.5*(a+b),f=gapAt(m);
                if(std::fabs(f)<1e-11) { a=b=m; break; }
                if(fa*f<=0) b=m; else { a=m;fa=f; }
            }
            const double angle=0.5*(a+b);
            gapAt(angle);
            const SolidClassifier classifier(body,1e-7);
            double penetration=0;
            int hitFace=-1;
            const SphereCheck sphere=checkSphere(body,classifier,onEdge.center,radius,penetration,hitFace);
            const auto location=classifyPointOnFace(body,support,onFace.point,1e-7);
            std::cout<<std::setprecision(17)<<"transition angle="<<angle<<" centerGap="<<distance(onFace.center,onEdge.center)
                     <<" edgeParameter="<<parameter<<" faceUV="<<uv.x()<<','<<uv.y()
                     <<" contactSeparation="<<distance(onFace.point,onEdge.point)
                     <<" faceLocation="<<int(location)
                     <<" sphere="<<(sphere==SphereCheck::Clear?"clear":sphere==SphereCheck::Collision?"collision":"unknown")
                     <<" penetration="<<penetration<<" hitFace="<<hitFace<<'\n';
            // La sola coincidenza radiale non convalida la transizione.
            return distance(onFace.center,onEdge.center)<=1e-7 && location!=PointLocation::Outside
                && sphere==SphereCheck::Clear ? 0 : 2;
        }
        const SolidClassifier classifier(body, 1e-7);
        if (onlySample >= int(angles.size())) throw std::invalid_argument("sample index out of range");
        std::map<int, int> accepted;
        int missing = 0, withoutClear = 0, ambiguousClear = 0, total = 0, errors = 0, visited = 0;
        int clear = 0, collisions = 0, unknown = 0, edgeCandidates = 0;
        double maxResidual = 0;
        std::cout << std::setprecision(17)
                  << "sample,angle,face,u,v,cx,cy,cz,px,py,pz,residual,sphere,penetration,hitFace,support,edge\n";
        for (std::size_t sample = 0; sample < angles.size(); ++sample) {
            if (onlySample >= 0 && int(sample) != onlySample) continue;
            ++visited;
            const double angle = angles[sample];
            const Vec3 direction = std::cos(angle) * frame.xDir() + std::sin(angle) * frame.yDir();
            const Vec3 side = cross(normal, direction);
            int found = 0;
            std::vector<Vec3> clearCenters;
            const auto recordClear = [&](const Vec3 &center) {
                for (const auto &previous : clearCenters)
                    if (distance(center,previous)<1e-6) return;
                clearCenters.push_back(center);
            };
            bool faceCandidateClear = false;
            for (int id : adjacent) {
                const Face &face = body.face(FaceId(id));
                std::vector<Contact> roots;
                for (Vec2 uv : starts[id]) {
                    try {
                        Contact c;
                        if (!solve(face, radius, origin, normal, side, uv, c)) continue;
                        if (dot(c.center - origin, direction) <= 0) continue;
                        bool duplicate = false;
                        for (const Contact &r : roots) duplicate |= distance(c.point, r.point) < 1e-6;
                        if (duplicate) continue;
                        roots.push_back(c);
                        if (classifyPointOnFace(body, FaceId(id), c.point, 1e-7) == PointLocation::Outside) continue;
                        const Vec3 onCap = c.center + radius * normal;
                        if (classifyPointOnFace(body, cap, onCap, 1e-7) == PointLocation::Outside) continue;
                        const double residual = std::max({std::fabs(dot(c.center - origin, normal)),
                            std::fabs(dot(c.center - origin, side)), std::fabs(distance(c.center, c.point) - radius)});
                        maxResidual = std::max(maxResidual, residual);
                        ++accepted[id]; ++total; ++found;
                        SphereCheck check = SphereCheck::Unknown;
                        double penetration = 0;
                        int hitFace = -1;
                        try { check = checkSphere(body, classifier, c.center, radius, penetration, hitFace); }
                        catch (const std::domain_error &) { ++errors; }
                        const char *status = "unknown";
                        if (check == SphereCheck::Clear) { ++clear; status = "clear"; recordClear(c.center); }
                        else if (check == SphereCheck::Collision) { ++collisions; status = "collision"; }
                        else ++unknown;
                        faceCandidateClear |= check == SphereCheck::Clear;
                        std::cout << sample << ',' << angle << ',' << id << ',' << uv.x() << ',' << uv.y()
                                  << ',' << c.center.x() << ',' << c.center.y() << ',' << c.center.z()
                                  << ',' << c.point.x() << ',' << c.point.y() << ',' << c.point.z()
                                  << ',' << residual << ',' << status << ',' << penetration << ',' << hitFace << ",face,-1\n";
                    } catch (const std::domain_error &) { ++errors; }
                }
            }
            // Cerca anche quando i contatti sulle facce sono in collisione o incerti:
            // un ramo sfera-spigolo puo' proseguire oltre una faccia sottile.
            // Esamina le cuciture fra i fianchi, conservando anche i candidati
            // respinti dal controllo di collisione per rendere visibile l'esito.
            if (!faceCandidateClear) for (EdgeId id : body.edges()) {
                const Edge &edge = body.edge(id);
                if (!edge.forward.valid() || !edge.backward.valid() || !edge.curve) continue;
                if (!adjacent.count(body.finFace(edge.forward).index)
                    || !adjacent.count(body.finFace(edge.backward).index)) continue;
                std::vector<Contact> roots;
                for (int k = 0; k < 32; ++k) for (double sign : {-1.0, 1.0}) {
                    try {
                        double t = edge.range.lo + edge.range.length() * (k + 0.5) / 32;
                        double rho = std::max(0.0, dot(edge.curve->point(t) - origin, direction) + sign * radius);
                        Contact c;
                        if (!solveEdge(*edge.curve, edge.range, radius, origin, direction, t, rho, c)) continue;
                        bool duplicate = false;
                        for (const auto &r : roots) duplicate |= distance(c.center, r.center) < 1e-6;
                        if (duplicate) continue;
                        roots.push_back(c);
                        if (classifyPointOnFace(body, cap, c.center + radius * normal, 1e-7) == PointLocation::Outside) continue;
                        const double residual = std::max(std::fabs(distance(c.center, c.point) - radius),
                            std::fabs(dot(c.center - c.point, normalized(edge.curve->derivative(t)))));
                        double penetration = 0;
                        int hitFace = -1;
                        SphereCheck check = SphereCheck::Unknown;
                        try { check = checkSphere(body, classifier, c.center, radius, penetration, hitFace); }
                        catch (const std::domain_error &) { ++errors; }
                        const char *status = "unknown";
                        if (check == SphereCheck::Clear) { ++clear; status = "clear"; recordClear(c.center); }
                        else if (check == SphereCheck::Collision) { ++collisions; status = "collision"; }
                        else ++unknown;
                        maxResidual = std::max(maxResidual, residual);
                        ++found; ++total; ++edgeCandidates;
                        std::cout << sample << ',' << angle << ",-1," << t << ',' << rho
                                  << ',' << c.center.x() << ',' << c.center.y() << ',' << c.center.z()
                                  << ',' << c.point.x() << ',' << c.point.y() << ',' << c.point.z()
                                  << ',' << residual << ',' << status << ',' << penetration << ',' << hitFace << ",edge," << id.index << '\n';
                    } catch (const std::domain_error &) { ++errors; }
                }
            }
            if (!found) ++missing;
            if (clearCenters.empty()) ++withoutClear;
            if (clearCenters.size()>1) ++ambiguousClear;
        }
        std::cerr << std::setprecision(12) << "samples=" << visited << " candidates=" << total
                  << " withoutCandidate=" << missing << " withoutClear=" << withoutClear
                  << " ambiguousClear=" << ambiguousClear << " numericalExceptions=" << errors
                  << " maxResidual=" << maxResidual << " clear=" << clear
                  << " collisions=" << collisions << " unknown=" << unknown << " edgeCandidates=" << edgeCandidates << '\n';
        for (int id : adjacent) std::cerr << "face=" << id << " candidates=" << accepted[id] << '\n';
        std::cerr << "LIMIT: discrete candidates only; no complete branch tracking, trim curves or output B-rep. Unknown distances are not certified.\n";
        return withoutClear || ambiguousClear || errors ? 2 : 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
