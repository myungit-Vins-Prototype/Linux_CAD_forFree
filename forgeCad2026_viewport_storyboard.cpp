#include "forgeCad2026_viewport.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_topology_ref.h"
#include "cad_spatial_sketch.h"
#include "fk_topology.h"
#include <limits>

namespace {
using namespace ForgeCad;
using namespace ForgeCad::Kernel;

int actualRoles(const GeometryRef &ref, const QVector<SketchObject> &sketches,
                const QVector<ExtrusionObject> &bodies, int owner) {
    int roles = geometryRefRoles(ref, sketches);
    if (ref.kind == 7 && ref.element.kind == -1) return DatumRoleCurve;
    ResolvedRef resolved;
    QString error;
    if (!resolveGeometryRef(ref, owner, sketches, bodies, resolved, &error)) return 0;
    if (!resolved.hasLine) roles &= ~DatumRoleLine;
    if (!resolved.hasPlane) roles &= ~DatumRolePlane;
    return roles;
}
}

QString CadViewport::storyboardPickContext() const {
    if (refPicking_ && refPickFinished_) {
        QStringList bodies;
        for (int body : pickBodies_) bodies.append(QString::number(body));
        return QStringLiteral("reference/%1/%2/%3").arg(refPickRoles_).arg(refPickOwner_).arg(bodies.join(','));
    }
    if (edgePicking_) return QStringLiteral("edges/%1/%2/%3").arg(edgePickEdit_).arg(edgePickExtend_).arg(edgePickHelix_);
    return {};
}

QVector<GeometryRef> CadViewport::storyboardReferences(SceneObjectKind kind, int index, bool wholeBody) const {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    QVector<GeometryRef> result;
    if (storyboardPickContext().isEmpty()) return result;
    const int owner = refPicking_ ? refPickOwner_ : edgePickEdit_;
    const int limit = owner < 0 ? int(extrusions_.size()) : owner;
    const int roles = refPicking_ ? refPickRoles_ : DatumRoleCurve | DatumRoleFace;
    if (wholeBody) {
        if (index < 0 || index >= modelBodies_.size()) return result;
        const quint64 id = modelBodies_.at(index).id;
        index = -1;
        for (int candidate : resultBodiesBefore(owner))
            if (extrusions_.at(candidate).modelBodyId == id) {index = candidate;break;}
        if (index < 0) return result;
    }
    const auto append = [&](GeometryRef ref) {
        if (actualRoles(ref, sketches_, extrusions_, limit) & roles) result.append(std::move(ref));
    };
    if (kind == SceneObjectKind::Plane) {
        GeometryRef ref;ref.kind=1;ref.index=index;
        if (index>=0 && index<3 && refPicking_) append(ref);
        return result;
    }
    if (kind == SceneObjectKind::None) {
        GeometryRef ref;ref.kind=0;
        if (refPicking_) append(ref);
        return result;
    }
    if (kind == SceneObjectKind::Sketch) {
        if (!refPicking_ || index<0 || index>=sketches_.size()) return result;
        const auto &sketch=sketches_.at(index);
        const int position=storyboardIndex(true,index), before=owner<0?-1:storyboardIndex(false,owner);
        if (!sketch.supportError.isEmpty() || (sketch.datumPlane>=limit && sketch.datumPlane>=0)
            || (before>=0 && position>=before) || (storyPosition_>=0 && position>=storyPosition_)) return result;
        GeometryRef ref;ref.kind=7;ref.index=index;ref.featureId=sketch.storyId;ref.element={-1,-1,-1};
        if (roles & DatumRoleCurve) result.append(ref);
        for (int i=0;i<sketch.segments.size();++i) {
            if (!isSketchEntityDisplayed(sketch, 0, i)) continue;
            ref.kind=7;ref.element={0,i,-1};append(ref);
            if (roles & DatumRolePoint) for(int end:{0,1}) {ref.kind=6;ref.element={0,i,end};append(ref);}
        }
        for (int i=0;i<sketch.curves.size();++i) {
            if (!isSketchEntityDisplayed(sketch, 1, i)) continue;
            ref.kind=7;ref.element={1,i,-1};append(ref);
            if (roles & DatumRolePoint) for(int p=0;p<sketch.curves.at(i).controlPoints.size();++p) {
                ref.kind=6;ref.element={1,i,p};append(ref);
            }
        }
        return result;
    }
    if (kind!=SceneObjectKind::Extrusion || index<0 || index>=limit || index>=extrusions_.size()) return result;
    const auto &body=extrusions_.at(index);
    const int position=storyboardIndex(false,index);
    if (body.suppressed || !body.error.isEmpty() || (storyPosition_>=0 && position>=storyPosition_)) return result;
    if (refPicking_ && !pickBodies_.isEmpty() && !pickBodies_.contains(index)) return result;
    if (edgePicking_ && ((edgePickExtend_ && body.solid) || (!pickedEdges_.isEmpty() && index!=edgePickBody_))) return result;
    if (edgePicking_ && index!=edgePickBody_ && !resultBodiesBefore(owner).contains(index)) return result;
    GeometryRef ref;ref.index=index;ref.featureId=body.featureId;
    if (body.feature==BodyFeature::DatumPlane && body.datumValid && refPicking_) {ref.kind=8;append(ref);}
    if (bool(body.curve) && refPicking_) {
        for(int i=0;i<qMax(1,int(body.curves.size()));++i) {
            auto curve=body.curves.isEmpty()?body.curve:body.curves.value(i);
            if (!curve) continue;
            ref.kind=9;ref.element={-1,-1,-1};setCurveReferenceComponent(ref,body,i);
            if (body.feature==BodyFeature::SpatialSketch && i<body.spatialSketch.entities.size()) {
                ref.point.geometry=kSpatialCurveReference;ref.point.subshape=body.spatialSketch.entities[i].id;
            }
            const auto domain=curve->domain();const auto point=curve->point(.5*(domain.lo+domain.hi));
            ref.point.x=point.x();ref.point.y=point.y();ref.point.z=point.z();append(ref);
        }
    }
    if (!body.forgeBody) return result;
    const Body &shape=*body.forgeBody;
    if (roles & (DatumRoleCurve|DatumRoleLine)) for(auto edge:shape.edges()) {
        const auto &e=shape.edge(edge);if(!e.curve || (edgePicking_ && edgePickExtend_ && !shape.isLaminar(edge)))continue;
        const auto point=e.curve->point(.5*(e.range.lo+e.range.hi));
        ref.kind=4;ref.point=edgeReference(shape,edge,point);append(ref);
    }
    if (roles & (DatumRoleFace|DatumRolePlane|DatumRoleLine)) for(auto face:shape.faces()) {
        const auto &f=shape.face(face);if(f.loops.empty())continue;
        const auto &loop=shape.loop(f.loops.front());if(!loop.first.valid())continue;
        ref.kind=5;ref.point=faceReference(shape,face,shape.vertex(shape.finStart(loop.first)).point);append(ref);
    }
    if (refPicking_ && (roles & DatumRolePoint)) for(auto vertex:shape.vertices()) {
        ref.kind=3;ref.point=vertexReference(shape,vertex);append(ref);
    }
    return result;
}

