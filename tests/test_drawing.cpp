#include "cad_drawing.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_boolean.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_primitives.h"
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_sheet.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void close(double a, double b, const char *message) { require(std::isfinite(a) && std::fabs(a - b) < 1e-7, message); }
constexpr quint64 kFeature = (quint64(1) << 60) + 3;
DrawingSourceBody source(Body body, quint64 id = kFeature) {
    auto ptr = std::make_shared<Body>(std::move(body));
    auto names = std::make_shared<ElementNames>(propagateNames(*ptr, makeHistory({}, *ptr), {}, "f" + std::to_string(id)));
    return {id, ptr, names};
}
DrawingView verticalView(double y = 0) {
    DrawingView view;
    view.plane = Frame3(Vec3(0,y,0), Vec3(0,1,0), Vec3(1,0,0));
    return view;
}
DrawingDocument drawing(const DrawingModelSnapshot &model, const DrawingView &view, const DrawingSection &section) {
    DrawingDocument d; d.model = model.version; d.views = {view}; d.dimensions = {dimensionOfSection(view, section)}; return d;
}
DrawingDimensionResult measure(const DrawingDocument &drawing, const DrawingModelSnapshot &model) {
    return evaluateDrawingDimension(drawing, model, drawing.dimensions.front().id);
}
void ready(const DrawingDocument &drawing, const DrawingModelSnapshot &model, double expected, const char *message) {
    const auto r = measure(drawing, model);
    if (r.status != DrawingDimensionStatus::Ready) throw std::runtime_error(r.message.toStdString());
    close(r.value, expected, message);
}
std::vector<DrawingSection> sections(const DrawingModelSnapshot &model, const DrawingView &view) {
    std::vector<DrawingSection> result;
    const auto error = sectionDrawingView(model, view, result);
    if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
    return result;
}

