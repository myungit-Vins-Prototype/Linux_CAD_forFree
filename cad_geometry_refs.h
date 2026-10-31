#pragma once

#include "cad_types.h"
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace ForgeCad {

// Enumerazione strutturale, senza decidere se il parametro e' attivo per la
// feature corrente. Dipendenze, migrazione e diagnostica applicano ciascuna
// la propria policy nel callback; ordine e duplicati restano al chiamante.
// NON e' il visitatore del codec binario: il suo ordine storico resta separato.
enum class GeometryRefFamily {
    Datum, Pattern, SurfaceTargets, RuledFirst, RuledSecond, Planar,
    LoftStartFaces, LoftEndFaces, LoftSections, LoftEdgeGuides,
    Extent, MoveAxis, RevolveAxis, DraftNeutral, ThickenDirection,
    SweepProfile, SweepPath, ExtractedSource, ExtractedSources, ExtractedTool,
    Count
};

template<class Feature, class Visit>
void visitGeometryRefs(Feature &feature, GeometryRefFamily family, Visit &&visit) {
    const auto list = [&](auto &refs) {
        for (int i = 0; i < refs.size(); ++i) visit(refs[i], family, i);
    };
    switch (family) {
    case GeometryRefFamily::Datum: list(feature.datum.refs); break;
    case GeometryRefFamily::Pattern: list(feature.pattern.refs); break;
    case GeometryRefFamily::SurfaceTargets: list(feature.surfaceTool.targets); break;
    case GeometryRefFamily::RuledFirst: list(feature.ruledFirst); break;
    case GeometryRefFamily::RuledSecond: list(feature.ruledSecond); break;
    case GeometryRefFamily::Planar: list(feature.planarRefs); break;
    case GeometryRefFamily::LoftStartFaces: list(feature.loftStartFaces); break;
    case GeometryRefFamily::LoftEndFaces: list(feature.loftEndFaces); break;
    case GeometryRefFamily::LoftSections: list(feature.loftSections); break;
    case GeometryRefFamily::LoftEdgeGuides: list(feature.loftEdgeGuides); break;
    case GeometryRefFamily::Extent: visit(feature.extentRef, family, 0); break;
    case GeometryRefFamily::MoveAxis: visit(feature.move.axis, family, 0); break;
    case GeometryRefFamily::RevolveAxis: visit(feature.revolveAxisRef, family, 0); break;
    case GeometryRefFamily::DraftNeutral: visit(feature.draftNeutral, family, 0); break;
    case GeometryRefFamily::ThickenDirection: visit(feature.thickenDirection, family, 0); break;
    case GeometryRefFamily::SweepProfile: visit(feature.sweepProfileRef, family, 0); break;
    case GeometryRefFamily::SweepPath: visit(feature.sweepPathRef, family, 0); break;
    case GeometryRefFamily::ExtractedSource: visit(feature.curveExtraction.source, family, 0); break;
    case GeometryRefFamily::ExtractedSources: list(feature.curveExtraction.sources); break;
    case GeometryRefFamily::ExtractedTool: visit(feature.curveExtraction.tool, family, 0); break;
    case GeometryRefFamily::Count: break;
    }
}

template<class Feature, class Visit>
void visitGeometryRefs(Feature &feature, std::initializer_list<GeometryRefFamily> families, Visit &&visit) {
    for (auto family : families) visitGeometryRefs(feature, family, visit);
}

template<class Feature, class Visit>
void visitGeometryRefs(Feature &feature, Visit &&visit) {
    for (int family = 0; family < int(GeometryRefFamily::Count); ++family)
        visitGeometryRefs(feature, GeometryRefFamily(family), visit);
}

enum class SketchGeometryRefFamily { Support, Pierce, Linked };

// La variante mutabile distacca i riferimenti condivisi prima del callback:
// le copie del documento conservate dall'Undo non vengono modificate.
template<class Sketch, class Visit>
void visitSketchGeometryRefs(Sketch &sketch, Visit &&visit) {
    if (sketch.support) {
        if constexpr (std::is_const_v<Sketch>) {
            visit(std::as_const(*sketch.support), SketchGeometryRefFamily::Support, 0);
        } else {
            auto ref = std::make_shared<GeometryRef>(*sketch.support);
            visit(*ref, SketchGeometryRefFamily::Support, 0);
            sketch.support = std::move(ref);
        }
    }
    for (int i = 0; i < sketch.linkedReferences.size(); ++i) {
        auto &link = sketch.linkedReferences[i];
        if (!link) continue;
        if constexpr (std::is_const_v<Sketch>) {
            visit(std::as_const(link->source), SketchGeometryRefFamily::Linked, i);
        } else {
            auto data = std::make_shared<SketchLinkedReference>(*link);
            visit(data->source, SketchGeometryRefFamily::Linked, i);
            link = std::move(data);
        }
    }
    for (int i = 0; i < sketch.geometricConstraints.size(); ++i) {
        auto &constraint = sketch.geometricConstraints[i];
        if (!constraint.pierce) continue;
        if constexpr (std::is_const_v<Sketch>) {
            visit(std::as_const(constraint.pierce->surface), SketchGeometryRefFamily::Pierce, i);
        } else {
            auto data = std::make_shared<SketchPierceData>(*constraint.pierce);
            visit(data->surface, SketchGeometryRefFamily::Pierce, i);
            constraint.pierce = std::move(data);
        }
    }
}

}
