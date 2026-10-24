#ifndef FORGECAD_DRAWING_H
#define FORGECAD_DRAWING_H

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "fk_history.h"
#include "fk_precision.h"
#include "cad_document_version.h"

namespace ForgeCad {

// Un check-in nullo identifica la copia di lavoro. Nessun percorso e' un ID.
using DrawingDocumentVersion = DocumentVersion;

// Snapshot immutabile preparato dal chiamante con i nomi della genealogia.
struct DrawingSourceBody {
    quint64 featureId = 0;
    std::shared_ptr<const Kernel::Body> body;
    std::shared_ptr<const Kernel::ElementNames> names;
    quint32 color=0xff40a6e6U; // ARGB del corpo per la sola presentazione
};
struct DrawingModelSnapshot {
    DrawingDocumentVersion version;
    std::vector<DrawingSourceBody> bodies;
};

enum class DrawingSectionStyle { Removed, Full, HalfRight, HalfLeft };
enum class DrawingViewKind { Section, ProjectedEdges };
enum class DrawingCurveKind { Section, ProjectedEdge, ApparentLine, ApparentArc };
// Transizioni ordinate fra piani paralleli: ascissa e offset assoluto dal primo
// piano nel frame della sezione (mm modello). I raccordi non sono bordi misurabili.
struct DrawingSectionTransition {
    double at=0,offset=0;
    bool operator==(const DrawingSectionTransition &b)const{return at==b.at&&offset==b.offset;}
};
struct DrawingSectionStep {
    double at=0,offset=0;
    std::vector<DrawingSectionTransition> additional{};
    std::vector<DrawingSectionTransition> transitions()const{
        std::vector<DrawingSectionTransition> result{{at,offset}};
        result.insert(result.end(),additional.begin(),additional.end());return result;
    }
    bool operator==(const DrawingSectionStep &b)const{return at==b.at&&offset==b.offset&&additional==b.additional;}
    bool operator!=(const DrawingSectionStep &b)const{return !(*this==b);}
};
// Due semipiani incidenti: cerniera lungo Y all'ascissa at, angolo in gradi
// verso +Z dal primo asse X. La vista mostra lo sviluppo rigido sul primo piano.
struct DrawingSectionBend {
    double at=0,angle=0;
    bool operator==(const DrawingSectionBend &b)const{return at==b.at&&angle==b.angle;}
    bool operator!=(const DrawingSectionBend &b)const{return !(*this==b);}
};
struct DrawingView {
    QUuid id = QUuid::createUuid();
    Kernel::Frame3 plane;
    double scale = 1.0; // presentazione: nessun effetto sulle misure
    double tolerance = Kernel::kBooleanConnectionTolerance;
    DrawingViewKind kind = DrawingViewKind::Section;
    bool sectionReferences = false; // sezioni del piano quotabili nella vista dei bordi
    DrawingSectionStyle sectionStyle=DrawingSectionStyle::Removed;
    std::optional<DrawingSectionStep> step;
    std::optional<DrawingSectionBend> bend;
};

// Rotazione dal secondo piano originale al piano comune della vista.
Kernel::Transform3 drawingSectionUnfoldTransform(const DrawingView &view);
DrawingView drawingSectionBentPlane(const DrawingView &view);

inline bool drawingHasProjection(const DrawingView &v){return v.kind==DrawingViewKind::ProjectedEdges||v.sectionStyle!=DrawingSectionStyle::Removed;}
inline bool drawingHalfSection(const DrawingView &v){return v.sectionStyle==DrawingSectionStyle::HalfRight||v.sectionStyle==DrawingSectionStyle::HalfLeft;}
// Coordinate locali: normale verso l’osservatore, la sezione completa conserva Z <= 0.
bool drawingRetainsPoint(const DrawingView &view,const Kernel::Vec3 &local);
std::vector<Kernel::Interval> drawingRetainedCurveRanges(const DrawingView &view,const Kernel::Curve<3> &curve,Kernel::Interval range,bool section=false);

struct DrawingCurve {
    QUuid viewId;
    quint64 featureId = 0;
    Kernel::FaceId sourceFace;
    std::string faceName;
    Kernel::CurvePtr<3> curve;
    Kernel::Interval range;
    bool coincident = false;
    bool multipleBranches = false;
    bool tangent = false;
    DrawingCurveKind kind = DrawingCurveKind::Section;
    Kernel::EdgeId sourceEdge{};
    std::string edgeName{};
    Kernel::CurvePtr<3> sourceGeometry; // geometria esatta prima della proiezione, solo cache
    Kernel::Interval sourceRange{};
    int contourSide=0; // lato analitico del raccordo rispetto all'asse e alla vista
    std::optional<Kernel::Transform3> unfoldedToSource{}; // solo cache, per agganci al piano originale
    bool hasSourceRange=false; // intervallo 3D trattenuto: il parametro della proiezione puo’ essere diverso
};
using DrawingSection = DrawingCurve;

// Il testimone e' nel piano della vista. Disambigua solo se appartiene a
// un unico ramo; non autorizza a scegliere il ramo semplicemente piu' vicino.
struct DrawingSectionReference {
    quint64 featureId = 0;
    QString faceName;
    Kernel::Vec2 witness;
    bool requiresWitness = false; // non riassociare un ramo eliminato al solo superstite
    DrawingCurveKind kind = DrawingCurveKind::Section;
    QString edgeName{};
    int contourSide=0; // +/-1 soltanto per un arco apparente esatto, schema7
};
enum class DrawingMeasure { SectionLength, EndpointDistance, HorizontalDistance, VerticalDistance,
    Distance, Angle, Radius, Diameter, AxisRadius, AxisDiameter, OrdinateX, OrdinateY };
inline bool drawingOrdinate(DrawingMeasure m){return m==DrawingMeasure::OrdinateX||m==DrawingMeasure::OrdinateY;}
enum class DrawingDimensionSeries { ChainHorizontal, ChainVertical, OrdinateX, OrdinateY };
enum class DrawingPointRole { Curve, Start, End, Midpoint, Center, Nearest, Origin, AxisX, AxisY,
    Quadrant0, Quadrant1, Quadrant2, Quadrant3 };
struct DrawingAttachment {
    DrawingSectionReference curve;
    DrawingPointRole role=DrawingPointRole::Curve;
    double fraction=0; // frazione di lunghezza, non parametro della curva
    QString supportFace;
    int curveSide=0,anchorSide=0; // lati UV esatti: Umin/Umax/Vmin/Vmax
    bool polar=false; // frazione angolare nel piano, per punti sulle coniche
    QString vertexName, boundaryName; // origine topologica e orientamento del tratto
    int clipBoundary=0; // bit 1: piano di taglio; bit 2: asse della mezza sezione; 4: gradino; 5: cerniera angolare
    QUuid axisId; // asse associato del foglio: nessun indice o coordinata grafica
};
struct DrawingDimension {
    QUuid id = QUuid::createUuid();
    QUuid viewId;
    DrawingSectionReference section;
    DrawingMeasure measure = DrawingMeasure::SectionLength;
    std::vector<DrawingAttachment> references; // vuoto: quota legacy su una curva
    int angleSides=0;
    int decimals=2;
};
struct DrawingDocument {
    DrawingDocumentVersion version{QUuid::createUuid(), {}};
    DrawingDocumentVersion model;
    std::vector<DrawingView> views;
    std::vector<DrawingDimension> dimensions;
};

enum class DrawingDimensionStatus { Ready, Missing, Ambiguous, VersionMismatch, Invalid };
struct DrawingDimensionResult {
    DrawingDimensionStatus status = DrawingDimensionStatus::Invalid;
    double value = std::numeric_limits<double>::quiet_NaN();
    QString message;
    enum class Geometry { None, Linear, Radius, Diameter, Angle, Ordinate } geometry=Geometry::None;
    Kernel::Vec2 first{},second{},vertex{},firstDirection{},secondDirection{};
    double sweep=0; // radianti con segno, solo per l'arco della quota angolare
};

// Output sostituito solo al successo. Nessuna mesh, nessun requisito di volume.
QString sectionDrawingView(const DrawingModelSnapshot &model, const DrawingView &view,
                           std::vector<DrawingSection> &sections);
// Tutti i bordi, inclusi quelli posteriori: nessuna classificazione HLR.
QString projectDrawingEdges(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves);
QString drawingViewCurves(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves);
DrawingDimension dimensionOfCurve(const DrawingView &view,const DrawingCurve &curve,DrawingMeasure measure=DrawingMeasure::SectionLength);
DrawingDimension dimensionOfSection(const DrawingView &view, const DrawingSection &section,
                                   DrawingMeasure measure = DrawingMeasure::SectionLength);
DrawingDimensionResult evaluateDrawingDimension(const DrawingDocument &document, const DrawingModelSnapshot &model,
                                               const QUuid &dimensionId);

// Aggiornamento esplicito della versione: aprire una tavola storica non
// significa collegarla automaticamente all'ultimo modello disponibile.
QString retargetDrawingModel(DrawingDocument &document, const DrawingDocumentVersion &version);

bool drawingNeedsSectionSchema(const DrawingDocument &document);
bool drawingNeedsAxisSchema(const DrawingDocument &document);

// Codec autonomo del prototipo, incorporabile in un futuro contenitore.
// Non modifica il formato .prt e non costituisce un archivio di revisioni.
QString encodeDrawingDocument(const DrawingDocument &document, QByteArray &bytes);
QString decodeDrawingDocument(const QByteArray &bytes, DrawingDocument &document);

}
#endif
