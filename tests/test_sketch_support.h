#pragma once

// Incluso nel friend ViewportInteractionTest.
static void sketchSupportRegressions() {
    using namespace ForgeCad;
    using namespace Kernel;
    DocumentState state;
    ExtrusionObject base;
    base.feature = BodyFeature::Primitive; base.featureId = 101; base.modelBodyId = 1;
    base.name = QStringLiteral("Base appoggio");
    base.primitive.size[0] = base.primitive.size[1] = base.primitive.size[2] = 4;
    buildGeometry(base, 0, {}, {});
    require(base.forgeBody && base.error.isEmpty(), "base per appoggio nominato");
    FaceId top;
    for (FaceId face : base.forgeBody->faces())
        if (projectPoint(*base.forgeBody->face(face).surface, Vec3(2,2,4)).distance < 1e-10) top = face;
    require(top.valid(), "faccia superiore della base");
    SketchObject sketch;
    sketch.name = QStringLiteral("Profilo con appoggio"); sketch.plane = kFacePlane;
    sketch.frame.origin[2] = 4;
    sketch.segments = {{{1,1},{2,1}},{{2,1},{2,2}},{{2,2},{1,2}},{{1,2},{1,1}}};
    auto support = std::make_shared<GeometryRef>();
    support->kind = 5; support->index = 0; support->featureId = base.featureId;
    support->point = faceReference(*base.forgeBody, top, Vec3(2,2,4));
    require(!support->point.name.isEmpty(), "appoggio con nome genealogico");
    sketch.support = support; sketch.faceSource = base.name;
    ExtrusionObject consumer;
    consumer.featureId = 102; consumer.modelBodyId = 2; consumer.sketchIndex = 0;
    consumer.plane = kFacePlane; consumer.distance = 1; consumer.name = QStringLiteral("Estrusione del profilo");
    state.sketches = {sketch}; state.extrusions = {base,consumer};
    CadViewport view; view.loadDocument(state);
    auto moved = view.extrusions()[0]; moved.primitive.size[2] = 5;
    require(view.updateBody(0,moved).isEmpty() && view.sketches()[0].supportError.isEmpty()
            && std::abs(view.sketches()[0].frame.origin[2]-5) < 1e-12,
            "la stessa faccia nominata segue la modifica parametrica");
    auto missing = std::make_shared<GeometryRef>(*view.sketches_[0].support);
    missing->point.name = QStringLiteral("faccia.eliminata");
    view.sketches_[0].support = missing;
    moved = view.extrusions()[0]; moved.primitive.size[2] = 7;
    require(view.updateBody(0,moved).isEmpty(), "modifica della base con appoggio perso");
    require(!view.sketches()[0].supportError.isEmpty() && view.sketches()[0].frame.origin[2] == 5
            && !view.extrusions()[1].forgeBody && !view.extrusions()[1].error.isEmpty(),
            "nome perso: piano precedente conservato, nessun riaggancio per indice o geometria");
    GeometryRef xy; xy.kind = 1; xy.index = 0;
    require(view.changeSketchSupport(0,xy).isEmpty() && view.sketches()[0].plane == 0
            && !view.sketches()[0].support && view.extrusions()[1].plane == 0 && view.extrusions()[1].forgeBody,
            "cambio esplicito su XY aggiorna schizzo, definizione e geometria");
    view.undo();
    require(view.sketches()[0].plane == kFacePlane && view.extrusions()[1].plane == kFacePlane,
            "Undo ripristina entrambi i piani");
    view.redo();
    require(view.sketches()[0].plane == 0 && view.extrusions()[1].plane == 0, "Redo ripristina XY");
    QByteArray bytes; DocumentState decoded;
    require(encodeDocument(view.currentDocument(),bytes,false).isEmpty() && decodeDocument(bytes,decoded).isEmpty(),
            "codec dell'appoggio corretto senza cache");
    CadViewport reopened; reopened.loadDocument(decoded);
    require(reopened.rebuildAllFeatures() == 0 && reopened.sketches()[0].plane == 0
            && reopened.extrusions()[1].plane == 0 && reopened.extrusions()[1].forgeBody,
            "riapertura e rebuild conservano il piano scelto");
    auto legacy = *support; legacy.point.name.clear();
    ResolvedRef resolved; QString error;
    require(resolveSketchSupport(legacy,2,view.sketches(),view.extrusions(),resolved,&error) && resolved.hasPlane,
            "appoggi legacy senza nome ancora leggibili");
}
