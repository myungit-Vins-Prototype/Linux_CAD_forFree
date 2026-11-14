#include "cad_drawing_center_selection.h"
#include "cad_drawing_detail.h"
#include "cad_drawing_editor.h"
#include "cad_drawing_local_section.h"
#include "cad_drawing_local_section_display.h"
#include "fk_boolean.h"
#include "fk_extrude.h"
#include "fk_primitives.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void need(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void ok(QString s) {
    if (!s.isEmpty())
        throw std::runtime_error(s.toStdString());
    QApplication::processEvents();
}
void near(double a, double b, const char *m) { need(std::abs(a - b) < 1e-6, m); }
DrawingSourceBody source(Body b, quint64 id) {
    auto p = std::make_shared<Body>(std::move(b));
    return {id, p,
            std::make_shared<ElementNames>(
                propagateNames(*p, makeHistory({}, *p), {}, "local" + std::to_string(id)))};
}
QByteArray bytes(const DrawingSheet &s) {
    QByteArray b;
    ok(encodeDrawingSheet(s, b));
    return b;
}
QGraphicsItem *item(DrawingSheetEditor &e, const QString &kind) {
    for (auto *i : e.canvas()->scene()->items())
        if (i->data(0).toString() == kind)
            return i;
    return nullptr;
}
void mouse(DrawingSheetEditor &e, QPointF p, bool click = false) {
    auto *v = e.canvas();
    QPointF q = v->mapFromScene(p), g = v->viewport()->mapToGlobal(q.toPoint());
    QMouseEvent a(click ? QEvent::MouseButtonPress : QEvent::MouseMove, q, g,
                  click ? Qt::LeftButton : Qt::NoButton, click ? Qt::LeftButton : Qt::NoButton,
                  Qt::NoModifier);
    QApplication::sendEvent(v->viewport(), &a);
    if (click) {
        QMouseEvent b(QEvent::MouseButtonRelease, q, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v->viewport(), &b);
    }
    QApplication::processEvents();
}
void key(DrawingSheetEditor &e, int code) {
    QKeyEvent a(QEvent::KeyPress, code, Qt::NoModifier);
    QApplication::sendEvent(e.canvas(), &a);
    QApplication::processEvents();
}
DrawingSheet blank(const DrawingEditorModel &m) {
    DrawingSheet s;
    s.drawing.model = m.snapshot.version;
    s.size = {297, 210};
    s.titleBlock.visible = false;
    return s;
}
const DrawingCurve &circle(const DrawingView &v, const std::vector<DrawingCurve> &curves, quint64 id) {
    for (const auto &c : curves)
        if (c.featureId == id && drawingPrimitive(v, c).kind == DrawingPrimitive::Kind::Conic)
            return c;
    throw std::runtime_error("bordo circolare assente");
}
void localSectionReferenceCodec(const DrawingEditorModel &model, const DrawingView &view,
                                const std::vector<DrawingCurve> &curves) {
    auto sheet = blank(model);
    sheet.drawing.views = {view};
    sheet.views = {{view.id, "Agganci e sezione locale", {85, 75}, {1, 2}}};
    const auto &rim = circle(view, curves, 1);
    const auto center = drawingAttachment(view, rim, model.snapshot, DrawingPointRole::Center);
    const auto point = drawingAttachment(view, rim, model.snapshot, DrawingPointRole::Nearest,
                                         .5 * (rim.range.lo + rim.range.hi));
    DrawingAxis axis;
    axis.viewId = view.id;
    axis.references = {center, point};
    sheet.axes = {axis};
    DrawingAnnotation note;
    note.viewId = view.id;
    note.text = "Centro del cerchio";
    note.reference = center;
    note.position = {160, 40};
    DrawingAnnotation axisNote = note;
    axisNote.id = QUuid::createUuid();
    axisNote.text = "Punto medio dell'asse";
    DrawingAttachment middle;
    middle.axisId = axis.id;
    middle.role = DrawingPointRole::Midpoint;
    axisNote.reference = middle;
    axisNote.position = {160, 65};
    sheet.annotations = {note, axisNote};
    sheet.centerMarks = {drawingCenterMark(view, rim, model.snapshot)};
    const auto radius = drawingDimensionBetween(view, model.snapshot, curves,
        {drawingAttachment(view, rim, model.snapshot, DrawingPointRole::Curve)},
        DrawingMeasure::Radius, {10, 10});
    sheet.drawing.dimensions = {radius};
    sheet.dimensions = {{radius.id, {115, 45}}};

    // Un taglio su un altro cerchio non altera gli agganci gia' presenti.
    const DrawingLocalSection local{{{16, -4}, {24, -4}, {24, 4}, {16, 4}}, 10, 2};
    for (int placement = 0; placement != 3; ++placement) {
        auto candidate = sheet;
        if (placement == 1) {
            candidate.drawing.views[0].localSection = local;
        } else if (placement == 2) {
            auto other = view;
            other.id = QUuid::createUuid();
            other.localSection = local;
            candidate.drawing.views.push_back(other);
            candidate.views.append({other.id, "Altra vista sezionata", {160, 145}, {1, 2}});
        }
        const auto saved = bytes(candidate);
        const auto root = QJsonDocument::fromJson(saved).object();
        if (placement) {
            need(root["axisReferenceSchema"].toInt() == 14 &&
                     root["annotationReferenceSchema"].toInt() == 14 &&
                     root["centerReferenceSchema"].toInt() == 14,
                 "schema14 per tutti gli agganci anche se il taglio e' su un'altra vista");
        }
        DrawingSheet restored;
        ok(decodeDrawingSheet(saved, restored));
        need(bytes(restored) == saved, "riapertura conserva sezioni, UUID, quote, assi, note e centri");
        DrawingSheetEditor reopened(restored, model);
        need(reopened.axisResult(axis.id).status == DrawingDimensionStatus::Ready &&
                 reopened.annotationResult(note.id).status == DrawingDimensionStatus::Ready &&
                 reopened.annotationResult(axisNote.id).status == DrawingDimensionStatus::Ready,
             "agganci geometrici e note collegate agli assi pronti dopo riapertura");
        need(reopened.dimensionResult(radius.id).status == DrawingDimensionStatus::Ready,
             "quota radiale pronta dopo riapertura della sezione locale");
        near(reopened.dimensionResult(radius.id).value, 5, "raggio conservato con la sezione locale");
        if (!placement)
            continue;
        for (const auto *key : {"axisReferenceSchema", "annotationReferenceSchema", "centerReferenceSchema"}) {
            for (const auto &schema : {QJsonValue(13), QJsonValue(15), QJsonValue(14.5),
                                      QJsonValue(QStringLiteral("14")), QJsonValue(QJsonValue::Null)}) {
                auto malformed = root;
                malformed[key] = schema;
                need(!decodeDrawingSheet(QJsonDocument(malformed).toJson(QJsonDocument::Compact), restored).isEmpty() &&
                         bytes(restored) == saved,
                     "schema degli agganci declassato, ignoto o malformato rifiutato atomicamente");
            }
        }
    }
}
// Quote e richiami sui tagli locali devono consentire le normali modifiche della tavola.
void editDimensionedLocalSection(const DrawingEditorModel &model, const DrawingView &view,
                                 const DrawingViewLayout &layout, const DrawingCurve &rim,
                                 const DrawingDimension &radius) {
    auto sheet = blank(model);
    auto other = view;
    other.id = QUuid::createUuid();
    other.localSection.reset();
    sheet.drawing.views = {view, other};
    sheet.views = {layout, {other.id, "Vista indipendente", {70, 60}, layout.features}};
    const auto length = dimensionOfCurve(view, rim, DrawingMeasure::SectionLength);
    sheet.drawing.dimensions = {radius, length};
    sheet.dimensions = {{radius.id, {95, 65}}, {length.id, {80, 55}}};
    const auto center = drawingAttachment(view, rim, model.snapshot, DrawingPointRole::Center);
    DrawingAxis axis;
    axis.viewId = view.id;
    axis.references = {center, drawingAttachment(view, rim, model.snapshot, DrawingPointRole::Nearest,
                                               .5 * (rim.range.lo + rim.range.hi))};
    sheet.axes = {axis};
    DrawingAnnotation note;
    note.viewId = view.id;
    note.text = "Foro sezionato";
    note.reference = center;
    note.position = {90, 45};
    sheet.annotations = {note};
    sheet.centerMarks = {drawingCenterMark(view, rim, model.snapshot)};
    const auto initial = bytes(sheet);
    const auto ready = [&](DrawingSheetEditor &editor) {
        need(editor.dimensionResult(radius.id).status == DrawingDimensionStatus::Ready &&
                 editor.dimensionResult(length.id).status == DrawingDimensionStatus::Ready &&
                 editor.axisResult(axis.id).status == DrawingDimensionStatus::Ready &&
                 editor.annotationResult(note.id).status == DrawingDimensionStatus::Ready,
             "quote legacy/agganci, asse e richiamo della sezione locale restano associati");
        near(editor.dimensionResult(radius.id).value, 6, "raggio R6 del foro conservato");
        near(editor.dimensionResult(length.id).value, 12 * kPi, "circonferenza esatta conservata");
    };
    DrawingSheetEditor editor(sheet, model);
    ready(editor);
    // Percorso generico changeViews: il taglio quotato di una vista non blocca le altre.
    ok(editor.setViewScale(other.id, DrawingScaleSource::Custom, 1.5));
    ready(editor);
    ok(editor.undo());
    need(bytes(editor.sheet()) == initial, "Undo scala dell'altra vista conserva ogni aggancio");
    ok(editor.setLocalSection(other.id, *view.localSection));
    ready(editor);
    ok(editor.undo());
    need(bytes(editor.sheet()) == initial, "nuova sezione su un'altra vista e Undo senza perdere quote");

    // Percorso updateView: profondita', piano iniziale, contorno, titolo e scala sono modificabili.
    auto section = *view.localSection;
    section.front = 17;
    section.depth = 6;
    section.outline[0] = {-34, -19};
    ok(editor.setLocalSection(view.id, section));
    ready(editor);
    const auto edited = bytes(editor.sheet());
    ok(editor.undo());
    need(bytes(editor.sheet()) == initial, "Undo della sezione quotata ripristina esattamente il foglio");
    ok(editor.redo());
    need(bytes(editor.sheet()) == edited, "Redo conserva contorno, profondita' e riferimenti originali");
    auto renamed = editor.sheet().views[0];
    renamed.title = "Sezione locale aggiornata";
    ok(editor.updateView(editor.sheet().drawing.views[0], renamed));
    ok(editor.setViewScale(view.id, DrawingScaleSource::Custom, 1.5));
    ready(editor);
    const auto beforeRemove = bytes(editor.sheet());
    need(!editor.setLocalSection(view.id, std::nullopt).isEmpty() &&
             bytes(editor.sheet()) == beforeRemove,
         "rimuovere il taglio con quote associate resta vietato e atomico");
    auto excluded = editor.sheet().views[0];
    excluded.features.clear();
    need(!editor.updateView(editor.sheet().drawing.views[0], excluded).isEmpty() &&
             bytes(editor.sheet()) == beforeRemove,
         "rimuovere la sorgente quotata resta vietato e atomico");

    // Un vero spostamento oltre il fondo mantiene gli UUID e segnala le quote mancanti.
    section.front = 16;
    section.depth = 12;
    ok(editor.setLocalSection(view.id, section));
    need(editor.sheet().drawing.dimensions.size() == 2 &&
             editor.dimensionResult(radius.id).status == DrawingDimensionStatus::Missing,
         "foro assente al nuovo piano segnalato senza cancellare o riassociare la quota");
    ok(editor.undo());
    need(bytes(editor.sheet()) == beforeRemove, "Undo recupera il piano e ogni riferimento");
    ready(editor);
    DrawingSheet restored;
    ok(decodeDrawingSheet(beforeRemove, restored));
    DrawingSheetEditor reopened(restored, model);
    ready(reopened);
    need(bytes(reopened.sheet()) == beforeRemove, "riapertura della sezione modificata senza perdite");

    // Il controllo dei richiami deve riconoscere i tagli locali anche senza quote.
    sheet.drawing.dimensions.clear();
    sheet.dimensions.clear();
    DrawingSheetEditor notes(sheet, model);
    ok(notes.setViewScale(other.id, DrawingScaleSource::Custom, 1.5));
    ok(notes.setLocalSection(view.id, *view.localSection));
    need(notes.axisResult(axis.id).status == DrawingDimensionStatus::Ready &&
             notes.annotationResult(note.id).status == DrawingDimensionStatus::Ready,
         "asse e richiamo locale non bloccano le modifiche senza quote");
    const auto notesBeforeRemove = bytes(notes.sheet());
    need(!notes.setLocalSection(view.id, std::nullopt).isEmpty() &&
             bytes(notes.sheet()) == notesBeforeRemove,
         "rimozione con richiami/assi/centri locali rifiutata senza corrompere il foglio");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        DrawingEditorModel m;
        m.snapshot.version = {QUuid::createUuid(), {}};
        m.snapshot.bodies = {
            source(makeCylinder(Frame3(), 5, 10), 1),
            source(makeCylinder(Frame3(Vec3(20, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3, 10), 2)};
        m.choices = {{1, "Primo cilindro"}, {2, "Secondo cilindro"}};
        DrawingView v;
        v.kind = DrawingViewKind::ProjectedEdges;
        v.scale = 2;
        DrawingViewLayout l{v.id, "Centri scelti", {85, 75}, {1, 2}};
        DrawingSheetEditor e(blank(m), m);
        e.resize(1300, 850);
        e.show();
        ok(e.insertView(v, l));
        e.fitSheet();
        QApplication::processEvents();
        const auto original = bytes(e.sheet());
        localSectionReferenceCodec(m, v, e.curves(v.id));
        auto mark = drawingCenterMark(v, circle(v, e.curves(v.id), 1), m.snapshot);
        ok(e.addCenterMarks({mark}));
        need(e.sheet().centerMarks.size() == 1 && !e.sheet().views[0].showCenterMarks,
             "solo il bordo scelto riceve il centro");
        need(item(e, "selectedCenterMarks") && !item(e, "centerMarks"), "nessun centro automatico globale");
        auto *cross = dynamic_cast<QGraphicsPathItem *>(item(e, "selectedCenterMarks"));
        need(cross && cross->path().boundingRect().width() < 2, "croce limitata al primo cerchio");
        const auto selected = bytes(e.sheet());
        ok(e.undo());
        need(bytes(e.sheet()) == original, "Undo centri atomico");
        ok(e.redo());
        need(bytes(e.sheet()) == selected, "Redo centri atomico");
        mark.axes = true;
        ok(e.addCenterMarks({mark}));
        need(e.sheet().centerMarks.size() == 1 && e.sheet().centerMarks[0].axes,
             "assi aggiunti senza duplicare il centro");
        auto *axes = dynamic_cast<QGraphicsPathItem *>(item(e, "selectedCenterLines"));
        need(axes && axes->path().boundingRect().right() < 20 && axes->acceptedMouseButtons() == Qt::NoButton,
             "assi soltanto del cerchio scelto e non quotabili");
        DrawingSheet restored;
        ok(decodeDrawingSheet(bytes(e.sheet()), restored));
        need(restored.centerMarks.size() == 1 && restored.centerMarks[0].axes, "centri e assi persistiti");
        auto json = QJsonDocument::fromJson(bytes(e.sheet())).object();
        need(json["schema"].toInt() == 29, "schema nuovo solo per centri espliciti");
        auto marks = json["centerMarks"].toArray();
        auto invalid = marks[0].toObject();
        invalid["axes"] = "yes";
        marks[0] = invalid;
        json["centerMarks"] = marks;
        const auto checkpoint = bytes(restored);
        need(!decodeDrawingSheet(QJsonDocument(json).toJson(), restored).isEmpty() &&
                 bytes(restored) == checkpoint,
             "lettura centri invalida atomica");
        // Il comando usa i bordi gia' selezionati; altrimenti permette la scelta diretta.
        ok(e.clearCenterMarks(v.id));
        int index = -1;
        for (std::size_t i = 0; i < e.curves(v.id).size(); ++i)
            if (&e.curves(v.id)[i] == &circle(v, e.curves(v.id), 2)) {
                index = int(i);
                break;
            }
        for (auto *i : e.canvas()->scene()->items())
            if (i->data(0).toString() == "projection" && i->data(1).toUuid() == v.id &&
                i->data(2).toInt() == index)
                i->setSelected(true);
        auto *centers = e.findChild<QAction *>("drawingCenterMarksAction");
        centers->trigger();
        QApplication::processEvents();
        need(e.sheet().centerMarks.size() == 1 && e.sheet().centerMarks[0].reference.curve.featureId == 2,
             "comando applicato al bordo preselezionato");
        ok(e.clearCenterMarks(v.id));
        e.canvas()->scene()->clearSelection();
        centers->trigger();
        mouse(e, {95, 75}, true);
        need(e.sheet().centerMarks.size() == 1 && e.sheet().centerMarks[0].reference.curve.featureId == 1,
             "scelta diretta del bordo con mouse");
        key(e, Qt::Key_Escape);
        // La quota radiale deve muoversi prima del primo appoggio.
        auto *smart = e.findChild<QAction *>("drawingSmartDimensionAction");
        auto *type = e.findChild<QComboBox *>("drawingDimensionType");
        type->setCurrentIndex(type->findData(int(DrawingMeasure::Radius)));
        smart->setChecked(true);
        mouse(e, {95, 75}, true);
        mouse(e, {110, 48});
        auto *preview = item(e, "dimensionPreviewText");
        if (!preview)
            preview = item(e, "dimensionPreview");
        need(preview, "anteprima raggio disponibile dopo un solo clic");
        const auto p1 = preview->sceneBoundingRect().center();
        mouse(e, {130, 40});
        preview = item(e, "dimensionPreviewText");
        if (!preview)
            preview = item(e, "dimensionPreview");
        need(preview && QLineF(p1, preview->sceneBoundingRect().center()).length() > 10,
             "raggio segue il mouse prima di essere appoggiato");
        const auto p2 = preview->sceneBoundingRect().center();
        mouse(e, {130, 40}, true);
        need(e.sheet().drawing.dimensions.size() == 1, "raggio inserito col secondo clic");
        near(e.dimensionResult(e.sheet().drawing.dimensions.back().id).value, 5, "valore raggio esatto");
        smart->setChecked(false);
        need(QLineF(p2, item(e, "dimension")->sceneBoundingRect().center()).length() < .01,
             "conferma senza salto della quota");

        // I comandi di scelta nel foglio si sostituiscono senza intercettarsi i clic.
        centers->trigger();
        need(centers->isChecked(), "modalita centri avviata");
        e.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();
        QApplication::processEvents();
        QPointer<QDialog> numbering = e.findChild<QDialog *>("drawingDimensionNumberingDialog");
        need(numbering && numbering->isVisible() && !centers->isChecked(),
             "numerazione termina la scelta dei centri");
        centers->trigger();
        QApplication::processEvents();
        need(centers->isChecked() && (!numbering || !numbering->isVisible()),
             "scelta dei centri chiude la numerazione");
        key(e, Qt::Key_Escape);

        // Arco R207 con centro esterno: nessun tratto enorme verso il centro remoto.
        auto arc = std::make_shared<Circle<2>>(makeCircle(Vec2(0, 207), 207));
        const double t0 = -kPi / 2 - .15, t1 = -kPi / 2 + .15;
        const auto left = arc->point(t0), right = arc->point(t1);
        ProfileLoop profile;
        profile.segments.push_back({arc, {t0, t1}, -1});
        const auto line = [&](Vec2 a, Vec2 b) {
            profile.segments.push_back({std::make_shared<Line<2>>(a, b - a), {0, distance(a, b)}, -1});
        };
        line(right, {right.x(), -4});
        line({right.x(), -4}, {left.x(), -4});
        line({left.x(), -4}, left);
        DrawingEditorModel remote;
        remote.snapshot.version = m.snapshot.version;
        remote.snapshot.bodies = {source(makeExtrusion(Frame3(), {reversed(profile), {}}, 5), 4)};
        remote.choices = {{4, "Arco R207"}};
        DrawingView rv;
        rv.kind = DrawingViewKind::ProjectedEdges;
        DrawingViewLayout rl{rv.id, "Raggio con centro lontano", {150, 120}, {4}};
        DrawingSheetEditor radiusEditor(blank(remote), remote);
        ok(radiusEditor.insertView(rv, rl));
        const auto &rc = circle(rv, radiusEditor.curves(rv.id), 4);
        auto radiusRef = drawingAttachment(rv, rc, remote.snapshot, DrawingPointRole::Curve,
                                           .5 * (rc.range.lo + rc.range.hi));
        auto largeRadius = drawingDimensionBetween(rv, remote.snapshot, radiusEditor.curves(rv.id),
                                                   {radiusRef}, DrawingMeasure::Radius, {0, 0});
        ok(radiusEditor.addDimension(largeRadius, {160, 100}));
        near(radiusEditor.dimensionResult(largeRadius.id).value, 207, "raggio R207 esatto");
        int leaders = 0;
        for (auto *i : radiusEditor.canvas()->scene()->items())
            if (i->data(1).toUuid() == rv.id)
                if (auto *line = dynamic_cast<QGraphicsLineItem *>(i)) {
                    ++leaders;
                    need(line->line().length() < 60, "richiamo compatto anche verso il centro esterno");
                }
        need(leaders > 0, "richiamo raggio presente");

        // Due intersezioni della stessa faccia cilindrica: il testimone appartiene al piano locale Z=3.
        DrawingEditorModel branched;
        branched.snapshot.version = m.snapshot.version;
        branched.snapshot.bodies = {
            source(makeCylinder(Frame3(Vec3(-20, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 5, 40), 5)};
        DrawingView bv;
        bv.kind = DrawingViewKind::ProjectedEdges;
        bv.localSection = DrawingLocalSection{{{-21, -6}, {21, -6}, {21, 6}, {-21, 6}}, 5, 2};
        std::vector<DrawingCurve> branches;
        ok(drawingViewCurves(branched.snapshot, bv, branches));
        auto branch = std::find_if(branches.begin(), branches.end(), [](const auto &c) {
            return c.kind == DrawingCurveKind::Section && c.multipleBranches &&
                   std::abs(c.curve->point(.5 * (c.range.lo + c.range.hi)).y() - 4) < 1e-6;
        });
        need(branch != branches.end(), "ramo superiore della sezione cilindrica");
        DrawingDocument branchDocument;
        branchDocument.model = branched.snapshot.version;
        branchDocument.views = {bv};
        auto branchDimension = dimensionOfCurve(bv, *branch);
        branchDocument.dimensions = {branchDimension};
        auto branchResult = evaluateDrawingDimension(branchDocument, branched.snapshot, branchDimension.id);
        need(branchResult.status == DrawingDimensionStatus::Ready,
             "testimone legacy sul piano della sezione locale");
        near(branchResult.value, 40, "lunghezza esatta del ramo cilindrico");
        auto branchRef = drawingAttachment(bv, *branch, branched.snapshot, DrawingPointRole::Curve);
        auto attached = drawingDimensionBetween(bv, branched.snapshot, branches, {branchRef},
                                                DrawingMeasure::SectionLength, {0, 0});
        need(evaluateDrawingAttachments(attached, bv, branched.snapshot, branches).status ==
                 DrawingDimensionStatus::Ready,
             "aggancio sul ramo locale esplicito");

        // Taglio locale di un blocco con foro cieco: oltre il fondo il foro scompare dal materiale.
        DrawingEditorModel solid;
        solid.snapshot.version = {QUuid::createUuid(), {}};
        auto body = makeBox(Frame3(Vec3(-30, -15, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 60, 30, 16);
        body =
            booleanOperation(body, makeCylinder(Frame3(Vec3(-12, 0, 8), Vec3(0, 0, 1), Vec3(1, 0, 0)), 6, 10),
                             BooleanOperation::Subtract);
        solid.snapshot.bodies = {source(std::move(body), 3)};
        solid.choices = {{3, "Blocco con foro cieco"}};
        DrawingView top;
        top.kind = DrawingViewKind::ProjectedEdges;
        top.scale = 2;
        DrawingViewLayout layout{top.id, "Sezione locale", {150, 120}, {3}};
        DrawingLocalSection local;
        local.front = 16;
        local.depth = 4;
        local.outline = {{-35, -20}, {0, -20}, {0, 20}, {-35, 20}};
        top.localSection = local;
        auto prepared = prepareDrawingView(solid.snapshot, top, layout, {}, [](int) {});
        need(prepared.display->sectionMaterial.contains({-40, 0}) &&
                 !prepared.display->sectionMaterial.contains({-24, 0}) &&
                 !prepared.display->sectionMaterial.contains({20, 0}),
             "tratteggio limitato al contorno e foro aperto");
        need(!prepared.display->localBoundary.isEmpty() && !prepared.display->sectionHatch.isEmpty(),
             "linea di strappo e tratteggio generati");
        for (const auto &c : prepared.curves)
            need(c.curve &&
                     (c.kind == DrawingCurveKind::Section ? c.sourceFace.valid() : bool(c.sourceGeometry)),
                 "solo curve del B-rep esatto, nessun bordo fittizio del contorno");

        const auto sectionCircle =
            std::find_if(prepared.curves.begin(), prepared.curves.end(), [&](const auto &c) {
                return c.kind == DrawingCurveKind::Section && drawingPrimitive(top, c).circular;
            });
        need(sectionCircle != prepared.curves.end(), "cerchio del foro tagliato esatto");
        const auto sectionRef =
            drawingAttachment(top, *sectionCircle, solid.snapshot, DrawingPointRole::Curve,
                              .5 * (sectionCircle->range.lo + sectionCircle->range.hi));
        const auto sectionRadius = drawingDimensionBetween(top, solid.snapshot, prepared.curves, {sectionRef},
                                                           DrawingMeasure::Radius, {-12, 0});
        const auto measured = evaluateDrawingAttachments(sectionRadius, top, solid.snapshot, prepared.curves);
        need(measured.status == DrawingDimensionStatus::Ready, "quota sul piano del taglio locale");
        near(measured.value, 6, "raggio esatto sul foro sezionato");
        editDimensionedLocalSection(solid, top, layout, *sectionCircle, sectionRadius);
        DrawingDocument referenceDocument;
        referenceDocument.model = solid.snapshot.version;
        referenceDocument.views = {top};
        referenceDocument.dimensions = {sectionRadius};
        QByteArray referenceBytes;
        ok(encodeDrawingDocument(referenceDocument, referenceBytes));
        DrawingDocument decodedReferences;
        ok(decodeDrawingDocument(referenceBytes, decodedReferences));
        near(evaluateDrawingDimension(decodedReferences, solid.snapshot, sectionRadius.id).value, 6,
             "quota locale ricostruita dal documento");
        top.localSection->depth = 12;
        auto deeper = prepareDrawingView(solid.snapshot, top, layout, {}, [](int) {});
        need(deeper.display->sectionMaterial.contains({-24, 0}), "profondita' oltre il fondo del foro");
        auto plain = top;
        plain.localSection.reset();
        const auto front = prepareDrawingView(solid.snapshot, plain, layout, {}, [](int) {});
        QPainterPath rightWindow;
        rightWindow.addRect(0, -50, 100, 100);
        const auto rightPaths = [&](const PreparedDrawingView &p) {
            QPainterPath result;
            for (const auto &c : p.display->curves)
                result.addPath(clipDrawingDetailPath(c.visible, rightWindow));
            for (const auto &c : p.display->silhouettes)
                result.addPath(clipDrawingDetailPath(c.visible, rightWindow));
            return result;
        };
        need(rightPaths(front) == rightPaths(deeper), "lato esterno al contorno conservato");
        DrawingSheetEditor cut(blank(solid), solid);
        cut.resize(1300, 850);
        cut.show();
        ok(cut.insertView(plain, layout));
        cut.fitSheet();
        const auto beforeCut = bytes(cut.sheet());
        ok(cut.setLocalSection(top.id, top.localSection));
        need(item(cut, "localSectionBoundary") &&
                 item(cut, "localSectionBoundary")->acceptedMouseButtons() == Qt::NoButton,
             "contorno locale presente ma non quotabile");
        const auto afterCut = bytes(cut.sheet());
        ok(cut.undo());
        need(bytes(cut.sheet()) == beforeCut, "Undo sezione locale");
        ok(cut.redo());
        need(bytes(cut.sheet()) == afterCut, "Redo sezione locale");
        ok(cut.setSectionHatch(top.id,false));need(!item(cut,"sectionHatch"),"tratteggio della sezione locale disattivabile");
        ok(cut.undo());need(bytes(cut.sheet())==afterCut&&item(cut,"sectionHatch"),"Undo ripristina il tratteggio locale");
        ok(decodeDrawingSheet(afterCut, restored));
        need(restored.drawing.views[0].localSection == top.localSection, "contorno e profondita' persistiti");
        DrawingSheetEditor reopened(restored, solid);
        need(item(reopened, "localSectionBoundary"), "sezione ricostruita dopo riapertura");
        auto bad = *top.localSection;
        bad.outline = {{-5, -5}, {5, 5}, {-5, 5}, {5, -5}};
        need(!cut.setLocalSection(top.id, bad).isEmpty() && bytes(cut.sheet()) == afterCut,
             "contorno auto-intersecante rifiutato atomicamente");
        bad = *top.localSection;
        bad.depth = 0;
        need(!cut.setLocalSection(top.id, bad).isEmpty() && bytes(cut.sheet()) == afterCut,
             "profondita' nulla rifiutata atomicamente");
        // Interazione reale: disegno del contorno, profondita', Applica e annullamento.
        ok(cut.setLocalSection(top.id, std::nullopt));
        ok(cut.beginLocalSection(top.id));
        auto *dialog = cut.findChild<QDialog *>("drawingLocalSectionDialog");
        need(dialog && dialog->isVisible(), "pannello sezione locale aperto");
        near(dialog->findChild<QDoubleSpinBox *>("drawingLocalSectionFront")->value(), 16,
             "piano iniziale proposto dai vertici, senza sovrastima del box circolare");
        dialog->findChild<QDoubleSpinBox *>("drawingLocalSectionFront")->setValue(16);
        dialog->findChild<QDoubleSpinBox *>("drawingLocalSectionDepth")->setValue(4);
        for (auto point : {QPointF(80, 160), QPointF(150, 160), QPointF(150, 80), QPointF(80, 80)})
            mouse(cut, point, true);
        dialog->findChild<QPushButton *>("drawingLocalSectionApply")->click();
        QApplication::processEvents();
        QApplication::processEvents();
        need(cut.sheet().drawing.views[0].localSection.has_value(),
             "sezione applicata dal contorno disegnato");
        near(cut.sheet().drawing.views[0].localSection->depth, 4, "profondita' del pannello applicata");
        const auto accepted = bytes(cut.sheet());
        ok(cut.beginLocalSection(top.id));
        cut.findChild<QAction *>("drawingDimensionNumberingAction")->trigger();
        QApplication::processEvents();
        need(!item(cut, "localSectionPreview"), "numerazione termina il contorno locale");
        QPointer<QDialog> localNumbering = cut.findChild<QDialog *>("drawingDimensionNumberingDialog");
        ok(cut.beginLocalSection(top.id));
        need(!localNumbering || !localNumbering->isVisible(), "sezione locale chiude la numerazione");
        key(cut, Qt::Key_Escape);

        ok(cut.beginLocalSection(top.id));
        key(cut, Qt::Key_Escape);
        need(bytes(cut.sheet()) == accepted && !item(cut, "localSectionPreview"),
             "Esc conserva la sezione confermata");
        auto rotated = cut.sheet().drawing.views[0];
        rotated.plane = Frame3({}, Vec3(0, 0, 1), Vec3(0, 1, 0));
        rotated.scale = 1;
        ok(cut.updateView(rotated, cut.sheet().views[0]));
        need(item(cut, "localSectionBoundary"), "sezione con vista ruotata e nuova scala");
        ok(cut.undo());
        QTemporaryDir temp;
        DrawingPdfReport report;
        ok(cut.exportPdf(temp.path() + "/local.pdf", report));
        const auto output = qEnvironmentVariable("FORGECAD_LOCAL_TOOLS_OUTPUT");
        if (!output.isEmpty()) {
            need(QDir().mkpath(output), "cartella QA");
            ok(cut.exportPdf(output + "/sezione-locale.pdf", report));
            cut.fitSheet();
            QApplication::processEvents();
            need(cut.grab().save(output + "/sezione-locale.png"), "cattura sezione locale");
            e.fitSheet();
            need(e.grab().save(output + "/centri-raggio.png"), "cattura centri e raggio");
        }
        cut.setReadOnly(true);
        need(!cut.setLocalSection(top.id, std::nullopt).isEmpty() && !cut.beginLocalSection(top.id).isEmpty(),
             "sezione rispetta sola lettura");
        std::cout << "PASS centri selettivi, mouse, raggio prima del piazzamento, sezione locale "
                     "B-rep/profondita', contorno, persistenza, Undo/Redo, PDF, sola lettura\n";
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
