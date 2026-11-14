#include "cad_drawing_editor.h"
#include "cad_drawing_template.h"
#include "cad_drawing_template_editor.h"
#include "cad_icons.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGraphicsView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
namespace {
void require(bool yes, const char *why) {
    if (!yes)
        throw std::runtime_error(why);
}
void ok(const QString &e) {
    if (!e.isEmpty())
        throw std::runtime_error(e.toStdString());
}
QByteArray bytes(const DrawingSheet &s) {
    QByteArray b;
    ok(encodeDrawingSheet(s, b));
    return b;
}
void checks() {
    QTemporaryDir config;
    require(config.isValid(), "config temporanea");
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    DrawingSheet sheet;
    sheet.size = {420, 297};
    sheet.drawing.model.documentId = QUuid::createUuid();
    sheet.titleBlock.visible = true;
    sheet.titleBlock.organization = "Officina prova";
    sheet.titleBlock.customLayout = true;
    sheet.titleBlock.items = standardDrawingFrame();
    DrawingFrameItem logo;
    logo.kind = DrawingFrameKind::Image;
    logo.rect = {-40, -65, 35, 15};
    QImage image(80, 32, QImage::Format_ARGB32);
    image.fill(QColor("#2486ab"));
    QBuffer buffer(&logo.imagePng);
    buffer.open(QIODevice::WriteOnly);
    require(image.save(&buffer, "PNG"), "logo fixture");
    sheet.titleBlock.items.append(logo);
    auto appearance = drawingTemplateOf(sheet);
    appearance.style.dimensionHeight = 4;
    appearance.scale = .5;
    QByteArray encoded;
    ok(encodeDrawingTemplate(appearance, encoded));
    require(!encoded.contains(sheet.drawing.model.documentId.toString(QUuid::WithoutBraces).toUtf8()) &&
                !encoded.contains("drawing\""),
            "template non conserva identita o viste CAD");
    DrawingSheetTemplate decoded;
    ok(decodeDrawingTemplate(encoded, decoded));
    QByteArray again;
    ok(encodeDrawingTemplate(decoded, again));
    require(again == encoded, "roundtrip template e PNG");
    const auto path = config.path() + "/officina.drwt";
    ok(saveDrawingTemplate(path, appearance));
    ok(loadDrawingTemplate(path, decoded));
    bool found = false;
    ok(saveDefaultDrawingTemplate(decoded, true));
    DrawingSheetTemplate def;
    ok(loadDefaultDrawingTemplate({420, 297}, def, found));
    require(found && def.titleBlock.organization == "Officina prova" &&
                preferredDrawingTemplateSize() == QSizeF(420, 297),
            "predefinito per A3 orizzontale");
    ok(loadDefaultDrawingTemplate({297, 420}, def, found));
    require(!found, "orientamenti separati");
    auto portrait = appearance;
    portrait.size = {297, 420};
    portrait.titleBlock.organization = "Verticale";
    ok(saveDefaultDrawingTemplate(portrait, false));
    ok(removeDefaultDrawingTemplate({420, 297}));
    ok(loadDefaultDrawingTemplate({297, 420}, def, found));
    require(found && def.titleBlock.organization == "Verticale", "rimozione di un formato conserva gli altri");
    DrawingSheet emptyCustom = sheet;
    emptyCustom.titleBlock.items.clear();
    emptyCustom.revisionTable.visible = true;
    DrawingRevisionTableLayout revision;
    ok(layoutDrawingRevisionTable(emptyCustom, defaultDrawingPageContext(emptyCustom), revision));
    require(revision.rect.bottom() < emptyCustom.size.height(), "template vuoto conserva tabella revisioni");
    auto small = emptyCustom;
    small.revisionTable.visible = false;
    small.size = {100, 100};
    require(!bytes(small).isEmpty(), "cartiglio personalizzato su formato piccolo");
    DrawingSheet loaded;
    ok(decodeDrawingSheet(bytes(sheet), loaded));
    require(loaded.titleBlock.customLayout && loaded.titleBlock.items.back().imagePng == logo.imagePng,
            "drw incorpora logo/layout");
    auto invalid = QJsonDocument::fromJson(bytes(sheet)).object();
    invalid["schema"] = 29;
    require(!decodeDrawingSheet(QJsonDocument(invalid).toJson(), loaded).isEmpty(), "downgrade schema rifiutato");
    auto bad = appearance;
    bad.titleBlock.items[0].rect.moveLeft(-99999);
    require(!encodeDrawingTemplate(bad, again).isEmpty(), "template fuori foglio rifiutato");
    bad = appearance;
    bad.titleBlock.items.back().imagePng = "bad PNG";
    require(!encodeDrawingTemplate(bad, again).isEmpty(), "logo malformato rifiutato");
    DrawingSheet legacy = sheet;
    legacy.titleBlock.customLayout = false;
    legacy.titleBlock.items.clear();
    ok(encodeDrawingTemplate(drawingTemplateOf(legacy), again));
    ok(decodeDrawingTemplate(again, decoded));
    require(!decoded.titleBlock.customLayout, "template standard compatibile");
    DrawingEditorModel model;
    model.snapshot.version = sheet.drawing.model;
    DrawingSheetEditor editor(legacy, model);
    const auto before = bytes(editor.sheet());
    ok(editor.applyPageTemplate(appearance));
    require(editor.sheet().scale == .5 && editor.sheet().style.dimensionHeight == 4 &&
                editor.sheet().titleBlock.customLayout,
            "applica stile e formato");
    ok(editor.undo());
    require(bytes(editor.sheet()) == before, "template applicato con un solo Undo");
    ok(editor.redo());
    editor.setReadOnly(true);
    require(!editor.applyPageTemplate(portrait).isEmpty(), "sola lettura");
    editor.setReadOnly(false);
    bool seen = false;
    QString failure;
    QTimer::singleShot(0, [&] {
        try {
            auto *dialog = editor.findChild<QDialog *>("drawingTemplateEditor");
            require(dialog, "editor template reale");
            seen = true;
            auto *items = dialog->findChild<QListWidget *>("templateItems");
            require(items && items->count() > 20, "cartiglio modificabile");
            dialog->findChild<QLineEdit *>("templateOrganization")->setText("Editor prova");
            dialog->findChild<QAction *>("templateAddText")->trigger();
            dialog->findChild<QLineEdit *>("templateItemText")->setText("NOTA {formato}");
            dialog->findChild<QPushButton *>("templateApply")->click();
            QApplication::processEvents();
            const auto out = qEnvironmentVariable("FORGECAD_TEMPLATE_PREVIEW");
            if (!out.isEmpty())
                require(dialog->grab().save(out), "anteprima template");
            dialog->findChild<QPushButton *>("templateClose")->click();
        } catch (const std::exception &e) {
            failure = e.what();
            if (auto *d = QApplication::activeModalWidget())
                d->close();
        }
    });
    showDrawingTemplateEditor(&editor, &editor);
    require(seen && failure.isEmpty(), failure.toUtf8().constData());
    require(editor.sheet().titleBlock.organization == "Editor prova" &&
                editor.sheet().titleBlock.items.back().text == "NOTA {formato}",
            "UI applica testi e campi");
    DrawingPdfReport report;
    ok(editor.exportPdf(config.path() + "/template.pdf", report));
    require(QFileInfo(config.path() + "/template.pdf").size() > 1000, "PDF con template incorporato");
    QSet<QByteArray> icons;
    for (const auto &name :
         {"drawingSection", "drawingTechnicalSection", "drawingSteppedSection", "drawingAlignedSection",
          "drawingLocalSection", "drawingDatum", "drawingGeometricTolerance", "drawingSurfaceTexture"}) {
        auto icon = commandIcon(name);
        require(!icon.isNull(), "icona mancante");
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        icon.pixmap(32, 32).save(&b, "PNG");
        require(!icons.contains(png), "icone duplicate");
        icons.insert(png);
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("ForgeCADTemplateTests");
    QApplication::setApplicationName("DrawingTemplates");
    try {
        checks();
        std::cout << "PASS template: codec, immagini, default formato/orientamento, editor, Undo, PDF e icone\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
