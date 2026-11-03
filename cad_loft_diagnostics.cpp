#include "cad_loft_diagnostics.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_forge.h"
#include <QRegularExpression>

namespace ForgeCad {
namespace {
GeometryRef wholeSketch(int index) {
    GeometryRef ref; ref.kind=7; ref.index=index; ref.element={-1,-1,-1}; return ref;
}
QString sectionLabel(int index, const ExtrusionObject &d, const QVector<SketchObject> &sketches,
                     const QVector<ExtrusionObject> &bodies) {
    const auto refs=d.loftSections;
    const auto ref=refs.isEmpty()?wholeSketch(d.loftSketches.value(index,-1)):refs.value(index);
    return QStringLiteral("Sezione %1 — %2").arg(index+1).arg(geometryRefText(ref,sketches,bodies));
}
int guideSource(int index,const ExtrusionObject &d) {
    return loftGuideSequence(d).value(index,-1);
}
QString guideLabel(int index,const ExtrusionObject &d,const QVector<SketchObject> &sketches,
                   const QVector<ExtrusionObject> &bodies) {
    const int source=guideSource(index,d), partial=source-d.loftGuides.size();
    QString name;
    if(source>=0 && source<d.loftGuides.size())name=geometryRefText(wholeSketch(d.loftGuides[source]),sketches,bodies);
    else if(partial>=0 && partial<d.loftGuidePaths.size())
        name=geometryRefText(wholeSketch(d.loftGuidePaths[partial].sketch),sketches,bodies)+QStringLiteral(" (catena)");
    else if(partial>=d.loftGuidePaths.size() && partial-d.loftGuidePaths.size()<d.loftEdgeGuides.size())
        name=geometryRefText(d.loftEdgeGuides[partial-d.loftGuidePaths.size()],sketches,bodies);
    return QStringLiteral("Guida %1 — %2").arg(index+1).arg(name.isEmpty()?QStringLiteral("riferimento non disponibile"):name);
}
}
LoftFailure loftFailure(const QString &error) {
    const auto index=[&](const QString &name) {
        const auto match=QRegularExpression(QStringLiteral("\\[\\[loft-%1=(\\d+)\\]\\]").arg(name)).match(error);
        bool ok=false;const int value=match.captured(1).toInt(&ok);return ok?value:-1;
    };
    return {index(QStringLiteral("guide")),index(QStringLiteral("section"))};
}
QString visibleLoftError(QString error) {
    error.remove(QRegularExpression(QStringLiteral("\\s*\\[\\[loft-(section|guide)=\\d+\\]\\]")));
    return error.trimmed();
}
QString namedLoftError(const QString &error,const ExtrusionObject &d,const QVector<SketchObject> &sketches,
                       const QVector<ExtrusionObject> &bodies) {
    if(error.isEmpty())return error;
    const auto failure=loftFailure(error);
    QString result=visibleLoftError(error);
    if(failure.guide>=0)result+=QStringLiteral("\n%1").arg(guideLabel(failure.guide,d,sketches,bodies));
    if(failure.section>=0)result+=QStringLiteral("\n%1").arg(sectionLabel(failure.section,d,sketches,bodies));
    if(failure.guide>=0)result+=QStringLiteral(" [[loft-guide=%1]]").arg(failure.guide);
    if(failure.section>=0)result+=QStringLiteral(" [[loft-section=%1]]").arg(failure.section);
    return result;
}
QVector<LoftDiagnosticPath> loftDiagnosticPaths(LoftFailure failure,const ExtrusionObject &d,int owner,
                                              const QVector<SketchObject> &sketches,const QVector<ExtrusionObject> &bodies) {
    QVector<LoftDiagnosticPath> result;
    const auto append=[&](bool guide, const QString &label, const GeometryRef &ref) {
        LoftDiagnosticPath item;item.guide=guide;item.label=label;QString error;
        if(geometryRefPath(ref,owner,sketches,bodies,item.path,&error) && !item.path.empty())result.append(std::move(item));
    };
    const int source=guideSource(failure.guide,d), partial=source-d.loftGuides.size();
    if(source>=0 && source<d.loftGuides.size())
        append(true,guideLabel(failure.guide,d,sketches,bodies),wholeSketch(d.loftGuides[source]));
    else if(partial>=0 && partial<d.loftGuidePaths.size()) {
        const auto &selection=d.loftGuidePaths[partial];
        if(selection.sketch>=0 && selection.sketch<sketches.size()) {
            LoftDiagnosticPath item;item.guide=true;item.label=guideLabel(failure.guide,d,sketches,bodies);QString error;
            if(sketchPath(sketchPathSubset(sketches[selection.sketch],selection),item.path,&error))result.append(std::move(item));
        }
    } else if(partial>=d.loftGuidePaths.size() && partial-d.loftGuidePaths.size()<d.loftEdgeGuides.size())
        append(true,guideLabel(failure.guide,d,sketches,bodies),d.loftEdgeGuides[partial-d.loftGuidePaths.size()]);
    const int sections=d.loftSections.isEmpty()?d.loftSketches.size():d.loftSections.size();
    if(failure.section>=0 && failure.section<sections)
        append(false,sectionLabel(failure.section,d,sketches,bodies),d.loftSections.isEmpty()
            ?wholeSketch(d.loftSketches[failure.section]):d.loftSections[failure.section]);
    return result;
}
}
