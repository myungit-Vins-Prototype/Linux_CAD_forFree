#include "cad_surface_mapping.h"
#include "cad_datum.h"
#include "cad_model_history.h"
#include "cad_topology_ref.h"
#include "cad_element_names.h"
#include "fk_surface_mapping.h"
#include "fk_quilt_mapping.h"
#include "fk_surface_algo.h"
#include "fk_body_check.h"
#include "fk_sew.h"
#include "fk_transform.h"
#include "fk_intersect.h"
#include "fk_curve_ops.h"
#include "fk_cancel.h"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ForgeCad {
namespace {
using namespace Kernel;
void require(bool value,const QString &message) {
    if (!value) throw std::domain_error(message.toStdString());
}
ForgeBody declared(Body body,const std::vector<ForgeBody> &operands,BodyHistory history) {
    const auto result=std::make_shared<const Body>(std::move(body));
    registerDeclaredHistory(result,operands,std::move(history));return result;
}
QString faceLabel(const GeometryRef &ref,const QVector<ExtrusionObject> &bodies,FaceId face) {
    return QStringLiteral("%1 / F%2").arg(bodies[ref.index].name).arg(face.index);
}
QString obstacle(const Body &source,FaceId face) {
    const auto type=source.face(face).surface->type();
    if(type==SurfaceType::Plane||type==SurfaceType::Cylinder||type==SurfaceType::Cone)return {};
    Vec3 point;Vec2 uv;
    if(faceInteriorPoint(source,face,point,&uv)) {
        const auto &surface=*source.face(face).surface;Vec3 d[9];surface.evaluate(uv.x(),uv.y(),2,d);
        const auto n=cross(d[3],d[1]);const double determinant=dot(n,n);
        if(determinant>0) {
            const auto unit=n/std::sqrt(determinant);
            const double curvature=(dot(unit,d[6])*dot(unit,d[2])-std::pow(dot(unit,d[4]),2))/determinant;
            if(std::isfinite(curvature)&&std::fabs(curvature)>1e-8)
                return QStringLiteral("doppia curvatura (K=%1 mm^-2): sviluppo esatto senza deformazione impossibile").arg(curvature,0,'g',6);
        }
    }
    return QStringLiteral("carrier non riconosciuto come sviluppabile; sviluppo esatto non disponibile");
}
}

