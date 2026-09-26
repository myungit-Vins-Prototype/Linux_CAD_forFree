#include "cad_export.h"

#include <APIHeaderSection_MakeHeader.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <IGESControl_Controller.hxx>
#include <Interface_Static.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Controller.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

namespace ForgeCad {

QString exportSuffix(ExportFormat format) {
    return format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces ? QStringLiteral("igs") : QStringLiteral("step");
}

QString exportBodies(const QString &path, const QVector<ExportBody> &bodies, ExportFormat format) {
    if (bodies.isEmpty()) return QStringLiteral("Non ci sono corpi da esportare.");
    try {
        // Documento XCAF: un'etichetta per corpo, con nome e colore; unita' mm.
        const Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
        Handle(TDocStd_Document) document;
        application->NewDocument(TCollection_ExtendedString("MDTV-XCAF"), document);
        XCAFDoc_DocumentTool::SetLengthUnit(document, 1.0, UnitsMethods_LengthUnit_Millimeter);
        const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        const Handle(XCAFDoc_ColorTool) colors = XCAFDoc_DocumentTool::ColorTool(document->Main());
        const Quantity_Color color(0.25, 0.65, 0.90, Quantity_TOC_RGB);  // come a video
        for (const ExportBody &body : bodies) {
            if (body.shape.IsNull()) continue;
            const TDF_Label label = shapes->AddShape(body.shape, false);
            // IGES accetta solo ASCII: lettere accentate senza accento (è -> e), il resto '_'.
            QString name = body.name;
            if (format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces) {
                QString ascii;
                for (const QChar c : name.normalized(QString::NormalizationForm_D))
                    if (c.unicode() < 128) ascii += c;
                    else if (c.category() != QChar::Mark_NonSpacing) ascii += QLatin1Char('_');
                name = ascii;
            }
            TDataStd_Name::Set(label, TCollection_ExtendedString(name.toUtf8().constData(), true));
            colors->SetColor(label, color, XCAFDoc_ColorSurf);
        }
        const QByteArray file = path.toUtf8();
        bool written = false;
        if (format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces) {
            IGESControl_Controller::Init();
            Interface_Static::SetCVal("write.iges.unit", "MM");
            Interface_Static::SetIVal("write.iges.brep.mode", format == ExportFormat::IgesSolids ? 1 : 0);
            Interface_Static::SetCVal("write.iges.header.author", "ForgeCAD");
            IGESCAFControl_Writer writer;  // unita' da write.iges.unit (mm)
            writer.SetColorMode(true);
            writer.SetNameMode(true);
            if (!writer.Transfer(document)) return QStringLiteral("La conversione in IGES non e' riuscita.");
            written = writer.Write(file.constData());
        } else {
            STEPControl_Controller::Init();
            // Lo schema va scelto prima di creare lo scrittore (definisce il modello).
            const char *schema = format == ExportFormat::StepAP203 ? "AP203" : format == ExportFormat::StepAP214 ? "AP214IS" : "AP242DIS";
            Interface_Static::SetCVal("write.step.schema", schema);
            Interface_Static::SetCVal("write.step.unit", "MM");
            STEPCAFControl_Writer writer;
            writer.SetColorMode(format != ExportFormat::StepAP203);
            writer.SetNameMode(true);
            if (!writer.Transfer(document, STEPControl_AsIs)) return QStringLiteral("La conversione in STEP non e' riuscita.");
            APIHeaderSection_MakeHeader header(writer.ChangeWriter().Model());
            header.SetOriginatingSystem(new TCollection_HAsciiString("ForgeCAD"));
            header.SetAuthorValue(1, new TCollection_HAsciiString("ForgeCAD"));
            written = writer.Write(file.constData()) == IFSelect_RetDone;
        }
        application->Close(document);
        if (!written) return QStringLiteral("Impossibile scrivere %1.").arg(path);
    } catch (const Standard_Failure &failure) {
        return QStringLiteral("Esportazione non riuscita: %1").arg(QString::fromUtf8(failure.GetMessageString()));
    }
    return {};
}

}