QString CadViewport::pickStoryboardReference(GeometryRef reference) {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    if (storyboardPickContext().isEmpty()) return QStringLiteral("Attiva il campo di selezione nel pannello.");
    SceneObjectKind kind=reference.kind==0?SceneObjectKind::None:reference.kind==1?SceneObjectKind::Plane
        :(reference.kind==6 || reference.kind==7)?SceneObjectKind::Sketch:SceneObjectKind::Extrusion;
    const auto available=storyboardReferences(kind,reference.index);
    const auto same=[](const GeometryRef &a,const GeometryRef &b) {
        return a.kind==b.kind && a.index==b.index && a.element==b.element && a.featureId==b.featureId
            && a.point.subshape==b.point.subshape && a.point.name==b.point.name && a.point.geometry==b.point.geometry;
    };
    bool valid=false;
    for(const auto &candidate:available)if(same(candidate,reference)){reference=candidate;valid=true;break;}
    if(!valid)return QStringLiteral("Questo riferimento non e' disponibile per il campo attivo o in questo punto della storia.");
    if (refPicking_) {
        cancelReferencePick();
        const auto finished=refPickFinished_;
        if(finished)finished(true,reference);
        return {};
    }
    const int body=reference.index;
    if (reference.kind!=4 && reference.kind!=5) return QStringLiteral("Scegli uno spigolo o una faccia del corpo.");
    if (edgePickHelix_) {
        const auto finished=helixPickFinished_;
        cancelEdgePick();
        if(finished)finished(body,reference.kind==4?1:2,reference.point);
        return {};
    }
    ensureBodyDisplay(body);
    const auto &shape=*extrusions_.at(body).forgeBody;
    QVector<int> edges;
    int faceId=-1;
    if (reference.kind==4) {
        const auto edge=resolveEdgeReference(shape,reference.point,std::numeric_limits<double>::max());
        const int display=extrusions_.at(body).display.edgeIds.indexOf(edge.index);
        if(display>=0)edges.append(display);
    } else {
        const auto face=resolveFaceReference(shape,reference.point,std::numeric_limits<double>::max());
        if(face.valid()) {
            faceId=face.index;
            for(auto loop:shape.face(face).loops)for(auto fin:shape.loopFins(loop)) {
                const int display=extrusions_.at(body).display.edgeIds.indexOf(shape.fin(fin).edge.index);
                if(display>=0 && !edges.contains(display))edges.append(display);
            }
        }
    }
    if(edges.isEmpty())return QStringLiteral("Il bordo non ha una rappresentazione selezionabile.");
    if(edgePickBody_!=body){edgePickBody_=body;pickedEdges_.clear();pickedFaces_.clear();}
    bool all=true;for(int edge:edges)all=all&&pickedEdges_.contains(edge);
    for(int edge:edges){pickedEdges_.removeAll(edge);if(!all)pickedEdges_.append(edge);}
    if(faceId>=0) {
        for(int i=pickedFaces_.size()-1;i>=0;--i)if(pickedFaces_.at(i).face==faceId)pickedFaces_.removeAt(i);
        if(!all && !edgePickExtend_)pickedFaces_.append({faceId,faceBoundaryReference(body,faceId,
            Vec3(reference.point.x,reference.point.y,reference.point.z)),edges});
    }
    edgePicked();
    return {};
}
