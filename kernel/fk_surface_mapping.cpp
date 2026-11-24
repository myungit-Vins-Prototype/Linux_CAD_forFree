#include "fk_surface_mapping.h"
#include "fk_mapping_carrier.h"
#include "fk_mapping_fit.h"
#include "fk_mapping_trim_witness.h"
#include "fk_quilt_mapping.h"
#include "fk_bspline_surface.h"
#include "fk_body_check.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_classify.h"
#include "fk_classify_uv.h"
#include "fk_curve_surface.h"
#include "fk_sew.h"
#include "fk_transform.h"
#include "fk_curve_algo.h"
#include "fk_curve_fit_bounds.h"
#include "fk_surface_curve_bounds.h"
#include "fk_surface_mapping_bounds.h"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <limits>
#include <set>
#include <tuple>
#include <stdexcept>
#include <sstream>
#include <iomanip>
namespace ForgeCad::Kernel {
namespace {
void require(bool value, const char *message) { if (!value) throw std::domain_error(message); }
void checked(const Body &body) {
    const auto issues = checkBody(body, {true, true});
    if (!issues.empty()) throw std::domain_error("Mappatura: " + describe(issues.front().code) + ": " + issues.front().message);
}
CurvePtr<2> basis2(CurvePtr<2> curve) {
    while (curve && curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<2> &>(*curve).basis();
    return curve;
}
struct FlatEdge { CurvePtr<3> curve; Interval range; bool reverse = false; };
FlatEdge lineImage(const Vec3 &a, const Vec3 &b) {
    const double length = distance(a, b);
    require(length > linearResolution(), "Sviluppo: bordo degenere o vertice del cono non gestito.");
    return {std::make_shared<Line<3>>(a, (b - a) / length), {0, length}, false};
}
CurvePtr<3> affineImage(const CurvePtr<2> &input, const Interval &range, const Frame3 &frame, double scaleX) {
    const auto curve = basis2(input);
    const auto point = [&](const Vec2 &p) { return frame.toGlobal(Vec3(scaleX * p.x(), p.y(), 0)); };
    if (curve->type() == CurveType::Line) {
        return std::make_shared<BSplineCurve<3>>(1, std::vector<double>{range.lo,range.lo,range.hi,range.hi},
            std::vector<Vec3>{point(curve->point(range.lo)),point(curve->point(range.hi))});
    }
    std::shared_ptr<BSplineCurve<2>> converted;
    const BSplineCurve<2> *spline = nullptr;
    if (curve->type() == CurveType::BSpline) spline = &static_cast<const BSplineCurve<2> &>(*curve);
    else if (curve->type() == CurveType::Circle) converted = std::make_shared<BSplineCurve<2>>(toBSpline(static_cast<const Circle<2> &>(*curve),range.lo,range.hi));
    else if (curve->type() == CurveType::Ellipse) converted = std::make_shared<BSplineCurve<2>>(toBSpline(static_cast<const Ellipse<2> &>(*curve),range.lo,range.hi));
    if (converted) spline = converted.get();
    require(spline, "Sviluppo: il bordo parametrico non ha una forma esatta supportata.");
    std::vector<Vec3> poles; for (const auto &p : spline->poles()) poles.push_back(point(p));
    return std::make_shared<BSplineCurve<3>>(spline->degree(),spline->knots(),poles,spline->weights());
}
BodyHistory mappedHistory(const Body &source, const Body &result) {
    auto h = makeHistory({&source}, result);
    const auto assign = [&](EntityKind kind, const auto &from, const auto &to) {
        require(from.size()==to.size(),"Mappatura: numero di entita' non conservato.");
        for (std::size_t i=0;i<to.size();++i) h.declareOrigins(kind,to[i].index,{{0,kind,from[i].index,HistoryRelation::Modified}});
    };
    assign(EntityKind::Face,source.faces(),result.faces()); assign(EntityKind::Edge,source.edges(),result.edges());
    assign(EntityKind::Vertex,source.vertices(),result.vertices()); return h;
}
std::pair<Interval,Interval> faceWindow(const Body &body, FaceId f) {
    Box box;
    const Frame3 xy;
    for (auto loop:body.face(f).loops) for (auto fin:body.loopFins(loop)) {
        const auto &edge=body.edge(body.fin(fin).edge); const auto &pc=body.fin(fin).pcurve;
        require(bool(pc),"Mappatura: SP-curve sorgente mancante.");
        box.add(curveBox(*embedCurve(pc,xy),edge.range));
    }
    require(!box.isEmpty() && box.hi.x()>box.lo.x() && box.hi.y()>box.lo.y(),"Mappatura: dominio di faccia non finito o degenere.");
    return {{box.lo.x(),box.hi.x()},{box.lo.y(),box.hi.y()}};
}
SurfacePtr fitMappedSurface(const Surface &source, Interval uw, Interval vw,
                           const std::function<Vec3(const Vec3 &)> &map,double tolerance,double &deviation,const std::vector<Plane> &cuts,double derivativeFraction,int degree) {
    auto us=source.uBreakpoints(uw),vs=source.vBreakpoints(vw);
    const bool affine=source.type()==SurfaceType::Plane||(source.type()==SurfaceType::Extrusion&&static_cast<const ExtrusionSurface &>(source).curve()->type()==CurveType::Line);
    if(affine) {
        Vec3 d[4];source.evaluate(0,0,1,d);
        const auto insert=[](std::vector<double> &grid,double value){if(value>grid.front()&&value<grid.back()){grid.push_back(value);std::sort(grid.begin(),grid.end());grid.erase(std::unique(grid.begin(),grid.end()),grid.end());}};
        for(const auto &cut:cuts) {
            const auto &f=cut.frame();const double a=dot(f.zDir(),d[2]),b=dot(f.zDir(),d[1]),c=dot(f.zDir(),d[0]-f.origin());
            if(std::fabs(b)<=angularResolution()&&std::fabs(a)>angularResolution())insert(us,-c/a);
            if(std::fabs(a)<=angularResolution()&&std::fabs(b)>angularResolution())insert(vs,-c/b);
        }
    }
    return detail::fitMappingSurface(std::move(us),std::move(vs),
        [&](double u,double v){return map(source.point(u,v));},{},tolerance,deviation,derivativeFraction,degree);
}
// Il punto di appoggio e' il centro dell'ingombro esatto nel piano
// sorgente, a quota zero. Lo spessore resta sullo stesso lato del piano.
Body placeRigidBody(const Body &source,const SurfaceWrapOptions &options,
                    const std::function<Frame3(const Vec2 &)> &placement,
                    BodyHistory *history,double *deviation) {
    Box bounds;const auto local=Transform3::fromFrame(options.sourcePlane).inverted();
    for(auto id:source.edges()) {
        const auto &edge=source.edge(id);TransformedCurve curve(edge.curve,local);
        bounds.add(curveBox(curve,edge.range));
    }
    require(!bounds.isEmpty(),"Posizionamento: sorgente senza geometria.");
    const Vec2 center(.5*bounds.lo.x()+.5*bounds.hi.x(),.5*bounds.lo.y()+.5*bounds.hi.y());
    const auto target=placement(center);
    const auto sourceFrame=Frame3(options.sourcePlane.toGlobal(Vec3(center.x(),center.y(),0)),
                                  options.sourcePlane.zDir(),options.sourcePlane.xDir());
    auto result=transformBody(source,Transform3::fromFrame(target)*Transform3::fromFrame(sourceFrame).inverted(),history);
    checked(result);if(deviation)*deviation=0.;return result;
}
// A continuous certificate and any directly measured residual use the same
// physical budget. Only a certificate enables the whole-carrier postcondition.
using CarrierCheck = std::function<detail::SurfaceMappingBoundResult(
    FaceId, const Surface &, Interval, Interval, double, double &)>;

Body mapBody(const Body &source,const std::function<Vec3(const Vec3 &)> &map,double tolerance,
             BodyHistory *history,double *deviation,const std::function<Vec3(const Vec3 &)> &carrierMap={},const std::vector<Plane> &cuts={},double derivativeFraction=.001,int degree=3,const detail::MappingFaceFunction &faceImage={},
             const std::function<std::optional<Body>()> &carrierBody={},
             const CarrierCheck &carrierProof={}) {
    // Guardie locali sul volume sorgente; le degenerazioni non diventano
    // corpi validi soltanto perche' il fit riesce.
    Box box;for(auto e:source.edges())box.add(curveBox(*source.edge(e).curve,source.edge(e).range));
    require(!box.isEmpty(),"Deformazione: corpo vuoto.");
    const auto &differentialMap=carrierMap?carrierMap:map;
    std::unique_ptr<SolidClassifier> material;
    std::vector<std::pair<SurfacePtr,FaceClassifier>> sheetFaces;
    if(carrierMap) {
        if(!source.isSheet())material=std::make_unique<SolidClassifier>(source,linearResolution(),true);
        else for(auto id:source.faces())sheetFaces.emplace_back(source.face(id).surface,FaceClassifier(source,id,linearResolution()));
    }
    const double h=1e-5*std::max(1.,box.diagonal());
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k) {
        Vec3 p(box.lo.x()+(box.hi.x()-box.lo.x())*i/2.,box.lo.y()+(box.hi.y()-box.lo.y())*j/2.,box.lo.z()+(box.hi.z()-box.lo.z())*k/2.);
        // I vertici del box possono essere fuori dal materiale (triangoli,
        // poligoni, fori). Non devono imporre un appoggio inesistente.
        if(material&&material->classify(p)==PointLocation::Outside)continue;
        if(!sheetFaces.empty()) {
            bool on=false;for(const auto &f:sheetFaces)if(projectPoint(*f.first,p).distance<=linearResolution()&&f.second.classify(p)!=PointLocation::Outside){on=true;break;}
            if(!on)continue;
        }
        const auto at=map(p);
        Vec3 axes[3];for(int a=0;a<3;++a){Vec3 q=p;const double step=p[a]+h<=box.hi[a]||box.hi[a]==box.lo[a]?h:-h;q[a]+=step;axes[a]=(differentialMap(q)-at)/step;}
        require(dot(cross(axes[0],axes[1]),axes[2])>1e-8,"Deformazione: inversione o collasso del corpo; ridurre spessore o estensione.");
    }
    if (carrierBody) {
        auto exact = carrierBody();
        if (exact)
            return std::move(*exact);
    }
    std::vector<Vec3> vertices;std::vector<Body::BuildEdge> edges;std::vector<Body::BuildFace> faces;
    // Il fit deve rispettare anche la connessione edge/superficie del B-rep,
    // che usa linearResolution() quando l'edge non e' tollerante. Il budget
    // dell'operazione non autorizza un edge lontano dalle facce adiacenti.
    const double connectionBudget=std::min(tolerance,linearResolution());
    const double fitBudget=.2*connectionBudget;
    std::map<int,int> vertexOf,edgeOf;double worst=0;
    // Cap fisico fissato prima del fit, distinto dall'errore misurato.
    const double pcurveBudget = .8 * connectionBudget;
    enum class FinProof { Legacy, Plane, Surface };
    struct FinContract {
        CurvePtr<3> curve;
        CurvePtr<2> pcurve;
        Interval range;
        double edgeTolerance;
        double pcurveTolerance;
        FinProof proof;
    };
    bool hasCertifiedFin = false, hasCertifiedCarrier = false;
    std::map<int, SurfacePtr> mappedImages;
    std::map<std::tuple<int, int, bool>, std::vector<FinContract>> finOrigins;
    for(auto v:source.vertices()){vertexOf[v.index]=int(vertices.size());vertices.push_back(map(source.vertex(v).point));}
    for(auto id:source.edges()) {
        cancellationPoint();const auto &edge=source.edge(id);double error=0;
        auto curve=fitCurve([&](double t){return map(edge.curve->point(t));},edge.range,edge.curve->breakpoints(edge.range),fitBudget,&error);
        worst=std::max(worst,error);edgeOf[id.index]=int(edges.size());
        const int a=vertexOf.at(source.edgeStart(id).index),b=vertexOf.at(source.edgeEnd(id).index);
        const double gap=std::max(distance(curve->point(edge.range.lo),vertices[a]),distance(curve->point(edge.range.hi),vertices[b]));
        require(gap<=fitBudget,"Deformazione: bordo sorgente non collegato ai vertici.");
        edges.push_back({a,b,curve,edge.range,gap>linearResolution()?gap*1.01:0.});
    }
    for(auto id:source.faces()) {
        const auto &face=source.face(id);const auto window=faceWindow(source,id);
        Body::BuildFace mapped;mapped.sense=face.sense;
        const auto image=faceImage?faceImage(id):detail::MappingFaceImage{};
        const bool planeImage = image.surface && image.surface->type() == SurfaceType::Plane;
        double faceDeviation = 0.;
        const auto fit = [&](double tolerance) {
            faceDeviation = 0.;
            try {
                return fitMappedSurface(*face.surface, window.first, window.second, differentialMap,
                    tolerance, faceDeviation, cuts, derivativeFraction, degree);
            } catch (const std::domain_error &e) {
                throw std::domain_error("Faccia F" + std::to_string(id.index) + ": " + e.what());
            }
        };
        if (image.surface) {
            mapped.surface = image.surface;
            mapped.sense = image.sense;
        } else {
            mapped.surface = fit(fitBudget);
        }
        if (carrierProof) {
            const auto verify = [&] {
                return carrierProof(id, *mapped.surface, window.first, window.second, fitBudget, faceDeviation);
            };
            auto proof = verify();
            if (proof.status != detail::SurfaceMappingBoundStatus::Unsupported) {
                // A sampled fit can stop just above the continuous budget.
                // Refit the candidate once with headroom, then prove it against
                // the original budget before recording any deviation.
                if (proof.status == detail::SurfaceMappingBoundStatus::Failed && !image.surface) {
                    auto refined = fit(.5 * fitBudget);
                    if (!sameSurfaceParameterization(*mapped.surface, *refined)) {
                        mapped.surface = std::move(refined);
                        proof = verify();
                    }
                }
                require(proof.status == detail::SurfaceMappingBoundStatus::Certified
                        && std::isfinite(proof.upperBound) && proof.upperBound >= 0
                        && proof.upperBound <= fitBudget,
                    "Deformazione: residuo globale del carrier fuori dal budget continuo.");
                faceDeviation = std::max(faceDeviation, proof.upperBound);
                hasCertifiedCarrier = true;
            }
        }
        worst = std::max(worst, faceDeviation);
        mappedImages.emplace(id.index, mapped.surface);
        for(auto loop:face.loops) {
            std::vector<Body::BuildFin> fins;
            for(auto use:source.loopFins(loop)) {
                const auto &fin=source.fin(use);const auto &edge=edges[edgeOf.at(fin.edge.index)];auto pc=fin.pcurve;
                double planeError = 0;
                if (planeImage) {
                    // Parametro originale del bordo effettivamente emesso.
                    // Il provider conserva coefficienti e supporto originali;
                    // il residuo fisico continuo include lo scarto normale.
                    pc = detail::fitPCurveOnPlane(edge.curve, mapped.surface, edge.range,
                        std::min(1e-10, .05 * fitBudget), pcurveBudget, &planeError);
                    require(bool(pc), "Deformazione: trasferimento SP-curve Plane non supportato.");
                } else if(image.surface) {
                    const auto &original=source.edge(fin.edge);
                    // Fit in UV seguito dalla verifica nello spazio fisico;
                    // il carrier e i suoi nodi non vengono riapprossimati.
                    double metric=1.;
                    for(int i=0;i<=16;++i) {
                        const auto uv=image.parameters(original.curve->point(original.range.lo+(original.range.hi-original.range.lo)*i/16.));
                        Vec3 d[4];image.surface->evaluate(uv.x(),uv.y(),1,d);metric=std::max({metric,norm(d[1]),norm(d[2])});
                    }
                    double uvError=0;
                    const auto curve=fitCurve([&](double t){const auto uv=image.parameters(original.curve->point(t));return Vec3(uv.x(),uv.y(),0);},original.range,
                        original.curve->breakpoints(original.range),std::min(1e-10,.05*fitBudget/metric),&uvError);
                    const auto planar=planarImage(*curve,original.range,Frame3());pc=planar.curve;
                    require(bool(pc),"Deformazione: trasferimento SP-curve non riuscito.");
                }
                FinProof proofKind = planeImage ? FinProof::Plane : FinProof::Legacy;
                double error = planeError;
                if (!planeImage) {
                    // Costruzione privata prima dell'UNICA prova; un Failed
                    // riconosciuto e' terminale e non torna ai campioni.
                    const auto candidate = detail::surfaceCurvePCurveCandidate(
                        edge.curve, mapped.surface, pc, edge.range);
                    const auto proof = detail::surfaceCurveResidualBound(
                        edge.curve, mapped.surface, candidate, edge.range, pcurveBudget);
                    if (proof.status == detail::SurfaceCurveBoundStatus::Unsupported) {
                        error = pcurveDeviation(*mapped.surface, *edge.curve, *pc, edge.range, 32);
                    } else {
                        require(proof.status == detail::SurfaceCurveBoundStatus::Certified
                                && std::isfinite(proof.upperBound) && proof.upperBound >= 0
                                && proof.upperBound <= pcurveBudget,
                            "Deformazione: SP-curve fuori dal budget continuo.");
                        pc = candidate;
                        error = proof.upperBound;
                        proofKind = FinProof::Surface;
                    }
                }
                require(error <= pcurveBudget, "Deformazione: SP-curve fuori tolleranza.");
                worst = std::max(worst, error);
                const double pcurveTolerance = proofKind == FinProof::Surface ? pcurveBudget :
                    (planeImage ? std::max(error, 1e-14) : std::max(error * 1.01, 1e-14));
                hasCertifiedFin = hasCertifiedFin || proofKind != FinProof::Legacy;
                finOrigins[{id.index, fin.edge.index, fin.sense}].push_back(
                    {edge.curve, pc, edge.range, edge.tolerance, pcurveTolerance, proofKind});
                fins.push_back({edgeOf.at(fin.edge.index),fin.sense,pc,pcurveTolerance});
            }
            mapped.loops.push_back(std::move(fins));
        }
        faces.push_back(std::move(mapped));
    }
    Body result=source.isSheet()?Body::buildSheet(vertices,edges,faces):Body::build(vertices,edges,faces);checked(result);
    BodyHistory preparedHistory;
    if (history || hasCertifiedFin || hasCertifiedCarrier) preparedHistory = mappedHistory(source, result);
    if (hasCertifiedFin || hasCertifiedCarrier) {
        const auto origin = [&](EntityKind kind, int index) {
            const auto &links = preparedHistory.origins(kind, index);
            require(preparedHistory.isDeclared(kind, index) && links.size() == 1
                    && links.front().operand == 0 && links.front().kind == kind
                    && links.front().relation == HistoryRelation::Modified,
                "Deformazione: genealogia del trasferimento incoerente.");
            return links.front().index;
        };
        // Anche senza history utente: identita' e cardinalita' delle vere fin
        // emesse da build/buildSheet, senza riassociazione per prossimita'.
        for (auto id : result.faces()) {
            cancellationPoint();
            const int sourceFace = origin(EntityKind::Face, id.index);
            const auto expected = mappedImages.find(sourceFace);
            const auto &face = result.face(id);
            require(expected != mappedImages.end() && face.surface == expected->second,
                "Deformazione: carrier modificato durante l'assemblaggio.");
            for (auto loop : face.loops) for (auto use : result.loopFins(loop)) {
                cancellationPoint();
                const auto &fin = result.fin(use);
                const auto &edge = result.edge(fin.edge);
                const int sourceEdge = origin(EntityKind::Edge, fin.edge.index);
                auto planned = finOrigins.find({sourceFace, sourceEdge, fin.sense});
                require(planned != finOrigins.end() && !planned->second.empty(),
                    "Deformazione: fin priva di origine dichiarata.");
                const auto contract = std::find_if(planned->second.begin(), planned->second.end(),
                    [&](const FinContract &entry) {
                        return entry.curve == edge.curve && entry.pcurve == fin.pcurve
                            && entry.range.lo == edge.range.lo && entry.range.hi == edge.range.hi
                            && entry.edgeTolerance == edge.tolerance
                            && entry.pcurveTolerance == fin.pcurveTolerance;
                    });
                require(contract != planned->second.end(),
                    "Deformazione: curva, parametro o budget modificato durante l'assemblaggio.");
                const FinProof kind = contract->proof;
                planned->second.erase(contract);
                const auto &original = source.edge(EdgeId(sourceEdge));
                require(edge.range.lo == original.range.lo && edge.range.hi == original.range.hi,
                    "Deformazione: parametro originale del bordo modificato.");
                if (kind == FinProof::Legacy) continue;
                require(std::isfinite(fin.pcurveTolerance) && fin.pcurveTolerance >= 0
                        && fin.pcurveTolerance <= pcurveBudget,
                    "Deformazione: budget finale della fin non valido.");
                double residual = 0;
                if (kind == FinProof::Plane) {
                    residual = detail::requirePCurveOnPlane(edge.curve, face.surface, fin.pcurve,
                        edge.range, std::min(pcurveBudget, fin.pcurveTolerance));
                } else {
                    const auto proof = detail::surfaceCurveResidualBound(edge.curve, face.surface,
                        fin.pcurve, edge.range, fin.pcurveTolerance);
                    require(proof.status == detail::SurfaceCurveBoundStatus::Certified
                            && std::isfinite(proof.upperBound) && proof.upperBound >= 0
                            && proof.upperBound <= fin.pcurveTolerance,
                        "Deformazione: fin finale fuori dal budget continuo.");
                    residual = proof.upperBound;
                }
                worst = std::max(worst, residual);
            }
        }
        for (const auto &planned : finOrigins)
            require(planned.second.empty(), "Deformazione: fin persa nell'assemblaggio.");
    }
    cancellationPoint();
    if(history)*history=std::move(preparedHistory);
    if(deviation)*deviation=worst;
    return result;
}

}
Body detail::mapBodyByFunction(const Body &source,const std::function<Vec3(const Vec3&)> &map,
    const std::function<Vec3(const Vec3&)> &carrierMap,double tolerance,BodyHistory *history,double *deviation,const std::vector<Plane> &cuts,double derivativeFraction,int degree,const MappingFaceFunction &faceImage) {
    return mapBody(source,map,tolerance,history,deviation,carrierMap,cuts,derivativeFraction,degree,faceImage);
}
Body unrollFace(const Body &source, FaceId id, const Frame3 &plane, BodyHistory *history) {
    require(source.contains(id),"Sviluppo: faccia sorgente non valida.");
    const auto &face=source.face(id);const auto type=face.surface->type();
    require(type==SurfaceType::Plane||type==SurfaceType::Cylinder||type==SurfaceType::Cone,
            "Sviluppo esatto disponibile per piani, cilindri e coni; il carrier selezionato non e' supportato.");
    const double radius=type==SurfaceType::Cylinder?static_cast<const CylindricalSurface&>(*face.surface).radius():1.;
    double sine=1,reference=0;
    if(type==SurfaceType::Cone){const auto&cone=static_cast<const ConicalSurface&>(*face.surface);sine=std::sin(cone.semiAngle());reference=cone.referenceRadius();}
    struct Use {CurvePtr<2> curve;Interval range;bool sense=true;int edge=-1;double budget=0;};
    std::vector<std::vector<Use>> loops;bool winding=false;
    for(auto loop:face.loops) {
        std::vector<Use> uses;
        for(auto id:source.loopFins(loop)) {
            const auto &fin=source.fin(id);const auto &edge=source.edge(fin.edge);
            require(bool(fin.pcurve),"Sviluppo esatto: SP-curve sorgente assente.");
            // Isometria esatta della curva di trim gia' rappresentata in UV.
            // Nessun nuovo fit: si trasporta il budget del B-rep sorgente,
            // incluse le connessioni tolleranti ai vertici del taglio.
            const double budget=std::max({linearResolution(),edge.tolerance,
                source.vertex(source.edgeStart(fin.edge)).tolerance,source.vertex(source.edgeEnd(fin.edge)).tolerance})+fin.pcurveTolerance;
            uses.push_back({fin.pcurve,edge.range,fin.sense,fin.edge.index,budget});
            const Vec2 a=fin.pcurve->point(edge.range.lo),b=fin.pcurve->point(edge.range.hi);
            winding=winding||(type!=SurfaceType::Plane&&distance(edge.curve->point(edge.range.lo),edge.curve->point(edge.range.hi))<=linearResolution()&&std::fabs(b.x()-a.x())>kPi);
        }
        loops.push_back(std::move(uses));
    }
    if(winding) {
        // Cilindro/cono completo senza cucitura topologica: i due anelli
        // diventano due lati e vengono collegati da due bordi del taglio.
        require(loops.size()==2&&loops[0].size()==1&&loops[1].size()==1,
                "Sviluppo: faccia periodica con fori o contorni complessi; dividere prima lungo una generatrice.");
        double heights[2],u0=0,u1=kTwoPi;
        for(int i=0;i<2;++i) {
            const auto &use=loops[i][0];const auto pc=basis2(use.curve);
            require(pc->type()==CurveType::Line,"Sviluppo periodico: contorno non isoparametrico.");
            const Vec2 a=pc->point(use.range.lo),b=pc->point(use.range.hi);
            require(std::fabs(a.y()-b.y())<1e-12&&std::fabs(std::fabs(a.x()-b.x())-kTwoPi)<1e-12,
                    "Sviluppo periodico: contorno non completo o non isoparametrico.");
            heights[i]=a.y();if(i==0){u0=std::min(a.x(),b.x());u1=u0+kTwoPi;}
        }
        const int bottom=heights[0]<heights[1]?0:1,top=1-bottom;
        std::vector<Vec2> corners={{u0,heights[bottom]},{u1,heights[bottom]},{u1,heights[top]},{u0,heights[top]}};
        std::vector<int> origins={loops[bottom][0].edge,-1,loops[top][0].edge,-1};
        if(!face.sense){std::reverse(corners.begin(),corners.end());origins={loops[top][0].edge,-1,loops[bottom][0].edge,-1};}
        std::vector<Use> opened;
        for(int i=0;i<4;++i){const Vec2 a=corners[i],b=corners[(i+1)%4];const double length=distance(a,b);require(length>0,"Sviluppo: altezza nulla.");opened.push_back({std::make_shared<Line<2>>(a,(b-a)/length),{0,length},true,origins[i]});}
        loops={opened};
    }
    std::vector<Vec3> points;std::vector<Body::BuildEdge> edges;Body::BuildFace flat;
    flat.surface=std::make_shared<Plane>(plane);flat.sense=face.sense;std::vector<int> edgeOrigins;std::vector<double> vertexBudgets;
    for(const auto &uses:loops) {
        require(!uses.empty(),"Sviluppo: contorno vuoto.");std::vector<Body::BuildFin> fins;const int first=int(points.size());
        for(std::size_t k=0;k<uses.size();++k) {
            cancellationPoint();const auto &use=uses[k];FlatEdge mapped;
            if(type!=SurfaceType::Cone){mapped.curve=affineImage(use.curve,use.range,plane,radius);mapped.range=use.range;}
            else {
                const auto pc=basis2(use.curve);require(pc->type()==CurveType::Line,"Sviluppo conico esatto: scegliere contorni isoparametrici.");
                const Vec2 a=pc->point(use.range.lo),b=pc->point(use.range.hi);
                const auto image=[&](Vec2 uv){const double r=reference/sine+uv.y(),angle=sine*uv.x();require(r*sine>linearResolution(),"Sviluppo: il contorno attraversa il vertice del cono.");return plane.toGlobal(Vec3(r*std::cos(angle),-r*std::sin(angle),0));};
                // Solo rumore entro gli arrotondamenti delle inversioni UV;
                // il criterio dipende dall'intero intervallo, non dalla pendenza.
                const double roundU=16*kMachineEpsilon*std::max({1.,std::fabs(a.x()),std::fabs(b.x())});
                const double roundV=16*kMachineEpsilon*std::max({1.,std::fabs(a.y()),std::fabs(b.y())});
                if(std::fabs(a.x()-b.x())<=roundU)mapped=lineImage(image(a),image(b));
                else {
                    require(std::fabs(a.y()-b.y())<=roundV,"Sviluppo conico esatto: bordo obliquo non isoparametrico.");
                    const double r=reference/sine+a.y(),sign=r>0?1.:-1.;
                    require(r*sine>linearResolution(),"Sviluppo: bordo sul vertice del cono.");
                    mapped.curve=std::make_shared<Circle<3>>(plane.origin(),sign*plane.xDir(),-sign*plane.yDir(),std::fabs(r));
                    const double lo=sine*a.x(),hi=sine*b.x();mapped.range={std::min(lo,hi),std::max(lo,hi)};mapped.reverse=hi<lo;
                }
            }
            const bool sense=use.sense!=mapped.reverse;const Vec3 start=mapped.curve->point(sense?mapped.range.lo:mapped.range.hi);
            const int vertex=int(points.size());points.push_back(start);
            const double budget=use.budget+uses[(k+1)%uses.size()].budget;
            vertexBudgets.push_back(use.budget+uses[(k+uses.size()-1)%uses.size()].budget);
            const int end=k+1==uses.size()?first:vertex+1;
            edges.push_back({sense?vertex:end,sense?end:vertex,mapped.curve,mapped.range,budget});edgeOrigins.push_back(use.edge);
            fins.push_back({int(edges.size()-1),sense,nullptr,0});
        }
        flat.loops.push_back(std::move(fins));
    }
    Body result=Body::buildSheet(points,edges,{flat});
    const auto vertices=result.vertices();for(std::size_t i=0;i<vertices.size();++i)result.vertex(vertices[i]).tolerance=vertexBudgets[i];
    require(computePCurves(result)==0,"Sviluppo: SP-curve piana non calcolabile.");checked(result);
    if(history) {
        *history=makeHistory({&source},result);
        history->declareOrigins(EntityKind::Face,result.faces().front().index,{{0,EntityKind::Face,id.index,HistoryRelation::Modified}});
        for(auto vertex:result.vertices())history->declareOrigins(EntityKind::Vertex,vertex.index,{{0,EntityKind::Face,id.index,HistoryRelation::Generated}});
        const auto es=result.edges();for(std::size_t i=0;i<es.size();++i)history->declareOrigins(EntityKind::Edge,es[i].index,
            edgeOrigins[i]>=0?std::vector<HistoryLink>{{0,EntityKind::Edge,edgeOrigins[i],HistoryRelation::Modified}}:std::vector<HistoryLink>{{0,EntityKind::Face,id.index,HistoryRelation::Generated}});
    }
    return result;
}
Body wrapBodyOnSurface(const Body &source, const Surface &target, const SurfaceWrapOptions &options, BodyHistory *history,double *deviation) {
    require(options.tolerance>=1e-9&&options.tolerance<=.01&&std::isfinite(options.tolerance),"Deformazione: tolleranza non valida.");
    Vec3 d[4];target.evaluate(options.anchor.x(),options.anchor.y(),1,d);
    const double scaleU=norm(d[2]),scaleV=norm(d[1]);require(scaleU>1e-12&&scaleV>1e-12&&norm(cross(d[2],d[1]))>1e-12*scaleU*scaleV,"Deformazione: punto di destinazione singolare.");
    const double c=std::cos(options.angle),s=std::sin(options.angle);
    const auto map=[&](const Vec3 &point) {
        cancellationPoint();const Vec3 p=options.sourcePlane.toLocal(point);
        const double x=c*p.x()-s*p.y()+options.shift.x(),y=s*p.x()+c*p.y()+options.shift.y();
        const double u=options.anchor.x()+x/scaleU,v=options.anchor.y()+y/scaleV;
        require((target.isUPeriodic()||target.uDomain().contains(u,detail::wrapTargetParameterSlack))&&(target.isVPeriodic()||target.vDomain().contains(v,detail::wrapTargetParameterSlack)),"Deformazione: corpo oltre il dominio della superficie destinazione.");
        return target.point(u,v)+(p.z()+options.normalOffset)*target.normal(u,v);
    };
    if(options.rigid)return placeRigidBody(source,options,[&](const Vec2 &center) {
        const double u=options.anchor.x()+(c*center.x()-s*center.y()+options.shift.x())/scaleU;
        const double v=options.anchor.y()+(s*center.x()+c*center.y()+options.shift.y())/scaleV;
        require((target.isUPeriodic()||target.uDomain().contains(u))&&(target.isVPeriodic()||target.vDomain().contains(v)),
                "Posizionamento: centro della sorgente oltre il dominio destinazione.");
        Vec3 derivatives[4];target.evaluate(u,v,1,derivatives);
        const auto normal=target.normal(u,v);
        const auto tangent=(c/scaleU)*derivatives[2]+(s/scaleV)*derivatives[1];
        return Frame3(derivatives[0]+options.normalOffset*normal,normal,tangent);
    },history,deviation);
    return mapBody(source,map,options.tolerance,history,deviation,{}, {},.001,3,{},[&] {
        return detail::wrapPlanarBodyOnCarrier(source,target,options,scaleU,scaleV,history,deviation);
    }, [&](FaceId face, const Surface &image, Interval u, Interval v, double budget, double &) {
        return detail::planarMappingResidualBound(*source.face(face).surface,
            target, options, scaleU, scaleV, image, u, v, budget);
    });
}
bool commonSurfaceWrapAnchor(const std::vector<SurfaceTargetFace> &targets,const Frame3 &plane,Vec2 &anchor) {
    if(targets.size()<2||!targets.front().body||!targets.front().body->contains(targets.front().face))return false;
    const auto &primary=targets.front();Box bounds;
    const auto projection=Transform3::projectionAlong(Vec3(0,0,1))*Transform3::fromFrame(plane).inverted();
    for(const auto &target:targets) {
        require(target.body&&target.body->contains(target.face),"Mappatura: faccia destinazione assente.");
        for(auto loop:target.body->face(target.face).loops)for(auto use:target.body->loopFins(loop)) {
            const auto &edge=target.body->edge(target.body->fin(use).edge);
            bounds.add(curveBox(TransformedCurve(edge.curve,projection),edge.range));
        }
    }
    if(bounds.isEmpty())return false;
    const auto center=.5*bounds.lo+.5*bounds.hi;double best=INFINITY;bool have=false;
    for(auto loop:primary.body->face(primary.face).loops)for(auto use:primary.body->loopFins(loop)) {
        const auto &fin=primary.body->fin(use);if(!fin.pcurve)continue;
        bool shared=false;
        for(std::size_t i=1;i<targets.size();++i)if(targets[i].body==primary.body)
            for(auto otherLoop:primary.body->face(targets[i].face).loops)for(auto otherUse:primary.body->loopFins(otherLoop))
                shared=shared||primary.body->fin(otherUse).edge==fin.edge;
        if(!shared)continue;
        const auto &edge=primary.body->edge(fin.edge);
        const auto closest=projectPoint(TransformedCurve(edge.curve,projection),center,edge.range);
        if(closest.distance<best){anchor=fin.pcurve->point(closest.parameter);best=closest.distance;have=true;}
    }
    return have;
}
using PointMap=std::function<Vec3(const Vec3 &)>;
using PlacementMap=std::function<Frame3(const Vec2 &)>;
// These projected boxes only suggest witnesses; no rejection relies on their
// conservativeness. A real contact must fail at a point of one source edge.
static std::optional<Interval> projectedBoxRange(const Box &box,const Vec3 &origin,
                                                 const Vec3 &axis,double physicalMargin) {
    if(box.isEmpty()||!isFinite(box.lo)||!isFinite(box.hi))return {};
    double lo=INFINITY,hi=-INFINITY,scale=1.;
    for(int corner=0;corner<8;++corner) {
        const Vec3 point(corner&1?box.hi.x():box.lo.x(),corner&2?box.hi.y():box.lo.y(),
                         corner&4?box.hi.z():box.lo.z());
        const double coordinate=dot(point-origin,axis);
        lo=std::min(lo,coordinate);hi=std::max(hi,coordinate);
        for(int k=0;k<3;++k)scale=std::max({scale,std::abs(point[k]),std::abs(origin[k])});
    }
    const double margin=physicalMargin+128.*std::numeric_limits<double>::epsilon()*scale;
    if(!std::isfinite(lo-margin)||!std::isfinite(hi+margin))return {};
    return Interval{lo-margin,hi+margin};
}
static std::vector<Interval> projectedGaps(const std::vector<Box> &boxes,const Vec3 &origin,
                                           const Vec3 &axis,double physicalMargin) {
    std::vector<Interval> coverage;
    for(const auto &box:boxes) {
        cancellationPoint();
        const auto range=projectedBoxRange(box,origin,axis,physicalMargin);
        if(!range)return {};
        coverage.push_back(*range);
    }
    if(coverage.empty())return {};
    std::sort(coverage.begin(),coverage.end(),[](Interval a,Interval b){return a.lo<b.lo;});
    std::vector<Interval> gaps;double covered=coverage.front().hi;
    for(std::size_t i=1;i<coverage.size();++i) {
        if(covered<coverage[i].lo)gaps.push_back({covered,coverage[i].lo});
        covered=std::max(covered,coverage[i].hi);
    }
    return gaps;
}
static void checkEdgeGapWitness(const Edge &edge,double first,Interval gap,
                                 const std::function<double(const Vec3 &)> &coordinate,const PointMap &map) {
    double lo=edge.range.lo,hi=edge.range.hi;
    const double middle=.5*gap.lo+.5*gap.hi;
    for(int step=0;step<64;++step) {
        cancellationPoint();const double parameter=.5*lo+.5*hi;
        if(!(lo<parameter&&parameter<hi))break;
        const auto point=edge.curve->point(parameter);const double value=coordinate(point);
        if(!std::isfinite(value))break;
        if(gap.lo<value&&value<gap.hi){(void)map(point);break;}
        if((value<middle)==(first<middle))lo=parameter;else hi=parameter;
    }
}
static void checkCurvedGapWitness(const Edge &edge,Interval gap,const Vec3 &normal,double offset,double precision,
                                   const std::function<double(const Vec3 &)> &coordinate,const PointMap &map) {
    std::vector<double> parameters{edge.range.lo,edge.range.hi};
    try {
        for(double limit:{gap.lo,gap.hi}) {
            cancellationPoint();
            const auto roots=planeRoots<3>(*edge.curve,edge.range,normal,limit+offset,precision);
            parameters.insert(parameters.end(),roots.parameters.begin(),roots.parameters.end());
            for(const auto &span:roots.coincident) {
                parameters.push_back(span.lo);
                parameters.push_back(span.hi);
            }
        }
    } catch(const std::domain_error &) {
        return; // This curve has no usable plane-root provider.
    }
    parameters.erase(std::remove_if(parameters.begin(),parameters.end(),[&](double t) {
        return !std::isfinite(t)||t<edge.range.lo||t>edge.range.hi;
    }),parameters.end());
    std::sort(parameters.begin(),parameters.end());
    for(std::size_t i=1;i<parameters.size();++i) {
        cancellationPoint();
        const double t=.5*parameters[i-1]+.5*parameters[i];
        if(!(parameters[i-1]<t&&t<parameters[i]))continue;
        const auto point=edge.curve->point(t);
        const double value=coordinate(point);
        if(gap.lo<value&&value<gap.hi)(void)map(point);
    }
}
static void checkProjectedGapWitnesses(const Body &source,const std::vector<Box> &boxes,
                                       const Frame3 &sourceChart,const Frame3 &targetChart,
                                       double tolerance,double precision,const PointMap &map) {
    const auto edges=source.edges();
    for(int axis=0;axis<2;++axis) {
        const auto gaps=projectedGaps(boxes,targetChart.origin(),axis==0?targetChart.xDir():targetChart.yDir(),
            4.*std::max({precision,linearResolution(),tolerance}));
        if(gaps.empty())continue;
        const auto &origin=sourceChart.origin();
        const auto &normal=axis==0?sourceChart.xDir():sourceChart.yDir();
        const auto coordinate=[&](const Vec3 &point){return dot(point-origin,normal);};
        const double offset=dot(normal,origin);
        for(auto id:edges) {
            cancellationPoint();const auto &edge=source.edge(id);
            if(!edge.range.isFinite())continue;
            const double a=coordinate(edge.curve->point(edge.range.lo)),b=coordinate(edge.curve->point(edge.range.hi));
            if(!std::isfinite(a)||!std::isfinite(b))continue;
            // Ordered gaps: evaluate endpoints once per edge/axis and visit
            // only gaps strictly enclosed by that same edge's endpoint image.
            auto gap=std::upper_bound(gaps.begin(),gaps.end(),std::min(a,b),
                [](double value,Interval interval){return value<interval.lo;});
            for(;gap!=gaps.end()&&gap->hi<std::max(a,b);++gap)
                checkEdgeGapWitness(edge,a,*gap,coordinate,map);
            if(detail::isStraightMappingEdge(*edge.curve)||!std::isfinite(offset))continue;
            std::optional<Interval> range;
            try {
                range=projectedBoxRange(curveBox(*edge.curve,edge.range),origin,normal,0.);
            } catch(const std::domain_error &) {
                continue;
            }
            if(!range)continue;
            // A curved edge can enter and leave on one side of a gap.
            const double lo=range->lo,hi=range->hi;
            gap=std::upper_bound(gaps.begin(),gaps.end(),lo,
                [](double value,Interval interval){return value<interval.hi;});
            for(;gap!=gaps.end()&&gap->lo<hi;++gap) {
                if(std::min(a,b)<gap->lo&&gap->hi<std::max(a,b))continue;
                checkCurvedGapWitness(edge,*gap,normal,offset,precision,coordinate,map);
            }
        }
    }
}
static void withFaceMapping(const std::vector<SurfaceTargetFace> &targets,const SurfaceWrapOptions &options,
                            const std::function<void(const PointMap &,const PlacementMap &,const PointMap &,const CarrierCheck &)> &use,double extension=0.,
                            const Body *source=nullptr) {
    require(!targets.empty()&&targets.size()<=255,"Deformazione: scegliere da 1 a 255 facce destinazione.");
    require(options.tolerance>=1e-9&&options.tolerance<=.01&&std::isfinite(options.tolerance),"Deformazione: tolleranza non valida.");
    require(isFinite(options.anchor)&&isFinite(options.shift)&&std::isfinite(options.angle)&&
            std::isfinite(options.normalOffset)&&isFinite(options.sourcePlane.origin()),
            "Deformazione: parametri di appoggio non finiti.");
    const double precision=std::min(linearResolution(),.02*options.tolerance);
    struct Patch {
        const Body *body;FaceId face;Box bounds;FaceParameterClassifier classifier;Vec2 seed;
    };
    std::vector<Patch> patches;
    for(const auto &target:targets) {
        require(target.body&&target.body->contains(target.face),"Deformazione: faccia destinazione assente.");
        const auto &surface=*target.body->face(target.face).surface;
        const auto u=surface.uDomain(),v=surface.vDomain();
        patches.push_back({target.body,target.face,faceBox(*target.body,target.face),
            FaceParameterClassifier(*target.body,target.face,precision),Vec2(u.isFinite()?.5*u.lo+.5*u.hi:0.,v.isFinite()?.5*v.lo+.5*v.hi:0.)});
    }
    const auto &primary=patches.front();const auto &first=primary.body->face(primary.face);
    Vec3 d[4];first.surface->evaluate(options.anchor.x(),options.anchor.y(),1,d);
    require(norm(d[2])>1e-12&&norm(d[1])>1e-12,"Deformazione: appoggio singolare.");
    const double orientation=first.sense?1.:-1.;
    const auto direction=orientation*first.surface->normal(options.anchor.x(),options.anchor.y());
    const auto axisU=normalized(d[2]),axisV=cross(direction,axisU);
    // Source X/Y are physical orthogonal distances in the support plane.
    // Normalizing Sv alone would shear that plane on oblique UV charts,
    // despite an unchanged target geometry (and unlike rigid placement).
    // Retain the regularity cutoff of the original UV chart.
    require(orientation*dot(normalized(d[1]),axisV)>1e-8,"Deformazione: appoggio degenere.");
    struct Contact {Vec3 point,normal;};
    std::map<std::tuple<double,double,bool>,Contact> contacts;
    enum class ContactUse { Trimmed, Carrier, Witness };
    const auto contact=[&](double x,double y,ContactUse use) {
        const bool carrier=use==ContactUse::Carrier,witness=use==ContactUse::Witness;
        const auto key=std::make_tuple(x,y,carrier);
        if(!witness) {
            const auto found=contacts.find(key);
            if(found!=contacts.end())return found->second;
        }
        // Limite della cache, non un vincolo sulla geometria ammissibile.
        // Fit e cancellazione mantengono i propri limiti di lavoro.
        if(!witness&&contacts.size()>=200000)contacts.clear();
        const auto base=d[0]+x*axisU+y*axisV;
        bool have=false,haveExtension=false,ambiguousExtension=false;Contact selected,extended;
        for(auto &patch:patches) {
            cancellationPoint();Interval range;
            if(!clipLineToBox(base,direction,patch.bounds.padded(precision+(carrier?extension:0.)),range))continue;
            const auto &face=patch.body->face(patch.face);const auto &surface=*face.surface;
            std::vector<double> hits;
            if(surface.type()==SurfaceType::BSpline) {
                const double margin=std::max(100*precision,.001*patch.bounds.diagonal());
                range={range.lo-margin,range.hi+margin};
                hits=numericCurveSurface(Line<3>(base,direction),range,surface,precision).parameters;
            } else {
                bool grazing=false;hits=intersectLineSurface(base,direction,surface,grazing,precision);
                require(!grazing,"Deformazione: appoggio tangente alla direzione di proiezione.");
            }
            for(double t:hits) {
                const auto point=base+t*direction;
                auto uv=patch.seed;
                if(!invertPoint(surface,point,uv,precision,std::max(1.,patch.bounds.diagonal())))continue;
                if(!witness)patch.seed=uv;
                const bool outside=patch.classifier.classify(uv)==PointLocation::Outside;
                const auto normal=(face.sense?1.:-1.)*surface.normal(uv.x(),uv.y());
                // Una falda rivolta dalla parte opposta non appartiene al
                // grafico regolare dell'appoggio scelto dalla prima faccia.
                if(dot(normal,direction)<=1e-6)continue;
                const Contact current{surface.point(uv.x(),uv.y()),normal};
                if(outside) {
                    if(carrier) {
                        if(haveExtension&&(distance(current.point,extended.point)>2*precision||distance(current.normal,extended.normal)>1e-4))ambiguousExtension=true;
                        else {extended=current;haveExtension=true;}
                    }
                    continue;
                }
                if(have) {
                    require(distance(current.point,selected.point)<=2*precision,
                            "Deformazione: superfici destinazione sovrapposte lungo la normale all'appoggio.");
                    require(distance(current.normal,selected.normal)<=1e-4,
                            "Deformazione: le superfici destinazione hanno una giunzione non tangente.");
                } else {selected=current;have=true;}
            }
        }
        if(!have&&carrier&&haveExtension&&!ambiguousExtension){selected=extended;have=true;}
        require(have,"Deformazione: un punto non incontra le facce destinazione; controllare vuoti, estensione e verso dell'appoggio.");
        if(!witness)contacts.emplace(key,selected);
        return selected;
    };
    const double cosine=std::cos(options.angle),sine=std::sin(options.angle);
    const auto located=[&](const Vec3 &p,ContactUse use=ContactUse::Trimmed) {
        try {return contact(cosine*p.x()-sine*p.y()+options.shift.x(),sine*p.x()+cosine*p.y()+options.shift.y(),use);}
        catch(const std::domain_error &e) {
            std::ostringstream message;message<<e.what()<<std::setprecision(12)
                <<" Punto nel piano sorgente: X="<<p.x()<<", Y="<<p.y()<<", Z="<<p.z()<<" mm.";
            throw std::domain_error(message.str());
        }
    };
    const auto map=[&](const Vec3 &point) {
        cancellationPoint();const auto p=options.sourcePlane.toLocal(point);
        const auto hit=located(p);
        return hit.point+(p.z()+options.normalOffset)*hit.normal;
    };
    const auto placement=[&](const Vec2 &center) {
        const auto hit=located(Vec3(center.x(),center.y(),0));
        // Derivata esatta della proiezione lungo direction sul piano
        // tangente locale: orientamento X coerente fra tutte le copie.
        const auto path=cosine*axisU+sine*axisV;
        const auto tangent=path-(dot(hit.normal,path)/dot(hit.normal,direction))*direction;
        return Frame3(hit.point+options.normalOffset*hit.normal,hit.normal,tangent);
    };
    const auto carrierMap=[&](const Vec3 &point) {
        const auto p=options.sourcePlane.toLocal(point);
        const auto hit=contact(cosine*p.x()-sine*p.y()+options.shift.x(),sine*p.x()+cosine*p.y()+options.shift.y(),ContactUse::Carrier);
        return hit.point+(p.z()+options.normalOffset)*hit.normal;
    };
    CarrierCheck check;
    if(source&&!options.rigid) {
        // Both checks use the same coordinates as the trimmed contact query.
        const auto &plane=options.sourcePlane;
        const auto x=cosine*plane.xDir()-sine*plane.yDir();
        const auto y=sine*plane.xDir()+cosine*plane.yDir();
        const auto origin=plane.origin()-options.shift.x()*x-options.shift.y()*y;
        const auto sourceChart=Frame3::fromAxes(origin,x,y,plane.zDir());
        const auto targetChart=Frame3::fromAxes(d[0],axisU,axisV,direction);
        bool continuousCoverage = false;
        if (options.angle == 0. && options.shift.x() == 0. && options.shift.y() == 0.) {
            Vec2 gapCenter;
            const auto coverage = detail::certifyMappingTrimCoverage(*source, targets, sourceChart, targetChart,
                                                                     precision, &gapCenter);
            continuousCoverage = coverage == detail::MappingTrimCoverage::Covered;
            if (coverage == detail::MappingTrimCoverage::Uncovered) {
                const auto base = sourceChart.toGlobal(Vec3(gapCenter.x(), gapCenter.y(), 0.));
                auto point = base;
                for (const auto &ray : {sourceChart.zDir(), -sourceChart.zDir()}) {
                    double t;
                    if (firstRayHit(*source, base, ray, precision, t)) {
                        point = base + t * ray;
                        break;
                    }
                }
                const auto p = plane.toLocal(point);
                std::ostringstream message;
                message << "Deformazione: un punto non incontra le facce destinazione; copertura continua dei trim."
                        << std::setprecision(12) << " Punto nel piano sorgente: X=" << p.x()
                        << ", Y=" << p.y() << ", Z=" << p.z() << " mm.";
                throw std::domain_error(message.str());
            }
        }
        if(patches.size()>1 && !continuousCoverage) {
            std::vector<Box> boxes;
            for(const auto &patch:patches)boxes.push_back(patch.bounds);
            checkProjectedGapWitnesses(*source,boxes,sourceChart,targetChart,options.tolerance,precision,map);
        }
        // The arrangement probes coverage only, independently of the fit cache and inversion seeds.
        detail::checkMappingTrimCoverage(*source,targets,sourceChart,targetChart,precision,
            [&](const Vec3 &point) { (void)located(options.sourcePlane.toLocal(point),ContactUse::Witness); });
        const bool hasSplinePatches = patches.size() > 1 && std::any_of(targets.begin(), targets.end(),
            [](const SurfaceTargetFace &target) {
                return target.body->face(target.face).surface->type() == SurfaceType::BSpline;
            });
        if (hasSplinePatches) {
            check = [&, sourceChart, targetChart](FaceId face, const Surface &image, Interval u, Interval v,
                                                  double budget, double &deviation) {
                const auto proof = detail::projectedMappingResidualBound(
                    *source, face, targets, options, targetChart, image, u, v, budget);
                if (proof.status == detail::SurfaceMappingBoundStatus::Unsupported) {
                    const double measured = detail::measureMappingKnotResidual(
                        *source, face, targets, sourceChart, targetChart, image, u, v, precision, map);
                    require(std::isfinite(measured) && measured <= budget,
                            "Deformazione: residuo del carrier sopra il budget su una pezza NURBS.");
                    deviation = std::max(deviation, measured);
                }
                return proof;
            };
        }
    }
    use(map,placement,carrierMap,check);
}
Body wrapBodyOnFaces(const Body &source,const std::vector<SurfaceTargetFace> &targets,
                     const SurfaceWrapOptions &options,BodyHistory *history,double *deviation) {
    Body result;
    Box bounds;for(auto id:source.edges()){const auto &edge=source.edge(id);bounds.add(curveBox(*edge.curve,edge.range));}
    withFaceMapping(targets,options,[&](const PointMap &map,const PlacementMap &placement,const PointMap &carrierMap,const CarrierCheck &check) {
        result=options.rigid?placeRigidBody(source,options,placement,history,deviation)
                            :mapBody(source,map,options.tolerance,history,deviation,carrierMap,{},.001,3,{},{},check);
    },bounds.isEmpty()?0.:bounds.diagonal(),&source);
    return result;
}
bool fitSurfaceWrapPlacement(const std::vector<Vec2> &points,const std::vector<SurfaceTargetFace> &targets,
                             SurfaceWrapOptions &options,double radius) {
    require(!points.empty()&&points.size()<=4096&&std::isfinite(radius)&&radius>0,
            "Centraggio: punti o raggio di ricerca non validi.");
    for(const auto &point:points)require(isFinite(point),"Centraggio: punto sorgente non finito.");
    std::vector<Vec2> candidates;
    for(int i=-8;i<=8;++i)for(int j=-8;j<=8;++j)if(i*i+j*j<=64)candidates.emplace_back(radius*i/8.,radius*j/8.);
    std::sort(candidates.begin(),candidates.end(),[](const Vec2 &a,const Vec2 &b){const double aa=dot(a,a),bb=dot(b,b);return aa<bb||(aa==bb&&(a.x()<b.x()||(a.x()==b.x()&&a.y()<b.y())));});
    bool found=false;Vec2 selected;
    const auto search=[&](const PlacementMap &placement) {
        std::size_t first=0;
        for(const auto &offset:candidates) {
            cancellationPoint();bool valid=true;
            for(std::size_t i=0;i<points.size();++i) {
                const auto index=(first+i)%points.size();
                try {placement(points[index]+offset);}
                catch(const std::domain_error &){first=index;valid=false;break;}
            }
            if(valid){found=true;selected=offset;break;}
        }
    };
    if(options.quilt) {
        SurfaceQuiltMap quilt(targets,options.anchor);const double c=std::cos(options.angle),s=std::sin(options.angle);
        search([&](const Vec2 &point){const auto p=quilt.fold(Vec3(c*point.x()-s*point.y()+options.shift.x(),s*point.x()+c*point.y()+options.shift.y(),0));return Frame3(p,Vec3(0,0,1),Vec3(1,0,0));});
    } else withFaceMapping(targets,options,[&](const PointMap &,const PlacementMap &placement,const PointMap &,const CarrierCheck &){search(placement);});
    if(found) {
        const double c=std::cos(options.angle),s=std::sin(options.angle);
        options.shift+=Vec2(c*selected.x()-s*selected.y(),s*selected.x()+c*selected.y());
    }
    return found;
}

}
