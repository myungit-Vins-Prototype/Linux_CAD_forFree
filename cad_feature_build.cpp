#include "cad_feature_build.h"
#include "cad_surface_tools.h"
#include "cad_element_names.h"
#include "cad_topology_ref.h"
#include "cad_curve_extract.h"
#include "cad_spatial_sketch.h"
#include "cad_feature_dependencies.h"
#include "cad_constraints.h"
#include "cad_cuda_tessellation.h"
#include "cad_datum.h"
#include "cad_extrude.h"
#include "cad_features.h"
#include "cad_forge.h"
#include "cad_pattern.h"
#include "cad_sketch_refs.h"
#include "fk_topology.h"
#include "fk_surface.h"
#include "fk_precision.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>

namespace ForgeCad {
namespace {
// Tratti dei contorni senza ripetizioni: rigenerando la storia con una
// topologia diversa a monte piu' riferimenti (frammenti di un vecchio
// bordo) possono indicare lo stesso spigolo, che nel loop va una volta.
static void appendDistinctSegments(std::vector<ForgeCad::Kernel::PathSegment> &segments,
                                   const std::vector<ForgeCad::Kernel::PathSegment> &pieces) {
    for (const auto &piece : pieces) {
        const bool repeated = std::any_of(segments.begin(), segments.end(), [&](const ForgeCad::Kernel::PathSegment &segment) {
            return segment.curve == piece.curve && segment.range.lo == piece.range.lo && segment.range.hi == piece.range.hi;
        });
        if (!repeated) segments.push_back(piece);
    }
}
}

// Geometria e tassellazione di un corpo (anche da un thread: non tocca il viewport).
void buildBody(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                      int quality, bool prepareDisplay) {
    QElapsedTimer timer;
    timer.start();
    // Le feature nuove vengono costruite prima di entrare nella storyboard:
    // riserva lo stesso ID che assegnera' normalizeModelHistory, prima che
    // la genealogia generi nomi persistenti. Le anteprime usano buildGeometry
    // direttamente e restano provvisorie fino alla conferma.
    if (!body.featureId) {
        body.featureId = 1;
        for (const auto &feature : bodies) body.featureId = std::max(body.featureId, feature.featureId + 1);
    }
    bool usedSnapshot = body.cachedGeometry && body.forgeBody;
    // La madre ricrea anche i risultati condivisi dai corpi derivati.
    if (body.feature == BodyFeature::Pattern && (body.patternComponent == 0
        || (body.patternComponent < 0 && hasDerivedPatternResults(index, bodies)))) usedSnapshot = false;
    for (int sketch : sketchesOf(body)) if (sketch >= 0 && sketch < sketches.size()
        && (!sketches.at(sketch).supportError.isEmpty() || !sketches.at(sketch).referenceError.isEmpty()
            || std::any_of(sketches.at(sketch).linkedReferences.begin(), sketches.at(sketch).linkedReferences.end(),
                [](const auto &link) { return !link || !link->error.isEmpty(); }))) usedSnapshot = false;
    // Le versioni precedenti potevano salvare come raccordo riuscito una
    // booleana no-op, identica alla base. Non perpetuare quel falso
    // risultato dalla cache: rigenerandolo il kernel restituisce l'errore
    // e la storyboard mostra l'ultimo stadio realmente valido. Una feature
    // soppressa e' uguale alla base per definizione: non si controlla.
    if (usedSnapshot && !body.suppressed && body.operation < 0 && body.feature == BodyFeature::Blend
        && body.firstBody >= 0 && body.firstBody < index && body.firstBody < bodies.size()
        && bodies.at(body.firstBody).forgeBody
        && !ForgeCad::forgeBlendHasEffect(bodies.at(body.firstBody).forgeBody, body.forgeBody))
        usedSnapshot = false;
    // Vecchie rivoluzioni complete di una spline chiusa non periodica:
    // la faccia senza cuciture impedisce booleane e proprieta' di massa.
    // Rigenera anche le feature derivate che ne conservano una nella cache.
    if (usedSnapshot) for (const auto id : body.forgeBody->faces()) {
        const auto &face = body.forgeBody->face(id);
        if (face.surface->type() != Kernel::SurfaceType::Revolution) continue;
        const auto &surface = static_cast<const Kernel::RevolutionSurface &>(*face.surface);
        const auto &curve = *surface.meridian();
        const auto range = curve.domain();
        if (!curve.isPeriodic() && range.isFinite()
            && Kernel::distance(curve.point(range.lo), curve.point(range.hi)) <= Kernel::kLinearResolution) {
            usedSnapshot = false;
            break;
        }
    }
    if (usedSnapshot) {
        // Appena aperto: B-rep e, nelle cache nuove, mesh salvati nel
        // documento al posto del calcolo.
        body.cachedGeometry = false;
        body.curve.reset();
        body.datumValid = false;
        body.solid = !body.forgeBody->isSheet();
        registerFeatureGenealogy(body, index, sketches, bodies);
    } else {
        body.cachedGeometry = false;
        buildGeometry(body, index, sketches, bodies);
    }
    const qint64 built = timer.elapsed();
    if (!usedSnapshot) body.display = {}; // nessuna mesh della geometria precedente
    if (prepareDisplay) {
        if (body.display.quality != quality) tessellateGeometry(body, quality, body.display);
        configurePatternInstances(body, index, sketches, bodies);
    }
    if (qEnvironmentVariableIsSet("FORGECAD_PROFILE"))
        std::fprintf(stderr, "PROFILO corpo %d \"%s\": costruzione %lld ms, tassellazione %lld ms (punti valutati con CUDA dall'avvio: %llu)\n",
                     index, qPrintable(body.name), built, timer.elapsed() - built,
                     (unsigned long long)ForgeCad::cudaTessellationPoints());
}

// Geometria esatta del corpo `index` (kernel ForgeCAD): gli schizzi e gli
// operandi (indici minori, gia' costruiti) vengono da `sketches` e
// `bodies`. In caso d'errore il corpo resta senza geometria con il
// messaggio in `error`. Usata dalla rigenerazione e dalle anteprime (in un
// thread, su copie).
namespace {
void buildGeometryImpl(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
}

// Costruzione con la genealogia: il risultato si registra con i suoi
// operandi (nomi topologici calcolati quando servono, cad_element_names) e gli
// avvisi della risoluzione dei riferimenti (ambigui) vanno nell'esito.
void buildGeometry(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    clearReferenceWarnings();
    body.referenceResolutions.clear();
    beginTopologyResolutionCapture();
    try { buildGeometryImpl(body, index, sketches, bodies); }
    catch (...) { endTopologyResolutionCapture(bodies); throw; }
    body.referenceResolutions = endTopologyResolutionCapture(bodies);
    if (body.forgeBody) registerFeatureGenealogy(body, index, sketches, bodies);
    QStringList warnings = takeReferenceWarnings();
    for (const auto &resolution : body.referenceResolutions)
        if (!resolution.warning.isEmpty() && !warnings.contains(resolution.warning)) warnings.append(resolution.warning);
    if (!warnings.isEmpty())
        body.notice = body.notice.isEmpty() ? warnings.join(QLatin1Char('\n')) : body.notice + QLatin1Char('\n') + warnings.join(QLatin1Char('\n'));
}

namespace {
void buildGeometryImpl(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    body.error.clear();
    body.notice.clear();
    body.forgeBody.reset();
    body.patternResults.clear();
    body.curve.reset();
    body.curves.clear();
    body.datumValid = false;
    body.solid = false;
    if (body.suppressed) {
        // La soppressione bypassa anche gli input della feature: un appoggio
        // o un Pierce non piu' valido non deve interrompere la catena.
        const ExtrusionObject *previous = nullptr;
        for (int candidate = index - 1; candidate >= 0; --candidate)
            if (bodies.at(candidate).modelBodyId == body.modelBodyId) { previous = &bodies.at(candidate); break; }
        if (previous) {
            body.forgeBody = previous->forgeBody;
            body.curve = previous->curve;
            body.curves = previous->curves;
            body.solid = previous->solid;
        }
        return;
    }
    for (int sketch : sketchesOf(body)) if (sketch>=0 && sketch<sketches.size())
        for (const auto &c : sketches.at(sketch).geometricConstraints)
            if (c.type==ConstraintType::Pierce && (!c.pierce || !c.pierce->error.isEmpty()
                || ForgeCad::constraintError(sketches.at(sketch),c)>1e-7)) {
                body.error=QStringLiteral("Vincolo Pierce non risolto nello schizzo %1: %2").arg(sketches.at(sketch).name,
                    c.pierce ? (c.pierce->error.isEmpty() ? QStringLiteral("punto fuori dalla superficie") : c.pierce->error) : QStringLiteral("riferimento mancante"));
                return;
            }
    for (int index : sketchesOf(body)) if (index >= 0 && index < sketches.size()) {
        const auto &sketch = sketches.at(index);
        QString error = sketch.referenceError;
        for (const auto &link : sketch.linkedReferences)
            if (!link || !link->error.isEmpty()) error = link ? link->error : QStringLiteral("riferimento mancante");
        if (!error.isEmpty()) {
            body.error = QStringLiteral("Riferimento associativo non risolto nello schizzo %1: %2").arg(sketch.name, error);
            return;
        }
    }
    for (int sketch : sketchesOf(body)) if (sketch >= 0 && sketch < sketches.size() && !sketches.at(sketch).supportError.isEmpty()) {
        body.error = QStringLiteral("Appoggio non risolto nello schizzo %1: %2").arg(sketches.at(sketch).name, sketches.at(sketch).supportError);
        return;
    }
    const auto operand = [&](int i) -> const ExtrusionObject * {
        return i >= 0 && i < index && i < bodies.size() ? &bodies.at(i) : nullptr;
    };
    const auto sketch = [&](int i) -> const SketchObject * { return i >= 0 && i < sketches.size() ? &sketches.at(i) : nullptr; };
    try {
        if (body.operation >= 0) {
            const ExtrusionObject *first = operand(body.firstBody), *second = operand(body.secondBody);
            if (!first || !second) {
                body.error = QStringLiteral("Operandi della booleana non validi.");
                return;
            }
            // A op B, poi op con gli altri strumenti uno dopo l'altro.
            body.forgeBody = ForgeCad::forgeBoolean(first->forgeBody, second->forgeBody, BooleanOperation(body.operation), &body.error);
            for (int tool : body.booleanTools) {
                if (!body.forgeBody) break;
                const ExtrusionObject *other = operand(tool);
                if (!other) {
                    body.forgeBody.reset();
                    body.error = QStringLiteral("Uno degli strumenti della booleana non esiste piu'.");
                    break;
                }
                body.forgeBody = ForgeCad::forgeBoolean(body.forgeBody, other->forgeBody, BooleanOperation(body.operation), &body.error);
                if (!body.forgeBody) body.error = QStringLiteral("Con \"%1\": %2").arg(other->name, body.error);
            }
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        switch (body.feature) {
        case BodyFeature::Transform: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo da spostare non esiste piu' o non ha geometria.");
                return;
            }
            body.forgeBody = ForgeCad::forgeMoveBody(base->forgeBody, body.move, index, sketches, bodies, &body.error);
            body.solid = base->solid && hasGeometry(body);
            return;
        }
        case BodyFeature::Scale: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base) {
                body.error = QStringLiteral("Il corpo da scalare non esiste piu'.");
                return;
            }
            body.forgeBody = ForgeCad::forgeScale(base->forgeBody, body.scaleFactor, body.scaleCenterMode, body.scaleCenter, &body.error);
            body.solid = base->solid && hasGeometry(body);
            return;
        }
        case BodyFeature::Draft: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo da sformare non esiste piu'.");
                return;
            }
            ForgeCad::ResolvedRef neutral;
            if (!ForgeCad::resolveGeometryRef(body.draftNeutral, index, sketches, bodies, neutral, &body.error)) return;
            if (!neutral.hasPlane) { body.error = QStringLiteral("Scegli un piano neutro o una faccia piana."); return; }
            body.forgeBody = ForgeCad::forgeDraft(base->forgeBody, body.offsetFaces, neutral.point,
                neutral.direction * (body.draftReverse ? -1.0 : 1.0), body.draftAngle, &body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::Thicken: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("La superficie da ispessire non esiste piu'.");
                return;
            }
            ForgeCad::Kernel::Vec3 direction;
            const ForgeCad::Kernel::Vec3 *along = nullptr;
            if (body.thickenDirection.kind >= 0) {
                ForgeCad::ResolvedRef resolved;
                if (!ForgeCad::resolveGeometryRef(body.thickenDirection, index, sketches, bodies, resolved, &body.error)) return;
                if (!resolved.hasLine && !resolved.hasPlane) {
                    body.error = QStringLiteral("La direzione dello spessore deve essere una retta, un asse, un piano o una faccia piana.");
                    return;
                }
                direction = resolved.direction;  // retta: la sua direzione; piano: la normale
                along = &direction;
            }
            body.forgeBody = ForgeCad::forgeThicken(base->forgeBody, body.offsetFaces, body.distance, body.thickenSide, along, &body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::SurfaceTools: buildSurfaceTool(body,index,sketches,bodies);return;
        case BodyFeature::SpatialSketch: {
            body.curves=buildSpatialSketch(body.spatialSketch,&body.error);
            if(!body.curves.isEmpty())body.curve=body.curves.first();
            return;
        }
        case BodyFeature::ExtractedCurve: {
            body.curves = extractCurves3D(body.curveExtraction, index, sketches, bodies, &body.error);
            if (!body.curves.isEmpty()) body.curve = body.curves.first();
            return;
        }
        case BodyFeature::ProjectedCurve: {
            const ExtrusionObject *base = operand(body.firstBody);
            const SketchObject *profile = sketch(body.sketchIndex);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo su cui proiettare non esiste piu'.");
                return;
            }
            if (!profile) {
                body.error = QStringLiteral("Lo schizzo da proiettare non esiste piu'.");
                return;
            }
            body.curves = ForgeCad::forgeProjectedCurves(base->forgeBody, *profile, body.projectionReverse, &body.error, &body.notice);
            if (!body.curves.isEmpty()) body.curve = body.curves.first();
            return;
        }
        case BodyFeature::Shell: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base) || !base->solid) {
                body.error = QStringLiteral("Il solido da svuotare non esiste piu' o non e' un solido.");
                return;
            }
            body.forgeBody = ForgeCad::forgeShell(base->forgeBody, body.offsetFaces, body.distance, &body.error);
            body.solid = hasGeometry(body);
            return;
        }
        case BodyFeature::Thread: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base) || !base->solid) {
                body.error = QStringLiteral("Il solido da filettare non esiste piu' o non e' un solido.");
                return;
            }
            body.forgeBody = ForgeCad::forgeThread(base->forgeBody, body.thread, &body.error);
            body.solid = hasGeometry(body);
            return;
        }
        case BodyFeature::DeleteFace: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo da cui togliere le facce non esiste piu' o non ha geometria.");
                return;
            }
            body.forgeBody = ForgeCad::forgeDeleteFaces(base->forgeBody, body.offsetFaces, &body.error, body.deleteComponent);
            body.solid = false;
            return;
        }
        case BodyFeature::SurfaceOffset: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo dell'offset non esiste piu' o non ha geometria.");
                return;
            }
            body.forgeBody = ForgeCad::forgeOffsetFaces(base->forgeBody, body.offsetFaces, body.distance, &body.error, nullptr, body.offsetSew);
            body.solid = false;
            return;
        }
        case BodyFeature::Sew: {
            QVector<ForgeCad::ForgeBody> sheets;
            for (int input : QVector<int>{body.firstBody} + body.booleanTools) {
                const ExtrusionObject *sheet = operand(input);
                if (!sheet || !isShapeBody(*sheet)) {
                    body.error = QStringLiteral("Una delle superfici da cucire non esiste piu' o non ha geometria.");
                    return;
                }
                sheets.append(sheet->forgeBody);
            }
            body.forgeBody = ForgeCad::forgeSew(sheets, body.sewTolerance, body.sewSolid, &body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::SheetTrim:
        case BodyFeature::SheetExtend: {
            const bool trim = body.feature == BodyFeature::SheetTrim;
            const ExtrusionObject *sheet = operand(body.firstBody);
            const ExtrusionObject *tool = trim && body.secondBody >= 0 ? operand(body.secondBody) : nullptr;
            if (!sheet) body.error = QStringLiteral("La superficie non esiste piu'.");
            else if (trim && body.secondBody >= 0 && !tool) body.error = QStringLiteral("Lo strumento del taglio non esiste piu'.");
            else if (trim) {
                ForgeCad::ForgeBody cutter;
                if (tool && tool->forgeBody) cutter = tool->forgeBody;
                else if (tool && tool->feature == BodyFeature::DatumPlane && tool->datumValid) {
                    const SketchFrame &f = tool->datumFrame;
                    const ForgeCad::Kernel::Frame3 frame(
                        ForgeCad::Kernel::Vec3(f.origin[0], f.origin[1], f.origin[2]),
                        ForgeCad::Kernel::Vec3(f.normal[0], f.normal[1], f.normal[2]),
                        ForgeCad::Kernel::Vec3(f.xAxis[0], f.xAxis[1], f.xAxis[2]));
                    cutter = ForgeCad::forgeTrimPlaneTool(sheet->forgeBody, frame, &body.error);
                }
                else if (body.sketchIndex >= 0) {
                    const SketchObject *s = sketch(body.sketchIndex);
                    if (!s) body.error = QStringLiteral("Lo schizzo di taglio non esiste piu'.");
                    else if (body.trimProject) {
                        // Proiezione sulla prima faccia incontrata: niente lame, niente parte da scegliere.
                        body.forgeBody = ForgeCad::forgeProjectedCut(sheet->forgeBody, *s, body.projectionMode, body.projectionReverse,
                                                                     &body.error, &body.notice);
                        body.solid = body.forgeBody && !body.forgeBody->isSheet();
                        return;
                    }
                    else cutter = ForgeCad::forgeTrimSketchTool(sheet->forgeBody, *s, &body.error);
                }
                if (body.error.isEmpty() && ((tool || body.sketchIndex >= 0) && !cutter))
                    body.error = QStringLiteral("Lo strumento del taglio non ha geometria valida.");
                if (body.error.isEmpty()) {
                    if (body.trimBoth) {
                        if (!tool || !tool->forgeBody)
                            body.error = QStringLiteral("Il taglio reciproco richiede due corpi o superfici.");
                        else body.forgeBody = ForgeCad::forgeTrimBoth(sheet->forgeBody, tool->forgeBody,
                                                                     body.trimKeep, body.trimToolKeep, &body.error);
                    } else {
                        body.forgeBody = ForgeCad::forgeTrimSheet(sheet->forgeBody, cutter, body.trimPlane,
                                                                 body.trimKeep, &body.error);
                    }
                }
            }
            else body.forgeBody = ForgeCad::forgeExtendSheet(sheet->forgeBody, body.blendEdges, body.blendSize, body.extendLinear, &body.error);
            return;
        }
        case BodyFeature::Blend: {
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base) {
                body.error = QStringLiteral("Il corpo da raccordare non esiste piu'.");
                return;
            }
            // Gli ID dei riferimenti valgono se gli spigoli sono stati scelti su questa base.
            const bool sameState = body.blendBaseFeature != 0 && body.blendBaseFeature == base->featureId;
            body.forgeBody = ForgeCad::forgeBlend(base->forgeBody, body.blendEdges, body.blendSize, body.blendChamfer, &body.error, body.chamferSpec,
                                                  sameState);
            // Due raccordi/smussi adiacenti possono intersecare le rispettive
            // zone. Se il secondo e' piu' grande, applicarlo sul raccordo
            // piccolo puo' non avere una superficie offset valida. Rigenera
            // allora dalla base comune in ordine decrescente di misura: la
            // lavorazione grande consuma la zona d'intersezione e quella
            // piccola viene rifatta sugli spigoli rimasti.
            if (!body.forgeBody && base->feature == BodyFeature::Blend
                && body.chamferSpec.fillet.mode == 0 && base->chamferSpec.fillet.mode == 0
                && body.blendSize > base->blendSize + 1e-12 * qMax(body.blendSize, base->blendSize)) {
                const ExtrusionObject *source = operand(base->firstBody);
                if (source && source->forgeBody) {
                    QString reorderedError;
                    ForgeCad::ForgeBody larger = ForgeCad::forgeBlend(source->forgeBody, body.blendEdges, body.blendSize,
                                                                      body.blendChamfer, &reorderedError, body.chamferSpec, false);
                    if (larger) {
                        ForgeCad::ForgeBody rebuilt = ForgeCad::forgeBlend(larger, base->blendEdges, base->blendSize,
                                                                           base->blendChamfer, &reorderedError, base->chamferSpec, false);
                        if (rebuilt) {
                            body.forgeBody = std::move(rebuilt);
                            body.error.clear();
                        }
                    }
                }
            }
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::Primitive:
            body.forgeBody = ForgeCad::forgePrimitive(body.primitive, &body.error);
            body.solid = hasGeometry(body);
            return;
        case BodyFeature::Helix: {
            ForgeCad::HelixBase base;
            bool ok = false;
            if (body.helix.source == 0) {
                const SketchObject *s = sketch(body.sketchIndex);
                if (!s) body.error = QStringLiteral("Lo schizzo della base dell'elica non esiste piu'.");
                else ok = ForgeCad::helixBaseFromSketch(*s, body.helix.curve, base, &body.error);
            } else {
                const ExtrusionObject *b = operand(body.firstBody);
                if (!b || !isShapeBody(*b)) body.error = QStringLiteral("Il corpo della base dell'elica non esiste piu'.");
                else ok = ForgeCad::forgeHelixBase(*b->forgeBody, body.helix.source, body.helix.reference, base, &body.error);
            }
            if (ok) body.curve = ForgeCad::helixCurve(body.helix, base, &body.error);
            return;
        }
        case BodyFeature::Sweep: {
            const SketchObject *profile = sketch(body.sketchIndex);
            if (!profile && body.sweepProfileRef.kind < 0) {
                body.error = QStringLiteral("Lo schizzo del profilo non esiste piu'.");
                return;
            }
            std::vector<ForgeCad::Kernel::PathSegment> path;
            if (body.sweepPathRef.kind >= 0) {
                if (!geometryRefPath(body.sweepPathRef, index, sketches, bodies, path, &body.error)) return;
            } else if (body.sweepPath == 0) {
                const SketchObject *pathSketch = sketch(body.pathSketch);
                if (!pathSketch) {
                    body.error = QStringLiteral("Lo schizzo del percorso non esiste piu'.");
                    return;
                }
                if (body.sweepProfileRef.kind < 0 && body.pathSketch == body.sketchIndex) {
                    body.error = QStringLiteral("Profilo e percorso devono stare in schizzi diversi.");
                    return;
                }
                SketchPathRef selection{body.pathSketch, body.pathSegments, body.pathCurves};
                const SketchObject selected = ForgeCad::sketchPathSubset(*pathSketch, selection);
                if (!ForgeCad::sketchPath(selected, path, &body.error)) return;
            } else {
                const ExtrusionObject *curve = operand(body.firstBody);
                if (!curve || !curve->curve) {
                    body.error = QStringLiteral("La curva del percorso non esiste piu' o non e' valida.");
                    return;
                }
                path = ForgeCad::curvePath(curve->curve);
            }
            if (body.sweepProfileRef.kind >= 0) {
                std::vector<ForgeCad::Kernel::PathSegment> section;
                if (!geometryRefPath(body.sweepProfileRef, index, sketches, bodies, section, &body.error)) return;
                body.forgeBody = ForgeCad::forgeSweep(section, path, body.sweepMode, &body.error, body.sweepSurface);
            } else {
                path = ForgeCad::alignPath(path, *profile);
                body.forgeBody = ForgeCad::forgeSweep(*profile, path, body.sweepMode, &body.error, body.sweepSurface);
            }
            // Una superficie non si fonde con i solidi.
            if (!body.sweepSurface) body.forgeBody = ForgeCad::forgeMergeFeatureResult(body, body.forgeBody, index, bodies, &body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::Loft: {
            body.forgeBody = ForgeCad::forgeReferencedLoft(body,index,sketches,bodies,&body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::Ruled: {
            // Le due catene dai riferimenti, ognuno usato per intero.
            std::vector<ForgeCad::Kernel::PathSegment> chains[2];
            const QVector<GeometryRef> *refs[2] = {&body.ruledFirst, &body.ruledSecond};
            for (int k = 0; k < 2; ++k) {
                if (refs[k]->isEmpty()) {
                    body.error = k == 0 ? QStringLiteral("Scegli la prima curva.") : QStringLiteral("Scegli la seconda curva.");
                    return;
                }
                for (const GeometryRef &ref : *refs[k]) {
                    std::vector<ForgeCad::Kernel::PathSegment> pieces;
                    QString reason;
                    if (!ForgeCad::geometryRefPath(ref, index, sketches, bodies, pieces, &reason)) {
                        body.error = QStringLiteral("%1: %2.").arg(k == 0 ? QStringLiteral("Prima curva") : QStringLiteral("Seconda curva"), reason);
                        return;
                    }
                    chains[k].insert(chains[k].end(), pieces.begin(), pieces.end());
                }
            }
            QVector<QPair<ForgeCad::ForgeBody, EdgePoint>> startFaces, endFaces;
            const QVector<GeometryRef> *faceRefs[2] = {&body.loftStartFaces, &body.loftEndFaces};
            for (int side = 0; side < 2; ++side)
                for (const GeometryRef &ref : *faceRefs[side]) {
                    if ((side == 0 ? body.loftStartContinuity : body.loftEndContinuity) != 3) continue;
                    if (ref.kind != 5 || ref.index < 0 || ref.index >= index || !bodies.at(ref.index).forgeBody) {
                        body.error = QStringLiteral("Una faccia tangente della superficie non esiste piu'."); return;
                    }
                    (side == 0 ? startFaces : endFaces).append({bodies.at(ref.index).forgeBody, ref.point});
                }
            body.forgeBody = ForgeCad::forgeRuledSurface(chains[0], chains[1], &body.error, body.loftStartContinuity,
                                                       body.loftEndContinuity, body.loftStartInfluence, body.loftEndInfluence,
                                                       startFaces, endFaces);
            body.solid = false;
            return;
        }
        case BodyFeature::BoundarySurface: {
            std::vector<ForgeCad::Kernel::PathSegment> segments;
            for (const GeometryRef &ref : body.planarRefs) {
                std::vector<ForgeCad::Kernel::PathSegment> pieces;
                QString reason;
                if (!ForgeCad::geometryRefPath(ref, index, sketches, bodies, pieces, &reason)) {
                    body.error = QStringLiteral("Curva del contorno: %1.").arg(reason);
                    return;
                }
                appendDistinctSegments(segments, pieces);
            }
            body.forgeBody = ForgeCad::forgeBoundarySurface(segments, &body.error);
            body.solid = false;
            return;
        }
        case BodyFeature::FillSurface: {
            const auto paths = [&](const QVector<GeometryRef> &refs, const QString &what, std::vector<ForgeCad::Kernel::PathSegment> &segments) {
                for (const GeometryRef &ref : refs) {
                    std::vector<ForgeCad::Kernel::PathSegment> pieces;
                    QString reason;
                    if (!ForgeCad::geometryRefPath(ref, index, sketches, bodies, pieces, &reason)) {
                        body.error = QStringLiteral("%1: %2.").arg(what, reason);
                        return false;
                    }
                    appendDistinctSegments(segments, pieces);
                }
                return true;
            };
            std::vector<ForgeCad::Kernel::PathSegment> boundary, guides;
            if (!paths(body.planarRefs, QStringLiteral("Curva del contorno"), boundary)) return;
            if (!paths(body.ruledFirst, QStringLiteral("Curva guida"), guides)) return;
            QVector<QPair<ForgeCad::ForgeBody, EdgePoint>> contacts;
            for (const GeometryRef &ref : body.ruledSecond) {
                if (ref.kind != 5 || ref.index < 0 || ref.index >= index || ref.index >= bodies.size() || !bodies.at(ref.index).forgeBody) {
                    body.error = QStringLiteral("Una faccia adiacente non esiste piu'.");
                    return;
                }
                contacts.append({bodies.at(ref.index).forgeBody, ref.point});
            }
            body.forgeBody = ForgeCad::forgeFillSurface(boundary, guides, contacts, body.fillContinuity, body.fillInfluence,
                                                        body.fillGuideWeight, &body.error, &body.notice, body.fillFaceInfluences, body.fillShapeFace, body.fillShapeWeight, body.fillFaceContinuities);
            body.solid = false;
            return;
        }
        case BodyFeature::PlanarSurface: {
            if (body.planarRefs.isEmpty()) {
                const SketchObject *s = sketch(body.sketchIndex);
                if (!s) {
                    body.error = QStringLiteral("Lo schizzo della superficie planare non esiste piu'.");
                    return;
                }
                body.forgeBody = ForgeCad::forgePlanarSketch(*s, &body.error);
            } else {
                std::vector<ForgeCad::Kernel::PathSegment> segments;
                for (const GeometryRef &ref : body.planarRefs) {
                    std::vector<ForgeCad::Kernel::PathSegment> pieces;
                    QString reason;
                    if (!ForgeCad::geometryRefPath(ref, index, sketches, bodies, pieces, &reason)) {
                        body.error = QStringLiteral("Bordo della superficie planare: %1.").arg(reason);
                        return;
                    }
                    appendDistinctSegments(segments, pieces);
                }
                body.forgeBody = ForgeCad::forgePlanarCurves(segments, &body.error);
            }
            body.solid = false;
            return;
        }
        case BodyFeature::Imported: {
            if (body.importData.isEmpty()) {
                // Documento del formato 8 (B-rep OpenCASCADE, non piu' letto).
                body.error = QStringLiteral("Il corpo va importato di nuovo da %1 (documento di una versione precedente).").arg(body.importSource);
                return;
            }
            body.forgeBody = ForgeCad::forgeImported(body.importData, &body.error);
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::Pattern: {
            if (body.patternComponent > 0) {
                const auto *source = operand(body.firstBody);
                if (!source || source->feature != BodyFeature::Pattern || !source->error.isEmpty()
                    || body.patternComponent - 1 >= source->patternResults.size()
                    || !source->patternResults.at(body.patternComponent - 1)
                    || (source->patternComponent == 0 && source->patternResults.at(body.patternComponent - 1) == source->forgeBody)) {
                    body.error = QStringLiteral("La componente %1 della ripetizione non esiste piu'.").arg(body.patternComponent + 1);
                    return;
                }
                body.forgeBody = source->patternResults.at(body.patternComponent - 1);
                body.solid = !body.forgeBody->isSheet();
                return;
            }
            const ExtrusionObject *base = operand(body.firstBody);
            if (!base || !isShapeBody(*base)) {
                body.error = QStringLiteral("Il corpo da ripetere non esiste piu' o non ha geometria.");
                return;
            }
            std::vector<ForgeCad::Kernel::Transform3> placements;
            if (!ForgeCad::patternPlacements(body.pattern, index, sketches, bodies, placements, &body.error)) return;
            if (body.pattern.featureOnly) {
                if (!ForgeCad::patternFeatureSupported(*base)) {
                    body.error = QStringLiteral("Scegli un'unione, una differenza o un'estrusione, rivoluzione o sweep solido che si unisce o sottrae.");
                    return;
                }
                // Lo strumento della booleana (o l'estrusione che si unisce o
                // sottrae) ripetuto, l'operazione una volta sola.
                ForgeCad::ForgeBody target, tool;
                BooleanOperation operation = BooleanOperation::Union;
                const auto uniteBodies = [&](QVector<int> list) -> ForgeCad::ForgeBody {
                    ForgeCad::ForgeBody united;
                    for (int i : list) {
                        const ExtrusionObject *b = operand(i);
                        if (!b || !b->forgeBody) return nullptr;
                        united = united ? ForgeCad::forgeBoolean(united, b->forgeBody, BooleanOperation::Union, &body.error) : b->forgeBody;
                        if (!united) return nullptr;
                    }
                    return united;
                };
                if (base->operation == 0 || base->operation == 2) {
                    const ExtrusionObject *first = operand(base->firstBody);
                    target = first ? first->forgeBody : nullptr;
                    tool = uniteBodies(QVector<int>{base->secondBody} + base->booleanTools);
                    operation = BooleanOperation(base->operation);
                } else if (mergingFeature(*base) && base->mergeOperation != 0 && !base->mergeBodies.isEmpty()) {
                    ExtrusionObject plain = *base;
                    plain.mergeOperation = 0;
                    plain.mergeProbe = false;
                    buildGeometry(plain, body.firstBody, sketches, bodies);
                    if (!plain.forgeBody) {
                        body.error = plain.error;
                        if (body.error.isEmpty()) body.error = QStringLiteral("Lo strumento della funzione non ha geometria valida.");
                        return;
                    }
                    tool = plain.forgeBody;
                    target = uniteBodies(base->mergeBodies);
                    operation = base->mergeOperation == 2 ? BooleanOperation::Difference : BooleanOperation::Union;
                }
                if (!target || !tool) {
                    if (body.error.isEmpty())
                        body.error = QStringLiteral("Gli operandi della funzione da ripetere non hanno geometria valida.");
                    return;
                }
                bool includeOriginal = !body.pattern.excluded.contains(0);
                if (body.secondBody >= 0) {
                    const auto *current = operand(body.secondBody);
                    if (!current || !current->forgeBody || current->modelBodyId != base->modelBodyId || body.secondBody <= body.firstBody) {
                        body.error = QStringLiteral("Il corpo di destinazione della funzione non e' valido."); return;
                    }
                    if (!includeOriginal) {
                        body.error = QStringLiteral("L'originale appartiene a una funzione precedente: non si puo' escludere mantenendo le lavorazioni successive."); return;
                    }
                    target = current->forgeBody;
                    includeOriginal = false; // gia' presente nel risultato delle lavorazioni successive
                    if (placements.empty()) { body.forgeBody = target; return; }
                }
                body.forgeBody = ForgeCad::forgePatternFeature(target, tool, operation, placements, &body.error, includeOriginal);
            } else {
                body.forgeBody = ForgeCad::forgePattern(base->forgeBody, placements, (body.pattern.kind != 2 || body.pattern.keepOriginal) && !body.pattern.excluded.contains(0), &body.error);
            }
            if (body.forgeBody && (body.patternComponent == 0
                || (body.patternComponent < 0 && hasDerivedPatternResults(index, bodies)))) {
                body.patternResults = ForgeCad::forgePatternComponents(body.forgeBody, &body.error);
                // Per copie disgiunte gli slot seguono i numeri originali,
                // inclusi i buchi delle esclusioni: le lavorazioni su un'altra
                // occorrenza non vengono spostate alla copia successiva.
                if (!body.pattern.featureOnly && !body.patternResults.isEmpty()) {
                    QString splitError;
                    const int perInstance = ForgeCad::forgePatternComponents(base->forgeBody, &splitError).size();
                    const int total = body.pattern.kind == 2 ? 2 : body.pattern.count * (body.pattern.kind == 0 ? body.pattern.count2 : 1);
                    QVector<int> active;
                    for (int i = 0; i < total; ++i)
                        if (!body.pattern.excluded.contains(i)
                            && (i != 0 || body.pattern.kind != 2 || body.pattern.keepOriginal)) active.append(i);
                    if (perInstance > 0 && body.patternResults.size() == perInstance * active.size()) {
                        QVector<ForgeCad::ForgeBody> resultSlots(total * perInstance);
                        int part = 0;
                        for (int occurrence : active)
                            for (int c = 0; c < perInstance; ++c) resultSlots[occurrence * perInstance + c] = body.patternResults.at(part++);
                        body.patternResults = std::move(resultSlots);
                    }
                }
                // I consumatori dei documenti precedenti al codec55 usano
                // l'intero aggregato, inclusi i suoi nomi topologici.
                if (body.patternComponent == 0) {
                    body.forgeBody.reset();
                    for (const auto &part : body.patternResults) if (part) { body.forgeBody = part; break; }
                }
            }
            body.solid = body.forgeBody && !body.forgeBody->isSheet();
            return;
        }
        case BodyFeature::DatumPlane:
            // I riferimenti si risolvono sulla geometria esatta dei corpi.
            body.datumValid = ForgeCad::computeDatum(body.datum, index, sketches, bodies, body.datumFrame, &body.error);
            return;
        case BodyFeature::Extrusion:
        case BodyFeature::Revolution: {
            const SketchObject *s = sketch(body.sketchIndex);
            if (!s) {
                body.error = QStringLiteral("Lo schizzo del corpo non esiste piu'.");
            } else if (body.feature == BodyFeature::Revolution) {
                if (body.revolveAxis == kRevolveAxisReference) {
                    // Asse scelto nella vista: una retta nel piano dello schizzo.
                    ForgeCad::ResolvedRef axis;
                    QPointF point, direction;
                    if (!ForgeCad::resolveGeometryRef(body.revolveAxisRef, index, sketches, bodies, axis, &body.error)) return;
                    if (!axis.hasLine) {
                        body.error = QStringLiteral("Il riferimento scelto come asse non e' una retta.");
                        return;
                    }
                    if (!ForgeCad::sketchLineFromWorld(*s, axis.point, axis.direction, point, direction, &body.error)) return;
                    body.forgeBody = ForgeCad::forgeRevolution(*s, point, direction, body.revolveAngle, &body.error);
                } else {
                    body.forgeBody = ForgeCad::forgeRevolution(*s, body.revolveAxis, body.revolveAngle, &body.error);
                }
                // Una superficie non si fonde con i solidi.
                if (body.forgeBody && !body.forgeBody->isSheet())
                    body.forgeBody = ForgeCad::forgeMergeFeatureResult(body, body.forgeBody, index, bodies, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
            } else {
                body.forgeBody = ForgeCad::forgeExtrusionFeature(body, index, sketches, bodies, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
            }
            return;
        }
        }
    } catch (const std::exception &failure) {
        body.error = QString::fromUtf8(failure.what());
    }
    if (body.error.isEmpty() && !hasGeometry(body)) body.error = QStringLiteral("Costruzione non riuscita.");
}
}  // namespace



void tessellateGeometry(const ExtrusionObject &body, int quality, BodyDisplay &display) {
    if (isDatumBody(body)) {
        display = {};  // disegnato come piano (drawDatumPlanes)
        display.quality = quality;
    } else if (body.curve) {
        ForgeCad::curveDisplay(*body.curve, quality, display);
        // Una polilinea per curva, nell'ordine di body.curves.
        for (int k = 1; k < body.curves.size(); ++k) {
            BodyDisplay more;
            ForgeCad::curveDisplay(*body.curves.at(k), quality, more);
            display.edges += more.edges;
        }
    } else if (body.forgeBody) ForgeCad::forgeTessellate(*body.forgeBody, quality, display);
    else display = {};
}

// Le copie separate di una ripetizione condividono la stessa mesh sulla
// GPU. Se le bounding box si toccano o si sovrappongono, il risultato
// booleano puo' avere facce diverse e resta la tassellazione finale.
void configurePatternInstances(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                                      const QVector<ExtrusionObject> &bodies) {
    body.display.instancedBase.reset();
    body.display.instanceTransforms.clear();
    if (body.operation >= 0 || body.feature != BodyFeature::Pattern || body.patternComponent >= 0 || body.pattern.featureOnly
        || body.firstBody < 0 || body.firstBody >= index || body.firstBody >= bodies.size()) return;
    const BodyDisplay &base = bodies.at(body.firstBody).display;
    if (base.vertices.isEmpty()) return;
    std::vector<ForgeCad::Kernel::Transform3> placements;
    QString error;
    if (!ForgeCad::patternPlacements(body.pattern, index, sketches, bodies, placements, &error)) return;
    const bool keepOriginal = (body.pattern.kind != 2 || body.pattern.keepOriginal) && !body.pattern.excluded.contains(0);
    QVector<QMatrix4x4> transforms;
    if (keepOriginal) transforms.append(QMatrix4x4());
    for (const ForgeCad::Kernel::Transform3 &t : placements) {
        const ForgeCad::Kernel::Vec3 p = t.translationPart();
        transforms.append(QMatrix4x4(
            float(t.matrix(0, 0)), float(t.matrix(0, 1)), float(t.matrix(0, 2)), float(p.x()),
            float(t.matrix(1, 0)), float(t.matrix(1, 1)), float(t.matrix(1, 2)), float(p.y()),
            float(t.matrix(2, 0)), float(t.matrix(2, 1)), float(t.matrix(2, 2)), float(p.z()),
            0.0f, 0.0f, 0.0f, 1.0f));
    }
    if (transforms.size() < 2) return;
    QVector3D lo = base.vertices.first(), hi = lo;
    for (const QVector3D &point : base.vertices) {
        lo.setX(qMin(lo.x(), point.x())); lo.setY(qMin(lo.y(), point.y())); lo.setZ(qMin(lo.z(), point.z()));
        hi.setX(qMax(hi.x(), point.x())); hi.setY(qMax(hi.y(), point.y())); hi.setZ(qMax(hi.z(), point.z()));
    }
    struct Bounds { QVector3D lo, hi; };
    QVector<Bounds> boxes;
    for (const QMatrix4x4 &matrix : transforms) {
        Bounds box;
        box.lo = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        box.hi = -box.lo;
        for (int mask = 0; mask < 8; ++mask) {
            const QVector3D corner(mask & 1 ? hi.x() : lo.x(), mask & 2 ? hi.y() : lo.y(), mask & 4 ? hi.z() : lo.z());
            const QVector3D point = matrix.map(corner);
            box.lo.setX(qMin(box.lo.x(), point.x())); box.lo.setY(qMin(box.lo.y(), point.y())); box.lo.setZ(qMin(box.lo.z(), point.z()));
            box.hi.setX(qMax(box.hi.x(), point.x())); box.hi.setY(qMax(box.hi.y(), point.y())); box.hi.setZ(qMax(box.hi.z(), point.z()));
        }
        boxes.append(box);
    }
    const float tolerance = 1e-5f * qMax(1.0f, (hi - lo).length());
    for (int i = 0; i < boxes.size(); ++i)
        for (int j = i + 1; j < boxes.size(); ++j) {
            const bool separated = boxes.at(i).hi.x() < boxes.at(j).lo.x() - tolerance || boxes.at(j).hi.x() < boxes.at(i).lo.x() - tolerance
                || boxes.at(i).hi.y() < boxes.at(j).lo.y() - tolerance || boxes.at(j).hi.y() < boxes.at(i).lo.y() - tolerance
                || boxes.at(i).hi.z() < boxes.at(j).lo.z() - tolerance || boxes.at(j).hi.z() < boxes.at(i).lo.z() - tolerance;
            if (!separated) return;
        }
    auto shared = std::make_shared<BodyDisplay>(base);
    shared->instancedBase.reset();
    shared->instanceTransforms.clear();
    body.display.instancedBase = std::move(shared);
    body.display.instanceTransforms = std::move(transforms);
}


}
