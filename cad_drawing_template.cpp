#include "cad_drawing_template.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>
namespace ForgeCad {
namespace {
constexpr qint64 maximumBytes = 8 * 1024 * 1024;
QStringList appearanceKeys() {
    return {"size", "titleBlock", "frameItems", "style", "dimensionSpacing", "dimensionText", "sheetScale"};
}
DrawingSheet blank() {
    DrawingSheet sheet;
    sheet.title = QStringLiteral("Template");
    sheet.drawing.model.documentId = QUuid("{00000000-0000-0000-0000-000000000001}");
    return sheet;
}
QString key(const QSizeF &s) {
    return QString::number(s.width(), 'g', 16) + "x" + QString::number(s.height(), 'g', 16);
}
QString defaultPath(const QSizeF &size) {
    const auto hash = QCryptographicHash::hash(key(size).toLatin1(), QCryptographicHash::Sha256).toHex().left(24);
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/drawing-templates/" +
           QString::fromLatin1(hash) + ".drwt";
}
} // namespace
DrawingSheetTemplate drawingTemplateOf(const DrawingSheet &sheet) {
    return {sheet.size, sheet.titleBlock, sheet.style, sheet.scale};
}
void applyDrawingTemplate(DrawingSheet &sheet, const DrawingSheetTemplate &t) {
    sheet.size = t.size;
    sheet.titleBlock = t.titleBlock;
    sheet.style = t.style;
    sheet.scale = t.scale;
}
QString encodeDrawingTemplate(const DrawingSheetTemplate &t, QByteArray &bytes) {
    auto sheet = blank();
    applyDrawingTemplate(sheet, t);
    QByteArray encoded;
    const auto error = encodeDrawingSheet(sheet, encoded);
    if (!error.isEmpty())
        return error;
    const auto source = QJsonDocument::fromJson(encoded).object();
    QJsonObject appearance;
    for (const auto &k : appearanceKeys())
        if (source.contains(k))
            appearance.insert(k, source[k]);
    appearance.insert("sheetScale", t.scale);
    auto result =
        QJsonDocument(QJsonObject{{"format", "ForgeCAD.DrawingTemplate"}, {"schema", 1}, {"appearance", appearance}})
            .toJson(QJsonDocument::Compact);
    if (result.size() > maximumBytes)
        return QStringLiteral("Template troppo grande (massimo 8 MiB).");
    bytes = std::move(result);
    return {};
}
QString decodeDrawingTemplate(const QByteArray &bytes, DrawingSheetTemplate &result) {
    if (bytes.size() > maximumBytes)
        return QStringLiteral("Template troppo grande (massimo 8 MiB).");
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(bytes, &parse);
    const auto root = json.object();
    if (parse.error != QJsonParseError::NoError || !json.isObject() || root["format"] != "ForgeCAD.DrawingTemplate" ||
        root["schema"].toInt(-1) != 1 || !root["appearance"].isObject())
        return QStringLiteral("Formato template non supportato.");
    const auto appearance = root["appearance"].toObject();
    for (auto i = appearance.begin(); i != appearance.end(); ++i)
        if (!appearanceKeys().contains(i.key()))
            return QStringLiteral("Campo estraneo al template: %1").arg(i.key());
    if (!appearance.contains("size") || !appearance.contains("sheetScale"))
        return QStringLiteral("Formato o scala del template assenti.");
    QByteArray encoded;
    auto error = encodeDrawingSheet(blank(), encoded);
    if (!error.isEmpty())
        return error;
    auto sheet = QJsonDocument::fromJson(encoded).object();
    sheet["schema"] = appearance.contains("frameItems")                       ? 30
                      : appearance["style"].toObject().contains("arrowWidth") ? 25
                                                                              : 23;
    for (auto i = appearance.begin(); i != appearance.end(); ++i)
        sheet[i.key()] = i.value();
    DrawingSheet decoded;
    error = decodeDrawingSheet(QJsonDocument(sheet).toJson(), decoded);
    if (!error.isEmpty())
        return error;
    result = drawingTemplateOf(decoded);
    return {};
}
QString saveDrawingTemplate(const QString &path, const DrawingSheetTemplate &t) {
    QByteArray bytes;
    auto e = encodeDrawingTemplate(t, bytes);
    if (!e.isEmpty())
        return e;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
QString loadDrawingTemplate(const QString &path, DrawingSheetTemplate &t) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return file.errorString();
    if (file.size() > maximumBytes)
        return QStringLiteral("Template troppo grande.");
    return decodeDrawingTemplate(file.read(maximumBytes + 1), t);
}
QString saveDefaultDrawingTemplate(const DrawingSheetTemplate &t, bool preferred) {
    const auto path = defaultPath(t.size);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return QStringLiteral("Impossibile creare la cartella dei template.");
    auto error = saveDrawingTemplate(path, t);
    if (!error.isEmpty())
        return error;
    if (preferred) {
        QSettings settings;
        settings.setValue("drawingTemplates/preferredSize", t.size);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return QStringLiteral("Impossibile salvare il formato preferito.");
    }
    return {};
}
QString removeDefaultDrawingTemplate(const QSizeF &size) {
    const auto path = defaultPath(size);
    if (QFile::exists(path) && !QFile::remove(path))
        return QStringLiteral("Impossibile rimuovere il predefinito.");
    return {};
}
QString loadDefaultDrawingTemplate(const QSizeF &size, DrawingSheetTemplate &t, bool &found) {
    found = false;
    const auto path = defaultPath(size);
    if (!QFile::exists(path))
        return {};
    DrawingSheetTemplate candidate;
    const auto error = loadDrawingTemplate(path, candidate);
    if (!error.isEmpty())
        return QStringLiteral("Template predefinito non leggibile: %1").arg(error);
    if (candidate.size != size)
        return QStringLiteral("Formato del template predefinito incoerente.");
    t = std::move(candidate);
    found = true;
    return {};
}
QSizeF preferredDrawingTemplateSize() {
    const auto s = QSettings().value("drawingTemplates/preferredSize", QSizeF(297, 210)).toSizeF();
    if (std::isfinite(s.width()) && std::isfinite(s.height()) && s.width() >= 50 && s.height() >= 50 &&
        s.width() <= 2000 && s.height() <= 2000)
        return s;
    return {297, 210};
}
} // namespace ForgeCad