// Patch razionale aperta, costruita con bordi isoparametrici esatti.
Body nurbsPatch() {
    auto s = std::make_shared<BSplineSurface>(2, 1, std::vector<double>{0,0,0,1,1,1},
        std::vector<double>{0,0,1,1}, 3, 2,
        std::vector<Vec3>{{-5,-4,0},{-5,4,0},{0,-4,3},{0,4,3},{5,-4,0},{5,4,0}},
        std::vector<double>{1,1,0.7,0.7,1,1});
    const std::vector<Vec3> points{s->point(0,0),s->point(1,0),s->point(1,1),s->point(0,1)};
    const std::vector<Body::BuildEdge> edges{{0,1,s->vIso(0),{0,1},0}, {1,2,s->uIso(1),{0,1},0},
                                          {3,2,s->vIso(1),{0,1},0}, {0,3,s->uIso(0),{0,1},0}};
    Body::BuildFace face; face.surface = s;
    face.loops = {{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
    Body body = Body::buildSheet(points, edges, {face});
    computePCurves(body);
    return body;
}
}

int main() {
    try {
        // Il piano attraversa l'interno di una lamina: nessun edge originale
        // giace sulla sezione, nessun volume e nessuna chiusura artificiale.
        DrawingModelSnapshot model{{QUuid::createUuid(), {}}, {source(makePlaneSheet(Frame3(), 5))}};
        auto view = verticalView();
        const auto first = sections(model, view);
        require(first.size() == 1 && first[0].sourceFace.valid(), "provenienza della sezione della lamina");
        auto document = drawing(model, view, first[0]);
        ready(document, model, 10, "lunghezza della sezione della superficie");
        for (double scale : {0.1, 1.0, 10.0}) {
            document.views[0].scale = scale;
            ready(document, model, 10, "la scala del foglio non modifica la quota");
        }
        // Variazione della geometria, poi ricostruzione del corpo dalla cache.
        model.bodies = {source(makePlaneSheet(Frame3(), 8))};
        ready(document, model, 16, "quota aggiornata dopo variazione della superficie");
        model.bodies = {source(readBodyBinary(writeBodyBinary(*model.bodies[0].body)))};
        ready(document, model, 16, "quota dopo riapertura del B-rep");
        QByteArray encoded;
        require(encodeDrawingDocument(document, encoded).isEmpty(), "codifica della tavola");
        QTemporaryDir directory; require(directory.isValid(), "directory temporanea");
        QFile file(directory.filePath("tavola.prototipo"));
        require(file.open(QIODevice::WriteOnly) && file.write(encoded) == encoded.size(), "salvataggio della tavola"); file.close();
        require(file.open(QIODevice::ReadOnly), "riapertura della tavola");
        DrawingDocument restored;
        require(decodeDrawingDocument(file.readAll(), restored).isEmpty(), "lettura della tavola"); file.close();
        require(restored.version.documentId == document.version.documentId && restored.views[0].id == view.id
                && restored.dimensions[0].id == document.dimensions[0].id
                && restored.dimensions[0].section.featureId == kFeature, "identita' persistenti anche oltre 53 bit");
        ready(restored, model, 16, "quota associata dopo salvataggio e riapertura");
        restored.views[0].plane = verticalView(50).plane;
        require(measure(restored, model).status == DrawingDimensionStatus::Missing, "piano spostato fuori dalla superficie");
        restored.views[0].plane = verticalView(3).plane;
        ready(restored, model, 16, "piano di vista traslato");
        std::cout << "PASS lamina aperta, rigenerazione, scala e persistenza\n";

        // Lamina cilindrica: sezione circolare interna alla faccia, senza caps.
        const Body cylinder = makeCylinder(Frame3(), 3, 10);
        FaceId side;
        for (FaceId f : cylinder.faces()) if (cylinder.face(f).surface->type() == SurfaceType::Cylinder) side = f;
        auto curved = source(facesAsSheet(cylinder, {side}));
        DrawingModelSnapshot curvedModel{model.version, {curved}};
        DrawingView horizontal; horizontal.plane = Frame3(Vec3(0,0,4), Vec3(0,0,1), Vec3(1,0,0));
        const auto circular = sections(curvedModel, horizontal);
        require(circular.size() == 1, "sezione circolare della lamina cilindrica");
        const auto circleDrawing = drawing(curvedModel, horizontal, circular[0]);
        ready(circleDrawing, curvedModel, 6*kPi, "circonferenza della sezione della superficie curva");
        auto chordDrawing = circleDrawing; chordDrawing.dimensions[0].measure = DrawingMeasure::EndpointDistance;
        ready(chordDrawing, curvedModel, 0, "corda e lunghezza d'arco sono misure distinte");
        DrawingView tangent; tangent.plane = Frame3(Vec3(3,0,0), Vec3(1,0,0), Vec3(0,0,1));
        const auto tangentPieces = sections(curvedModel, tangent);
        require(!tangentPieces.empty() && tangentPieces[0].tangent, "generatrice tangente dichiarata della superficie");
        ready(drawing(curvedModel, tangent, tangentPieces[0]), curvedModel, 10, "sezione tangente del cilindro");
        DrawingView oblique;
        oblique.plane = Frame3(Vec3(0,0,5), Vec3(1,0,1), Vec3(0,1,0));
        const auto elliptic = sections(curvedModel, oblique);
        require(elliptic.size() == 1, "sezione obliqua ellittica");
        const Ellipse<2> expectedEllipse(Vec2(0,0),Vec2(1,0),Vec2(0,1),3,3*std::sqrt(2.0));
        const double ellipseLength = arcLength(expectedEllipse);
        auto obliqueDrawing = drawing(curvedModel, oblique, elliptic[0]);
        ready(obliqueDrawing, curvedModel, ellipseLength, "misura dell'ellisse obliqua");
        QByteArray obliqueBytes;
        require(encodeDrawingDocument(obliqueDrawing, obliqueBytes).isEmpty(), "salvataggio piano orientato");
        require(decodeDrawingDocument(obliqueBytes, restored).isEmpty(), "riapertura piano orientato");
        ready(restored, curvedModel, ellipseLength, "misura del piano orientato dopo riapertura");
        std::cout << "PASS superficie curva e generatrice tangente\n";

        // Il foro interrompe la sezione: stessa faccia, due rami distinti.
        const Body holed = makePrism(Frame3(), {{-5,-5},{5,-5},{5,5},{-5,5}},
                                    {{{-1,-1},{-1,1},{1,1},{1,-1}}}, 2);
        FaceId bottom;
        for (FaceId f : holed.faces()) if (holed.face(f).surface->type() == SurfaceType::Plane) {
            const auto &p = static_cast<const Plane &>(*holed.face(f).surface).frame();
            if (std::fabs(p.origin().z()) < 1e-8 && std::fabs(p.zDir().z()) > 0.99) bottom = f;
        }
        require(bottom.valid(), "faccia piana forata");
        DrawingModelSnapshot withHole{model.version, {source(facesAsSheet(holed, {bottom}))}};
        const auto branches = sections(withHole, view);
        require(branches.size() == 2, "sezione limitata dal foro della faccia");
        auto branchDrawing = drawing(withHole, view, branches[0]);
        ready(branchDrawing, withHole, 4, "ramo scelto con ancoraggio interno");
        QByteArray branchBytes;
        require(encodeDrawingDocument(branchDrawing, branchBytes).isEmpty(), "persistenza ramo multiplo");
        DrawingDocument branchReload;
        require(decodeDrawingDocument(branchBytes, branchReload).isEmpty()
                && branchReload.dimensions[0].section.requiresWitness, "regola di disambiguazione conservata");
        const double opposite = branchDrawing.dimensions[0].section.witness.x() < 0 ? 3 : -3;
        DrawingModelSnapshot surviving{model.version, {source(makePlaneSheet(Frame3(Vec3(opposite,0,0),Vec3(0,0,1),Vec3(1,0,0)),1))}};
        require(measure(branchReload, surviving).status == DrawingDimensionStatus::Missing,
                "ramo eliminato non riassociato all'unico superstite");
        branchDrawing.dimensions[0].section.witness = Vec2(0,0);
        require(measure(branchDrawing, withHole).status == DrawingDimensionStatus::Ambiguous, "nessuna scelta silenziosa del ramo piu' vicino");
        require(std::isnan(measure(branchDrawing, withHole).value), "nessuna vecchia misura quando il riferimento e' ambiguo");
        branchDrawing.dimensions[0].section.faceName = QStringLiteral("nome-eliminato");
        require(measure(branchDrawing, withHole).status == DrawingDimensionStatus::Missing, "nome perso senza ripiego per indice");
        auto coincidentView = DrawingView();
        const auto boundary = sections(withHole, coincidentView);
        require(boundary.size() == 8, "bordi esterno e foro per piano coincidente");
        require(std::all_of(boundary.begin(), boundary.end(), [](const auto &p) { return p.coincident; }), "coincidenza dichiarata");
        std::cout << "PASS fori, rami multipli, riferimenti persi e coincidenza\n";

        DrawingModelSnapshot splineModel{model.version, {source(nurbsPatch())}};
        const auto splineSections = sections(splineModel, view);
        require(splineSections.size() == 1, "sezione interna della patch NURBS aperta");
        const auto &surface = *splineModel.bodies[0].body->face(splineModel.bodies[0].body->faces()[0]).surface;
        const double expectedSplineLength = arcLength(*surface.vIso(0.5), Interval{0,1});
        const auto splineDrawing = drawing(splineModel, view, splineSections[0]);
        ready(splineDrawing, splineModel, expectedSplineLength, "misura della sezione NURBS rispetto all'isoparametrica nota");
        std::cout << "PASS sezione interna di superficie NURBS razionale\n";

        // Archivio: nessun collegamento implicito a un check-in diverso.
        auto pinned = document;
        const QUuid oldCheckin = QUuid::createUuid(), newCheckin = QUuid::createUuid();
        pinned.model.checkinId = oldCheckin;
        auto archivedModel = model; archivedModel.version.checkinId = oldCheckin;
        ready(pinned, archivedModel, 16, "versione modello fissata");
        archivedModel.version.checkinId = newCheckin;
        require(measure(pinned, archivedModel).status == DrawingDimensionStatus::VersionMismatch, "versione nuova rifiutata senza aggiornamento esplicito");
        require(retargetDrawingModel(pinned, archivedModel.version).isEmpty(), "aggiornamento esplicito del modello");
        ready(pinned, archivedModel, 16, "quota dopo aggiornamento di versione");
        pinned.version.checkinId = QUuid::createUuid();
        require(!retargetDrawingModel(pinned, model.version).isEmpty(), "vietato aggiornare tavola archiviata");
        require(!retargetDrawingModel(document, {QUuid::createUuid(), {}}).isEmpty(), "documento modello diverso rifiutato");
        require(encodeDrawingDocument(pinned, encoded).isEmpty(), "persistenza dei riferimenti ai check-in");
        require(decodeDrawingDocument(encoded, restored).isEmpty() && restored.model.checkinId == newCheckin
                && restored.version.checkinId == pinned.version.checkinId, "check-in conservati dopo riapertura");
        std::cout << "PASS riferimenti alle versioni e aggiornamento esplicito\n";

        // Provenienza separata sulle giunzioni: API storica conserva i conteggi.
        const Body box = makeBox(Frame3(), 4, 4, 4);
        const auto beforeBytes = writeBodyBinary(box);
        const Body cutting = makePlaneSheet(Frame3(Vec3(0,0,0), Vec3(0,1,0), Vec3(1,0,0)), 20);
        const auto legacy = bodySectionCurves(cutting, box);
        const auto declared = bodySectionCurvesWithOrigins(cutting, box);
        require(declared.size() > legacy.size() && legacy.size() == 4, "API storica e provenienza separata sulle giunzioni");
        for (const auto &p : declared) require(p.firstFace.valid() && p.secondFace.valid(), "coppia di facce emessa dall'algoritmo");
        require(beforeBytes == writeBodyBinary(box), "sezioni non modificano il corpo sorgente");
        DrawingModelSnapshot solidModel{model.version, {source(box)}};
        auto middle = horizontal; middle.plane = Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));
        const auto solidSections = sections(solidModel, middle);
        require(solidSections.size() == 4, "sezione del solido");
        ready(drawing(solidModel, middle, solidSections[0]), solidModel, 4, "quota su sezione del solido");

        // Errori del codec e della geometria non pubblicano uno stato parziale.
        auto invalid = QJsonDocument::fromJson(encoded).object(); invalid["schema"] = 2;
        const auto previousId = restored.version.documentId;
        require(!decodeDrawingDocument(QJsonDocument(invalid).toJson(), restored).isEmpty()
                && restored.version.documentId == previousId, "schema sconosciuto rifiutato atomicamente");
        require(!decodeDrawingDocument(encoded.left(encoded.size()/2), restored).isEmpty(), "file troncato rifiutato");
        invalid = QJsonDocument::fromJson(encoded).object();
        auto invalidViews = invalid["views"].toArray(); auto invalidView = invalidViews[0].toObject();
        invalidView["x"] = QJsonArray{0,0,0}; invalidViews[0] = invalidView; invalid["views"] = invalidViews;
        require(!decodeDrawingDocument(QJsonDocument(invalid).toJson(), restored).isEmpty(), "assi degeneri rifiutati");
        auto duplicate = document; duplicate.views.push_back(duplicate.views.front());
        require(!encodeDrawingDocument(duplicate, encoded).isEmpty(), "identita' duplicate rifiutate");
        auto invalidPlane = view; invalidPlane.tolerance = -1;
        auto retained = first;
        require(!sectionDrawingView(model, invalidPlane, retained).isEmpty() && retained[0].curve == first[0].curve,
                "errore geometrico senza output parziale");
        auto staleNames = model; staleNames.bodies[0].names = curved.names;
        require(!sectionDrawingView(staleNames, view, retained).isEmpty(), "nomi appartenenti ad altro corpo rifiutati");
        auto duplicateSource = model; duplicateSource.bodies.push_back(duplicateSource.bodies.front());
        require(measure(document, duplicateSource).status == DrawingDimensionStatus::Invalid, "feature duplicata senza scelta del primo corpo");
        bool otherViewRejected = false;
        try { dimensionOfSection(DrawingView(), first[0]); } catch (const std::invalid_argument &) { otherViewRejected = true; }
        require(otherViewRejected, "sezione appartenente ad altra vista rifiutata");
        std::cout << "PASS provenienza e rifiuto atomico dei dati incoerenti\n";
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
