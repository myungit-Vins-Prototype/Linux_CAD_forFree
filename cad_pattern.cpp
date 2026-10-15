#include "cad_pattern.h"
#include "cad_element_names.h"
#include "fk_history_tools.h"

#include <cmath>
#include <exception>
#include <memory>
#include <map>
#include <set>

#include "cad_datum.h"
#include "fk_boolean.h"
#include "fk_topology.h"
#include "fk_transform.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

// Le copie delle superfici restano shell indipendenti, anche se coincidenti.
// La cucitura cambierebbe i bordi e potrebbe creare una topologia non manifold.
ForgeBody collectSheets(const std::vector<ForgeBody> &bodies) {
    std::vector<HistoryLink> faceOrigins,edgeOrigins,pointOrigins;
    std::vector<Vec3> points;
    std::vector<double> tolerances;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    for(std::size_t operand=0;operand<bodies.size();++operand) {
        const Body &body=*bodies[operand];
        std::map<int, int> pointOf, edgeOf;
        std::set<int> used;
        for (EdgeId e : body.edges()) used.insert({body.edgeStart(e).index, body.edgeEnd(e).index});
        for (VertexId v : body.vertices()) {
            if (!used.count(v.index)) continue;
            pointOf[v.index] = int(points.size());
            points.push_back(body.vertex(v).point);
            pointOrigins.push_back({int(operand),EntityKind::Vertex,v.index,HistoryRelation::Same});
            tolerances.push_back(body.vertex(v).tolerance);
        }
        for (EdgeId e : body.edges()) {
            const Edge &edge = body.edge(e);
            edgeOf[e.index] = int(edges.size());
            edgeOrigins.push_back({int(operand),EntityKind::Edge,e.index,HistoryRelation::Same});
            edges.push_back({pointOf.at(body.edgeStart(e).index), pointOf.at(body.edgeEnd(e).index),
                             edge.curve, edge.range, edge.tolerance});
        }
        for (FaceId f : body.faces()) {
            const Face &face = body.face(f);
            Body::BuildFace copy;
            copy.surface = face.surface;
            copy.sense = face.sense;
            for (LoopId l : face.loops) {
                std::vector<Body::BuildFin> loop;
                for (FinId id : body.loopFins(l)) {
                    const Fin &fin = body.fin(id);
                    loop.push_back({edgeOf.at(fin.edge.index), fin.sense, fin.pcurve, fin.pcurveTolerance});
                }
                if (!loop.empty()) copy.loops.push_back(std::move(loop));
            }
            faceOrigins.push_back({int(operand),EntityKind::Face,f.index,HistoryRelation::Same});
            faces.push_back(std::move(copy));
        }
    }
    Body result = Body::buildSheet(points, edges, faces);
    // I vertici isolati delle facce senza bordo sono aggiunti dopo i punti.
    std::size_t k = 0;
    for (VertexId v : result.vertices())
        if (k < tolerances.size()) result.vertex(v).tolerance = tolerances[k++];
    std::vector<const Body *> pointers;for(const auto &b:bodies)pointers.push_back(b.get());
    auto history=makeHistory(pointers,result);
    const auto fs=result.faces();for(std::size_t i=0;i<fs.size();++i)history.declareOrigins(EntityKind::Face,fs[i].index,{faceOrigins[i]});
    const auto es=result.edges();for(std::size_t i=0;i<edgeOrigins.size();++i)history.declareOrigins(EntityKind::Edge,es.at(i).index,{edgeOrigins[i]});
    const auto vs=result.vertices();for(std::size_t i=0;i<pointOrigins.size();++i)history.declareOrigins(EntityKind::Vertex,vs.at(i).index,{pointOrigins[i]});
    const auto geometry=std::make_shared<const Body>(std::move(result));registerDeclaredHistory(geometry,bodies,std::move(history));return geometry;
}

static ForgeBody moved(const ForgeBody &base,const Transform3 &transform) {
    BodyHistory history;Body body=transformBody(*base,transform,&history);
    const auto result=std::make_shared<const Body>(std::move(body));registerDeclaredHistory(result,{base},std::move(history));return result;
}
static ForgeBody combined(const ForgeBody &a,const ForgeBody &b,Kernel::BooleanOperation operation) {
    BodyHistory history;Body body=booleanOperation(*a,*b,operation,{},&history);
    const auto result=std::make_shared<const Body>(std::move(body));registerDeclaredHistory(result,{a,b},std::move(history));return result;
}

// Unione a coppie (albero bilanciato): le booleane restano tra corpi piccoli.
ForgeBody uniteAll(std::vector<ForgeBody> bodies) {
    while (bodies.size() > 1) {
        std::vector<ForgeBody> next;
        for (std::size_t k = 0; k + 1 < bodies.size(); k += 2) next.push_back(combined(bodies[k],bodies[k+1],Kernel::BooleanOperation::Unite));
        if (bodies.size() % 2) next.push_back(std::move(bodies.back()));
        bodies = std::move(next);
    }
    return std::move(bodies.front());
}

}

