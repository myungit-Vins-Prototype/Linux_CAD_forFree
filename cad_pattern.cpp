#include "cad_pattern.h"

#include <cmath>
#include <exception>
#include <memory>

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

// Unione a coppie (albero bilanciato): le booleane restano tra corpi piccoli.
Body uniteAll(std::vector<Body> bodies) {
    while (bodies.size() > 1) {
        std::vector<Body> next;
        for (std::size_t k = 0; k + 1 < bodies.size(); k += 2) next.push_back(booleanOperation(bodies[k], bodies[k + 1], Kernel::BooleanOperation::Unite));
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
        std::vector<Body> copies;
        if (keepOriginal) copies.push_back(*base);
        for (const Transform3 &t : placements) copies.push_back(transformBody(*base, t));
        if (copies.empty()) return setError(error, QStringLiteral("Nessuna istanza.")), nullptr;
        if (copies.size() > 1 && base->isSheet())
            return setError(error, QStringLiteral("Si ripetono solo i solidi (le superfici non si uniscono).")), nullptr;
        return std::make_shared<const Body>(uniteAll(std::move(copies)));
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
        std::vector<Body> tools{*tool};
        for (const Transform3 &t : placements) tools.push_back(transformBody(*tool, t));
        const Body allTools = uniteAll(std::move(tools));
        Body result = booleanOperation(*target, allTools, operation == ::BooleanOperation::Union ? Kernel::BooleanOperation::Unite : Kernel::BooleanOperation::Subtract);
        if (result.faces().empty()) return setError(error, QStringLiteral("Il risultato e' vuoto.")), nullptr;
        return std::make_shared<const Body>(std::move(result));
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
        return std::make_shared<const Body>(transformBody(*base, m));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Spostamento non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

}