namespace {
int mappingOwner(const GeometryRef &ref,int owner,const QVector<ExtrusionObject> &bodies) {
    if(ref.featureId) {
        for(int i=0;i<owner&&i<bodies.size();++i)if(bodies[i].featureId==ref.featureId)return i;
        return -1;
    }
    return ref.index>=0&&ref.index<owner&&ref.index<bodies.size()?ref.index:-1;
}
bool isFlattening(const ExtrusionObject &body) {
    return body.feature==BodyFeature::SurfaceTools&&body.surfaceTool.mode==3&&body.surfaceTool.mappingQuilt;
}
}
int sourceSurfaceFlattening(const QVector<GeometryRef> &sources,int owner,const QVector<ExtrusionObject> &bodies) {
    int common=-1;
    for(const auto &source:sources) {
        const int index=mappingOwner(source,owner,bodies);
        if(index<0)return -1;
        const int root=patternSourceIndex(bodies,index);
        if(root<0||bodies[root].feature!=BodyFeature::Pattern||bodies[root].pattern.kind<3)return -1;
        int flat=-1;
        for(const auto &ref:bodies[root].pattern.refs) {
            const int candidate=mappingOwner(ref,root,bodies);
            if(ref.kind!=5||candidate<0||!isFlattening(bodies[candidate])||(flat>=0&&flat!=candidate))return -1;
            flat=candidate;
        }
        if(flat<0||(common>=0&&common!=flat))return -1;
        common=flat;
    }
    return common;
}
bool resolveFlattenedMapping(SurfaceToolParameters &p,int owner,const QVector<ExtrusionObject> &bodies,
                             GeometryRef &plane,QString *error) {
    try {
        require(p.targets.size()>=2,QStringLiteral("Scegli facce e piano di riferimento."));
        plane=p.targets[1];
        if(plane.kind!=11)return true;
        require(p.mode==4&&p.mappingQuilt&&!p.mappingRigid,QStringLiteral("La mappa di appiattimento richiede Piega da superfici appiattite."));
        const int index=mappingOwner(plane,owner,bodies);
        require(index>=0&&isFlattening(bodies[index])&&!bodies[index].suppressed&&bool(bodies[index].forgeBody),
                QStringLiteral("Appiattimento sorgente non disponibile."));
        const auto &flat=bodies[index].surfaceTool;
        require(flat.targets.size()==2&&!flat.sources.isEmpty()&&flat.sources.size()<=2,
                QStringLiteral("Mappa dell'appiattimento incompleta."));
        QVector<GeometryRef> targets;
        for(int i=0;i<p.targets.size();++i)if(i!=1)targets.append(p.targets[i]);
        require(targets.size()==flat.sources.size(),QStringLiteral("Le facce destinazione devono essere quelle dell'appiattimento."));
        QVector<GeometryRef> ordered;
        for(auto ref:flat.sources) {
            const int body=mappingOwner(ref,index,bodies);
            require(body>=0&&bool(bodies[body].forgeBody),QStringLiteral("Faccia dell'appiattimento non disponibile."));
            const auto face=resolveFaceReference(*bodies[body].forgeBody,ref.point,1e-6);
            require(face.valid(),QStringLiteral("Faccia dell'appiattimento mancante o ambigua."));
            bool found=false;
            for(const auto &target:targets)if(target.kind==5&&mappingOwner(target,owner,bodies)==body)
                found=found||resolveFaceReference(*bodies[body].forgeBody,target.point,1e-6)==face;
            require(found,QStringLiteral("Le facce destinazione non corrispondono all'appiattimento scelto."));
            ref.index=body;ordered.append(ref);
        }
        const auto link=p.targets[1];p.targets={ordered[0],link};
        for(int i=1;i<ordered.size();++i)p.targets.append(ordered[i]);
        plane=flat.targets[1];
        require(plane.kind==1||plane.kind==8,QStringLiteral("Piano originale dell'appiattimento non disponibile."));
        if(plane.kind==8)plane.index=mappingOwner(plane,index,bodies);
        p.mappingU=flat.mappingU;p.mappingV=flat.mappingV;p.mappingAngle=-flat.mappingAngle;
        const double a=flat.mappingAngle*Kernel::kPi/180.,c=std::cos(a),s=std::sin(a);
        p.mappingX=-c*flat.mappingX-s*flat.mappingY;
        p.mappingY=s*flat.mappingX-c*flat.mappingY;
        return true;
    } catch(const std::exception &e){if(error)*error=QString::fromUtf8(e.what());return false;}
}
void buildSurfaceMapping(ExtrusionObject &feature,int owner,const QVector<SketchObject> &sketches,
                         const QVector<ExtrusionObject> &bodies) {
    using namespace Kernel;
    try {
        auto p=feature.surfaceTool;
        require(p.mode==3||p.mode==4,QStringLiteral("Operazione di mappatura sconosciuta."));
        require((p.targets.size()==2||(p.mode==4&&p.targets.size()>2&&p.targets.size()<=256))&&p.targets[0].kind==5,QStringLiteral("Scegli facce e piano di riferimento."));
        require(!p.mappingRigid||p.mode==4,QStringLiteral("Il posizionamento rigido richiede la deformazione su superficie."));
        require(!p.mappingQuilt||!p.mappingRigid,QStringLiteral("Scegli un solo metodo di mappatura."));
        auto sources=p.sources;
        if(sources.isEmpty()) {
            auto ref=p.targets[0];
            if(p.mode==4){ref={};ref.kind=11;ref.index=feature.firstBody;}
            sources.append(ref);
        }
        require(!sources.isEmpty()&&sources.size()<=256,QStringLiteral("Scegli da 1 a 256 corpi o facce."));
        ResolvedRef planeRef;QString reason;GeometryRef planeSource;
        require(resolveFlattenedMapping(p,owner,bodies,planeSource,&reason),reason);
        const bool planeValid=resolveGeometryRef(planeSource,owner,sketches,bodies,planeRef,&reason);
        require(planeValid&&planeRef.hasPlane,
                reason.isEmpty()?QStringLiteral("Scegli un piano valido."):reason);
        const Vec3 axis=std::fabs(planeRef.direction.x())<.9?Vec3(1,0,0):Vec3(0,1,0);
        Frame3 plane(planeRef.point,planeRef.direction,axis);
        std::vector<ForgeBody> results;
        std::vector<std::pair<GeometryRef,FaceId>> faces;
        std::map<int,std::vector<FaceId>> selected;
        std::set<int> whole;
        QStringList errors;
        for(const auto &ref:sources) {
            require(ref.index>=0&&ref.index<owner&&ref.index<bodies.size()&&bool(bodies[ref.index].forgeBody),QStringLiteral("Sorgente non disponibile."));
            require(ref.kind==5||(p.mode==4&&ref.kind==11),QStringLiteral("Scegli facce da distendere oppure corpi e facce da deformare."));
            if(ref.kind==11){require(whole.insert(ref.index).second,QStringLiteral("Corpo sorgente duplicato."));continue;}
            const auto &source=*bodies[ref.index].forgeBody;
            const auto face=resolveFaceReference(source,ref.point,1e-6);
            require(face.valid(),QStringLiteral("Faccia sorgente mancante o ambigua in %1.").arg(bodies[ref.index].name));
            auto &group=selected[ref.index];
            require(std::find(group.begin(),group.end(),face)==group.end(),QStringLiteral("Faccia sorgente duplicata."));
            group.push_back(face);faces.push_back({ref,face});
            if(p.mode==3&&!p.mappingQuilt){const auto error=obstacle(source,face);if(!error.isEmpty())errors.append(faceLabel(ref,bodies,face)+QStringLiteral(": ")+error);}
        }
        require(errors.isEmpty(),errors.join(QLatin1Char('\n')));
        for(const auto &[index,group]:selected)
            require(!whole.count(index),QStringLiteral("%1 e' gia' selezionato come corpo intero: togli le sue singole facce.").arg(bodies[index].name));
        if(p.mode==3) {
            const double angle=p.mappingAngle*kPi/180.;
            plane=Frame3(plane.origin()+p.mappingX*plane.xDir()+p.mappingY*plane.yDir(),plane.zDir(),
                         std::cos(angle)*plane.xDir()+std::sin(angle)*plane.yDir());
            if(p.mappingQuilt) {
                require(faces.size()<=2&&selected.size()==1,
                    QStringLiteral("Appiattimento: scegli una faccia oppure due facce tangenti dello stesso corpo."));
                std::vector<SurfaceTargetFace> patches;
                for(const auto &[ref,face]:faces)patches.push_back({bodies[ref.index].forgeBody.get(),face});
                SurfaceQuiltMap map(patches,Vec2(p.mappingU,p.mappingV));
                SurfaceFlattenStats stats;BodyHistory history;
                auto flattened=map.flattenSheet(plane,p.tolerance,&stats,&history);
                results.push_back(declared(std::move(flattened),{bodies[faces.front().first.index].forgeBody},std::move(history)));
                feature.notice=QStringLiteral("%1 facce appiattite con deformazione, unite lungo il bordo comune. Variazione lineare locale campionata: %2% / %3%; area locale: %4% / %5% (%6 punti). Scarto dei contorni: %7 mm. Non e' uno sviluppo esatto; i campioni non certificano estremi globali.")
                    .arg(faces.size()).arg(100*(stats.minimumStretch-1),0,'f',2).arg(100*(stats.maximumStretch-1),0,'f',2)
                    .arg(100*(stats.minimumAreaRatio-1),0,'f',2).arg(100*(stats.maximumAreaRatio-1),0,'f',2)
                    .arg(stats.samples).arg(stats.boundaryDeviation,0,'g',6);
            } else {
            double right=0,bottom=0;
            for(const auto &[ref,face]:faces) {
                cancellationPoint();BodyHistory history;const auto source=bodies[ref.index].forgeBody;
                ForgeBody flat;
                try { auto developed=unrollFace(*source,face,plane,&history);flat=declared(std::move(developed),{source},std::move(history)); }
                catch(const OperationCancelled &){throw;}
                catch(const std::exception &e){throw std::domain_error((faceLabel(ref,bodies,face)+QStringLiteral(": ")+QString::fromUtf8(e.what())).toStdString());}
                Box bounds;
                for(auto edge:flat->edges()) {
                    const auto image=planarImage(*flat->edge(edge).curve,flat->edge(edge).range,plane);
                    if(image.curve)bounds.add(curveBox(*embedCurve(image.curve,Frame3()),image.range));
                }
                if(!results.empty()) {
                    const auto translation=(right+1.-bounds.lo.x())*plane.xDir()+(bottom-bounds.lo.y())*plane.yDir();
                    auto moved=transformBody(*flat,Transform3::translation(translation),&history);
                    flat=declared(std::move(moved),{flat},std::move(history));
                    right+=1.+bounds.hi.x()-bounds.lo.x();
                } else {right=bounds.hi.x();bottom=bounds.lo.y();}
                results.push_back(flat);
            }
            feature.notice=QStringLiteral("%1 facce sviluppate isometricamente, senza nuovo fit. Facce multiple come isole separate a 1 mm; conservata la precisione dei bordi sorgenti.").arg(results.size());
            }
        } else {
            std::vector<SurfaceTargetFace> targetFaces;
            std::set<std::pair<int,int>> targetKeys;
            for(int slot=0;slot<p.targets.size();++slot) {
                if(slot==1)continue;
                const auto &ref=p.targets[slot];
                require(ref.kind==5&&ref.index>=0&&ref.index<owner&&ref.index<bodies.size()&&bool(bodies[ref.index].forgeBody),QStringLiteral("Superficie destinazione non disponibile."));
                const auto carrier=bodies[ref.index].forgeBody;const auto face=resolveFaceReference(*carrier,ref.point,1e-6);
                require(face.valid(),QStringLiteral("Faccia destinazione mancante o ambigua in %1.").arg(bodies[ref.index].name));
                require(targetKeys.insert({ref.index,face.index}).second,QStringLiteral("Superficie destinazione duplicata."));
                targetFaces.push_back({carrier.get(),face});
            }
            const auto &primary=targetFaces.front();const bool sense=primary.body->face(primary.face).sense;
            require(!p.mappingQuilt||!p.mappingRigid,QStringLiteral("Scegli un solo metodo di mappatura."));
            if(targetFaces.size()==1&&!sense&&!p.mappingRigid&&!p.mappingQuilt)plane=Frame3::fromAxes(plane.origin(),plane.xDir(),-plane.yDir(),-plane.zDir());
            SurfaceWrapOptions options;options.sourcePlane=plane;options.anchor=Vec2(p.mappingU,p.mappingV);
            options.shift=Vec2(p.mappingX,p.mappingY);options.angle=p.mappingAngle*kPi/180.;
            options.rigid=p.mappingRigid;options.quilt=p.mappingQuilt;
            std::unique_ptr<SurfaceQuiltMap> quilt;
            if(p.mappingQuilt)quilt=std::make_unique<SurfaceQuiltMap>(targetFaces,options.anchor);
            options.normalOffset=p.mappingOffset*(p.mappingQuilt||p.mappingRigid||targetFaces.size()>1||sense?1.:-1.);options.tolerance=p.tolerance;
            double worst=0;
            const auto deform=[&](const ForgeBody &source,const QString &label) {
                try {
                    cancellationPoint();BodyHistory history;double deviation=0;
                    auto result=quilt?quilt->bend(*source,options,&history,&deviation):targetFaces.size()==1&&!p.mappingRigid
                        ?wrapBodyOnSurface(*source,*primary.body->face(primary.face).surface,options,&history,&deviation)
                        :wrapBodyOnFaces(*source,targetFaces,options,&history,&deviation);
                    results.push_back(declared(std::move(result),{source},std::move(history)));worst=std::max(worst,deviation);
                } catch(const OperationCancelled &){throw;}
                catch(const std::exception &e){throw std::domain_error((label+QStringLiteral(": ")+QString::fromUtf8(e.what())).toStdString());}
            };
            for(int index:whole)deform(bodies[index].forgeBody,bodies[index].name);
            for(const auto &[index,group]:selected) {
                BodyHistory history;const auto source=bodies[index].forgeBody;
                auto sheet=facesAsSheet(*source,group,&history);
                deform(declared(std::move(sheet),{source},std::move(history)),bodies[index].name);
            }
            feature.notice=p.mappingQuilt
                ?QStringLiteral("%1 sorgenti piegate mediante una mappa comune piano/superfici su %2 facce. Piano sorgente piegato sulle superfici, quota lungo le normali; scarto B-rep misurato: %3 mm. Nessuna isometria generale garantita.").arg(results.size()).arg(targetFaces.size()).arg(worst,0,'g',6)
                :p.mappingRigid
                ?QStringLiteral("%1 sorgenti posizionate su %2 facce; ogni copia segue la normale locale. Forma e dimensioni conservate; la base resta rigida.").arg(results.size()).arg(targetFaces.size())
                :QStringLiteral("%1 sorgenti deformate su %2 facce destinazione, come unico appoggio. Scarto B-rep misurato: %3 mm; possibile stiramento tangenziale.").arg(results.size()).arg(targetFaces.size()).arg(worst,0,'g',6);
        }
        require(!results.empty(),QStringLiteral("Nessun risultato dalla selezione."));
        if(results.size()==1)feature.forgeBody=results.front();
        else {
            std::vector<const Body *> pointers;for(const auto &result:results)pointers.push_back(result.get());
            BodyHistory history;auto result=Body::collect(pointers,&history);
            require(checkBody(result,{true,true}).empty(),QStringLiteral("Collezione delle superfici non valida."));
            feature.forgeBody=declared(std::move(result),results,std::move(history));
        }
        feature.solid=!feature.forgeBody->isSheet();
    } catch(const OperationCancelled &){throw;}
    catch(const std::exception &e){feature.forgeBody.reset();feature.error=QString::fromUtf8(e.what());}
}
}