bool patternPlacements(const PatternParameters &p, int owner, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                       std::vector<Transform3> &placements, QString *error) {
    placements.clear();
    const auto resolve = [&](int k, ResolvedRef &r) {
        if (k >= p.refs.size() || p.refs.at(k).kind < 0) {
            setError(error, k == 0 ? QStringLiteral("Manca il riferimento della ripetizione.") : QStringLiteral("Manca la seconda direzione."));
            return false;
        }
        QString reason;
        if (!resolveGeometryRef(p.refs.at(k), owner, sketches, bodies, r, &reason)) {
            setError(error, QStringLiteral("Riferimento della ripetizione non valido: %1.").arg(reason));
            return false;
        }
        return true;
    };
    // Direzione di una retta o normale di un piano.
    const auto direction = [&](int k, bool flip, Vec3 &d) {
        ResolvedRef r;
        if (!resolve(k, r)) return false;
        if (!r.hasLine && !r.hasPlane) {
            setError(error, QStringLiteral("La direzione deve essere una retta (asse, spigolo rettilineo, segmento) o un piano (la normale)."));
            return false;
        }
        d = flip ? -r.direction : r.direction;
        return true;
    };
    switch (p.kind) {
    case 0: {
        if (p.count < 1 || p.count2 < 1 || p.count * p.count2 < 2) return setError(error, QStringLiteral("Servono almeno due istanze.")), false;
        if (!(std::fabs(p.spacing) > 1e-9) || (p.count2 > 1 && !(std::fabs(p.spacing2) > 1e-9)))
            return setError(error, QStringLiteral("Il passo deve essere diverso da zero.")), false;
        Vec3 d1, d2;
        if (!direction(0, p.flip, d1)) return false;
        if (p.count2 > 1 && !direction(1, p.flip2, d2)) return false;
        if (p.count2 > 1 && norm(cross(d1, d2)) < 1e-9) return setError(error, QStringLiteral("Le due direzioni sono parallele.")), false;
        for (int j = 0; j < p.count2; ++j)
            for (int i = 0; i < p.count; ++i)
                if (i > 0 || j > 0) placements.push_back(Transform3::translation(i * p.spacing * d1 + j * p.spacing2 * d2));
        break;
    }
    case 1: {
        if (p.count < 2) return setError(error, QStringLiteral("Servono almeno due istanze.")), false;
        ResolvedRef r;
        if (!resolve(0, r)) return false;
        if (!r.hasLine) return setError(error, QStringLiteral("L'asse deve essere una retta (asse del modello, spigolo rettilineo, faccia cilindrica o conica, segmento).")), false;
        double step = p.angle;
        if (p.spread) step = std::fabs(std::fabs(p.angle) - 360.0) < 1e-9 ? p.angle / p.count : p.angle / (p.count - 1);
        if (!(std::fabs(step) > 1e-9)) return setError(error, QStringLiteral("L'angolo deve essere diverso da zero.")), false;
        const Vec3 axis = p.flip ? -r.direction : r.direction;
        for (int i = 1; i < p.count; ++i) placements.push_back(Transform3::rotation(r.point, axis, i * step * kPi / 180.0));
        break;
    }
    case 2: {
        ResolvedRef r;
        if (!resolve(0, r)) return false;
        if (!r.hasPlane) return setError(error, QStringLiteral("Lo specchio vuole un piano (di riferimento, di costruzione o una faccia piana).")), false;
        placements.push_back(Transform3::reflection(r.point, r.direction));
        break;
    }
    default:
        return setError(error, QStringLiteral("Tipo di ripetizione non valido.")), false;
    }
    if (placements.size() > 500) return setError(error, QStringLiteral("Troppe istanze (al massimo 500).")), false;
    return true;
}

ForgeBody forgePattern(const ForgeBody &base, const std::vector<Transform3> &placements, bool keepOriginal, QString *error) {
    if (!base) return setError(error, QStringLiteral("Il corpo da ripetere non ha geometria valida.")), nullptr;
    try {
        std::vector<ForgeBody> copies;
        if (keepOriginal) copies.push_back(base);
        for (const Transform3 &t : placements) copies.push_back(moved(base,t));
        if (copies.empty()) return setError(error, QStringLiteral("Nessuna istanza.")), nullptr;
        if (copies.size() == 1) return copies.front();
        if (base->isSheet()) return collectSheets(copies);
        return uniteAll(std::move(copies));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Ripetizione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgePatternFeature(const ForgeBody &target, const ForgeBody &tool, ::BooleanOperation operation, const std::vector<Transform3> &placements,
                              QString *error) {
    if (!target || !tool) return setError(error, QStringLiteral("Gli operandi della funzione non hanno geometria valida.")), nullptr;
    if (operation != ::BooleanOperation::Union && operation != ::BooleanOperation::Difference)
        return setError(error, QStringLiteral("Si ripetono solo le unioni e le differenze.")), nullptr;
    try {
        std::vector<ForgeBody> tools{tool};
        for (const Transform3 &t : placements) tools.push_back(moved(tool,t));
        const auto allTools=uniteAll(std::move(tools));
        const auto result=combined(target,allTools,operation==::BooleanOperation::Union?Kernel::BooleanOperation::Unite:Kernel::BooleanOperation::Subtract);
        if (result->faces().empty()) return setError(error, QStringLiteral("Il risultato e' vuoto.")), nullptr;
        return result;
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Ripetizione della funzione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeMoveBody(const ForgeBody &base, const TransformParameters &move, int owner, const QVector<SketchObject> &sketches,
                        const QVector<ExtrusionObject> &bodies, QString *error) {
    if (!base) {
        setError(error, QStringLiteral("Il corpo da spostare non ha geometria."));
        return nullptr;
    }
    try {
        Transform3 m = Transform3::translation(Vec3(move.translation[0], move.translation[1], move.translation[2]));
        if (std::fabs(move.angle) > 1e-12) {
            ResolvedRef axis;
            QString why;
            if (!resolveGeometryRef(move.axis, owner, sketches, bodies, axis, &why) || !axis.hasLine) {
                setError(error, QStringLiteral("L'asse della rotazione non e' valido%1.").arg(why.isEmpty() ? QString() : QStringLiteral(": ") + why));
                return nullptr;
            }
            m = m * Transform3::rotation(axis.point, axis.direction, move.angle * M_PI / 180.0);
        }
        return moved(base,m);
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Spostamento non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

}
