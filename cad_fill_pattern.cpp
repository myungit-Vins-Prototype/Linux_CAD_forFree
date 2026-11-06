#include "cad_fill_pattern.h"
#include "cad_datum.h"
#include "cad_forge.h"
#include "cad_feature_build.h"
#include "cad_topology_ref.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_unify.h"
#include <map>
#include <set>
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
bool fillPatternPlacements(const PatternParameters &p, int source, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    std::vector<Kernel::Transform3> &placements, QString *error) {
    using namespace Kernel;
    placements.clear();
    try {
        const auto require=[](bool b,const QString &m){if(!b)throw std::domain_error(m.toStdString());};
        require(source>=0&&source<owner&&source<bodies.size(),QStringLiteral("Scegli il corpo o la funzione da ripetere."));
        require(p.kind>=3&&p.kind<=6&&!p.refs.isEmpty()&&p.refs.size()<=64,QStringLiteral("Scegli un contorno chiuso di riempimento."));
        require(std::isfinite(p.spacing)&&p.spacing>1e-7&&std::isfinite(p.spacing2)&&p.spacing2>1e-7&&std::isfinite(p.angle),QStringLiteral("I passi devono essere positivi e finiti."));
        require(p.variations.isEmpty()&&p.excluded.isEmpty(),QStringLiteral("Il riempimento automatico non usa variazioni o esclusioni numerate."));
        ForgeBody seed=bodies[source].forgeBody;
        if(p.featureOnly) {
            const auto &feature=bodies[source];
            if(feature.operation==0||feature.operation==2) {
                require(feature.booleanTools.isEmpty(),QStringLiteral("Per il riempimento seleziona una funzione con un solo strumento."));
                require(feature.secondBody>=0&&feature.secondBody<source,QStringLiteral("Strumento della funzione non disponibile."));seed=bodies[feature.secondBody].forgeBody;
            } else {
                ExtrusionObject plain=feature;plain.mergeOperation=0;plain.mergeProbe=false;
                buildGeometry(plain,source,sketches,bodies);require(bool(plain.forgeBody),plain.error);seed=plain.forgeBody;
            }
        }
        require(bool(seed),QStringLiteral("Sorgente priva di geometria."));
        ForgeBody boundary;std::vector<FaceId> selected;
        if(p.refs.size()==1&&p.refs[0].kind==7&&p.refs[0].element.kind<0) {
            const auto &ref=p.refs[0];
            require(ref.index>=0&&ref.index<sketches.size(),QStringLiteral("Schizzo del contorno non disponibile."));
            QString reason;boundary=forgePlanarSketch(sketches[ref.index],&reason);require(bool(boundary),reason);selected=boundary->faces();
        } else {
            std::map<int,std::vector<FaceId>> groups;std::set<std::pair<int,int>> unique;
            const Plane *common=nullptr;
            for(const auto &ref:p.refs) {
                require(ref.kind==5,QStringLiteral("Scegli uno schizzo chiuso oppure piu' facce piane."));
                int index=ref.index;
                if(ref.featureId){index=-1;for(int i=0;i<owner&&i<bodies.size();++i)if(bodies[i].featureId==ref.featureId){index=i;break;}}
                require(index>=0&&index<owner&&index<bodies.size()&&bool(bodies[index].forgeBody),QStringLiteral("Faccia del contorno non disponibile."));
                const auto body=bodies[index].forgeBody;const auto face=resolveFaceReference(*body,ref.point,1e-6);
                require(face.valid(),QStringLiteral("Faccia del contorno mancante o ambigua."));
                const auto &surface=*body->face(face).surface;
                require(surface.type()==SurfaceType::Plane,QStringLiteral("Il riempimento richiede facce piane."));
                const auto &plane=static_cast<const Plane &>(surface);
                if(common)require(norm(cross(common->frame().zDir(),plane.frame().zDir()))<=kAngularResolution&&
                    std::fabs(dot(common->frame().zDir(),plane.frame().origin()-common->frame().origin()))<=kLinearResolution,
                    QStringLiteral("Le facce del contorno devono essere complanari."));
                else common=&plane;
                require(unique.insert({index,face.index}).second,QStringLiteral("Faccia del contorno duplicata."));groups[index].push_back(face);
            }
            std::vector<Body> sheets;for(const auto &[index,faces]:groups)sheets.push_back(facesAsSheet(*bodies[index].forgeBody,faces));
            if(sheets.size()==1)boundary=std::make_shared<const Body>(unifySameDomain(sheets[0],kLinearResolution));
            else {
                std::vector<const Body *> parts;for(const auto &sheet:sheets)parts.push_back(&sheet);
                const auto joined=sewSheets(parts,kLinearResolution,false);
                boundary=std::make_shared<const Body>(unifySameDomain(joined.body,kLinearResolution));
            }
            selected=boundary->faces();
        }
        require(!selected.empty(),QStringLiteral("Contorno senza regioni chiuse."));
        const auto &surface=*boundary->face(selected.front()).surface;
        require(surface.type()==SurfaceType::Plane,QStringLiteral("Il riempimento richiede un contorno piano."));
        const auto &basis=static_cast<const Plane&>(surface).frame();const double a=p.angle*kPi/180.;
        const Frame3 frame(basis.origin(),basis.zDir(),std::cos(a)*basis.xDir()+std::sin(a)*basis.yDir());const Plane plane(frame);
        struct Region { FaceId face;FaceClassifier classifier;std::vector<std::pair<CurvePtr<2>,Interval>> edges;std::vector<std::pair<CurvePtr<2>,Interval>> holes; };
        std::vector<Region> regions;Box limits;
        for(auto face:selected) {
            require(boundary->face(face).surface->type()==SurfaceType::Plane,QStringLiteral("Il contorno deve essere complanare."));
            Region region{face,FaceClassifier(*boundary,face,1e-7),{}, {}};
            int loopIndex=0;
            for(auto loop:boundary->face(face).loops) {
                const auto fins=boundary->loopFins(loop);
                const bool hole=loopIndex++>0;
                for(auto fin:fins) {
                    const auto &edge=boundary->edge(boundary->fin(fin).edge);auto pc=exactPCurve(plane,edge.curve,edge.range,1e-7);
                    require(bool(pc),QStringLiteral("Il bordo del contorno non e' nel piano."));
                    region.edges.push_back({pc,edge.range});if(hole)region.holes.push_back({pc,edge.range});limits.add(curveBox(*embedCurve(pc,Frame3()),edge.range));
                }
            }
            regions.push_back(std::move(region));
        }
        Box footprint;
        for(auto face:seed->faces()) {
            const auto box=faceBox(*seed,face);
            for(int mask=0;mask<8;++mask)footprint.add(frame.toLocal(Vec3(mask&1?box.hi.x():box.lo.x(),mask&2?box.hi.y():box.lo.y(),mask&4?box.hi.z():box.lo.z())));
        }
        require(!limits.isEmpty()&&!footprint.isEmpty(),QStringLiteral("Contorno o sorgente vuoti."));
        const Vec2 center((footprint.lo.x()+footprint.hi.x())*.5,(footprint.lo.y()+footprint.hi.y())*.5);
        const double hx=(footprint.hi.x()-footprint.lo.x())*.5,hy=(footprint.hi.y()-footprint.lo.y())*.5;
        const auto fits=[&](Vec2 at) {
            const Vec2 corners[4]={{at.x()-hx,at.y()-hy},{at.x()+hx,at.y()-hy},{at.x()+hx,at.y()+hy},{at.x()-hx,at.y()+hy}};
            for(const auto &region:regions) {
                bool inside=true;
                for(auto q:corners)if(region.classifier.classify(frame.toGlobal(Vec3(q.x(),q.y(),0)))==PointLocation::Outside)inside=false;
                if(!inside)continue;
                if(region.classifier.classify(frame.toGlobal(Vec3(at.x(),at.y(),0)))==PointLocation::Outside)inside=false;
                // Controlla anche fori interamente compresi nell'ingombro,
                // compresi quelli che ne toccano il bordo. Partiziona le
                // curve sui quattro lati, senza dedurre il risultato da pixel.
                for(const auto &hole:region.holes) {
                    std::vector<double> cuts{hole.second.lo,hole.second.hi};
                    for(int axis=0;axis<2;++axis)for(double bound:{axis?at.y()-hy:at.x()-hx,axis?at.y()+hy:at.x()+hx}) {
                        const Vec2 normal=axis?Vec2(0,1):Vec2(1,0);
                        const auto roots=planeRoots<2>(*hole.first,hole.second,normal,bound,1e-9);
                        cuts.insert(cuts.end(),roots.parameters.begin(),roots.parameters.end());
                        for(auto piece:roots.coincident){cuts.push_back(piece.lo);cuts.push_back(piece.hi);}
                    }
                    std::sort(cuts.begin(),cuts.end());
                    for(std::size_t k=1;k<cuts.size();++k) {
                        const Vec2 q=hole.first->point(.5*cuts[k-1]+.5*cuts[k]);
                        if(q.x()>at.x()-hx+1e-7&&q.x()<at.x()+hx-1e-7&&q.y()>at.y()-hy+1e-7&&q.y()<at.y()+hy-1e-7)inside=false;
                    }
                }
                for(int side=0;side<4&&inside;++side) {
                    const Vec2 start=corners[side],end=corners[(side+1)%4];const double length=distance(start,end);if(length<1e-10)continue;
                    Line<2> line(start,(end-start)/length);std::vector<double> cuts{0,length};
                    for(const auto &edge:region.edges)for(const auto &hit:intersectCurves(line,{0,length},*edge.first,edge.second,1e-7).points)cuts.push_back(hit.s);
                    std::sort(cuts.begin(),cuts.end());
                    for(std::size_t k=1;k<cuts.size();++k){const Vec2 q=line.point(.5*(cuts[k]+cuts[k-1]));if(region.classifier.classify(frame.toGlobal(Vec3(q.x(),q.y(),0)))==PointLocation::Outside)inside=false;}
                }
                if(inside)return true;
            }
            return false;
        };
        std::size_t tried=0;
        const auto candidate=[&](Vec2 at){cancellationPoint();require(++tried<=1000000,QStringLiteral("Ricerca oltre il limite: aumentare il passo."));if(!fits(at))return;require(placements.size()<500,QStringLiteral("Oltre500 copie: aumentare il passo di riempimento."));placements.push_back(Transform3::translation((at.x()-center.x())*frame.xDir()+(at.y()-center.y())*frame.yDir()));};
        const double loX=limits.lo.x()+hx,hiX=limits.hi.x()-hx,loY=limits.lo.y()+hy,hiY=limits.hi.y()-hy;
        if(p.kind==6) {
            const Vec2 origin((limits.lo.x()+limits.hi.x())*.5,(limits.lo.y()+limits.hi.y())*.5);candidate(origin);
            const double reach=std::hypot(limits.hi.x()-limits.lo.x(),limits.hi.y()-limits.lo.y())*.5;
            require(reach/p.spacing2<=10000,QStringLiteral("Troppi anelli: aumentare il passo radiale."));
            for(int ring=1;ring<=int(reach/p.spacing2);++ring){const double radius=ring*p.spacing2;const double count=std::floor(kTwoPi*radius/p.spacing);require(count<=1000000,QStringLiteral("Passo circonferenziale troppo piccolo."));const int n=std::max(1,int(count));for(int i=0;i<n;++i){double t=kTwoPi*i/n;candidate(origin+Vec2(radius*std::cos(t),radius*std::sin(t)));}}
        } else if(loX<=hiX&&loY<=hiY) {
            const double dy=p.kind==5?p.spacing*std::sqrt(3.)*.5:p.spacing2;
            require((hiY-loY)/dy<=1000000&&(hiX-loX)/p.spacing<=1000000,QStringLiteral("Passo troppo piccolo per il contorno."));
            for(int row=0;row<=int(std::floor((hiY-loY)/dy+1e-10));++row) {
                const double offset=p.kind!=3&&row%2?p.spacing*.5:0;
                for(int col=0;col<=int(std::floor((hiX-loX-offset)/p.spacing+1e-10));++col)
                    if(loX+offset<=hiX)candidate(Vec2(loX+offset+col*p.spacing,loY+row*dy));
            }
        }
        require(!placements.empty(),QStringLiteral("Nessuna copia interamente contenuta: ridurre la sorgente o cambiare il passo."));
        return true;
    } catch(const OperationCancelled &){throw;} catch(const std::exception &e){if(error)*error=QString::fromUtf8(e.what());placements.clear();return false;}
}
}
