#pragma once

// Una feature soppressa non usa il proprio profilo: conserva lo stadio
// precedente anche se l'appoggio o un vincolo del profilo non e' risolvibile.
static void historySuppressionTests() {
    using namespace ForgeCad;
    using namespace Kernel;
    ExtrusionObject previous;
    previous.feature = BodyFeature::Primitive;
    previous.featureId = 100;
    previous.modelBodyId = 10;
    previous.forgeBody = std::make_shared<const Body>(makeBox(Frame3{}, 3, 4, 5));
    previous.solid = true;
    const QVector<ExtrusionObject> stages{previous};
    for (int invalid = 0; invalid < 3; ++invalid) {
        SketchObject sketch;
        sketch.name = QStringLiteral("Profilo non risolvibile");
        sketch.segments = {{{0, 0}, {1, 0}}};
        if (invalid == 0) sketch.supportError = QStringLiteral("Appoggio eliminato");
        else {
            SketchConstraint constraint;
            constraint.type = ConstraintType::Pierce;
            constraint.first = {0, 0, 0};
            if (invalid == 2) {
                auto pierce = std::make_shared<SketchPierceData>();
                pierce->error = QStringLiteral("Faccia non risolta");
                constraint.pierce = std::move(pierce);
            }
            sketch.geometricConstraints.append(constraint);
        }
        const QVector<SketchObject> sketches{sketch};
        ExtrusionObject feature;
        feature.feature = BodyFeature::Extrusion;
        feature.featureId = 101;
        feature.modelBodyId = previous.modelBodyId;
        feature.sketchIndex = 0;
        feature.mergeOperation = 1;
        feature.mergeBodies = {0};
        feature.suppressed = true;
        buildGeometry(feature, 1, sketches, stages);
        require(feature.error.isEmpty() && feature.forgeBody == previous.forgeBody && feature.solid,
                "feature soppressa bypassa appoggio/Pierce e conserva lo stadio precedente");

        // Non basta nascondere l'errore: lo stadio deve essere utilizzabile
        // dalla lavorazione successiva dello stesso corpo.
        ExtrusionObject downstream;
        downstream.feature = BodyFeature::Scale;
        downstream.featureId = 102;
        downstream.modelBodyId = previous.modelBodyId;
        downstream.firstBody = 1;
        downstream.scaleFactor = 2;
        buildGeometry(downstream, 2, sketches, {previous, feature});
        require(downstream.error.isEmpty() && downstream.forgeBody && downstream.solid,
                "lavorazione dopo la feature soppressa ancora calcolabile");

        feature.suppressed = false;
        buildGeometry(feature, 1, sketches, stages);
        require(!feature.error.isEmpty() && !feature.forgeBody,
                "riattivare la feature mantiene la validazione di appoggio/Pierce");
    }
}
