#include "cad_surface_tools.h"
#include "cad_solid_trim.h"
#include "cad_surface_mapping.h"
#include "cad_datum.h"
#include "cad_element_names.h"
#include "cad_topology_ref.h"
#include "fk_surface_tools.h"
#include "fk_surface_refit.h"
#include "fk_surface_poles_constraints.h"
#include "fk_surface_poles_conversion.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_intersect.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
} // namespace
Kernel::FaceId resolveSurfacePoleFace(const Kernel::Body &body, const EdgePoint &reference) {
    using namespace Kernel;
    std::vector<FaceId> named;
    if (!reference.name.isEmpty()) {
        const auto names=elementNamesOf(body);
        if (!names) return {};
        for (int index:names->find(EntityKind::Face,reference.name.toStdString()))
            if (body.contains(FaceId(index)) && int(body.face(FaceId(index)).surface->type())==reference.geometry)
                named.push_back(FaceId(index));
        if (named.size()==1) return named.front();
        if (named.empty()) return {};
    }
    const FaceId exact(reference.subshape);
    if (!body.contains(exact) || int(body.face(exact).surface->type())!=reference.geometry) return {};
    if (!named.empty() && std::find(named.begin(),named.end(),exact)==named.end()) return {};
    const auto &face=body.face(exact);int edges=0;
    for (const auto loop:face.loops) edges+=int(body.loopFins(loop).size());
    const int context=std::min(int(face.loops.size()),0x7fff)|(std::min(edges,0xffff)<<15);
    return context==reference.context?exact:FaceId{};
}
QByteArray surfaceBasisHash(const ForgeBody &body) {
    if (!body || body->faces().size() != 1)
        return {};
    return surfaceBasisHash(body, body->faces().front());
}
QByteArray surfaceBasisHash(const ForgeBody &body, Kernel::FaceId face) {
    if (!body || !body->contains(face)) return {};
    Kernel::BSplineSurfacePtr s;
    try { s=Kernel::surfacePoleSupport(*body,face); }
    catch(const std::exception &) { return {}; }
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << s->uDegree() << s->vDegree() << s->uPoleCount() << s->vPoleCount();
    for (const auto *knots : {&s->uKnots(), &s->vKnots()}) {
        out << quint32(knots->size());
        for (double k : *knots)
            out << k;
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
void buildSurfaceTool(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    if (body.surfaceTool.mode == 5) { buildSolidTrim(body,index,bodies); return; }
    if (body.surfaceTool.mode >= 3) { buildSurfaceMapping(body,index,sketches,bodies); return; }
    using namespace Kernel;
    try {
        const auto &p = body.surfaceTool;
        require(p.mode >= 0 && p.mode <= 2, "Tipo di funzione superficie non valido.");
        Body result;
        BodyHistory history;
        const auto base = [&] {
            require(body.firstBody >= 0 && body.firstBody < index && body.firstBody < bodies.size() &&
                        bool(bodies[body.firstBody].forgeBody),
                    "Superficie sorgente non disponibile.");
            return bodies[body.firstBody].forgeBody;
        };
        if (p.mode == 1) {
            const auto family = [&](const QVector<GeometryRef> &refs) {
                std::vector<PathSegment> curves;
                for (const auto &ref : refs) {
                    std::vector<PathSegment> pieces;
                    QString error;
                    require(geometryRefPath(ref, index, sketches, bodies, pieces, &error), qPrintable(error));
                    require(pieces.size() == 1,
                            "Ogni riga della rete deve indicare una singola curva continua; usare Estrai bordo3D per concatenare.");
                    curves.push_back(pieces.front());
                }
                return curves;
            };
            FaceAliases labels;
            result = curveNetworkSheet(family(body.ruledFirst), family(body.ruledSecond), p.tolerance, &labels);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {}, makeHistory({}, *body.forgeBody), std::move(labels));
            body.notice = QStringLiteral("Rete a celle di Coons, continuita' interna G0. Curve interpolate entro %1 mm.").arg(p.tolerance);
        } else if (p.mode == 0) {
            const auto source = base();
            require(body.blendEdges.size() == 1, "Scegliere un solo bordo libero da estendere.");
            const auto edge = resolveEdgeReference(*source, body.blendEdges.front(), 1e-5);
            require(edge.valid(), "Bordo di estensione mancante o ambiguo.");
            const auto targets = p.targets.isEmpty() ? QVector<GeometryRef>{body.extentRef} : p.targets;
            require(!targets.isEmpty() && targets.size() <= 256, "Scegliere da1a256 riferimenti finali.");
            std::vector<Body> pieces;
            QMap<int, std::vector<FaceId>> faces;
            for (const auto &ref : targets) {
                if (ref.kind == 5) {
                    require(ref.index >= 0 && ref.index < index && ref.index < bodies.size(), "Riferimento finale non disponibile.");
                    const auto other = bodies[ref.index].forgeBody;
                    require(bool(other), "Riferimento finale privo di geometria.");
                    const auto face = resolveFaceReference(*other, ref.point, 1e-5);
                    require(face.valid(), "Faccia finale mancante o ambigua.");
                    auto &selected = faces[ref.index];
                    require(std::find(selected.begin(), selected.end(), face) == selected.end(), "Faccia finale duplicata.");
                    selected.push_back(face);
                } else {
                    ResolvedRef target;
                    QString error;
                    require(resolveGeometryRef(ref, index, sketches, bodies, target, &error) && target.hasPlane,
                            qPrintable(error.isEmpty() ? QStringLiteral("Scegliere piani o facce finali.") : error));
                    Box box;
                    for (const auto v : source->vertices()) box.add(source->vertex(v).point);
                    pieces.push_back(makePlaneSheet(normalFrame(target.direction, target.point),
                        std::max(1.0, box.diagonal() + distance((box.lo + box.hi) / 2, target.point) + 2 * p.maximum)));
                }
            }
            for (auto it = faces.cbegin(); it != faces.cend(); ++it)
                pieces.push_back(facesAsSheet(*bodies[it.key()].forgeBody, it.value()));
            std::vector<const Body *> operands;
            for (const auto &piece : pieces) operands.push_back(&piece);
            const Body cutter = pieces.size() == 1 ? pieces.front() : sewSheets(operands, p.tolerance, false).body;
            result = extendSheetTo(*source, edge, cutter, p.maximum, body.extendLinear, p.tolerance, &history);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {source}, std::move(history));
        } else {
            const auto source = base();
            QVector<SurfacePoleFace> selected=p.poleFaces;
            if (selected.isEmpty()) {
                require(source->faces().size()==1,"Scegliere le facce NURBS da modificare.");
                selected.append({faceReference(*source,source->faces().front(),{}),p.basis,p.edits});
            }
            require(selected.size()<=256,"Scegliere fino a 256 facce.");
            std::vector<FaceId> convertedFaces;
            for(const auto &entry:selected) {
                const auto face=resolveSurfacePoleFace(*source,entry.face);
                require(face.valid(),"Faccia da convertire non disponibile.");
                convertedFaces.push_back(face);
            }
            const auto prepared=prepareSurfacePoleBody(*source,convertedFaces,p.tolerance);
            std::vector<SurfaceFacePoleChange> changes;
            double deviation=0;
            for (const auto &entry:selected) {
                const auto face=resolveSurfacePoleFace(*source,entry.face);
                require(face.valid() && entry.basis.size()==32 && surfaceBasisHash(source,face)==entry.basis,
                        "Una faccia o la sua griglia NURBS a monte e' cambiata: riselezionare le facce.");
                SurfaceFacePoleChange change; change.face=face;
                if(source->face(face).surface->type()!=SurfaceType::BSpline)
                    change.replacement=std::dynamic_pointer_cast<const BSplineSurface>(prepared.face(face).surface);
                for (const auto &e:entry.edits) change.changes.push_back({e.u,e.v,Vec3(e.dx,e.dy,e.dz),e.weightFactor});
                if (p.refitU || p.refitV) {
                    require(std::isfinite(p.refitTolerance) && p.refitTolerance>=1e-7 && p.refitTolerance<=1000,
                            "Scarto di ricostruzione non valido.");
                    const auto original=std::dynamic_pointer_cast<const BSplineSurface>(prepared.face(face).surface);
                    const auto fitted=refitSurfacePoles(*original,p.refitU,p.refitV);
                    require(fitted.sampledDeviation<=p.refitTolerance,qPrintable(QStringLiteral("Faccia F%1: scarto campionato %2 mm oltre %3 mm; aumentare i poli U/V.")
                        .arg(face.index).arg(fitted.sampledDeviation,0,'g',6).arg(p.refitTolerance,0,'g',6)));
                    change.replacement=fitted.surface;
                    deviation=std::max(deviation,fitted.sampledDeviation);
                }
                changes.push_back(std::move(change));
            }
            require(p.poleContinuity>=0&&p.poleContinuity<=2,"Opzione di continuita' non valida.");
            SurfacePoleContinuityReport continuity;
            if(p.poleContinuity){
                SurfacePoleConstraints constraints(prepared,changes,p.poleContinuity,p.tolerance);
                require(!p.refitU||constraints.refitDeviation()<=p.refitTolerance,"La griglia vincolata supera lo scostamento ammesso: aumentare i poli U/V.");
                changes=constraints.propagate(changes,&continuity);
            }
            changes.erase(std::remove_if(changes.begin(),changes.end(),[](const auto &face){return !face.replacement&&face.changes.empty();}),changes.end());
            result=editBodyFacesPoles(prepared,changes,p.tolerance,&history);
            std::vector<FaceId> affected;for(const auto &face:changes)affected.push_back(face.face);
            const auto checked=checkSurfacePoleContinuity(prepared,result,affected,p.tolerance);
            require(p.poleContinuity!=2||checked.angle<=1e-5,"Continuita': tangenza G1 non rispettata; modifica non applicata.");
            if (p.refitU) body.notice=QStringLiteral("%1 facce; griglie %2 × %3; scarto campionato iniziale massimo %4 mm.")
                .arg(selected.size()).arg(p.refitU).arg(p.refitV).arg(deviation,0,'g',6);
            if(checked.connected)body.notice+=QStringLiteral(" Giunti: %1; apertura campionata %2 mm; scarto G1 massimo %3° su %4 giunti tangenti; controllo campionato.")
                .arg(checked.connected).arg(checked.gap,0,'g',6).arg(checked.angle*180/3.141592653589793,0,'g',6).arg(checked.tangent);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {source}, std::move(history));
        }
        body.solid = body.forgeBody && !body.forgeBody->isSheet();
    } catch (const std::exception &e) {
        body.forgeBody.reset();
        body.error = QString::fromUtf8(e.what());
    }
}
QByteArray surfaceToolsExtras(const QVector<ExtrusionObject> &bodies) {
    QJsonArray records;
    bool mapping=false, multiple=false, targetSet=false, rigid=false, quilt=false, flatten=false, linked=false, solidTrim=false, refit=false, poleFaces=false;
    for (int i = 0; i < bodies.size(); ++i) {
        const auto &b = bodies[i];
        if (b.feature != BodyFeature::SurfaceTools && b.fillFaceContinuities.isEmpty())
            continue;
        const auto &p = b.surfaceTool;
        QJsonArray edits, contacts;
        for (const auto &e : p.edits)
            edits.append(QJsonArray{e.u, e.v, e.dx, e.dy, e.dz, e.weightFactor});
        for (int c : b.fillFaceContinuities)
            contacts.append(c);
        QJsonObject record{{"index", i},
                                   {"mode", p.mode},
                                   {"maximum", p.maximum},
                                   {"tolerance", p.tolerance},
                                   {"basis", QString::fromLatin1(p.basis.toHex())},
                                   {"edits", edits},
                                   {"contacts", contacts}};
        if(p.poleContinuity)record["continuity"]=p.poleContinuity;
        if (!p.poleFaces.isEmpty()) {
            poleFaces=true; QJsonArray faces;
            for (const auto &entry:p.poleFaces) {
                const auto &f=entry.face; QJsonArray changes;
                for (const auto &e:entry.edits) changes.append(QJsonArray{e.u,e.v,e.dx,e.dy,e.dz,e.weightFactor});
                faces.append(QJsonObject{{"face",QJsonArray{f.x,f.y,f.z,f.subshape,f.geometry,f.context,f.role,f.name}},
                    {"basis",QString::fromLatin1(entry.basis.toHex())},{"edits",changes}});
            }
            record["poleFaces"]=faces;
        }
        if (p.refitU || p.refitV) { refit=true; record["refit"]=QJsonArray{p.refitU,p.refitV,p.refitTolerance}; }
        if (p.mode == 5) {solidTrim=true;record["trimSides"]=QJsonArray{p.trimReverseFirst,p.trimReverseSecond};}
        if (p.mode == 3 || p.mode == 4) {
            mapping=true;
            linked=linked||(p.mode==4&&p.targets.size()>1&&p.targets[1].kind==11);
            targetSet=targetSet||(p.mode==4&&p.targets.size()>2);
            record["mapping"]=QJsonObject{{"u",p.mappingU},{"v",p.mappingV},{"x",p.mappingX},{"y",p.mappingY},
                {"angle",p.mappingAngle},{"offset",p.mappingOffset},{"copy",p.mappingCopy}};
            if(p.mode>=3&&p.mappingQuilt) {
                quilt=true;flatten=flatten||p.mode==3;auto settings=record["mapping"].toObject();settings["quilt"]=true;record["mapping"]=settings;
            }
            if(p.mode==4&&p.mappingRigid) {
                rigid=true;auto settings=record["mapping"].toObject();settings["rigid"]=true;record["mapping"]=settings;
            }
        }
        if (!p.sources.isEmpty()) {
            multiple=true;
            QJsonArray sources;
            for (const auto &r:p.sources)
                sources.append(QJsonArray{r.kind,r.index,QString::number(r.featureId),r.point.x,r.point.y,r.point.z,
                    r.point.subshape,r.point.geometry,r.point.context,r.point.role,r.point.name});
            record["sources"]=sources;
        }
        if (!p.targets.isEmpty()) {
            QJsonArray targets;
            for (const auto &r : p.targets)
                targets.append(QJsonArray{r.kind, r.index, QString::number(r.featureId), r.point.x, r.point.y, r.point.z,
                    r.point.subshape, r.point.geometry, r.point.context, r.point.role, r.point.name});
            record["targets"] = targets;
        }
        records.append(record);
    }
    const bool analytic=std::any_of(bodies.cbegin(),bodies.cend(),[](const auto &body){
        return body.feature==BodyFeature::SurfaceTools && body.surfaceTool.mode==2 &&
            std::any_of(body.surfaceTool.poleFaces.cbegin(),body.surfaceTool.poleFaces.cend(),[](const auto &face){return face.face.geometry!=int(Kernel::SurfaceType::BSpline);});
    });
    const bool continuity = std::any_of(bodies.cbegin(),bodies.cend(),[](const auto &body){return body.feature==BodyFeature::SurfaceTools&&body.surfaceTool.mode==2&&body.surfaceTool.poleContinuity;});
    const bool extendedRefit = std::any_of(bodies.cbegin(), bodies.cend(), [](const ExtrusionObject &body) {
        return body.feature == BodyFeature::SurfaceTools && body.surfaceTool.mode == 2 &&
               (body.surfaceTool.refitU > 64 || body.surfaceTool.refitV > 64);
    });
    return records.isEmpty() ? QByteArray()
                             : QJsonDocument(QJsonObject{{"schema", analytic ? 14 : continuity ? 13 : extendedRefit ? 12 : poleFaces ? 11 : refit ? 10 : solidTrim ? 9 : linked ? 8 : flatten ? 7 : quilt ? 6 : rigid ? 5 : targetSet ? 4 : multiple ? 3 : mapping ? 2 : 1}, {"features", records}}).toJson(QJsonDocument::Compact);
}
QString applySurfaceToolsExtras(const QByteArray &bytes, QVector<ExtrusionObject> &output) {
    auto bodies = output;
    try {
        QSet<int> present;
        if (!bytes.isEmpty()) {
            require(bytes.size() <= 4 * 1024 * 1024, "Estensioni superfici oltre4MiB.");
            QJsonParseError error;
            const auto root = QJsonDocument::fromJson(bytes, &error).object();
            require(error.error == QJsonParseError::NoError && root.size() == 2 && (root["schema"].toDouble(-1) == 1 || root["schema"].toDouble(-1) == 2 || root["schema"].toDouble(-1) == 3 || root["schema"].toDouble(-1) == 4 || root["schema"].toDouble(-1) == 5 || root["schema"].toDouble(-1) == 6 || root["schema"].toDouble(-1) == 7 || root["schema"].toDouble(-1) == 8 || root["schema"].toDouble(-1) == 9 || root["schema"].toDouble(-1) == 10 || root["schema"].toDouble(-1) == 11 || root["schema"].toDouble(-1) == 12 || root["schema"].toDouble(-1) == 13 || root["schema"].toDouble(-1) == 14) &&
                        root["features"].isArray(),
                    "Estensioni superfici danneggiate.");
            const auto integer = [](const QJsonValue &v, int low, int high) {
                const double d = v.toDouble(NAN);
                require(std::isfinite(d) && d == std::floor(d) && d >= low && d <= high, "Intero superficie non valido.");
                return int(d);
            };
            for (const auto &entry : root["features"].toArray()) {
                require(entry.isObject(), "Record superficie non valido.");
                const auto o = entry.toObject();
                require(o.size() == 7 + int(o.contains("targets")) + int(o.contains("mapping")) + int(o.contains("sources")) + int(o.contains("trimSides")) + int(o.contains("refit")) + int(o.contains("poleFaces")) + int(o.contains("continuity")), "Record superficie incompleto.");
                const int i = integer(o["index"], 0, bodies.size() - 1);
                require(!present.contains(i), "Estensione superficie duplicata.");
                present.insert(i);
                auto &b = bodies[i];
                require(b.feature == BodyFeature::SurfaceTools || b.feature == BodyFeature::FillSurface,
                        "Estensione su un tipo di funzione incompatibile.");
                auto &p = b.surfaceTool;
                p.mode = integer(o["mode"], 0, root["schema"].toInt()>=9?5:root["schema"].toInt()>=2?4:2);
                p.poleContinuity=0;
                if(o.contains("continuity")){
                    require(root["schema"].toInt()>=13&&b.feature==BodyFeature::SurfaceTools&&p.mode==2,"Continuita' su funzione o schema incompatibile.");
                    p.poleContinuity=integer(o["continuity"],0,2);
                }
                p.refitU=p.refitV=0; p.refitTolerance=.1;
                if(o.contains("refit")) {
                    require(root["schema"].toInt()>=10 && b.feature==BodyFeature::SurfaceTools && p.mode==2 && o["refit"].isArray() && o["refit"].toArray().size()==3,
                            "Ricostruzione poli senza schema10 o su funzione incompatibile.");
                    const auto fit=o["refit"].toArray();
                    const int maxCount = root["schema"].toInt() >= 12 ? Kernel::surfaceRefitMaxPoles : 64;
                    p.refitU=integer(fit[0],4,maxCount); p.refitV=integer(fit[1],4,maxCount);
                    p.refitTolerance=fit[2].toDouble(NAN);
                    require(std::isfinite(p.refitTolerance)&&p.refitTolerance>=1e-7&&p.refitTolerance<=1000,"Scarto ricostruzione non valido.");
                }
                if(p.mode==5) {
                    const auto sides=o["trimSides"].toArray();
                    require(root["schema"].toInt()>=9&&sides.size()==2&&sides[0].isBool()&&sides[1].isBool(),"Taglio tra superfici senza schema9 o lati validi.");
                    p.trimReverseFirst=sides[0].toBool();p.trimReverseSecond=sides[1].toBool();
                } else require(!o.contains("trimSides"),"Lati di taglio su funzione incompatibile.");
                if (p.mode == 3 || p.mode == 4) {
                    require(o["mapping"].isObject(),"Parametri di mappatura assenti.");
                    const auto m=o["mapping"].toObject();
                    require(m.size()==7+int(m.contains("rigid"))+int(m.contains("quilt"))&&m["copy"].isBool(),"Parametri di mappatura incompleti.");
                    p.mappingQuilt=false;
                    if(m.contains("quilt")) {
                        require((p.mode==4?root["schema"].toInt()>=6:p.mode==3&&root["schema"].toInt()>=7)&&m["quilt"].isBool(),"Mappa delle superfici senza schema compatibile (6 piegatura, 7 appiattimento).");
                        p.mappingQuilt=m["quilt"].toBool();
                    }
                    p.mappingRigid=false;
                    if(m.contains("rigid")) {
                        require(p.mode==4&&root["schema"].toInt()>=5&&m["rigid"].isBool(),"Posizionamento rigido senza schema5 o incompatibile.");
                        p.mappingRigid=m["rigid"].toBool();
                    }
                    require(!p.mappingQuilt||!p.mappingRigid,"Appiattimento e posizionamento rigido sono metodi distinti.");
                    const auto number=[&](const char*key){double v=m[key].toDouble(NAN);require(std::isfinite(v)&&std::fabs(v)<=1e9,"Parametro di mappatura non valido.");return v;};
                    p.mappingU=number("u");p.mappingV=number("v");p.mappingX=number("x");p.mappingY=number("y");p.mappingAngle=number("angle");p.mappingOffset=number("offset");p.mappingCopy=m["copy"].toBool();
                } else require(!o.contains("mapping"),"Mappatura su funzione incompatibile.");
                p.maximum = o["maximum"].toDouble(NAN);
                p.tolerance = o["tolerance"].toDouble(NAN);
                require(std::isfinite(p.maximum) && p.maximum > 0 && p.maximum <= 1e6 && std::isfinite(p.tolerance) &&
                            p.tolerance >= 1e-9 && p.tolerance <= 0.01,
                        "Limiti superficie non validi.");
                require(o["basis"].isString(), "Base NURBS assente.");
                const auto hex = o["basis"].toString().toLatin1();
                p.basis = QByteArray::fromHex(hex);
                require((hex.isEmpty() || hex.size() == 64) && p.basis.toHex() == hex, "Impronta base NURBS non valida.");
                require(o["edits"].isArray() && o["contacts"].isArray(), "Tabelle superficie mancanti.");
                require(o["edits"].toArray().size() <= 4096, "Troppi poli modificati.");
                p.edits.clear();
                QSet<quint64> poles;
                for (const auto &row : o["edits"].toArray()) {
                    require(row.isArray() && row.toArray().size() == 6, "Polo incompleto.");
                    const auto a = row.toArray();
                    SurfacePoleEdit e;
                    e.u = integer(a[0], 0, 4095);
                    e.v = integer(a[1], 0, 4095);
                    e.dx = a[2].toDouble(NAN);
                    e.dy = a[3].toDouble(NAN);
                    e.dz = a[4].toDouble(NAN);
                    e.weightFactor = a[5].toDouble(NAN);
                    const quint64 key = quint64(e.u) * 4096 + e.v;
                    require(!poles.contains(key) && std::isfinite(e.dx) && std::isfinite(e.dy) && std::isfinite(e.dz) &&
                                std::isfinite(e.weightFactor) && e.weightFactor > 0 && e.weightFactor <= 1e6,
                            "Polo duplicato o valori non validi.");
                    poles.insert(key);
                    p.edits.append(e);
                }
                p.poleFaces.clear();
                if (o.contains("poleFaces")) {
                    require(root["schema"].toInt()>=11 && b.feature==BodyFeature::SurfaceTools && p.mode==2 &&
                        p.basis.isEmpty() && p.edits.isEmpty() && o["poleFaces"].isArray(),"Facce poli incompatibili.");
                    const auto faces=o["poleFaces"].toArray();
                    require(!faces.isEmpty() && faces.size()<=256,"Numero di facce poli non valido.");
                    QSet<QString> selectedFaces; int total=0;
                    for (const auto &value:faces) {
                        require(value.isObject(),"Faccia poli non valida.");
                        const auto item=value.toObject(); const auto f=item["face"].toArray();
                        require(item.size()==3 && f.size()==8,"Faccia poli incompleta.");
                        SurfacePoleFace entry; auto &point=entry.face;
                        point.x=f[0].toDouble(NAN); point.y=f[1].toDouble(NAN); point.z=f[2].toDouble(NAN);
                        require(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"Punto faccia poli non valido.");
                        point.subshape=integer(f[3],0,2147483647); point.geometry=integer(f[4],-1,2147483647);
                        point.context=integer(f[5],-1,2147483647); point.role=integer(f[6],0,1);
                        require(point.geometry==int(Kernel::SurfaceType::BSpline) ||
                            (root["schema"].toInt()>=14 && point.geometry>=0 && point.geometry<int(Kernel::SurfaceType::BSpline)),
                            "Conversione analitica senza schema14 o tipo supportato.");
                        require(f[7].isString()&&f[7].toString().size()<=4096,"Nome faccia poli non valido.");point.name=f[7].toString();
                        const auto key=QString::number(point.subshape);
                        require(!selectedFaces.contains(key),"Faccia poli duplicata.");selectedFaces.insert(key);
                        const auto hex=item["basis"].toString().toLatin1();entry.basis=QByteArray::fromHex(hex);
                        require(hex.size()==64 && entry.basis.toHex()==hex,"Base faccia poli non valida.");
                        require(item["edits"].isArray(),"Modifiche faccia poli mancanti.");
                        QSet<quint64> seen;
                        for (const auto &change:item["edits"].toArray()) {
                            const auto a=change.toArray();require(a.size()==6,"Polo faccia incompleto.");
                            SurfacePoleEdit e;e.u=integer(a[0],0,4095);e.v=integer(a[1],0,4095);
                            e.dx=a[2].toDouble(NAN);e.dy=a[3].toDouble(NAN);e.dz=a[4].toDouble(NAN);e.weightFactor=a[5].toDouble(NAN);
                            const quint64 key=quint64(e.u)*4096+e.v;
                            require(!seen.contains(key)&&std::isfinite(e.dx)&&std::isfinite(e.dy)&&std::isfinite(e.dz)&&
                                std::isfinite(e.weightFactor)&&e.weightFactor>0&&e.weightFactor<=1e6,"Polo faccia duplicato o non valido.");
                            seen.insert(key);entry.edits.append(e);
                            require(++total<=4096,"Troppi poli modificati.");
                        }
                        p.poleFaces.append(entry);
                    }
                }
                const auto readRefs=[&](const char *key,QVector<GeometryRef> &refs,bool source) {
                    refs.clear();
                    if (!o.contains(key)) return;
                    require(o[key].isArray()&&!o[key].toArray().isEmpty()&&o[key].toArray().size()<=256&&
                        b.feature==BodyFeature::SurfaceTools&&(source?p.mode>=3:(p.mode==0||p.mode>=3)),"Riferimenti superficie non validi.");
                    if(source)require(root["schema"].toInt()>=3,"Sorgenti multiple senza schema3.");
                    QSet<QString> selected;
                    for(const auto &value:o[key].toArray()) {
                        require(value.isArray()&&value.toArray().size()==11,"Riferimento superficie incompleto.");
                        const auto a=value.toArray();GeometryRef r;r.kind=integer(a[0],1,11);
                        require(source?(r.kind==5||(p.mode==4&&r.kind==11)):(r.kind==1||r.kind==5||r.kind==8||(r.kind==11&&p.mode==4&&p.mappingQuilt&&refs.size()==1&&root["schema"].toInt()>=8)),"Tipo di riferimento superficie non valido.");
                        r.index=integer(a[1],0,r.kind==1?2:i-1);bool valid=false;
                        require(a[2].isString(),"Identita' sorgente assente.");r.featureId=a[2].toString().toULongLong(&valid);
                        require(valid&&QString::number(r.featureId)==a[2].toString(),"Identita' sorgente non valida.");
                        r.point.x=a[3].toDouble(NAN);r.point.y=a[4].toDouble(NAN);r.point.z=a[5].toDouble(NAN);
                        require(std::isfinite(r.point.x)&&std::isfinite(r.point.y)&&std::isfinite(r.point.z),"Punto sorgente non valido.");
                        r.point.subshape=integer(a[6],-1,2147483647);r.point.geometry=integer(a[7],-1,2147483647);
                        r.point.context=integer(a[8],-1,2147483647);r.point.role=integer(a[9],0,1);
                        require(a[10].isString()&&a[10].toString().size()<=4096,"Nome topologico non valido.");r.point.name=a[10].toString();
                        const auto identity=QStringLiteral("%1/%2/%3").arg(r.kind).arg(r.index).arg(r.kind==5?r.point.subshape:-1);
                        require(!selected.contains(identity),"Riferimento superficie duplicato.");selected.insert(identity);refs.append(r);
                    }
                };
                readRefs("targets",p.targets,false);readRefs("sources",p.sources,true);
                if(p.mode==0&&!p.targets.isEmpty())b.extentRef=p.targets.front();
                b.fillFaceContinuities.clear();
                for (const auto &c : o["contacts"].toArray())
                    b.fillFaceContinuities.append(integer(c, -1, 2));
                require(b.fillFaceContinuities.isEmpty() ||
                            (b.feature == BodyFeature::FillSurface && b.fillFaceContinuities.size() == b.ruledSecond.size()),
                        "Continuita' per faccia incompatibili.");
                if (b.feature == BodyFeature::SurfaceTools) {
                    require(b.fillFaceContinuities.isEmpty(), "Contatti fill su operazione superficie.");
                    if (p.mode == 3 || p.mode == 4)
                        {
                            require(p.targets.size()>=2&&p.targets[0].kind==5,"Mappatura senza faccia e piano.");
                            require(p.targets.size()==2||(p.mode==4&&root["schema"].toInt()>=4),"Destinazioni multiple senza schema4.");
                            for(int target=2;target<p.targets.size();++target)require(p.targets[target].kind==5,"Destinazione multipla non faccia.");
                        }
                    if(p.mode==5)require(p.targets.size()==2&&p.targets[0].kind==5&&p.targets[1].kind==5&&p.sources.isEmpty(),"Taglio tra superfici senza due facce valide.");
                    if (p.mode == 0)
                        require(b.blendEdges.size() == 1 && b.extentRef.kind >= 0, "Estensione senza bordo o destinazione.");
                    if (p.mode == 1)
                        require(b.ruledFirst.size() >= 2 && b.ruledFirst.size() <= 16 && b.ruledSecond.size() >= 2 &&
                                    b.ruledSecond.size() <= 16,
                                "Rete senza due famiglie valide.");
                    if (p.mode == 2)
                        require((!p.poleFaces.isEmpty() || p.basis.size()==32) &&
                            (p.refitU || !p.edits.isEmpty() || std::any_of(p.poleFaces.cbegin(),p.poleFaces.cend(),[](const SurfacePoleFace &f){return !f.edits.isEmpty();})),
                            "Modifica poli senza base o modifiche.");
                }
            }
        }
        for (int i = 0; i < bodies.size(); ++i)
            require(bodies[i].feature != BodyFeature::SurfaceTools || present.contains(i), "Funzione superficie senza parametri.");
        output = std::move(bodies);
        return {};
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
}
} // namespace ForgeCad
