// CadViewport e' ancora definito nel .cpp della finestra. Questa unita' di
// test lo include per verificare interazioni e rendering senza esportare API di test.
#include "../forgeCad2026_gui.cpp"
#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_body_io.h"
#include "fk_step.h"
#include "fk_classify.h"
#include "fk_sew.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QtEndian>
#include <fstream>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
class ViewportInteractionTest {
public:
    // Analisi -> Micro-geometrie sul vecchio corpo finale di Loft_offset
    // (importato da STEP): le due strisce sottili, i quattro spigoli corti e i
    // due giunti quasi tangenti, tutti ritrovati come riferimenti nella vista.
    static void microFeatureMarksTest() {
        using namespace ForgeCad;
        std::ifstream in(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/loft_offset_trimmed.body", std::ios::binary);
        require(bool(in), "fixture del corpo rumoroso");
        std::stringstream content;
        content << in.rdbuf();
        const Kernel::Body noisy = Kernel::readBodyBinary(content.str());
        DocumentState state;
        ExtrusionObject body;
        body.feature = BodyFeature::Imported;
        body.name = QStringLiteral("Corpo finale rumoroso");
        body.importSource = QStringLiteral("loft_offset_trimmed.body");
        Kernel::ExchangeBody exchange;
        exchange.name = "corpo";
        exchange.body = noisy;
        body.importData = QByteArray::fromStdString(Kernel::writeStep({exchange}));
        state.extrusions.append(body);
        CadViewport viewport;
        viewport.loadDocument(state);
        require(viewport.extrusions_.size() == 1 && viewport.extrusions_.front().forgeBody, "corpo importato");
        QStringList lines;
        const QVector<GeometryRef> marks = microFeatureMarks(viewport.extrusions_.front(), 0, lines);
        int thin = 0, shortEdges = 0, nearTangent = 0;
        for (const QString &line : lines) {
            thin += line.startsWith(QStringLiteral("Faccia sottile"));
            shortEdges += line.startsWith(QStringLiteral("Spigolo corto"));
            nearTangent += line.startsWith(QStringLiteral("Spigolo quasi tangente"));
        }
        std::cout << lines.join(QLatin1Char('\n')).toStdString() << std::endl;
        require(thin == 2 && shortEdges == 4 && nearTangent == 2, "micro-geometrie del corpo rumoroso");
        for (const GeometryRef &mark : marks) {
            ResolvedRef resolved;
            QString reason;
            if (!resolveGeometryRef(mark, 1, viewport.sketches_, viewport.extrusions_, resolved, &reason))
                std::cout << "tipo " << mark.kind << " id " << mark.point.subshape << ": " << reason.toStdString() << std::endl;
            require(reason.isEmpty(), "segno risolto nella vista");
        }
        viewport.setReferenceMarks(marks);
        require(viewport.refMarks_.size() == marks.size(), "segni nella vista");
    }
    static void automaticSnapConstraints() {
        using namespace ForgeCad;
        const auto has = [](const SketchObject &sketch, ConstraintType type, ConstraintRef a, ConstraintRef b) {
            for (const auto &c : sketch.geometricConstraints)
                if (c.type==type && c.first==a && c.second==b) return true;
            return false;
        };
        for (auto tool : {DrawingTool::Circle,DrawingTool::Ellipse,DrawingTool::Arc}) {
            CadViewport v;
            v.sketches_={SketchObject()}; v.activeSketch_=0;
            v.geometrySnap_=true; v.originSnap_=true;
            v.drawingTool_=tool;
            v.sketches_[0].segments.append({QPointF(3,0),QPointF(5,0)});
            v.curveControlPoints_={QPointF(0,0),QPointF(3,0)};
            if(tool==DrawingTool::Ellipse) v.curveControlPoints_.append(QPointF(0,2));
            if(tool==DrawingTool::Arc) v.curveControlPoints_.append(QPointF(0,3));
            v.finalizePrimitive();
            auto &sketch=v.sketches_[0];
            require(sketch.curves.size()==1,"creazione primitiva con snap");
            require(has(sketch,ConstraintType::Coincident,{1,0,0},{2,0,-1}),"centro non vincolato all'origine");
            require(has(sketch,ConstraintType::Coincident,{1,0,1},{0,0,0}),"snap primitiva non persistente");
            sketch.curves[0].controlPoints[0]+=QPointF(0.2,0.1);
            require(solveSketch(sketch).ok,"risoluzione vincolo origine");
            require(QLineF(sketch.curves[0].controlPoints[0],QPointF()).length()<1e-6,"origine non mantenuta");
        }
        CadViewport v;
        v.sketches_={SketchObject()}; v.activeSketch_=0;
        v.geometrySnap_=true; v.originSnap_=true; v.gridSnap_=false;
        v.drawingTool_=DrawingTool::Spline;
        v.sketches_[0].segments.append({QPointF(2,2),QPointF(4,2)});
        v.curveControlPoints_={QPointF(),QPointF(3,2),QPointF(4,-2)};
        const QPointF closing=v.snapPoint(QPointF(0.001,0.001),false);
        require(closing==QPointF(),"snap chiusura spline");
        v.curveControlPoints_.append(closing);
        v.finalizeCurve();
        auto &sketch=v.sketches_[0];
        require(has(sketch,ConstraintType::Midpoint,{1,0,1},{0,0,-1}),"snap punto interno spline non vincolato");
        require(has(sketch,ConstraintType::Coincident,{1,0,0},{1,0,3}),"chiusura spline non vincolata");
        const auto &curve=sketch.curves[0];
        int handles=0;
        for(int control : {0,3}) for(int side : {0,1}) handles+=CadViewport::visibleSplineHandle(curve,control,side);
        require(handles==2 && CadViewport::visibleSplineHandle(curve,0,1)
            && CadViewport::visibleSplineHandle(curve,3,0),"maniglie ridondanti alla chiusura");
        const QPointF hidden=curve.tangentHandles[0].first;
        int ci=-1,pi=-1; EditablePointKind kind=EditablePointKind::Control;
        const bool found=v.findCurveEditPoint(hidden,ci,pi,kind);
        require(!found || pi!=0 || kind!=EditablePointKind::TangentIn,"maniglia nascosta selezionabile");
        sketch.curves[0].controlPoints[3]=QPointF(0.1,0.1);
        require(solveSketch(sketch).ok && QLineF(sketch.curves[0].controlPoints[0],sketch.curves[0].controlPoints[3]).length()<1e-6,
                "chiusura persa dopo modifica");

        // La chiusura deve valere anche lontano dall'origine, senza due
        // coincidenze all'origine che nascondano un vincolo non riconosciuto.
        SketchObject closed;
        closed.curves.append(sketch.curves[0]);
        for(auto &p:closed.curves[0].controlPoints) p+=QPointF(10,10);
        for(auto &h:closed.curves[0].tangentHandles) { h.first+=QPointF(10,10); h.second+=QPointF(10,10); }
        SketchConstraint closure;
        closure.type=ConstraintType::Coincident; closure.first={1,0,0}; closure.second={1,0,3};
        closed.geometricConstraints.append(closure);
        closed.curves[0].controlPoints[3]+=QPointF(0.3,-0.2);
        require(applicableConstraints(closed,{closure.first,closure.second}).contains(ConstraintType::Coincident),
            "chiusura spline rifiutata dal sistema di vincoli");
        require(solveSketch(closed).ok && QLineF(closed.curves[0].controlPoints[0],closed.curves[0].controlPoints[3]).length()<1e-6,
            "vincolo di chiusura inefficace fuori dall'origine");

        SketchObject onCurve;
        CurveObject ellipse;
        ellipse.tool=DrawingTool::Ellipse; ellipse.controlPoints={QPointF(10,10),QPointF(15,10),QPointF(10,12)};
        onCurve.curves.append(ellipse);
        const QPointF p(10+5/std::sqrt(2.0),10+2/std::sqrt(2.0));
        onCurve.segments.append({p,QPointF(20,20)});
        v.recordCoincidences(onCurve,0);
        require(has(onCurve,ConstraintType::PointOnCurve,{0,0,0},{1,0,-1}),"snap su ellisse non vincolato");
        const int count=onCurve.geometricConstraints.size();
        v.recordCoincidences(onCurve,0);
        require(onCurve.geometricConstraints.size()==count,"vincoli snap duplicati");
        onCurve.segments[0].first+=QPointF(0.05,0.03);
        require(solveSketch(onCurve).ok,"risoluzione punto su ellisse");
        for(const auto &c:onCurve.geometricConstraints) require(constraintError(onCurve,c)<1e-6,"snap non mantenuto dopo modifica");
        QTemporaryDir directory;
        DocumentState state,loaded; state.sketches={sketch,onCurve};
        const auto path=directory.filePath(QStringLiteral("snaps.prt"));
        require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),"persistenza snap .prt");
        require(has(loaded.sketches[0],ConstraintType::Coincident,{1,0,0},{1,0,3}),"vincolo chiusura perso alla riapertura");
    }

    static void ellipseTrim() {
        using namespace ForgeCad;
        for (bool rotated : {false, true}) {
            SketchObject sketch;
            CurveObject ellipse;
            ellipse.tool = DrawingTool::Ellipse;
            ellipse.construction = true;
            const QPointF center(7,-3), axis = rotated ? QPointF(0.6,0.8) : QPointF(1,0);
            const QPointF perpendicular(-axis.y(),axis.x());
            ellipse.controlPoints = {center, center + 2*axis, center + 5*perpendicular};
            sketch.curves.append(ellipse);
            const auto original = curveGeometry(ellipse).front();
            const auto point = [&](double t) {
                const auto p = original.curve->point(t);
                return QPointF(p.x(),p.y());
            };
            const auto diameter = [&](double t) {
                const QPointF d = 2*(point(t)-center);
                return SketchSegment{center-d,center+d};
            };
            sketch.segments.append(diameter(0));
            const auto preview = trimPreview(sketch,{1,0},point(M_PI/2));
            require(preview.size()>2,"anteprima taglio ellisse assente");
            auto result = trimSketchEntity(sketch,{1,0},point(M_PI/2));
            require(result.error.isEmpty() && sketch.curves.size()==1,"taglio ellisse fallito");
            require(sketch.curves[0].tool==DrawingTool::Nurbs && sketch.curves[0].construction,
                    "arco ellittico razionale e costruzione");
            const auto verify = [&] {
                const auto pieces = curveGeometry(sketch.curves[0]);
                require(pieces.size()==1,"arco ellittico non valido");
                const auto &g=pieces.front();
                for(int i=0;i<=100;++i) {
                    const auto p=g.curve->point(g.range.lo+g.range.length()*i/100.0);
                    const QPointF d=QPointF(p.x(),p.y())-center;
                    const double x=QPointF::dotProduct(d,axis)/2, y=QPointF::dotProduct(d,perpendicular)/5;
                    require(std::fabs(x*x+y*y-1)<1e-10,"arco fuori dall'ellisse originale");
                }
            };
            verify();
            QTemporaryDir directory;
            DocumentState state, loaded;
            state.sketches.append(sketch);
            const QString path=directory.filePath(QStringLiteral("ellipse.prt"));
            require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),
                "salvataggio e riapertura arco ellittico");
            require(loaded.sketches.size()==1 && loaded.sketches[0].curves.size()==1,
                "arco ellittico perso alla riapertura");
            sketch=loaded.sketches[0];
            verify();
            sketch.segments.append(diameter(M_PI/2));
            result=trimSketchEntity(sketch,{1,0},point(5*M_PI/4));
            require(result.error.isEmpty() && sketch.curves.size()==1,"secondo taglio arco ellittico fallito");
            verify();
            const auto kept=curveGeometry(sketch.curves[0]).front();
            const auto expected=original.curve->point(3*M_PI/2);
            require(Kernel::distance(kept.start(),expected)<1e-7,"secondo taglio estremo errato");

            SketchObject wrap;
            wrap.curves.append(ellipse);
            wrap.segments.append(diameter(M_PI/2));
            result=trimSketchEntity(wrap,{1,0},point(0));
            require(result.error.isEmpty() && wrap.curves.size()==1,"taglio sulla chiusura ellisse");
            const auto retained=curveGeometry(wrap.curves[0]).front();
            require(Kernel::distance(retained.start(),original.curve->point(M_PI/2))<1e-7
                && Kernel::distance(retained.end(),original.curve->point(3*M_PI/2))<1e-7,
                "taglio sulla chiusura tiene il tratto sbagliato");
            SketchObject isolated;
            isolated.curves.append(ellipse);
            require(trimSketchEntity(isolated,{1,0},point(0)).error.isEmpty() && isolated.curves.isEmpty(),
                "ellisse senza intersezioni non eliminata");
        }
    }

    static void extensionContourPicking() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10.0));
        const auto edge = plate->edges().front();
        const auto &e = plate->edge(edge);
        const QVector<EdgePoint> selected{edgeReference(*plate, edge, e.curve->point(0.5 * (e.range.lo + e.range.hi)))};
        require(completeExtensionContours(plate, selected).size() == 4, "completamento del contorno libero");
        require(completeExtensionContours(plate, selected + selected).size() == 4, "contorno senza bordi duplicati");
        CadViewport viewport;
        ExtrusionObject feature;
        feature.forgeBody = plate;
        forgeTessellate(*plate, 0, feature.display);
        viewport.extrusions_ = {feature};
        QWidget parent;
        int appliedEdges = 0;
        QTimer::singleShot(0, [&] {
            auto *dialog = parent.findChild<QDialog *>();
            if (!dialog) return;
            dialog->findChild<QCheckBox *>()->setChecked(true);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        const auto result = extendDialog(&parent, &viewport, QStringLiteral("Test contorni estensione"), 0, -1,
            selected, 1.0, false, true, [&](const QVector<EdgePoint> &edges, double, bool) {
                appliedEdges = edges.size();
                return QString();
            });
        require(result.applied && appliedEdges == 4 && result.edges.size() == 4,
                "conferma e riselezione usano il contorno completo");
        std::cout << "Estensione: scelta del contorno completo OK" << std::endl;
    }
    static void trimPartPicking() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        CadViewport v;
        v.resize(800, 600);
        v.setViewNormal(0);
        v.setReferencePlanesVisible(false);
        const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10));
        const ForgeBody wall = std::make_shared<const Body>(makePlaneSheet(
            Frame3(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0)), 5));
        ExtrusionObject a, b;
        a.name = QStringLiteral("Lastra"); a.forgeBody = plate;
        b.name = QStringLiteral("Parete"); b.forgeBody = wall;
        forgeTessellate(*plate, 0, a.display);
        forgeTessellate(*wall, 1, b.display);
        v.extrusions_ = {a, b};
        QString error;
        const auto regions = forgeSheetPieces(wall, plate, 0, &error);
        require(regions.size() == 2, "regioni della parete");
        int side = -1, part = -1;
        // Selezione sul secondo corpo, anche se il primo non ha regioni.
        v.setViewNormal(2);
        v.setTrimPartPickCallback(0, {}, 1, regions,
            [&](int s, int p, EdgePoint) { side = s; part = p; });
        const auto point = regions.at(0).point;
        const QPoint cursor = v.projectWorldPoint(QVector3D(point.x, point.y, point.z)).toPoint();
        v.updateHover(cursor);
        require(v.trimPartHover_ == 0, "hover delle regioni disponibili del secondo corpo");
        v.trimPartHover_ = -1;
        v.extrusions_[1].visible = false;
        const auto click = [&](Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(cursor), QPointF(cursor), Qt::LeftButton, Qt::LeftButton, modifiers);
            v.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(cursor), QPointF(cursor), Qt::LeftButton, Qt::NoButton, modifiers);
            v.mouseReleaseEvent(&release);
        };
        click();
        require(side == 1 && part == 0, "clic senza hover sul corpo nascosto dall'anteprima");
        v.selection_ = {SceneObjectKind::Extrusion, 0};
        v.setTrimPartPickCallback(0, {}, 1, {}, [](int, int, EdgePoint) {});
        click(); click(Qt::ControlModifier);
        require(v.selection_.kind == SceneObjectKind::Extrusion && v.selection_.index == 0 && !v.boxSelecting_,
                "taglio senza regioni non seleziona la scena");
        v.setTrimPartPickCallback(-1, {}, -1, {}, {});
        v.extrusions_[1].visible = true;
        for (bool reverse : {false, true}) {
            QWidget parent;
            ExtrusionObject definition;
            definition.firstBody = reverse ? 1 : 0;
            definition.secondBody = reverse ? 0 : 1;
            definition.trimBoth = true;
            std::exception_ptr failure;
            bool inspected = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = parent.findChild<QDialog *>();
                if (!dialog) return;
                try {
                    const auto boxes = dialog->findChildren<QComboBox *>();
                    require(boxes.size() == 4 && boxes.at(2)->count() == (reverse ? 2 : 0)
                                && boxes.at(3)->count() == (reverse ? 0 : 2),
                            "pannello conserva le regioni quando un solo corpo si divide");
                    require(bool(v.trimPartPickFinished_) && v.trimPartPickDisplays_.size() == 2,
                            "pannello mantiene attiva la scelta delle regioni disponibili");
                    QPushButton *pick = nullptr;
                    for (auto *button : dialog->findChildren<QPushButton *>())
                        if (button->text() == QStringLiteral("Dalla vista")) { pick = button; break; }
                    require(pick != nullptr, "pulsante scelta corpo");
                    pick->click();
                    require(v.refPicking_ && !v.trimPartPickFinished_, "Dalla vista sospende la scelta delle parti");
                    pick->click();
                    require(!v.refPicking_ && bool(v.trimPartPickFinished_), "ritorno alla scelta delle parti");
                    if (reverse) {
                        dialog->findChild<QCheckBox *>()->setChecked(false);
                        require(boxes.at(2)->count() == 2 && boxes.at(3)->count() == 0 && v.preview_.valid,
                                "taglio singolo disponibile dopo errore del secondo corpo");
                    }
                    inspected = true;
                } catch (...) { failure = std::current_exception(); }
                dialog->reject();
            });
            trimDialog(&parent, &v, QStringLiteral("Test taglio"), -1, definition,
                       [](const ExtrusionObject &) { return QString(); });
            if (failure) std::rethrow_exception(failure);
            require(inspected && !v.trimPartPickFinished_, "chiusura pannello ripristina la selezione normale");
        }
        std::cout << "Taglio: hover, clic e regioni parziali OK" << std::endl;
    }
    static void offsetFacePicking() {
        using namespace ForgeCad;
        CadViewport v;
        v.resize(800, 600);
        v.setViewNormal(0);
        v.setReferencePlanesVisible(true);
        PrimitiveParameters box;
        require(v.createPrimitive(box, QStringLiteral("Box selezione facce")).isEmpty(), "box selezione facce");
        ExtrusionObject datum;
        datum.feature = BodyFeature::DatumPlane;
        datum.name = QStringLiteral("Piano davanti al box");
        GeometryRef xy;
        xy.kind = 1; xy.index = 0;
        datum.datum.refs = {xy}; datum.datum.distance = 100; datum.datum.size = 100;
        require(v.createBody(datum).isEmpty(), "datum davanti al corpo");
        require(v.beginReferencePick(DatumRoleFace, -1).isEmpty(), "inizio scelta facce");
        GeometryRef picked;
        const QPoint cursor = v.projectWorldPoint(QVector3D(0.5f, 0.5f, 0)).toPoint();
        require(v.pickReference(cursor, picked) && picked.kind == 5 && picked.index == 0,
                "scelta facce ignora il datum davanti al corpo");
        v.updateHover(cursor);
        require(v.refHoverValid_ && v.refHover_.kind == 5 && v.hover_.kind == SceneObjectKind::None,
                "hover delle facce senza evidenziare i piani");
        v.extrusions_[0].visible = false;
        require(!v.pickReference(cursor, picked), "nessun piano selezionabile nella scelta facce");
        v.refPickRoles_ = DatumRolePlane;
        require(v.pickReference(cursor, picked) && (picked.kind == 8 || picked.kind == 1),
                "i piani restano selezionabili quando richiesti");
        ExtrusionObject definition;
        definition.feature = BodyFeature::SurfaceOffset;
        ExtrusionObject separate = definition;
        separate.offsetSew = false;
        require(CadViewport::previewKey(definition, -1) != CadViewport::previewKey(separate, -1),
                "la checkbox invalida la cache dell'anteprima");
        std::cout << "Selezione facce offset e cache cucitura: OK" << std::endl;
    }
    static void deletionDuringEdgePick() {
        for (int removed : {0, 2}) {
            CadViewport viewport;
            viewport.extrusions_.resize(3);
            viewport.edgePicking_ = true;
            viewport.edgePickBody_ = 2;
            viewport.edgePickEdit_ = 2;
            viewport.pickedEdges_ = {0};
            viewport.hoverEdgeBody_ = 2;
            viewport.hoverEdge_ = 0;
            viewport.preview_.valid = true;
            const auto generation = viewport.preview_.generation;
            viewport.removeBodies(QSet<int>{removed});
            require(viewport.extrusions_.size() == 2, "rimozione corpo durante scelta spigoli");
            require(!viewport.edgePicking_ && viewport.edgePickBody_ == -1 && viewport.edgePickEdit_ == -1,
                    "scelta annullata prima del ricalcolo, per corpo rimosso o rinumerato");
            require(viewport.pickedEdges_.isEmpty() && viewport.hoverEdgeBody_ == -1 && viewport.hoverEdge_ == -1,
                    "nessun indice di spigolo residuo dopo la rimozione");
            require(!viewport.preview_.valid && viewport.preview_.generation > generation,
                    "anteprima invalidata anche per risultati asincroni in arrivo");
        }
        std::cout << "Rimozione durante scelta spigoli: OK" << std::endl;
    }
    // Diagnostica facoltativa: --display-stats file.prt. Dimensioni della
    // visualizzazione salvata (o ricalcolata) di ogni corpo e del suo B-rep.
    static void displayStats(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        QElapsedTimer timer;
        timer.start();
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento");
        std::cout << "lettura " << timer.elapsed() << " ms" << std::endl;
        timer.restart();
        CadViewport viewport;
        viewport.loadDocument(document);
        std::cout << "loadDocument " << timer.elapsed() << " ms" << std::endl;
        for (int i = 0; i < viewport.extrusions_.size(); ++i) {
            const ExtrusionObject &body = viewport.extrusions_.at(i);
            qsizetype edgePoints = 0;
            for (const auto &edge : body.display.edges) edgePoints += edge.size();
            std::cout << "B" << i << " " << body.name.toStdString() << " visibile=" << body.visible
                      << " cache=" << body.cachedGeometry << " qualita'=" << body.display.quality
                      << " triangoli=" << body.display.vertices.size() / 3 << " polilinee=" << body.display.edges.size()
                      << " punti spigoli=" << edgePoints << " triangleFaces=" << body.display.triangleFaces.size();
            if (body.forgeBody) {
                std::size_t poles = 0;
                for (auto f : body.forgeBody->faces())
                    if (auto spline = std::dynamic_pointer_cast<const Kernel::BSplineSurface>(body.forgeBody->face(f).surface))
                        poles += std::size_t(spline->uPoleCount()) * std::size_t(spline->vPoleCount());
                std::cout << " facce=" << body.forgeBody->faces().size() << " poli B-spline=" << poles;
            }
            std::cout << std::endl;
        }
    }
    // Diagnostica facoltativa: --mesh-stats file.prt [lato scarto angolo].
    static void meshStats(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(!args.isEmpty() && loadDocumentFile(args.first(), document).isEmpty(), "lettura documento mesh");
        QVector<ExportBody> bodies;
        for (int i = 0; i < document.extrusions.size(); ++i) {
            ExtrusionObject &feature = document.extrusions[i];
            if (!feature.forgeBody && !feature.suppressed)
                CadViewport::buildGeometry(feature, i, document.sketches, document.extrusions);
            if (feature.visible && feature.error.isEmpty() && feature.forgeBody) {
                std::cout << "corpo " << feature.name.toStdString() << " sheet=" << feature.forgeBody->isSheet() << std::endl;
                bodies.append({feature.name, feature.forgeBody, {}, QColor()});
            }
        }
        StlExportOptions options;
        options.maxEdgeLength = args.size() > 1 ? args.at(1).toDouble() : 1.0;
        options.deflection = args.size() > 2 ? args.at(2).toDouble() : 0.05;
        options.angle = args.size() > 3 ? args.at(3).toDouble() : 10.0;
        const StlBuildResult stl = buildBinaryStl(bodies, options);
        const ObjBuildResult obj = buildQuadObj(bodies, options);
        std::cout << "STL triangoli=" << stl.triangleCount << " errore=" << stl.error.toStdString() << '\n'
                  << "OBJ quad=" << obj.quadCount << " triangoli=" << obj.triangleCount
                  << " errore=" << obj.error.toStdString() << std::endl;
        require(stl.error.isEmpty() && obj.error.isEmpty(), "costruzione mesh del documento");
        if (args.size() > 4) require(saveQuadObj(args.at(4), obj.data).isEmpty(), "salvataggio OBJ diagnostico");
    }
    // --offset-lofts [distanza] file.prt ...
    static void offsetLofts(const QStringList &arguments) {
        using namespace ForgeCad;
        bool explicitDistance = false;
        const double parsed = arguments.value(0).toDouble(&explicitDistance);
        const double offsetDistance = explicitDistance ? parsed : 0.1;
        const QStringList paths = explicitDistance ? arguments.mid(1) : arguments;
        require(!paths.isEmpty(), "specificare un documento per la diagnostica loft");
        for (const QString &path : paths) {
            DocumentState document;
            require(loadDocumentFile(path, document).isEmpty(), "lettura documento offset loft");
            for (int i = 0; i < document.extrusions.size(); ++i) {
                auto &feature = document.extrusions[i];
                if (!feature.forgeBody && !feature.suppressed)
                    CadViewport::buildGeometry(feature, i, document.sketches, document.extrusions);
                if (feature.feature != BodyFeature::Loft || !feature.forgeBody) continue;
                for (bool sew : {false, true}) {
                    QString error;
                    std::cout << path.toStdString() << " loft " << i << " tutte le facce, cucitura=" << sew << std::endl;
                    const auto result = forgeOffsetFaces(feature.forgeBody, {}, offsetDistance, &error, nullptr, sew);
                    std::cout << "  " << (result ? "OK" : error.toStdString()) << std::endl;
                    require(bool(result), "offset di tutte le facce del loft del documento");
                }
                for (const auto face : feature.forgeBody->faces()) {
                    if (feature.forgeBody->face(face).surface->type() != Kernel::SurfaceType::BSpline) continue;
                    const auto surface = feature.forgeBody->face(face).surface;
                    const auto u = surface->uDomain(), v = surface->vDomain();
                    const auto ref = faceReference(*feature.forgeBody, face, surface->point((u.lo+u.hi)/2, (v.lo+v.hi)/2));
                    QString error;
                    std::cout << path.toStdString() << " loft " << i << " face " << face.index << std::endl;
                    const auto result = forgeOffsetFaces(feature.forgeBody, {ref}, offsetDistance, &error, nullptr, false);
                    std::cout << "  " << (result ? "OK" : error.toStdString()) << std::endl;
                    require(bool(result), "offset della faccia loft del documento");
                }
            }
        }
    }
    static void offsetLoftSides(const QString &path, double distance, bool rebuild = false, const QString &output = {}) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento offset laterali");
        CadViewport viewport;
        viewport.loadDocument(document);
        const int count = viewport.extrusions_.size();
        int tested = 0;
        for (int i = 0; i < count; ++i) {
            auto feature = viewport.extrusions_.at(i);
            if (feature.feature != BodyFeature::Loft || !feature.forgeBody) continue;
            if (rebuild) {
                feature.cachedGeometry = false;
                CadViewport::buildGeometry(feature, i, viewport.sketches_, viewport.extrusions_);
                require(feature.error.isEmpty() && bool(feature.forgeBody), "rigenerazione loft dalle sezioni");
                viewport.extrusions_[i] = feature;
            }
            ExtrusionObject definition;
            definition.feature = BodyFeature::SurfaceOffset;
            definition.firstBody = i;
            definition.distance = distance;
            definition.name = QStringLiteral("Offset laterali regressione");
            for (const auto face : feature.forgeBody->faces()) {
                const auto surface = feature.forgeBody->face(face).surface;
                if (surface->type() != Kernel::SurfaceType::BSpline) continue;
                const auto u = surface->uDomain(), v = surface->vDomain();
                definition.offsetFaces.push_back(faceReference(*feature.forgeBody, face,
                    surface->point(u.lo + 0.25 * u.length(), v.lo + 0.5 * v.length())));
            }
            if (definition.offsetFaces.isEmpty()) continue;
            if (!output.isEmpty()) {
                require(rebuild, "salvare la variante solo dopo la rigenerazione");
                require(QFileInfo(path).absoluteFilePath() != QFileInfo(output).absoluteFilePath(), "salvare una copia separata");
                viewport.extrusions_.resize(i + 1);
                for (auto &body : viewport.extrusions_) body.visible = false;
                for (auto &sketch : viewport.sketches_) sketch.visible = false;
            }
            for (bool sew : {false, true}) {
                if (!output.isEmpty() && !sew) continue;
                definition.offsetSew = sew;
                const QString error = viewport.createBody(definition);
                std::cout << feature.name.toStdString() << " laterali: d=" << distance << " cucitura=" << sew
                          << " " << (error.isEmpty() ? "OK" : error.toStdString()) << std::endl;
                require(error.isEmpty(), "creazione offset laterali dal comando dell'applicazione");
                const auto result = viewport.extrusions_.back().forgeBody;
                require(result && Kernel::checkBody(*result).empty(), "offset laterali valido");
                std::cout << "  laterali loft=" << definition.offsetFaces.size() << " facce offset=" << result->faces().size() << std::endl;
                if (sew) require(result->shells().size() == 1, "laterali dell'offset cucite");
                Kernel::TessellationOptions options;
                options.deflection = 0.02;
                require(Kernel::tessellate(*result, options).failedFaces == 0, "tutte le facce dell'offset visualizzabili");
                ++tested;
                if (!output.isEmpty()) {
                    require(saveDocumentFile(output, viewport.documentState()).isEmpty(), "salvataggio variante loft e offset rigenerati");
                    DocumentState saved;
                    require(loadDocumentFile(output, saved).isEmpty() && saved.extrusions.back().forgeBody
                            && Kernel::checkBody(*saved.extrusions.back().forgeBody).empty(), "rilettura della variante rigenerata");
                    return;
                }
            }
        }
        require(tested > 0, "almeno un loft verificato");
    }
    static void extendTopologyFace(const QString &path, int bodyIndex, int faceIndex, double distance) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per estensione faccia");
        CadViewport viewport;
        viewport.loadDocument(document);
        require(bodyIndex >= 0 && bodyIndex < viewport.extrusions_.size(), "indice corpo dell'estensione");
        const ExtrusionObject &feature = viewport.extrusions_.at(bodyIndex);
        require(bool(feature.forgeBody), "geometria del corpo da estendere");
        const Kernel::Body &body = *feature.forgeBody;
        const Kernel::FaceId face(faceIndex);
        require(body.contains(face), "faccia da estendere");
        const Kernel::Surface &surface = *body.face(face).surface;
        std::cout << "B" << bodyIndex << ":F" << faceIndex << " " << feature.name.toStdString()
                  << " feature=" << int(feature.feature) << " faces=" << body.faces().size()
                  << " edges=" << body.edges().size()
                  << " domain=[" << surface.uDomain().lo << "," << surface.uDomain().hi
                  << "]x[" << surface.vDomain().lo << "," << surface.vDomain().hi << "]" << std::endl;
        int tested = 0;
        bool sameSurfaceFailed = false;
        QVector<EdgePoint> allBoundaryEdges;
        for (Kernel::LoopId loop : body.face(face).loops)
            for (Kernel::FinId fin : body.loopFins(loop)) {
                const Kernel::EdgeId edge = body.fin(fin).edge;
                if (!body.isLaminar(edge)) continue;
                const Kernel::Edge &geometry = body.edge(edge);
                const Kernel::Fin &finGeometry = body.fin(fin);
                if (finGeometry.pcurve) {
                    std::cout << "  E" << edge.index << " UV";
                    for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                        const Kernel::Vec2 uv = finGeometry.pcurve->point(
                            geometry.range.lo + fraction * geometry.range.length());
                        std::cout << " (" << uv.x() << "," << uv.y() << ")";
                    }
                    std::cout << std::endl;
                }
                const auto reference = edgeReference(body, edge,
                    geometry.curve->point(0.5 * (geometry.range.lo + geometry.range.hi)));
                allBoundaryEdges.append(reference);
                for (bool linear : {false, true}) {
                    QString error;
                    const ForgeBody result = forgeExtendSheet(feature.forgeBody, {reference}, distance, linear, &error);
                    std::cout << "  E" << edge.index << " linear=" << linear << ": "
                              << (result ? "OK" : error.toStdString()) << std::endl;
                    if (!linear && !result) sameSurfaceFailed = true;
                    if (result) {
                        require(Kernel::checkBody(*result).empty(), "corpo esteso valido");
                        Kernel::TessellationOptions options;
                        options.deflection = 0.02;
                        const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
                        std::size_t points = 0, triangles = 0;
                        for (const Kernel::FaceMesh &part : mesh.faces) {
                            points += part.points.size();
                            triangles += part.triangles.size();
                        }
                        std::cout << "    result faces=" << result->faces().size()
                                  << " meshFaces=" << mesh.faces.size() << " points=" << points
                                  << " triangles=" << triangles << " failed=" << mesh.failedFaces << std::endl;
                        require(mesh.failedFaces == 0 && triangles > 0, "corpo esteso visualizzabile");
                    }
                    CadViewport applied;
                    applied.loadDocument(document);
                    const QString createError = applied.createSheetExtend(
                        bodyIndex, {reference}, distance, linear, QStringLiteral("Estensione regressione"));
                    require(createError.isEmpty(), "applicazione dell'estensione dal pannello");
                    const ExtrusionObject &created = applied.extrusions_.back();
                    std::cout << "    applied visible=" << created.visible
                              << " vertices=" << created.display.vertices.size()
                              << " edges=" << created.display.edges.size()
                              << " sourceVisible=" << applied.extrusions_.at(bodyIndex).visible << std::endl;
                    require(created.visible && !created.display.vertices.isEmpty(),
                            "la feature applicata conserva una mesh visibile");
                    if (edge.index == 0 && !linear) {
                        CadViewport previewed;
                        previewed.loadDocument(document);
                        previewed.requestExtendPreview(bodyIndex, {reference}, distance, linear, -1);
                        QElapsedTimer timeout;
                        timeout.start();
                        while (!(previewed.preview_.valid || !previewed.preview_.error.isEmpty()) && timeout.elapsed() < 30000)
                            QApplication::processEvents(QEventLoop::AllEvents, 50);
                        require(previewed.preview_.valid && previewed.preview_.geometry,
                                "anteprima dell'estensione pronta");
                        const ForgeBody previewGeometry = previewed.preview_.geometry;
                        const QString promotedError = previewed.createSheetExtend(
                            bodyIndex, {reference}, distance, linear, QStringLiteral("Estensione da anteprima"));
                        require(promotedError.isEmpty(), "conferma dell'estensione mostrata in anteprima");
                        require(previewed.preview_.key.isEmpty(), "anteprima chiusa alla conferma");
                        require(previewed.extrusions_.back().forgeBody == previewGeometry
                                    && previewed.extrusions_.back().visible
                                    && !previewed.extrusions_.back().display.vertices.isEmpty(),
                                "geometria dell'anteprima promossa e visibile");
                    }
                }
                ++tested;
            }
        for (bool linear : {false, true}) {
            QString error;
            const ForgeBody result = forgeExtendSheet(feature.forgeBody, allBoundaryEdges, distance, linear, &error);
            std::cout << "  ALL linear=" << linear << ": " << (result ? "OK" : error.toStdString()) << std::endl;
            require(bool(result), "estensione contemporanea di tutti i bordi della faccia");
            Kernel::TessellationOptions options;
            options.deflection = 0.02;
            const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
            std::size_t triangles = 0;
            for (const Kernel::FaceMesh &part : mesh.faces) triangles += part.triangles.size();
            std::cout << "    result faces=" << result->faces().size() << " triangles=" << triangles
                      << " failed=" << mesh.failedFaces << std::endl;
            require(Kernel::checkBody(*result).empty() && mesh.failedFaces == 0 && triangles > 0,
                    "risultato visibile dell'estensione dell'intera faccia");
            CadViewport applied;
            applied.loadDocument(document);
            const QString createError = applied.createSheetExtend(
                bodyIndex, allBoundaryEdges, distance, linear, QStringLiteral("Estensione intera faccia"));
            require(createError.isEmpty(), "applicazione dell'estensione dell'intera faccia");
            const ExtrusionObject &created = applied.extrusions_.back();
            std::cout << "    applied visible=" << created.visible
                      << " vertices=" << created.display.vertices.size()
                      << " edges=" << created.display.edges.size()
                      << " sourceVisible=" << applied.extrusions_.at(bodyIndex).visible << std::endl;
            require(created.visible && !created.display.vertices.isEmpty(),
                    "feature visibile dell'estensione dell'intera faccia");
            if (!linear) {
                CadViewport previewed;
                previewed.loadDocument(document);
                previewed.requestExtendPreview(bodyIndex, allBoundaryEdges, distance, linear, -1);
                QElapsedTimer timeout;
                timeout.start();
                while (!(previewed.preview_.valid || !previewed.preview_.error.isEmpty()) && timeout.elapsed() < 30000)
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                require(previewed.preview_.valid && previewed.preview_.geometry,
                        "anteprima dell'estensione dell'intera faccia pronta");
                const ForgeBody previewGeometry = previewed.preview_.geometry;
                const QString promotedError = previewed.createSheetExtend(
                    bodyIndex, allBoundaryEdges, distance, linear, QStringLiteral("Estensione faccia da anteprima"));
                require(promotedError.isEmpty(), "conferma dell'estensione dell'intera faccia");
                const ExtrusionObject &promoted = previewed.extrusions_.back();
                require(previewed.preview_.key.isEmpty() && promoted.forgeBody == previewGeometry
                            && promoted.visible && !promoted.display.vertices.isEmpty(),
                        "estensione dell'intera faccia promossa e visibile");
            }
        }
        require(tested > 0, "almeno un bordo libero della faccia da estendere");
        require(!sameSurfaceFailed, "estensione B-spline lungo la stessa superficie");
    }
    static void repairExtensionContours(const QString &path, const QString &output, const QStringList &indices) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState state;
        require(loadDocumentFile(path, state).isEmpty(), "lettura modello da rigenerare");
        // "from=N": rigenera tutta la storia da N senza cambiare i contorni.
        QSet<int> changed;
        int first = state.extrusions.size();
        bool nearestRemap = false;
        for (const auto &index : indices) {
            if (index == QStringLiteral("nearest")) nearestRemap = true;
            else if (index.startsWith(QStringLiteral("from="))) first = std::min(first, index.mid(5).toInt());
            else changed.insert(index.toInt());
        }
        const auto original = state.extrusions;
        for (int index : changed) {
            require(index >= 0 && index < state.extrusions.size(), "indice estensione");
            auto &feature = state.extrusions[index];
            require(feature.feature == BodyFeature::SheetExtend && feature.firstBody >= 0, "feature di estensione");
            feature.blendEdges = completeExtensionContours(original.at(feature.firstBody).forgeBody, feature.blendEdges);
            require(!feature.blendEdges.isEmpty(), "contorni dell'estensione completi");
            first = std::min(first, index);
        }
        for (int index = first; index < state.extrusions.size(); ++index) {
            auto &feature = state.extrusions[index];
            const auto remap = [&](int base, EdgePoint &point, int kind) {
                if (base < first || base >= index || !state.extrusions.at(base).forgeBody) return;
                const auto &body = *state.extrusions.at(base).forgeBody;
                Box box;
                for (auto v : body.vertices()) box.add(body.vertex(v).point);
                const double reach = 1e-3 * std::max(1.0, box.diagonal());
                const Vec3 p(point.x, point.y, point.z);
                if (kind == 4 && nearestRemap) {
                    // Topologia cambiata a monte: gli ID non valgono piu', conta solo lo spigolo piu' vicino.
                    EdgeId best;
                    double bestDistance = reach * 100.0;
                    for (auto candidate : body.edges()) {
                        const auto &geometry = body.edge(candidate);
                        const double distance = norm(projectPoint(*geometry.curve, p, geometry.range).point - p);
                        if (distance < bestDistance) { bestDistance = distance; best = candidate; }
                    }
                    require(best.valid(), "spigolo piu' vicino dopo rigenerazione");
                    point = edgeReference(body, best, projectPoint(*body.edge(best).curve, p, body.edge(best).range).point);
                } else if (kind == 4) {
                    const auto e = resolveEdgeReference(body, point, reach, ReferenceState::Other);
                    require(e.valid(), "riferimento allo spigolo dopo rigenerazione");
                    point = edgeReference(body, e, projectPoint(*body.edge(e).curve, p, body.edge(e).range).point);
                } else if (kind == 5) {
                    const auto f = resolveFaceReference(body, point, reach, ReferenceState::Other);
                    require(f.valid(), "riferimento alla faccia dopo rigenerazione");
                    point = faceReference(body, f, p);
                }
            };
            for (auto &point : feature.blendEdges) remap(feature.firstBody, point, 4);
            for (auto *refs : {&feature.planarRefs, &feature.datum.refs})
                for (auto &ref : *refs) remap(ref.index, ref.point, ref.kind);
            if (nearestRemap) {
                // Piu' frammenti vecchi possono finire sullo stesso spigolo nuovo.
                QSet<QPair<int, int>> seen;
                for (int k = feature.blendEdges.size() - 1; k >= 0; --k) {
                    const QPair<int, int> key(-1, feature.blendEdges[k].subshape);
                    if (seen.contains(key)) feature.blendEdges.remove(k); else seen.insert(key);
                }
                seen.clear();
                for (int k = feature.planarRefs.size() - 1; k >= 0; --k) {
                    if (feature.planarRefs[k].kind != 4) continue;
                    const QPair<int, int> key(feature.planarRefs[k].index, feature.planarRefs[k].point.subshape);
                    if (seen.contains(key)) feature.planarRefs.remove(k); else seen.insert(key);
                }
            }
            feature.cachedGeometry = false;
            CadViewport::buildGeometry(feature, index, state.sketches, state.extrusions);
            std::cout << index << " " << feature.name.toStdString() << ": " << feature.error.toStdString() << std::endl;
            if (!feature.error.isEmpty() && feature.firstBody >= 0 && state.extrusions.at(feature.firstBody).forgeBody) {
                const auto &body = *state.extrusions.at(feature.firstBody).forgeBody;
                for (const auto &point : feature.blendEdges)
                    std::cout << "  rif (" << point.x << "," << point.y << "," << point.z << ") id=" << point.subshape << std::endl;
                for (auto e : body.edges()) if (body.isLaminar(e)) {
                    const auto &edge = body.edge(e);
                    const auto a = edge.curve->point(edge.range.lo), m = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi)), b = edge.curve->point(edge.range.hi);
                    std::cout << "  libero E" << e.index << " (" << a.x() << "," << a.y() << "," << a.z() << ") ("
                              << m.x() << "," << m.y() << "," << m.z() << ") (" << b.x() << "," << b.y() << "," << b.z() << ")" << std::endl;
                }
            }
            require(feature.error.isEmpty(), "rigenerazione della storia dopo estensione");
            CadViewport::tessellateGeometry(feature, 0, feature.display);
        }
        require(saveDocumentFile(output, state).isEmpty(), "salvataggio modello con contorni completi");
    }
    static void auditSheetHistory(const QString &path, const QString &outputDirectory) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per controllo superfici");
        if (!outputDirectory.isEmpty())
            require(QDir().mkpath(outputDirectory), "cartella di esportazione della geometria diagnostica");
        CadViewport viewport;
        viewport.loadDocument(document);
        for (int i = 0; i < viewport.extrusions_.size(); ++i) {
            const auto &feature = viewport.extrusions_.at(i);
            std::cout << "B" << i << " " << feature.name.toStdString() << " base=" << feature.firstBody
                      << " second=" << feature.secondBody << " length=" << feature.blendSize
                      << " linear=" << feature.extendLinear << " sewTol=" << feature.sewTolerance << std::endl;
            if (!feature.forgeBody) continue;
            const auto &body = *feature.forgeBody;
            if (!outputDirectory.isEmpty()) {
                std::ofstream file((outputDirectory + QStringLiteral("/B%1.body").arg(i)).toStdString(), std::ios::binary);
                file << writeBodyBinary(body);
                require(bool(file), "scrittura della geometria diagnostica");
            }
            const auto issues = checkBody(body);
            int free = 0;
            for (auto e : body.edges()) free += body.isLaminar(e);
            std::cout << " faces=" << body.faces().size() << " free=" << free << " issues=" << issues.size() << std::endl;
            for (const auto &issue : issues) std::cout << " ISSUE " << issue.message << std::endl;
            for (auto f : body.faces()) {
                const auto box = faceBox(body, f);
                std::cout << " F" << f.index << " y=" << box.lo.y() << ".." << box.hi.y() << " free:";
                for (auto e : faceBoundaryEdges(body, f)) if (body.isLaminar(e)) {
                    const auto &edge = body.edge(e);
                    const auto a = edge.curve->point(edge.range.lo), b = edge.curve->point(edge.range.hi);
                    std::cout << " E" << e.index << "(" << a.x() << "," << a.y() << "," << a.z()
                              << " -> " << b.x() << "," << b.y() << "," << b.z() << ")";
                }
                std::cout << std::endl;
            }
            if (feature.feature == BodyFeature::SheetExtend && feature.firstBody >= 0) {
                require(feature.firstBody < viewport.extrusions_.size()
                            && bool(viewport.extrusions_.at(feature.firstBody).forgeBody), "base dell'estensione disponibile");
                const auto &base = *viewport.extrusions_.at(feature.firstBody).forgeBody;
                for (const auto &ref : feature.blendEdges) {
                    const auto edge = resolveEdgeReference(base, ref, 0.1);
                    std::cout << " selected=" << edge.index << " stored=" << ref.subshape;
                    if (edge.valid()) {
                        const auto &e = base.edge(edge);
                        const auto mid = e.curve->point((e.range.lo + e.range.hi) * 0.5);
                        std::cout << " midpoint=" << mid.x() << "," << mid.y() << "," << mid.z();
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
    static void trimTopologyBodies(const QString &path, int first, int second, bool verifyClosed = false) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per taglio superfici");
        CadViewport viewport;
        viewport.loadDocument(document);
        QVector<SheetPiece> regions[2];
        for (int target : {first, second}) {
            const int tool = target == first ? second : first;
            require(target >= 0 && target < viewport.extrusions_.size()
                        && tool >= 0 && tool < viewport.extrusions_.size(), "indici corpi del taglio");
            const ExtrusionObject &a = viewport.extrusions_.at(target), &b = viewport.extrusions_.at(tool);
            std::cout << "B" << target << " " << a.name.toStdString() << " feature=" << int(a.feature)
                      << " solidFlag=" << a.solid << " loftSurface=" << a.loftSurface
                      << " sheet=" << bool(a.forgeBody && a.forgeBody->isSheet())
                      << " faces=" << (a.forgeBody ? a.forgeBody->faces().size() : 0)
                      << " <- B" << tool << " " << b.name.toStdString() << " feature=" << int(b.feature)
                      << " solidFlag=" << b.solid << " loftSurface=" << b.loftSurface
                      << " sheet=" << bool(b.forgeBody && b.forgeBody->isSheet())
                      << " faces=" << (b.forgeBody ? b.forgeBody->faces().size() : 0) << std::endl;
            require(bool(a.forgeBody) && bool(b.forgeBody), "geometria dei corpi del taglio disponibile");
            for (auto face : a.forgeBody->faces()) {
                const auto box = Kernel::faceBox(*a.forgeBody, face);
                std::cout << "  F" << face.index << " type=" << int(a.forgeBody->face(face).surface->type())
                          << " box=" << box.lo.x() << "," << box.lo.y() << "," << box.lo.z()
                          << " / " << box.hi.x() << "," << box.hi.y() << "," << box.hi.z() << std::endl;
            }
            QString error;
            const QVector<SheetPiece> pieces = forgeSheetPieces(a.forgeBody, b.forgeBody, 0, &error);
            regions[target == first ? 0 : 1] = pieces;
            std::cout << "  pieces=" << pieces.size() << " error=" << error.toStdString();
            for (const SheetPiece &piece : pieces) std::cout << " area=" << piece.area;
            std::cout << std::endl;
        }
        if (verifyClosed) {
            require(regions[0].size() == 2 && regions[1].size() == 2, "due regioni su entrambi i corpi");
            bool closed = false;
            for (const auto &a : regions[0]) for (const auto &b : regions[1]) {
                QString error;
                const auto joined = forgeSew({a.geometry, b.geometry}, 1e-6, false, &error);
                require(bool(joined) && error.isEmpty() && Kernel::checkBody(*joined).empty(), "applicazione del taglio reciproco");
                int free = 0;
                for (auto e : joined->edges()) free += joined->isLaminar(e);
                closed = closed || free == 0;
                std::cout << "  taglio reciproco: bordi liberi=" << free << std::endl;
            }
            require(closed, "almeno una scelta produce il loft chiuso dal piano");
        }
    }
    // Riproduzione del raccordo sui contorni delle facce di un documento.
    // Salva solo dopo aver verificato risultato, chiusura e rilettura della copia.
    static void blendDocumentFaces(const QStringList &args) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        require(args.size() >= 5, "uso: --blend-document-faces input output corpo raggio facce...");
        require(QFileInfo(args[0]).absoluteFilePath() != QFileInfo(args[1]).absoluteFilePath(),
                "il risultato deve essere una copia del documento");
        bool validBody = false, validRadius = false;
        const int baseIndex = args[2].toInt(&validBody);
        const double radius = args[3].toDouble(&validRadius);
        require(validBody && validRadius && std::isfinite(radius) && radius > 0.0, "corpo e raggio validi");
        DocumentState state;
        require(loadDocumentFile(args[0], state).isEmpty(), "lettura documento da raccordare");
        require(baseIndex >= 0 && baseIndex < state.extrusions.size(), "indice del corpo da raccordare");
        const auto base = state.extrusions.at(baseIndex).forgeBody;
        require(bool(base), "geometria del corpo da raccordare disponibile");
        QVector<EdgePoint> references;
        std::set<int> selected;
        const auto faces = base->faces();
        for (const auto &value : args.mid(4)) {
            bool valid = false;
            const FaceId face(value.toInt(&valid));
            require(valid && std::find(faces.begin(), faces.end(), face) != faces.end(), "indice faccia valido");
            for (const auto edge : faceBoundaryEdges(*base, face)) {
                if (!selected.insert(edge.index).second) continue;
                const auto &e = base->edge(edge);
                references.append(edgeReference(*base, edge, e.curve->point(0.5 * (e.range.lo + e.range.hi))));
            }
        }
        require(!references.isEmpty(), "contorni delle facce selezionati");
        // La copia conserva la storia originale; mostra soltanto la nuova variante.
        for (auto &feature : state.extrusions) feature.visible = false;
        for (auto &sketch : state.sketches) sketch.visible = false;
        CadViewport viewport;
        viewport.loadDocument(state);
        const QString error = viewport.createBlend(baseIndex, references, radius, false,
            QStringLiteral("Raccordi facce loft R%1 mm").arg(radius));
        std::cout << "R=" << radius << " mm: " << error.toStdString() << std::endl;
        require(error.isEmpty(), "creazione raccordi sui contorni delle facce");
        // Il corpo logico era nascosto con gli altri: la variante nuova deve vedersi all'apertura.
        viewport.setObjectVisible(SceneObjectKind::Extrusion, int(viewport.extrusions_.size()) - 1, true);
        require(viewport.extrusions_.back().visible, "raccordo visibile nella copia");
        const auto result = viewport.extrusions_.back().forgeBody;
        require(bool(result) && forgeBlendHasEffect(base, result), "raccordi effettivamente costruiti");
        require(checkBody(*result).empty(), "B-rep raccordato valido");
        for (const auto edge : result->edges()) require(!result->isLaminar(edge), "corpo raccordato chiuso");
        require(saveDocumentFile(args[1], viewport.documentState()).isEmpty(), "salvataggio copia raccordata");
        DocumentState saved;
        require(loadDocumentFile(args[1], saved).isEmpty(), "rilettura copia raccordata");
        require(saved.extrusions.back().forgeBody
                    && checkBody(*saved.extrusions.back().forgeBody).empty()
                    && saved.extrusions.back().forgeBody->faces().size() == result->faces().size(),
                "raccordi conservati nella copia salvata");
        std::cout << "Copia verificata: " << args[1].toStdString() << ", facce=" << result->faces().size() << std::endl;
    }
    static void loftCorner(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura loft di regressione");
        for (int i = 0; i < document.extrusions.size(); ++i) {
            auto &feature = document.extrusions[i];
            feature.cachedGeometry = false;
            CadViewport::buildGeometry(feature, i, document.sketches, document.extrusions);
            std::cout << i << " " << feature.name.toStdString() << std::endl;
        }
        const auto body = document.extrusions.back().forgeBody;
        require(bool(body), "loft rigenerato");
        // Riproduzione facoltativa della catena del file di esempio: bordo
        // longitudinale e le due continuazioni sui raccordi dei coperchi.
        QVector<EdgePoint> references;
        for (int index : {11, 77, 98}) {
            const Kernel::EdgeId edge(index);
            const auto &e = body->edge(edge);
            const auto p = e.curve->point(0.5 * (e.range.lo + e.range.hi));
            references.push_back(edgeReference(*body, edge, p));
        }
        QString error;
        const auto result = forgeBlend(body, references, 0.5, false, &error);
        std::cout << "Catena loft: " << error.toStdString() << std::endl;
        require(bool(result), "raccordo della catena tangente del loft rigenerato");
    }
    // Rendering di un file STEP importato da un punto di vista dato, per
    // confrontare a occhio la tassellazione: --render-step file.stp out.png
    // cx cy cz nx ny nz [semi-lato] (centro, normale verso l'osservatore).
    static void renderStep(const QStringList &args) {
        using namespace ForgeCad;
        QVector<ImportedPart> parts;
        require(importCadFile(args.at(0), parts).isEmpty() && !parts.isEmpty(), "importazione STEP");
        CadViewport v;
        v.resize(1200, 1200);
        v.importParts(parts, args.at(0));
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        const QVector3D c(args.at(2).toFloat(), args.at(3).toFloat(), args.at(4).toFloat());
        const QVector3D n = QVector3D(args.at(5).toFloat(), args.at(6).toFloat(), args.at(7).toFloat()).normalized();
        const float half = args.size() > 8 ? args.at(8).toFloat() : 2.0f;
        QVector3D x = QVector3D::crossProduct(QVector3D(0, 0, 1), n);
        if (x.length() < 1e-3f) x = QVector3D(1, 0, 0);
        v.setViewFrame(x.normalized(), n);
        v.fitView({c - QVector3D(half, half, half), c + QVector3D(half, half, half)});
        v.update();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        require(v.grabFramebuffer().save(args.at(1)), "salvataggio dell'immagine");
    }
    // Stato del viewport durante "Modifica parametri" della funzione `index`
    // di un documento: --render-edit doc.prt indice prima.png dopo.png.
    static void renderEdit(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        CadViewport v;
        v.resize(1000, 800);
        v.loadDocument(document);
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        v.fitAll();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        for (int i = 0; i < v.extrusions_.size(); ++i) {
            const ExtrusionObject &e = v.extrusions_.at(i);
            std::cout << i << " " << e.name.toStdString() << " visible=" << e.visible << " feature=" << int(e.feature)
                      << " body=" << e.modelBodyId << " suppressed=" << e.suppressed << " ops=";
            for (int o : CadViewport::bodyOperands(e)) std::cout << o << ",";
            std::cout << " err=" << e.error.toStdString() << std::endl;
        }
        require(v.grabFramebuffer().save(args.at(2)), "immagine prima");
        const int index = args.at(1).toInt();
        v.requestPreview(v.extrusions_.at(index), index);
        QElapsedTimer timer;
        timer.start();
        while ((!v.preview_.valid && v.preview_.error.isEmpty()) && timer.elapsed() < 120000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        std::cout << "preview valid=" << v.preview_.valid << " error=" << v.preview_.error.toStdString() << " replaced=";
        for (int r : v.preview_.replaced) std::cout << r << ",";
        std::cout << " triangles=" << v.preview_.display.vertices.size() / 3 << std::endl;
        v.update();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        require(v.grabFramebuffer().save(args.at(3)), "immagine dopo");
    }
    // Tempi della vista su un documento: --bench-view doc.prt [ripetizioni].
    // Disegno dopo rotazione e zoom, movimento del mouse senza tasti (hover)
    // e trascinamento con il sinistro (orbita), come li fa l'utente.
    static void benchView(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        const int count = args.size() > 1 ? args.at(1).toInt() : 30;
        CadViewport v;
        v.resize(1400, 900);
        const auto paintNow = [](CadViewport &w) {
            w.makeCurrent();
            w.paintGL();
            glFinish();
            w.doneCurrent();
        };
        v.loadDocument(document);
        if (args.size() > 2) v.setAntialiasing(args.at(2).toInt());
        v.show();
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        v.fitAll();
        paintNow(v);
        v.makeCurrent();
        std::cout << "renderer: " << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << " dpr " << v.devicePixelRatioF()
                  << " campioni " << v.bufferSamples_ << std::endl;
        v.doneCurrent();
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < count; ++i) { v.yaw_ += 2.0f; paintNow(v); }
        std::cout << "paint dopo rotazione: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) { v.setZoom(v.zoom_ * (i % 2 ? 1.05f : 0.95f)); paintNow(v); }
        std::cout << "paint dopo zoom: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            const QPointF p(300 + 20 * i, 300 + 7 * i);
            v.updateHover(p.toPoint());
        }
        if (qEnvironmentVariableIsSet("BENCH_HOVER_ONLY")) {
            t.restart();
            for (int i = 0; i < 400; ++i) {
                const QPointF p(200 + (i * 37) % 900, 150 + (i * 53) % 600);
                v.updateHover(p.toPoint());
            }
            std::cout << "hover x400: " << double(t.nsecsElapsed()) / 1e6 / 400 << " ms" << std::endl;
            return;
        }
        std::cout << "hover (solo evento): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        {
            QPointF p(400, 400);
            QMouseEvent press(QEvent::MouseButtonPress, p, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            v.mousePressEvent(&press);
            for (int i = 0; i < count; ++i) {
                p += QPointF(6, 2);
                QMouseEvent move(QEvent::MouseMove, p, p, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                v.mouseMoveEvent(&move);
                paintNow(v);
            }
            QMouseEvent release(QEvent::MouseButtonRelease, p, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            v.mouseReleaseEvent(&release);
        }
        std::cout << "orbita (evento + paint): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            QWheelEvent wheel(QPointF(500, 400), QPointF(500, 400), QPoint(), QPoint(0, i % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            v.wheelEvent(&wheel);
            paintNow(v);
        }
        std::cout << "rotella (evento + paint): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
    }
    // Confronta la ricostruzione parametrica completa con l'apertura dello
    // snapshot B-rep + mesh: --bench-load doc.prt [ripetizioni snapshot].
    static void benchLoad(const QStringList &args) {
        using namespace ForgeCad;
        const int count = args.size() > 1 ? qMax(1, args.at(1).toInt()) : 5;
        QElapsedTimer timer;
        timer.start();
        DocumentState source;
        require(loadDocumentFile(args.at(0), source).isEmpty(), "lettura del documento da misurare");
        for (ExtrusionObject &body : source.extrusions) {
            body.cachedGeometry = false;
            body.forgeBody.reset();
            body.curve.reset();
            body.display = {};
        }
        CadViewport rebuilt;
        rebuilt.loadDocument(std::move(source));
        const double rebuiltMs = double(timer.nsecsElapsed()) / 1e6;

        QTemporaryDir directory;
        require(directory.isValid(), "directory temporanea per lo snapshot");
        const QString snapshot = directory.filePath(QStringLiteral("snapshot.prt"));
        require(saveDocumentFile(snapshot, rebuilt.currentDocument(), true).isEmpty(), "salvataggio dello snapshot");
        double snapshotMs = 0.0;
        for (int i = 0; i < count; ++i) {
            timer.restart();
            DocumentState state;
            require(loadDocumentFile(snapshot, state).isEmpty(), "lettura dello snapshot");
            CadViewport opened;
            opened.loadDocument(std::move(state));
            snapshotMs += double(timer.nsecsElapsed()) / 1e6;
        }
        snapshotMs /= count;
        std::cout << "ricostruzione completa: " << rebuiltMs << " ms\n"
                  << "apertura snapshot: " << snapshotMs << " ms (media di " << count << ")\n"
                  << "accelerazione: " << rebuiltMs / snapshotMs << "x\n"
                  << "file origine: " << QFileInfo(args.at(0)).size() << " byte, snapshot: "
                  << QFileInfo(snapshot).size() << " byte" << std::endl;
    }
    // Come --bench-view ma nella finestra completa dell'app, con le
    // impostazioni dell'utente copiate: tempo da un evento al fotogramma
    // mostrato (frameSwapped). --bench-window doc.prt [ripetizioni]
    static void benchWindow(const QStringList &args) {
        const int count = args.size() > 1 ? args.at(1).toInt() : 30;
        PdfWindow window;
        window.resize(2560, 1400);
        window.show();
        for (int i = 0; i < 20; ++i) QApplication::processEvents();
        require(window.openDocumentPath(args.at(0)), "apertura del documento");
        CadViewport *v = nullptr;
        for (QOpenGLWidget *w : window.findChildren<QOpenGLWidget *>())
            if (auto *c = dynamic_cast<CadViewport *>(w); c && (!v || c->width() > v->width())) v = c;
        require(v, "viewport");
        for (int i = 0; i < 20; ++i) QApplication::processEvents();
        int frames = 0;
        QObject::connect(v, &QOpenGLWidget::frameSwapped, [&] { ++frames; });
        v->makeCurrent();
        std::cout << "renderer: " << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << " size " << v->width() << "x" << v->height()
                  << " campioni " << v->bufferSamples_ << std::endl;
        v->doneCurrent();
        const auto waitFrame = [&] {
            const int start = frames;
            QElapsedTimer t;
            t.start();
            while (frames == start && t.elapsed() < 2000) QApplication::processEvents(QEventLoop::AllEvents, 5);
        };
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_SHOW")) {
            v->setObjectVisible(SceneObjectKind::Extrusion, qEnvironmentVariableIntValue("FORGECAD_BENCH_SHOW"), true);
            v->fitAll();
            for (int i = 0; i < 20; ++i) QApplication::processEvents();
        }
        const QPointF center(v->width() / 2.0, v->height() / 2.0);
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_SHOT")) v->grabFramebuffer().save(qEnvironmentVariable("FORGECAD_BENCH_SHOT"));
        {
            const SceneSelection hit = v->pickSceneObject(center.toPoint(), false);
            std::cout << "al centro: tipo " << int(hit.kind) << " indice " << hit.index << std::endl;
        }
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < count; ++i) {
            QWheelEvent wheel(center, v->mapToGlobal(center), QPoint(), QPoint(0, i % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(v, &wheel);
            waitFrame();
        }
        std::cout << "rotella -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        QPointF p = center;
        // Hover sul corpo prima del clic, poi il primo fotogramma dell'orbita
        // a parte: e' l'attesa che si nota prima che la vista cominci a girare.
        t.restart();
        {
            QMouseEvent hover(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &hover);
            waitFrame();
        }
        std::cout << "primo hover -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        t.restart();
        QMouseEvent press(QEvent::MouseButtonPress, p, v->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &press);
        std::cout << "pressione: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            p += QPointF(8, 3);
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            waitFrame();
            if (i == 0) {
                std::cout << "primo fotogramma dell'orbita: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
                t.restart();
            }
        }
        std::cout << "orbita -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / std::max(1, count - 1) << " ms" << std::endl;
        QMouseEvent release(QEvent::MouseButtonRelease, p, v->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &release);
        t.restart();
        for (int i = 0; i < count; ++i) {
            p += QPointF(-8, 4);
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            waitFrame();
        }
        std::cout << "hover -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_IDLE")) {
            QElapsedTimer idle;
            idle.start();
            while (idle.elapsed() < qEnvironmentVariableIntValue("FORGECAD_BENCH_IDLE")) QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        // Clic semplice sul corpo (selezione al rilascio): tempo sincrono, poi il primo fotogramma dell'orbita successiva.
        for (int i = 0; i < 3; ++i) {
            const QPointF c = center + QPointF(20 * i, 10 * i);
            t.restart();
            QMouseEvent clickPress(QEvent::MouseButtonPress, c, v->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &clickPress);
            QMouseEvent clickRelease(QEvent::MouseButtonRelease, c, v->mapToGlobal(c), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &clickRelease);
            std::cout << "clic " << i << ": " << double(t.nsecsElapsed()) / 1e6 << " ms";
            t.restart();
            QPointF q = c;
            QMouseEvent orbitPress(QEvent::MouseButtonPress, q, v->mapToGlobal(q), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &orbitPress);
            for (int k = 0; k < 3; ++k) {
                q += QPointF(8, 3);
                QMouseEvent move(QEvent::MouseMove, q, v->mapToGlobal(q), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(v, &move);
            }
            waitFrame();
            std::cout << ", poi orbita al primo fotogramma " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
            QMouseEvent orbitRelease(QEvent::MouseButtonRelease, q, v->mapToGlobal(q), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &orbitRelease);
        }
        // Lavoro sincrono di ogni movimento senza tasti (hover), senza aspettare i fotogrammi.
        for (int i = 0; i < 6; ++i) {
            p += QPointF(i % 2 ? -15 : 12, 6);
            t.restart();
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            const double sent = double(t.nsecsElapsed()) / 1e6;
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            std::cout << "hover " << i << ": evento " << sent << " ms, con il timer " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        }
        window.close();
    }
    // Funzioni proprietarie delle facce cliccate su una griglia di pixel:
    // --pick-owner doc.prt
    static void pickOwner(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        CadViewport v;
        v.resize(1000, 800);
        v.loadDocument(document);
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        v.fitAll();
        std::map<std::string, int> owners;
        QElapsedTimer t;
        double worst = 0.0, total = 0.0, pickTotal = 0.0;
        int clicks = 0;
        for (int y = 40; y < 800; y += 40)
            for (int x = 40; x < 1000; x += 40) {
                t.restart();
                const SceneSelection hit = v.pickSceneObject(QPoint(x, y));
                if (hit.kind != SceneObjectKind::Extrusion) continue;
                FaceHit face;
                if (!v.pickBodyFace(hit.index, QPoint(x, y), face)) continue;
                const double pickMs = double(t.nsecsElapsed()) / 1e6;
                const int owner = v.faceOwnerFeature(hit.index, QPoint(x, y), face);
                const double ms = double(t.nsecsElapsed()) / 1e6;
                pickTotal += pickMs;
                worst = std::max(worst, ms);
                total += ms;
                ++clicks;
                ++owners[v.extrusions_.at(hit.index).name.toStdString() + " -> " + v.extrusions_.at(owner).name.toStdString()];
            }
        // Dettaglio del raggio esatto sul corpo visibile: con e senza la finestra
        // attorno al punto della tassellazione.
        for (int i = 0; i < v.extrusions_.size(); ++i) {
            const ExtrusionObject &e = v.extrusions_.at(i);
            if (!e.visible || !e.forgeBody) continue;
            double withWindow = 0.0, without = 0.0, sketchPart = 0.0;
            int rays = 0, agree = 0;
            for (int y = 40; y < 800; y += 40)
                for (int x = 40; x < 1000; x += 40) {
                    QVector3D origin, direction;
                    v.viewRay(QPoint(x, y), origin, direction);
                    float meshT = 0.0f;
                    if (!CadViewport::meshRayHit(e.display, origin, direction, meshT)) continue;
                    ++rays;
                    if (qEnvironmentVariableIsSet("DUMP_RAYS")) {
                        static std::ofstream raysOut(qgetenv("DUMP_RAYS").toStdString());
                        raysOut.precision(17);
                        raysOut << origin.x() << " " << origin.y() << " " << origin.z() << " " << direction.x() << " " << direction.y() << " "
                                << direction.z() << " " << meshT << "\n";
                        static bool dumped = false;
                        if (!dumped) {
                            std::ofstream bodyOut(qgetenv("DUMP_RAYS").toStdString() + ".bin", std::ios::binary);
                            const std::string data = Kernel::writeBodyBinary(*e.forgeBody);
                            bodyOut.write(data.data(), std::streamsize(data.size()));
                            dumped = true;
                        }
                    }
                    FaceHit a, b;
                    t.restart();
                    const double slack = std::max(1e-6, 0.01 * e.display.rayIndex->bounds.diagonal());
                    const Kernel::Interval window{double(meshT) - slack, double(meshT) + slack};
                    const bool ha = forgePickFace(*e.forgeBody, origin, direction, a, e.display.rayIndex.get(), &window);
                    withWindow += double(t.nsecsElapsed()) / 1e6;
                    t.restart();
                    const bool hb = forgePickFace(*e.forgeBody, origin, direction, b, e.display.rayIndex.get());
                    without += double(t.nsecsElapsed()) / 1e6;
                    agree += ha == hb && (!ha || a.face == b.face);
                    t.restart();
                    v.pickSceneObject(QPoint(x, y), false);
                    sketchPart += double(t.nsecsElapsed()) / 1e6;
                }
            std::cout << "corpo " << i << " raggi " << rays << " finestra " << withWindow / std::max(1, rays) << " ms, tutte le facce "
                      << without / std::max(1, rays) << " ms, concordi " << agree << ", pick sulla mesh " << sketchPart / std::max(1, rays) << " ms" << std::endl;
        }
        for (const auto &[name, count] : owners) std::cout << count << "  " << name << std::endl;
        std::cout << "clic " << clicks << " media " << total / std::max(1, clicks) << " ms (di cui raggio " << pickTotal / std::max(1, clicks)
                  << "), peggiore " << worst << " ms" << std::endl;
        for (int i = 0; i < v.extrusions_.size(); ++i) if (v.extrusions_.at(i).visible) std::cout << "visibile " << i << " " << v.extrusions_.at(i).name.toStdString() << std::endl;
    }
    static void run(bool render) {
        extensionContourPicking();
        trimPartPicking();
        deletionDuringEdgePick();
        using namespace ForgeCad;
        CadViewport v;
        {
            ExpressionSpinBox value;
            value.setKeyboardTracking(false);
            int previews = 0;
            value.onReturn = [&] { ++previews; };
            value.findChild<QLineEdit *>()->setText(QStringLiteral("1/2"));
            require(previews == 0, "nessuna anteprima durante la digitazione");
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(&value, &enter);
            require(previews == 1 && value.value() == 0.5, "Invio conferma il valore e richiede una sola anteprima");
            value.clearFocus();
            require(previews == 1, "cambio di focus senza nuove anteprime");
            value.stepBy(1);
            require(previews == 2, "le frecce aggiornano l'anteprima");
            setDisplayLengthUnit(LengthUnit::Inch);
            value.setRange(0.0, 1000.0);
            value.findChild<QLineEdit *>()->setText(QStringLiteral("2"));
            value.interpretText();
            require(std::fabs(value.value() - 50.8) < 1e-12 && value.suffix().trimmed() == QStringLiteral("in")
                        && formatLength(25.4) == QStringLiteral("1 in"),
                    "campi in pollici convertiti internamente in millimetri");
            value.setLengthMeasurement(false);
            value.setSuffix(QStringLiteral(" °"));
            setDisplayLengthUnit(LengthUnit::Foot);
            require(value.suffix().trimmed() == QStringLiteral("°"), "gli angoli non cambiano con l'unita' lineare");
            setDisplayLengthUnit(LengthUnit::Millimeter);
        }
        {
            // Filettatura parametrica: riconoscimento automatico della faccia
            // esterna e aggiunta del profilo elicoidale al cilindro.
            const Kernel::Body cylinder = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 5.0, 10.0);
            Kernel::FaceId side;
            for (Kernel::FaceId face : cylinder.faces())
                if (cylinder.face(face).surface->type() == Kernel::SurfaceType::Cylinder) { side = face; break; }
            require(side.valid(), "faccia cilindrica per il filetto");
            const Kernel::FinId fin = cylinder.loop(cylinder.face(side).loops.front()).first;
            ThreadParameters parameters;
            parameters.standard = 0;
            parameters.designation = QStringLiteral("M10 x 1,5");
            parameters.pitch = 1.5;
            parameters.length = 6.0;
            parameters.face = faceReference(cylinder, side, cylinder.finPoint(fin, 0.5));
            ThreadFaceInfo info;
            QString error;
            require(forgeThreadFaceInfo(cylinder, parameters.face, info, &error) && !info.internal
                        && std::fabs(info.diameter - 10.0) < 1e-9 && std::fabs(info.length - 10.0) < 1e-9,
                    "misure automatiche della faccia da filettare");
            const ForgeBody base = std::make_shared<const Kernel::Body>(cylinder);
            const ForgeBody threaded = forgeThread(base, parameters, &error);
            require(threaded && error.isEmpty() && Kernel::massProperties(*threaded).volume > Kernel::massProperties(cylinder).volume,
                    "filettatura geometrica esterna");
            int sweptFaces = 0;
            for (Kernel::FaceId face : threaded->faces())
                sweptFaces += threaded->face(face).surface->type() == Kernel::SurfaceType::BSpline;
            require(sweptFaces > 0, "filettatura esterna con superfici sweep B-spline, non facce poliedriche");
            const Kernel::Body bore = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(0, 0, -1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 12.0);
            const Kernel::Body ring = Kernel::booleanOperation(cylinder, bore, Kernel::BooleanOperation::Subtract);
            Kernel::FaceId inside;
            for (Kernel::FaceId face : ring.faces()) {
                if (ring.face(face).surface->type() != Kernel::SurfaceType::Cylinder) continue;
                const auto &surface = static_cast<const Kernel::CylindricalSurface &>(*ring.face(face).surface);
                if (std::fabs(surface.radius() - 3.0) < 1e-9) { inside = face; break; }
            }
            require(inside.valid(), "faccia interna per il filetto");
            parameters.pitch = 1.0;
            parameters.length = 2.0;
            const Kernel::FinId insideFin = ring.loop(ring.face(inside).loops.front()).first;
            parameters.face = faceReference(ring, inside, ring.finPoint(insideFin, 0.5));
            require(forgeThreadFaceInfo(ring, parameters.face, info, &error) && info.internal,
                    "riconoscimento automatico del foro interno");
            const ForgeBody threadedHole = forgeThread(std::make_shared<const Kernel::Body>(ring), parameters, &error);
            require(threadedHole && error.isEmpty() && Kernel::massProperties(*threadedHole).volume < Kernel::massProperties(ring).volume,
                    "filettatura geometrica interna");
            DocumentState state;
            ExtrusionObject root;
            root.name = QStringLiteral("Cilindro");
            root.feature = BodyFeature::Primitive;
            root.primitive.kind = PrimitiveKind::Cylinder;
            root.primitive.size[0] = 5.0;
            root.primitive.size[1] = 10.0;
            ExtrusionObject feature;
            feature.name = QStringLiteral("Filettatura 1");
            feature.feature = BodyFeature::Thread;
            feature.firstBody = 0;
            feature.thread = parameters;
            state.extrusions = {root, feature};
            QTemporaryDir dir;
            const QString path = dir.filePath(QStringLiteral("filetto.prt"));
            require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio della filettatura");
            DocumentState loaded;
            require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.size() == 2
                        && loaded.extrusions.at(1).feature == BodyFeature::Thread
                        && loaded.extrusions.at(1).thread.designation == parameters.designation
                        && std::fabs(loaded.extrusions.at(1).thread.pitch - 1.0) < 1e-12,
                    "lettura della filettatura dal documento");
        }
        {
            CadViewport selection;
            int picks = 0;
            selection.selectionCallback_ = [&](const SceneSelection &) { ++picks; };
            const QPointF a(20, 20), b(100, 100);
            QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            selection.mousePressEvent(&press);
            require(picks == 0, "selezione differita al rilascio");
            selection.mouseMoveEvent(&move);
            selection.mouseReleaseEvent(&release);
            require(picks == 0, "rotazione senza selezione");
            selection.mousePressEvent(&press);
            QMouseEvent clickRelease(QEvent::MouseButtonRelease, a, a, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            selection.mouseReleaseEvent(&clickRelease);
            require(picks == 1, "clic seleziona una sola volta");
        }
        {
            // Misura: aggancio al punto medio di uno spigolo, risolto sulla curva esatta.
            CadViewport measure;
            measure.resize(800, 600);
            PrimitiveParameters box;
            box.kind = PrimitiveKind::Box;
            box.size[0] = 4.0;
            box.size[1] = 3.0;
            box.size[2] = 2.0;
            require(measure.createPrimitive(box, QStringLiteral("Blocco misura")).isEmpty(), "corpo da misurare");
            measure.fitAll();
            const ExtrusionObject &body = measure.extrusions_.last();
            const Kernel::EdgeId edge = body.forgeBody->edges().front();
            const Kernel::Vec3 middle = edgeMidpoint(*body.forgeBody, edge);
            measure.refPickOwner_ = -1;
            measure.refPickRoles_ = DatumRolePoint | DatumRoleCurve | DatumRoleFace | kMeasureSnapRole;
            GeometryRef picked;
            require(measure.pickReference(measure.projectWorldPoint(QVector3D(middle.x(), middle.y(), middle.z())).toPoint(), picked)
                        && picked.kind == kGeometryRefEdgeMidpoint,
                    "aggancio al punto medio dello spigolo");
            MeasureEntity entity;
            require(measureEntity(picked, measure.sketches_, measure.extrusions_, entity, nullptr) && entity.kind == MeasureEntity::Kind::Point,
                    "punto medio misurabile");
            {
                // Scelta del corpo dalla faccia cliccata (finestre con l'elenco dei corpi).
                require(measure.createPrimitive(box, QStringLiteral("Secondo blocco")).isEmpty(), "secondo corpo");
                QDialog dialog;
                auto *combo = new QComboBox(&dialog);
                const QVector<int> candidates = measure.resultBodiesBefore(-1);
                for (int index : candidates) combo->addItem(QString::number(index));
                combo->setCurrentIndex(int(candidates.size()) - 1);
                BodyPicker picker(dialog, &measure, -1, candidates, combo);
                int changes = 0;
                picker.changed = [&] { ++changes; };
                picker.start(true);
                require(picker.active() && picker.waiting(), "scelta del corpo in attesa");
                GeometryRef face;
                face.kind = 5;
                face.index = candidates.first();
                require(picker.handle(true, face) && combo->currentIndex() == 0 && !picker.active() && !picker.waiting() && changes == 1,
                        "la faccia cliccata sceglie il corpo");
                require(!picker.handle(true, face), "clic fuori dalla scelta del corpo");
            }
            const Kernel::Edge &exact = body.forgeBody->edge(Kernel::EdgeId(picked.point.subshape));
            const Kernel::Vec3 expected = 0.5 * (exact.curve->point(exact.range.lo) + exact.curve->point(exact.range.hi));
            require(distance(entity.point, expected) < 1e-12, "punto medio esatto");
        }
        {
            CadViewport topology;
            topology.resize(800, 600);
            PrimitiveParameters box;
            box.kind = PrimitiveKind::Box;
            box.size[0] = 4.0;
            box.size[1] = 3.0;
            box.size[2] = 2.0;
            require(topology.createPrimitive(box, QStringLiteral("Blocco ID")).isEmpty(),
                    "corpo per le etichette topologiche");
            topology.fitAll();
            topology.setTopologyIdsVisible(true);
            const QVector<CadViewport::TopologyLabelItem> labels = topology.topologyLabelItems();
            require(!labels.isEmpty(), "etichette topologiche visibili");
            CadViewport::TopologyLabelItem edgeLabel, faceLabel;
            bool haveEdge = false, haveFace = false;
            for (const CadViewport::TopologyLabelItem &item : labels) {
                require(pointDistance(item.anchor, item.box.center()) > 8.0,
                        "spazio per la freccia dell'etichetta topologica");
                if (item.edge && !haveEdge) edgeLabel = item, haveEdge = true;
                if (!item.edge && !haveFace) faceLabel = item, haveFace = true;
            }
            require(haveEdge && haveFace, "etichette di bordi e facce");
            const auto click = [&](const CadViewport::TopologyLabelItem &item) {
                const QPointF p = item.box.center();
                QMouseEvent press(QEvent::MouseButtonPress, p, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                topology.mousePressEvent(&press);
            };
            click(edgeLabel);
            require(topology.topologyHighlightBody_ == edgeLabel.body
                        && topology.topologyHighlightEdge_ == edgeLabel.subshape
                        && topology.topologyHighlightFace_ < 0,
                    "clic sull'ID illumina il bordo");
            click(edgeLabel);
            require(topology.topologyHighlightBody_ < 0, "secondo clic spegne il bordo");
            click(faceLabel);
            require(topology.topologyHighlightBody_ == faceLabel.body
                        && topology.topologyHighlightFace_ == faceLabel.subshape
                        && topology.topologyHighlightEdge_ < 0,
                    "clic sull'ID della faccia illumina il contorno");
            QImage topologyLabels(topology.size(), QImage::Format_ARGB32_Premultiplied);
            topologyLabels.fill(QColor(35, 45, 58));
            QPainter topologyPainter(&topologyLabels);
            topology.drawTopologyLabels(topologyPainter);
            topologyPainter.end();
            require(topologyLabels.save(QStringLiteral("/tmp/forgecad-topology-labels.png")),
                    "rendering delle frecce topologiche");
            topology.setTopologyIdsVisible(false);
            require(topology.topologyHighlightBody_ < 0, "nascondere gli ID spegne l'evidenziazione");
        }
        {
            // Estrusione simmetrica e nei due versi: un solo prisma dal piano
            // spostato (volumi e quote esatti); con la fine "fino a" il secondo
            // verso si unisce al primo.
            SketchObject square;
            square.plane = 0;
            const QPointF c[4] = {QPointF(5, 5), QPointF(6, 5), QPointF(6, 6), QPointF(5, 6)};
            for (int k = 0; k < 4; ++k) square.segments.append({c[k], c[(k + 1) % 4]});
            const QVector<SketchObject> sketches{square};
            const auto zRange = [](const Kernel::Body &body, double &lo, double &hi) {
                lo = 1e300, hi = -1e300;
                for (Kernel::VertexId v : body.vertices()) lo = std::min(lo, body.vertex(v).point[2]), hi = std::max(hi, body.vertex(v).point[2]);
            };
            const auto build = [&](ExtrusionObject e, const QVector<ExtrusionObject> &bodies, double volume, double lo, double hi, const char *what) {
                e.feature = BodyFeature::Extrusion;
                e.sketchIndex = 0;
                QString error;
                const ForgeBody body = forgeExtrusionFeature(e, int(bodies.size()), sketches, bodies, &error);
                require(body && error.isEmpty(), what);
                double zlo, zhi;
                zRange(*body, zlo, zhi);
                require(std::fabs(Kernel::massProperties(*body).volume - volume) < 1e-9 && std::fabs(zlo - lo) < 1e-12
                            && std::fabs(zhi - hi) < 1e-12 && body->shells().size() == 1, what);
            };
            ExtrusionObject e;
            e.distance = 2.0;
            e.extrudeSides = 1;
            build(e, {}, 2.0, -1.0, 1.0, "estrusione simmetrica");
            e.extrudeSides = 2;
            e.distance2 = 0.5;
            build(e, {}, 2.5, -0.5, 2.0, "estrusione nei due versi");
            e.distance = -2.0;
            build(e, {}, 2.5, -2.0, 0.5, "estrusione nei due versi, prima distanza negativa");
            ExtrusionObject block;
            block.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 1.0, 1.0, 3.0));
            block.solid = true;
            e.distance = 1.0;
            e.extent = 1;
            e.extentRef.kind = 3;
            e.extentRef.index = 0;
            e.extentRef.point = {1.0, 1.0, 3.0};
            build(e, {block}, 3.5, -0.5, 3.0, "fino a un vertice e secondo verso");
            e.extrudeSides = 1;
            QString error;
            e.feature = BodyFeature::Extrusion;
            require(!forgeExtrusionFeature(e, 1, sketches, {block}, &error) && !error.isEmpty(), "simmetrica solo con la distanza");
            // Formato 21: versi e seconda distanza nel documento.
            DocumentState state;
            state.sketches = sketches;
            ExtrusionObject saved;
            saved.name = QStringLiteral("Due versi");
            saved.feature = BodyFeature::Extrusion;
            saved.sketchIndex = 0;
            saved.extrudeSides = 2;
            saved.distance2 = 0.75;
            state.extrusions = {saved};
            QTemporaryDir sidesDir;
            const QString sidesPath = sidesDir.filePath(QStringLiteral("versi.prt"));
            require(saveDocumentFile(sidesPath, state, false).isEmpty(), "salvataggio dei versi");
            DocumentState sidesLoaded;
            require(loadDocumentFile(sidesPath, sidesLoaded).isEmpty() && sidesLoaded.extrusions.size() == 1
                        && sidesLoaded.extrusions.at(0).extrudeSides == 2 && sidesLoaded.extrusions.at(0).distance2 == 0.75,
                    "lettura dei versi dal documento");
        }
        {
            // La parte cliccata appartiene alla funzione che l'ha creata: la
            // faccia di sopra, accorciata dallo smusso, resta della base; la
            // faccia dello smusso e' dello smusso.
            const Kernel::Body block = Kernel::makeBox(Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 4.0, 3.0, 2.0);
            const Kernel::Body chamfered = Kernel::blendEdges(block, {Kernel::nearestEdge(block, Kernel::Vec3(4, 1.5, 2), 1e-6)}, 0.5, true);
            const std::vector<std::pair<int, ForgeBody>> chain{{0, std::make_shared<const Kernel::Body>(block)},
                                                               {1, std::make_shared<const Kernel::Body>(chamfered)}};
            const auto faceAt = [&](const Kernel::Vec3 &p) {
                for (Kernel::FaceId f : chamfered.faces())
                    if (Kernel::projectPoint(*chamfered.face(f).surface, p).distance < 1e-9
                        && Kernel::classifyPointOnFace(chamfered, f, p, 1e-9) == Kernel::PointLocation::Inside)
                        return f.index;
                return -1;
            };
            const Kernel::Vec3 top(1, 1, 2), bevel(3.75, 1.5, 1.75);
            require(faceAt(top) >= 0 && faceAt(bevel) >= 0, "facce del corpo smussato");
            require(forgeFaceOwner(chamfered, faceAt(top), top, chain) == 0, "faccia della base: funzione base");
            require(forgeFaceOwner(chamfered, faceAt(bevel), bevel, chain) == 1, "faccia dello smusso: lo smusso");
            // Evidenziazione della feature scelta nell'albero: solo la faccia dello smusso.
            BodyDisplay display;
            forgeTessellate(chamfered, 1, display);
            require(display.triangleFaces.size() * 3 == display.vertices.size(), "faccia di ogni triangolo della tassellazione");
            const QVector<int> bevelFaces = forgeFeatureFaces(chamfered, display.faceIds, display.faceLabelPoints, 1, chain);
            require(bevelFaces == QVector<int>{faceAt(bevel)}, "facce create dallo smusso");
            const QVector<int> baseFaces = forgeFeatureFaces(chamfered, display.faceIds, display.faceLabelPoints, 0, chain);
            require(baseFaces.size() == 6 && !baseFaces.contains(faceAt(bevel)), "facce della base dopo lo smusso");
        }
        {
            // Modifica di una funzione intermedia: la storia torna a quel punto.
            // Le funzioni successive spariscono, il corpo che solo loro
            // consumavano (lo strumento della differenza) torna visibile.
            CadViewport rollback;
            PrimitiveParameters base;
            base.size[0] = 4.0; base.size[1] = 3.0; base.size[2] = 2.0;
            PrimitiveParameters tool = base;
            tool.origin[0] = 3.0;
            require(rollback.createPrimitive(base, QStringLiteral("Base")).isEmpty()
                        && rollback.createPrimitive(tool, QStringLiteral("Utensile")).isEmpty()
                        && rollback.createBoolean(::BooleanOperation::Difference, 0, {1}, QStringLiteral("Differenza")).isEmpty(),
                    "storia per il ritorno indietro");
            rollback.requestPreview(rollback.extrusions_.at(0), 0);
            require(rollback.preview_.later == QSet<int>{2} && rollback.preview_.restored == QSet<int>{1},
                    "modifica della base: differenza nascosta, utensile di nuovo visibile");
            rollback.requestPreview(rollback.extrusions_.at(2), 2);
            require(rollback.preview_.later.isEmpty() && rollback.preview_.restored.isEmpty(), "modifica dell'ultima funzione");
            rollback.clearPreview();
            require(rollback.preview_.later.isEmpty(), "chiusura dell'anteprima");
        }
        // La fusione parametrica e' condivisa da estrusione e sweep: il probe
        // conserva il bersaglio realmente intersecato e produce un solo solido.
        ExtrusionObject mergeTarget;
        mergeTarget.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 3.0, 3.0));
        mergeTarget.solid = true;
        ExtrusionObject sweepMerge;
        sweepMerge.feature = BodyFeature::Sweep;
        sweepMerge.mergeOperation = 1;
        sweepMerge.mergeProbe = true;
        sweepMerge.mergeBodies = {0};
        const ForgeBody sweepTool = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(2, 1, 1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 2.0, 1.0, 1.0));
        QString sweepMergeError;
        const ForgeBody sweepUnited = forgeMergeFeatureResult(sweepMerge, sweepTool, 1, {mergeTarget}, &sweepMergeError);
        require(sweepUnited && sweepMergeError.isEmpty() && sweepMerge.mergeBodies == QVector<int>{0}
                    && sweepUnited->shells().size() == 1,
                "unione automatica del risultato sweep");
        require(CadViewport::hiddenOperands(sweepMerge).contains(0), "bersaglio della sweep unita nascosto");
        CadViewport storyboard;
        PrimitiveParameters box;
        box.size[0] = 4.0;
        box.size[1] = 3.0;
        box.size[2] = 2.0;
        require(storyboard.createPrimitive(box, QStringLiteral("Blocco")).isEmpty(), "radice della storyboard");
        require(storyboard.modelBodies().size() == 1 && storyboard.extrusions().at(0).featureId != 0
                    && storyboard.extrusions().at(0).modelBodyId == storyboard.modelBodies().at(0).id,
                "identita' persistenti di corpo e feature");
        require(storyboard.createScale(0, 1.1, 0, {}, QStringLiteral("Scala 1")).isEmpty()
                    && storyboard.createScale(1, 1.1, 0, {}, QStringLiteral("Scala 2")).isEmpty(),
                "catena di feature nello stesso corpo logico");
        require(storyboard.modelBodies().size() == 1 && !storyboard.extrusions().at(0).visible
                    && !storyboard.extrusions().at(1).visible && storyboard.extrusions().at(2).visible,
                "solo il tip della storyboard e' visibile");
        {
            ExtrusionObject failed = storyboard.extrusions_.at(2);
            failed.featureId = 0;
            failed.forgeBody.reset();
            failed.display = {};
            failed.error = QStringLiteral("errore di rigenerazione simulato");
            storyboard.extrusions_.append(failed);
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
            require(storyboard.extrusions().at(2).visible && !storyboard.extrusions().at(3).visible
                        && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(2).featureId,
                    "una feature fallita lascia visibile l'ultimo stadio valido");
            // Corretta (riattivata o con un raggio che riesce): torna il tip
            // visibile anche se il suo flag era spento mentre falliva.
            storyboard.extrusions_[3].error.clear();
            storyboard.extrusions_[3].forgeBody = storyboard.extrusions_.at(2).forgeBody;
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
            require(!storyboard.extrusions().at(2).visible && storyboard.extrusions().at(3).visible
                        && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(3).featureId,
                    "una feature fallita che torna valida diventa il tip visibile");
            storyboard.extrusions_.removeLast();
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
        }
        const quint64 lastFeature = storyboard.extrusions().at(2).featureId;
        require(storyboard.moveFeature(2, -1).isEmpty() && storyboard.extrusions().at(1).featureId == lastFeature,
                "riordino di due feature compatibili");
        require(storyboard.setFeatureSuppressed(2, true).isEmpty() && storyboard.extrusions().at(2).suppressed
                    && storyboard.extrusions().at(1).visible,
                "soppressione ripristina lo stadio precedente");
        require(storyboard.setFeatureSuppressed(2, false).isEmpty() && storyboard.extrusions().at(2).visible,
                "riattivazione della feature");
        storyboard.deleteFeature(2);
        require(storyboard.extrusions().size() == 2 && storyboard.extrusions().at(1).visible
                    && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(1).featureId,
                "eliminazione del tip ripristina la feature precedente");
        storyboard.undo();
        require(storyboard.extrusions().size() == 3 && storyboard.extrusions().at(2).visible,
                "undo dell'eliminazione nella storyboard");
        {
            // Riferimenti che sopravvivono al riordino. Una faccia si ritrova dalla
            // sua superficie anche quando altre feature ne cambiano i bordi; uno
            // spigolo dalla sua geometria anche quando il suo ID, in un altro stato
            // del corpo, e' un altro spigolo dello stesso tipo.
            CadViewport reorder;
            PrimitiveParameters block;
            block.size[0] = 10.0;
            block.size[1] = 8.0;
            block.size[2] = 6.0;
            require(reorder.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco per il riordino dei raccordi");
            const ForgeBody boxBody = reorder.extrusions().at(0).forgeBody;
            const Kernel::EdgeId vertical = Kernel::nearestEdge(*boxBody, Kernel::Vec3(10, 0, 3), 1e-6);
            const Kernel::EdgeId otherVertical = Kernel::nearestEdge(*boxBody, Kernel::Vec3(0, 0, 3), 1e-6);
            require(vertical.valid() && otherVertical.valid(), "spigoli verticali del blocco");
            EdgePoint misleading = edgeReference(*boxBody, vertical, Kernel::Vec3(10, 0, 3));
            misleading.subshape = otherVertical.index;  // stesso tipo e contesto, altro spigolo
            require(ForgeCad::resolveEdgeReference(*boxBody, misleading, 1e-3, ForgeCad::ReferenceState::Other) == vertical,
                    "dopo un riordino un ID che non passa per il punto non vale");
            require(ForgeCad::resolveEdgeReference(*boxBody, misleading, 1e-3, ForgeCad::ReferenceState::Same) == otherVertical,
                    "sulla stessa base l'ID vale piu' del punto");
            require(reorder.createBlend(0, {edgeReference(*boxBody, vertical, Kernel::Vec3(10, 0, 3))}, 0.5, false, QStringLiteral("R1")).isEmpty(),
                    "raccordo dello spigolo verticale");
            const ForgeBody filleted = reorder.extrusions().at(1).forgeBody;
            const auto planeAt = [](const Kernel::Body &body, int axis, double value) {
                for (Kernel::FaceId f : body.faces()) {
                    const auto *plane = dynamic_cast<const Kernel::Plane *>(body.face(f).surface.get());
                    if (plane && std::fabs(plane->frame().origin()[axis] - value) < 1e-9 && std::fabs(std::fabs(plane->frame().zDir()[axis]) - 1.0) < 1e-12)
                        return f;
                }
                return Kernel::FaceId();
            };
            const Kernel::FaceId top = planeAt(*filleted, 2, 6.0);
            require(top.valid() && ForgeCad::faceBoundaryEdges(*filleted, top).size() == 5, "faccia superiore con l'arco del raccordo");
            EdgePoint topFace = faceReference(*filleted, top, Kernel::Vec3(5, 4, 6));
            topFace.role = kEdgePointFaceBoundary;
            std::vector<Kernel::EdgeId> onBox;
            require(ForgeCad::resolveBlendEdges(*boxBody, {topFace}, 1e-3, onBox, ForgeCad::ReferenceState::Other) && onBox.size() == 4,
                    "la faccia si ritrova sul blocco con i suoi 4 bordi");
            // R2: i bordi della faccia x = 0 (non toccano R1), poi R2 prima di R1.
            const Kernel::FaceId left = planeAt(*filleted, 0, 0.0);
            EdgePoint leftFace = faceReference(*filleted, left, Kernel::Vec3(0, 4, 3));
            leftFace.role = kEdgePointFaceBoundary;
            require(left.valid() && reorder.createBlend(1, {leftFace}, 1.0, false, QStringLiteral("R2")).isEmpty(), "raccordo dei bordi della faccia x = 0");
            const double inOrder = Kernel::massProperties(*reorder.extrusions().at(2).forgeBody).volume;
            require(reorder.moveFeature(2, -1).isEmpty(), "R2 spostato prima di R1");
            const ExtrusionObject &first = reorder.extrusions().at(1), &second = reorder.extrusions().at(2);
            require(first.name == QStringLiteral("R2") && first.error.isEmpty() && first.forgeBody, "R2 sul blocco dopo il riordino");
            require(second.name == QStringLiteral("R1") && second.error.isEmpty() && second.forgeBody, "R1 dopo R2");
            const double removed = Kernel::massProperties(*first.forgeBody).volume - Kernel::massProperties(*second.forgeBody).volume;
            require(std::fabs(removed - 0.25 * (1.0 - M_PI / 4.0) * 6.0) < 1e-8, "R1 raccorda lo spigolo verticale giusto");
            require(std::fabs(Kernel::massProperties(*second.forgeBody).volume - inOrder) < 1e-9 * inOrder, "stesso risultato nei due ordini");
        }
        {
            // Storia unica: una feature si sposta anche tra feature di altri
            // corpi, mai prima della feature che crea il suo corpo.
            CadViewport history;
            require(history.createPrimitive(box, QStringLiteral("A")).isEmpty() && history.createPrimitive(box, QStringLiteral("B")).isEmpty()
                        && history.createScale(0, 1.1, 0, {}, QStringLiteral("Scala A")).isEmpty(),
                    "due corpi e una feature del primo");
            const quint64 scaleId = history.extrusions().at(2).featureId;
            require(history.moveFeature(2, -1).isEmpty() && history.extrusions().at(1).featureId == scaleId
                        && history.extrusions().at(1).firstBody == 0 && history.extrusions().at(1).visible,
                    "feature spostata prima della feature di un altro corpo");
            require(!history.moveFeature(1, -1).isEmpty() && history.extrusions().at(1).featureId == scaleId,
                    "una feature non va prima della feature che crea il suo corpo");
        }
        // Il proprietario viene ritrovato dall'ID della feature e la
        // sotto-entita' dal suo ID B-rep, anche se indice e punto-cache sono
        // volutamente fuorvianti.
        const int referencedFeature = 1;
        const ForgeBody referencedBody = storyboard.extrusions().at(referencedFeature).forgeBody;
        require(referencedBody && referencedBody->edges().size() > 1, "corpo per i riferimenti topologici");
        const Kernel::EdgeId persistentEdge = referencedBody->edges().front();
        const Kernel::Edge &edgeGeometry = referencedBody->edge(persistentEdge);
        const Kernel::Vec3 edgePoint = edgeGeometry.curve->point(0.5 * (edgeGeometry.range.lo + edgeGeometry.range.hi));
        EdgePoint persistentPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        const Kernel::Edge &otherGeometry = referencedBody->edge(referencedBody->edges().back());
        const Kernel::Vec3 wrongPoint = otherGeometry.curve->point(0.5 * (otherGeometry.range.lo + otherGeometry.range.hi));
        persistentPoint.x = wrongPoint.x(); persistentPoint.y = wrongPoint.y(); persistentPoint.z = wrongPoint.z();
        require(resolveEdgeReference(*referencedBody, persistentPoint, 1e-12) == persistentEdge,
                "ID topologico preferito al punto piu' vicino");
        EdgePoint renumberedPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        renumberedPoint.subshape = std::numeric_limits<int>::max();
        require(resolveEdgeReference(*referencedBody, renumberedPoint, 1e-12) == persistentEdge,
                "firma geometrico-topologica recupera uno spigolo rinumerato");
        const Kernel::FaceId persistentFace = referencedBody->faces().front();
        const EdgePoint facePoint = faceReference(*referencedBody, persistentFace, edgePoint);
        require(resolveFaceReference(*referencedBody, facePoint, 1e-12) == persistentFace,
                "riferimento persistente a una faccia");
        GeometryRef persistentRef;
        persistentRef.kind = 4;
        persistentRef.index = 0; // indice volutamente errato
        persistentRef.featureId = storyboard.extrusions().at(referencedFeature).featureId;
        persistentRef.point = persistentPoint;
        ResolvedRef resolvedPersistent;
        require(resolveGeometryRef(persistentRef, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr)
                    && resolvedPersistent.hasCurve,
                "proprietario del riferimento ritrovato dall'ID della feature");
        GeometryRef deletedOwner = persistentRef;
        deletedOwner.featureId = std::numeric_limits<quint64>::max();
        require(!resolveGeometryRef(deletedOwner, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr),
                "un proprietario eliminato non si lega per errore all'indice riutilizzato");
        GeometryRef legacyRef = persistentRef;
        legacyRef.point = {edgePoint.x(), edgePoint.y(), edgePoint.z()};
        storyboard.extrusions_[referencedFeature].extentRef = legacyRef;
        storyboard.extrusions_[referencedFeature].blendEdges = {persistentPoint};
        storyboard.setModelBodyMeshColor(0, QColor(184, 72, 116));
        QTemporaryDir storyboardDir;
        const QString storyboardPath = storyboardDir.filePath(QStringLiteral("storyboard.prt"));
        require(storyboardDir.isValid() && saveDocumentFile(storyboardPath, storyboard.currentDocument(), false).isEmpty(),
                "salvataggio della storyboard");
        DocumentState storyboardLoaded;
        require(loadDocumentFile(storyboardPath, storyboardLoaded).isEmpty() && storyboardLoaded.modelBodies.size() == 1
                    && storyboardLoaded.extrusions.at(1).featureId == lastFeature
                    && storyboardLoaded.modelBodies.first().meshColor == QColor(184, 72, 116),
                "persistenza e lettura della storyboard");
        int loadedVisible = 0;
        quint64 loadedVisibleId = 0;
        for (const ExtrusionObject &feature : storyboardLoaded.extrusions)
            if (feature.visible) ++loadedVisible, loadedVisibleId = feature.featureId;
        require(loadedVisible == 1 && loadedVisibleId == storyboardLoaded.modelBodies.first().tipFeatureId,
                "l'apertura senza cache conserva visibile il tip serializzato");
        require(storyboardLoaded.extrusions.at(referencedFeature).extentRef.featureId == persistentRef.featureId
                    && storyboardLoaded.extrusions.at(referencedFeature).extentRef.point.subshape == persistentEdge.index
                    && storyboardLoaded.extrusions.at(referencedFeature).blendEdges.first().context != -1,
                "migrazione e persistenza dei riferimenti topologici nel formato 20");
        CadViewport booleanStory;
        PrimitiveParameters firstBox, secondBox;
        firstBox.size[0] = firstBox.size[1] = firstBox.size[2] = 2.0;
        secondBox = firstBox;
        secondBox.origin[0] = 1.0;
        require(booleanStory.createPrimitive(firstBox, QStringLiteral("A")).isEmpty()
                    && booleanStory.createPrimitive(secondBox, QStringLiteral("B")).isEmpty()
                    && booleanStory.createBoolean(::BooleanOperation::Union, 0, {1}, QStringLiteral("Unione")).isEmpty(),
                "booleana nella storyboard");
        require(booleanStory.modelBodies().size() == 2 && booleanStory.extrusions().at(2).modelBodyId == booleanStory.extrusions().at(0).modelBodyId
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "la booleana appartiene al corpo A e consuma il corpo strumento");
        require(booleanStory.setFeatureSuppressed(2, true).isEmpty() && booleanStory.extrusions().at(0).visible
                    && booleanStory.extrusions().at(1).visible,
                "sopprimere la booleana ripristina entrambi gli operandi");
        require(booleanStory.setFeatureSuppressed(2, false).isEmpty() && !booleanStory.extrusions().at(0).visible
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "riattivare la booleana nasconde di nuovo gli operandi");
        CadViewport interleavedStory;
        require(interleavedStory.createPrimitive(firstBox, QStringLiteral("Corpo A")).isEmpty()
                    && interleavedStory.createPrimitive(secondBox, QStringLiteral("Corpo B")).isEmpty()
                    && interleavedStory.createScale(0, 1.1, 0, {}, QStringLiteral("A1")).isEmpty()
                    && interleavedStory.createScale(1, 1.1, 0, {}, QStringLiteral("B1")).isEmpty()
                    && interleavedStory.createScale(2, 1.1, 0, {}, QStringLiteral("A2")).isEmpty(),
                "storyboard con feature di corpi intercalati");
        const quint64 a2Id = interleavedStory.extrusions().at(4).featureId;
        const quint64 b1Id = interleavedStory.extrusions().at(3).featureId;
        require(interleavedStory.moveFeatureTo(4, 2).isEmpty()
                    && interleavedStory.extrusions().at(2).featureId == a2Id
                    && interleavedStory.extrusions().at(4).featureId == b1Id,
                "drag logico tra feature con altri corpi intercalati");
        ForgeCad::HistoryGraphDialog historyGraph;
        historyGraph.setDocument(interleavedStory.currentDocument());
        require(historyGraph.nodeCount() >= interleavedStory.extrusions().size()
                    && historyGraph.edgeCount() == 3 && historyGraph.invalidDependencyCount() == 0,
                "grafo diagnostico della storyboard");
        const double graphZoom = historyGraph.zoomFactor();
        historyGraph.zoomBy(1.25);
        require(historyGraph.zoomFactor() > graphZoom,
                "zoom interattivo del grafo diagnostico");
        historyGraph.zoomBy(0.001);
        require(historyGraph.zoomFactor() >= 0.719,
                "limite leggibile dello zoom del grafo diagnostico");
        historyGraph.show();
        QApplication::processEvents();
        QGraphicsView *graphView = historyGraph.findChild<QGraphicsView *>();
        QGraphicsItem *movableNode = nullptr;
        if (graphView)
            for (QGraphicsItem *item : graphView->scene()->items())
                if ((item->flags() & QGraphicsItem::ItemIsMovable) && item->data(0).toInt() == 1) {
                    movableNode = item;
                    break;
                }
        require(movableNode, "blocchi spostabili nel grafo diagnostico");
        const QPointF movedPosition = movableNode->pos() + QPointF(57.0, 31.0);
        movableNode->setPos(movedPosition);
        QApplication::processEvents();
        require(movableNode->pos() == movedPosition,
                "spostamento di un blocco con aggiornamento dei collegamenti");
        historyGraph.setDocument(interleavedStory.currentDocument());
        QApplication::processEvents();
        bool preservedPosition = false;
        for (QGraphicsItem *item : graphView->scene()->items())
            preservedPosition = preservedPosition
                || ((item->flags() & QGraphicsItem::ItemIsMovable) && item->pos() == movedPosition);
        require(preservedPosition, "posizione manuale conservata dopo la ricostruzione del grafo");
        DocumentState invalidGraph = interleavedStory.currentDocument();
        invalidGraph.extrusions[4].firstBody = 99;
        historyGraph.setDocument(invalidGraph);
        require(historyGraph.invalidDependencyCount() == 1,
                "dipendenza mancante evidenziata dal grafo diagnostico");
        const auto renderedAlpha = [](QWidget &widget) {
            widget.resize(500, 300);
            QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            widget.render(&painter);
            return image.pixelColor(widget.rect().center()).alpha();
        };
        FunctionDialogPanel translucentPanel;
        translucentPanel.setPanelOpacity(25);
        translucentPanel.setBackdropBlur(18);
        const int lowOpacity = renderedAlpha(translucentPanel);
        translucentPanel.setPanelOpacity(100);
        require(lowOpacity > 0 && renderedAlpha(translucentPanel) > lowOpacity,
                "opacita' regolabile dei pannelli funzione");
        require(translucentPanel.backdropBlur() == 18, "sfocatura della scena regolabile sui pannelli funzione");
        translucentPanel.setPanelOpacity(70);
        translucentPanel.show();
        QApplication::processEvents();
        translucentPanel.hide();
        FunctionDialogPanel scrollablePanel;
        QFormLayout *scrollableForm = scrollablePanel.createScrollableForm();
        scrollableForm->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Extrusion, &scrollablePanel));
        QLabel *longPanelNote = wrappedNote(QStringLiteral(
            "Questo messaggio volutamente lungo verifica che le spiegazioni delle funzioni vadano a capo, "
            "ricevano tutta l'altezza necessaria e non finiscano dietro ai pulsanti o alle righe successive. "
            "Il controllo deve restare leggibile anche quando il pannello viene ridotto alla sua dimensione minima."),
            &scrollablePanel);
        QLabel *rowAfterLongNote = new QLabel(QStringLiteral("Riga successiva"), &scrollablePanel);
        scrollableForm->addRow(longPanelNote);
        scrollableForm->addRow(rowAfterLongNote);
        auto *panelScroll = scrollablePanel.findChild<QScrollArea *>(QStringLiteral("functionDialogScroll"));
        require(panelScroll,
                "contenuto scorrevole dei pannelli funzione");
        scrollablePanel.setFixedSize(400, 280);
        scrollablePanel.show();
        QApplication::processEvents();
        require(longPanelNote->sizePolicy().hasHeightForWidth()
                    && longPanelNote->height() >= 3 * longPanelNote->fontMetrics().height()
                    && longPanelNote->geometry().bottom() < rowAfterLongNote->geometry().top(),
                "note lunghe a capo senza sovrapporre le righe successive");
        require(panelScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
                    && panelScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
                "pannelli funzione senza tagli orizzontali e con scorrimento verticale");
        scrollablePanel.hide();
        FunctionDialogPanel embeddedPanel(&v);
        embeddedPanel.setWindowTitle(QStringLiteral("Pannello test"));
        require(embeddedPanel.isEmbedded() && !embeddedPanel.isWindow() && embeddedPanel.parentWidget() == &v,
                "pannello funzione sovrapposto nelle coordinate del viewport");
        v.setGlassPanel(123, QRect(10, 20, 300, 220), 16, 14, true);
        require(v.glassPanels_.contains(123) && v.glassPanels_.value(123).blur == 16
                    && v.glassPanels_.value(123).radius == 14,
                "registrazione del pannello per la composizione GPU");
        v.removeGlassPanel(123);
        require(!v.glassPanels_.contains(123), "rimozione del pannello dalla composizione GPU");
        embeddedPanel.resize(420, 300);
        embeddedPanel.placeAtLeft();
        const QPoint initialPanelPosition = embeddedPanel.pos();
        const QPoint dragStart = embeddedPanel.mapToGlobal(QPoint(20, 20));
        embeddedPanel.beginEmbeddedMove(dragStart);
        embeddedPanel.updateEmbeddedMove(dragStart + QPoint(40, 30));
        embeddedPanel.endEmbeddedMove();
        require(embeddedPanel.pos() == initialPanelPosition + QPoint(40, 30),
                "trascinamento del vetro aggiorna il ritaglio della scena");
        embeddedPanel.setCornerRadius(18);
        require(embeddedPanel.cornerRadius() == 18 && !embeddedPanel.mask().contains(QPoint(0, 0))
                    && embeddedPanel.mask().contains(embeddedPanel.rect().center()),
                "angoli arrotondati configurabili dei pannelli funzione");
        auto *panelGrip = embeddedPanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip"));
        require(panelGrip && !embeddedPanel.isSizeGripEnabled(),
                "grip interno senza ridimensionamento nativo della finestra niri");
        const QSize viewportBeforeGrip = v.size();
        const QSize panelBeforeGrip = embeddedPanel.size();
        const QPointF gripLocal(10, 10);
        const QPointF gripGlobal(panelGrip->mapToGlobal(gripLocal.toPoint()));
        QMouseEvent gripPress(QEvent::MouseButtonPress, gripLocal, gripGlobal,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripMove(QEvent::MouseMove, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripRelease(QEvent::MouseButtonRelease, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panelGrip, &gripPress);
        QApplication::sendEvent(panelGrip, &gripMove);
        QApplication::sendEvent(panelGrip, &gripRelease);
        require(embeddedPanel.size() == panelBeforeGrip + QSize(30, 20) && v.size() == viewportBeforeGrip,
                "il grip ridimensiona soltanto il pannello funzione");
        require(QSettings().value(QStringLiteral("view/functionPanelSizes/Pannello_test")).toSize() == embeddedPanel.size(),
                "ridimensionamento manuale del pannello memorizzato");
        const QPalette panelPalette = embeddedPanel.palette();
        require(std::abs(panelPalette.color(QPalette::WindowText).lightness() - embeddedPanel.panelColor().lightness()) > 100,
                "contrasto del testo sul vetro");
        QComboBox panelCombo(&embeddedPanel);
        panelCombo.addItems({QStringLiteral("Prima voce"), QStringLiteral("Seconda voce")});
        panelCombo.ensurePolished();
        panelCombo.view()->ensurePolished();
        require(std::abs(panelCombo.palette().color(QPalette::Text).lightness()
                         - embeddedPanel.panelColor().lighter(112).lightness()) > 100
                    && std::abs(panelCombo.view()->palette().color(QPalette::Text).lightness()
                                - embeddedPanel.panelColor().darker(125).lightness()) > 100,
                "testo leggibile nel campo e nella tendina dei pannelli funzione");
        const QColor selectionText = panelPalette.color(QPalette::HighlightedText);
        const QColor selectionBackground = panelPalette.color(QPalette::Highlight);
        require(selectionBackground.red() > 230 && selectionBackground.green() > 120
                    && selectionText.lightness() < 80,
                "testo leggibile nella selezione dei menu a discesa");
        require(!ForgeCad::commandIcon(QStringLiteral("panelOpacity")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelBlur")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelCorners")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("edit")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("visibility")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("rename")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("meshColor")).isNull(),
                "icone delle impostazioni dei pannelli");
        FloatingPanel constraintStylePanel(&v, QStringLiteral("Vincoli"),
                                           QStringLiteral("test/constraintPanelStyle"), QSize(380, 360));
        require(constraintStylePanel.isEmbedded()
                    && constraintStylePanel.objectName() == QStringLiteral("functionDialogPanel")
                    && constraintStylePanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip")),
                "pannello Vincoli con lo stesso vetro e grip dei pannelli funzione");
        LoftSelectionDiagram loftDiagram;
        require(renderedAlpha(loftDiagram) == 255, "grafica Loft sempre opaca");
        FeatureOperationDiagram extrusionDiagram(FeatureOperationDiagram::Extrusion);
        FeatureOperationDiagram revolutionDiagram(FeatureOperationDiagram::Revolution);
        require(renderedAlpha(extrusionDiagram) == 255 && renderedAlpha(revolutionDiagram) == 255,
                "grafiche Estrusione e Rivoluzione sempre opache");
        v.resize(900, 650);
        v.setViewNormal(0);
        v.zoom_ = 20;
        const double ratio = v.axisDisplayLength() / v.zoom_;
        v.zoom_ *= 3;
        require(std::fabs(v.axisDisplayLength() / v.zoom_ - ratio) < 1e-12, "dimensione assi durante zoom");
        v.zoom_ = 20;
        v.yaw_ = 0.0f;
        v.pitch_ = 0.0f;
        v.orbitView(QPoint(360, 360));
        require(std::fabs(v.yaw_) == 180.0f && std::fabs(v.pitch_) == 180.0f,
                "orbita orizzontale e verticale oltre i vecchi limiti");
        v.orbitView(QPoint(360, 360));
        require(std::fabs(v.yaw_) < 1e-6f && std::fabs(v.pitch_) < 1e-6f,
                "giro completo di 360 gradi sui due assi della vista");
        v.setViewNormal(0);
        v.selectPlane(0);
        const auto dragPlane = [&](bool cancel) {
            const auto corners = v.selection_.kind == SceneObjectKind::Plane ? v.planeCorners(0)
                : v.datumCorners(v.extrusions_.at(v.selection_.index).datumFrame, v.extrusions_.at(v.selection_.index).datum.size);
            const QPoint corner = v.projectWorldPoint(corners.at(2)).toPoint();
            require(v.beginPlaneResize(corner), "maniglia del piano");
            const QPoint end = corner + (corner - v.projectWorldPoint((corners.at(0) + corners.at(2)) * 0.5f).toPoint()) / 2;
            QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            v.mouseMoveEvent(&move);
            v.finishPlaneResize(cancel);
        };
        const auto initial = v.referencePlaneHalf(0);
        dragPlane(true);
        require(v.referencePlaneHalf(0) == initial, "Esc ripristina la dimensione automatica");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "Esc mantiene uguali i tre piani standard");
        dragPlane(false);
        require(v.referencePlaneHalf(0) > initial * 1.4, "ridimensionamento piano standard");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "il ridimensionamento manuale aggiorna tutti i piani standard");
        ExtrusionObject datum;
        datum.name = QStringLiteral("Piano test");
        datum.feature = BodyFeature::DatumPlane;
        GeometryRef ref;
        ref.kind = 1; ref.index = 0;
        datum.datum.refs = {ref}; datum.datum.distance = 2; datum.datum.size = 3;
        require(v.createBody(datum).isEmpty(), "creazione datum");
        const int datumIndex = int(v.extrusions_.size()) - 1;
        v.selectObject(SceneObjectKind::Extrusion, datumIndex);
        dragPlane(false);
        const double resized = v.extrusions_.at(datumIndex).datum.size;
        require(resized > 4.2, "ridimensionamento datum");
        v.undo();
        require(v.extrusions_.at(datumIndex).datum.size == 3, "Undo dimensione datum");
        v.redo();
        require(v.extrusions_.at(datumIndex).datum.size == resized, "Redo dimensione datum");

        // Una curva 3D autonoma (elica/spirale) offre sia la curva sia i suoi
        // estremi come riferimenti grafici del piano normale.
        CadViewport curveReferences;
        curveReferences.resize(800, 600);
        ExtrusionObject pathBody;
        pathBody.name = QStringLiteral("Curva percorso");
        pathBody.feature = BodyFeature::Helix;
        const auto pathLine = std::make_shared<Kernel::Line<3>>(Kernel::Vec3(2, 3, 1), normalized(Kernel::Vec3(1, 0.2, 0.4)));
        pathBody.curve = std::make_shared<Kernel::TrimmedCurve<3>>(pathLine, 0.0, 5.0);
        curveDisplay(*pathBody.curve, 1, pathBody.display);
        curveReferences.extrusions_.append(pathBody);
        curveReferences.refPickOwner_ = -1;
        GeometryRef pickedCurve, pickedEnd;
        const Kernel::Vec3 middle3 = pathBody.curve->point(2.5), end3 = pathBody.curve->point(5.0);
        curveReferences.refPickRoles_ = DatumRoleCurve;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(middle3.x(), middle3.y(), middle3.z())).toPoint(), pickedCurve)
                    && pickedCurve.kind == 9,
                "curva 3D selezionabile come riferimento del piano normale");
        curveReferences.refPickRoles_ = DatumRolePoint;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(end3.x(), end3.y(), end3.z())).toPoint(), pickedEnd)
                    && pickedEnd.kind == 10 && pickedEnd.element.point == 1,
                "estremo finale della curva 3D selezionabile graficamente");
        ResolvedRef resolvedEnd;
        require(resolveGeometryRef(pickedEnd, 1, {}, curveReferences.extrusions_, resolvedEnd, nullptr)
                    && resolvedEnd.hasPoint && distance(resolvedEnd.point, end3) < 1e-12,
                "estremo della curva risolto sul dominio esatto");
        DatumParameters normalAtEnd;
        normalAtEnd.mode = 2;
        normalAtEnd.refs = {pickedCurve, pickedEnd};
        SketchFrame normalFrame;
        QString normalError;
        require(computeDatum(normalAtEnd, 1, {}, curveReferences.extrusions_, normalFrame, &normalError),
                "piano normale costruito sull'estremo scelto della curva 3D");
        require(distance(Kernel::Vec3(normalFrame.origin[0], normalFrame.origin[1], normalFrame.origin[2]), end3) < 1e-9,
                "origine del piano normale coincidente con l'estremo della curva");

        // La conversione di un'elica crea un riferimento con molti poli. Il
        // vincolo Fix deve essere analizzato direttamente, senza una matrice
        // densa quadrata che rendeva l'operazione apparentemente infinita.
        Kernel::HelixSpec helixSpec;
        helixSpec.frame = Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0));
        helixSpec.radius = 5.0;
        helixSpec.pitch = 2.0;
        helixSpec.turns = 3.0;
        const auto helixCurve = std::make_shared<Kernel::HelixCurve>(helixSpec);
        SketchObject projectedHelix;
        projectedHelix.plane = 0;
        require(appendProjectedCurve(projectedHelix, helixCurve, helixCurve->domain(), true, true).isEmpty(),
                "conversione rapida di una elica nello schizzo");
        require(projectedHelix.curves.size() == 1 && projectedHelix.curves.first().tool == DrawingTool::Converted,
                "elica convertita come B-spline di riferimento");
        const SketchAnalysis projectedAnalysis = analyzeSketch(projectedHelix);
        require(projectedAnalysis.fullyDefined() && projectedAnalysis.curveDefined.value(0),
                "riferimento convertito fissato senza analisi cubica dei poli");

        Kernel::HelixSpec longHelixSpec = helixSpec;
        longHelixSpec.turns = 1000.0;
        const Kernel::HelixCurve longHelix(longHelixSpec);
        BodyDisplay boundedDisplay;
        curveDisplay(longHelix, 2, boundedDisplay);
        require(boundedDisplay.edges.size() == 1 && boundedDisplay.edges.first().size() == 32769,
                "campionamento grafico delle eliche lunghe limitato");

        const int sketch = v.createSketch(0, QStringLiteral("Test riferimenti"));
        require(sketch >= 0, "creazione schizzo");
        v.selectSketch(sketch);
        v.selectSketchReference(2, 0);
        require(v.sketches_.at(sketch).segments.size() == 1, "riferimento asse X");
        require(v.constraintSelection().size() == 1, "asse scelto per i vincoli");
        v.selectPlane(1);
        require(v.sketches_.at(sketch).segments.size() == 1, "riuso di una retta coincidente");
        v.selectSketchReference(2, 1);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimento asse Y");
        require(analyzeSketch(v.sketches_.at(sketch)).fullyDefined(), "riferimenti fissi senza liberta' residue");
        v.selectSketchReference(2, 2);
        v.selectPlane(0);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimenti degeneri non modificano lo schizzo");
        v.undo();
        require(v.sketches_.at(sketch).segments.size() == 1, "Undo riferimento");
        v.redo();
        require(v.sketches_.at(sketch).segments.size() == 2, "Redo riferimento");

        SketchObject work = v.sketches_.at(sketch);
        const int line = int(work.segments.size());
        work.segments.append({QPointF(1, 2), QPointF(4, 3)});
        work.constraints.append(-1); work.segmentLengths.append(0); work.segmentAngles.append(-1);
        const auto fixed = work.segments.at(0);
        work.geometricConstraints.append(makeConstraint(work, ConstraintType::Parallel, {{0, line, -1}, {0, 0, -1}}));
        require(solveSketch(work).ok, "vincolo parallelo all'asse");
        require(work.segments.at(0) == fixed, "il riferimento resta fisso");
        require(std::fabs(work.segments.at(line).second.y() - work.segments.at(line).first.y()) < 1e-8, "segmento parallelo al riferimento");
        {
            // Quota d'angolo: il settore del puntatore decide tra angolo interno e supplementare.
            SketchObject angle;
            angle.segments = {{QPointF(0, 0), QPointF(2, 0)}, {QPointF(0, 0), QPointF(1, std::sqrt(3.0))}};
            angle.constraints = {-1, -1}; angle.segmentLengths = {0, 0}; angle.segmentAngles = {-1, -1};
            SketchConstraint dimension = makeConstraint(angle, ConstraintType::Angle, {{0, 0, -1}, {0, 1, -1}});
            require(std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "quota d'angolo iniziale");
            require(chooseAngleSector(angle, dimension, QPointF(1.0, 0.3)) && std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "settore interno");
            require(chooseAngleSector(angle, dimension, QPointF(-1.0, 0.5)) && std::fabs(std::fabs(dimension.value) - 120.0) < 1e-9, "settore supplementare");
            require(chooseAngleSector(angle, dimension, QPointF(-1.0, -0.3)) && std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "settore opposto al vertice");
            require(chooseAngleSector(angle, dimension, QPointF(0.5, -1.0)) && std::fabs(std::fabs(dimension.value) - 120.0) < 1e-9, "settore supplementare sotto");
            const auto before = angle.segments;
            angle.geometricConstraints.append(dimension);
            require(solveSketch(angle).ok, "quota nel settore supplementare");
            for (int k = 0; k < 2; ++k)
                require(pointDistance(angle.segments.at(k).first, before.at(k).first) < 1e-9 && pointDistance(angle.segments.at(k).second, before.at(k).second) < 1e-9,
                        "il settore non cambia la geometria");
            angle.geometricConstraints.last().value = dimension.value < 0 ? -90.0 : 90.0;
            angle.geometricConstraints.append(makeConstraint(angle, ConstraintType::Fix, {{0, 0, -1}}));
            require(solveSketch(angle).ok, "quota supplementare a 90 gradi");
            const QPointF d = angle.segments.at(0).second - angle.segments.at(0).first, e = angle.segments.at(1).second - angle.segments.at(1).first;
            require(std::fabs(d.x() * e.x() + d.y() * e.y()) < 1e-8, "rette perpendicolari");
        }
        {
            // Misura: distanze minime esatte tra punti, spigoli e facce, angoli, interassi.
            using namespace ForgeCad::Kernel;
            using K = MeasureEntity::Kind;
            const auto box = std::make_shared<const Body>(makeBox(Frame3(), 10.0, 20.0, 30.0));
            const auto holeA = std::make_shared<const Body>(makeCylinder(Frame3(Vec3(50, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5.0, 10.0));
            const auto holeB = std::make_shared<const Body>(makeCylinder(Frame3(Vec3(80, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3.0, 10.0));
            const auto ball = std::make_shared<const Body>(makeSphere(Frame3(Vec3(0, 0, 60), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4.0));
            const auto faceWith = [](const ForgeBody &body, const std::function<bool(const Face &, const Vec3 &)> &test) {
                for (FaceId f : body->faces()) {
                    const Face &face = body->face(f);
                    Vec3 n(0, 0, 0);
                    if (face.surface->type() == SurfaceType::Plane) {
                        const Vec3 z = static_cast<const Plane &>(*face.surface).frame().zDir();
                        n = face.sense ? z : -z;
                    }
                    if (test(face, n)) {
                        MeasureEntity e;
                        e.kind = K::Face;
                        e.body = body;
                        e.face = f;
                        return e;
                    }
                }
                throw std::runtime_error("faccia di prova non trovata");
            };
            const auto planar = [&](const ForgeBody &body, const Vec3 &normal) {
                return faceWith(body, [normal](const Face &, const Vec3 &n) { return dot(n, normal) > 1 - 1e-12; });
            };
            const auto ofType = [&](const ForgeBody &body, SurfaceType type) {
                return faceWith(body, [type](const Face &face, const Vec3 &) { return face.surface->type() == type; });
            };
            const auto edgeThrough = [&](const Vec3 &a, const Vec3 &b) {
                for (EdgeId e : box->edges()) {
                    const Edge &edge = box->edge(e);
                    const Vec3 p = edge.curve->point(edge.range.lo), q = edge.curve->point(edge.range.hi);
                    if ((distance(p, a) < 1e-9 && distance(q, b) < 1e-9) || (distance(p, b) < 1e-9 && distance(q, a) < 1e-9)) {
                        MeasureEntity entity;
                        entity.kind = K::Curve;
                        entity.segments.push_back({edge.curve, edge.range});
                        return entity;
                    }
                }
                throw std::runtime_error("spigolo di prova non trovato");
            };
            const auto pointAt = [](const Vec3 &p) {
                MeasureEntity e;
                e.kind = K::Point;
                e.point = p;
                return e;
            };
            const auto distanceOf = [](const MeasureEntity &a, const MeasureEntity &b) {
                const MeasureReport r = measureEntities({a, b});
                if (!r.ok || !r.hasDistance) throw std::runtime_error(r.error.toStdString());
                return r.distance;
            };
            const MeasureEntity left = planar(box, Vec3(-1, 0, 0)), right = planar(box, Vec3(1, 0, 0)), top = planar(box, Vec3(0, 0, 1));
            require(std::fabs(distanceOf(left, right) - 10.0) < 1e-9, "misura faccia-faccia parallele");
            const MeasureReport facing = measureEntities({left, right});
            require(facing.lines.join(QLatin1Char('\n')).contains(QStringLiteral("Angolo tra le normali: 180°")), "angolo tra le normali opposte");
            const MeasureEntity vertical = edgeThrough(Vec3(0, 0, 0), Vec3(0, 0, 30)), across = edgeThrough(Vec3(0, 20, 30), Vec3(10, 20, 30));
            require(std::fabs(distanceOf(vertical, across) - 20.0) < 1e-9, "misura spigolo-spigolo");
            require(std::fabs(distanceOf(right, vertical) - 10.0) < 1e-9, "misura faccia-spigolo");
            require(std::fabs(distanceOf(pointAt(Vec3(5, 5, 50)), top) - 20.0) < 1e-9, "misura punto-faccia interno");
            require(std::fabs(distanceOf(pointAt(Vec3(20, 30, 40)), top) - std::sqrt(300.0)) < 1e-9, "misura punto-faccia sul bordo");
            require(std::fabs(distanceOf(ofType(ball, SurfaceType::Sphere), top) - 26.0) < 1e-7, "misura sfera-faccia");
            const MeasureEntity cylinderA = ofType(holeA, SurfaceType::Cylinder), cylinderB = ofType(holeB, SurfaceType::Cylinder);
            require(std::fabs(distanceOf(cylinderA, cylinderB) - 22.0) < 1e-7, "misura cilindro-cilindro");
            require(measureEntities({cylinderA, cylinderB}).lines.join(QLatin1Char('\n')).contains(QStringLiteral("interasse: 30")), "interasse dei cilindri");
            const MeasureReport single = measureEntities({top});
            require(single.ok && single.lines.first().contains(QStringLiteral("200")), "area della faccia");
            require(measureEntities({vertical, across}).lines.join(QLatin1Char('\n')).contains(QStringLiteral("Angolo tra le rette: 90°")), "angolo tra spigoli");
        }
        {
            // Sposta / copia entita' dello schizzo: i vincoli verso le entita' ferme spariscono, quelli interni restano.
            SketchObject moving;
            moving.segments = {{QPointF(0, 0), QPointF(4, 0)}, {QPointF(4, 0), QPointF(4, 3)}, {QPointF(10, 0), QPointF(12, 0)}};
            moving.constraints = {-1, -1, -1}; moving.segmentLengths = {0, 0, 0}; moving.segmentAngles = {-1, -1, -1};
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Coincident, {{0, 0, 1}, {0, 1, 0}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Horizontal, {{0, 0, -1}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Parallel, {{0, 0, -1}, {0, 2, -1}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Distance, {{0, 0, -1}}));
            SketchObject moved = moving;
            SketchMove shift;
            shift.translation = QPointF(1, 2);
            QVector<SketchEntity> created;
            require(moveSketchEntities(moved, {{0, 0}}, shift, &created).error.isEmpty() && created.size() == 1, "spostamento di un segmento");
            require(moved.segments.at(0).first == QPointF(1, 2) && moved.segments.at(1) == moving.segments.at(1), "solo il segmento scelto si sposta");
            bool coincident = false, horizontal = false, parallel = false, length = false;
            for (const SketchConstraint &c : moved.geometricConstraints) {
                coincident = coincident || (c.type == ConstraintType::Coincident && (c.first.element == 1 || c.second.element == 1));
                horizontal = horizontal || c.type == ConstraintType::Horizontal;
                parallel = parallel || c.type == ConstraintType::Parallel;
                length = length || c.type == ConstraintType::Distance;
            }
            require(!coincident && horizontal && parallel && length, "vincoli dopo lo spostamento");
            require(solveSketch(moved).ok, "schizzo spostato coerente");
            SketchObject copied = moving;
            SketchMove turn;
            turn.angle = 90.0;
            turn.copy = true;
            require(moveSketchEntities(copied, {{0, 0}, {0, 1}}, turn, &created).error.isEmpty() && created.size() == 2, "copia ruotata");
            require(copied.segments.size() == 5 && pointDistance(copied.segments.at(3).second, QPointF(0, 4)) < 1e-12, "copia ruotata di 90 gradi");
            bool copiedCoincident = false, copiedHorizontal = false;
            for (const SketchConstraint &c : copied.geometricConstraints) {
                copiedCoincident = copiedCoincident || (c.type == ConstraintType::Coincident && c.first.element >= 3 && c.second.element >= 3);
                copiedHorizontal = copiedHorizontal || (c.type == ConstraintType::Horizontal && c.first.element >= 3);
            }
            require(copiedCoincident && !copiedHorizontal, "vincoli copiati con la rotazione");
            require(solveSketch(copied).ok, "schizzo con la copia coerente");
        }
        {
            // Aggancio alla geometria e alla griglia indipendenti.
            const QVector<SketchSegment> segments = {{QPointF(0.1, 0.1), QPointF(1.1, 0.1)}};
            const QPointF raw(0.12, 0.13);
            require(snapSegments(raw, segments, {}, true, true, 0.25, 0.05).point == QPointF(0.1, 0.1), "aggancio alla geometria");
            require(snapSegments(raw, segments, {}, false, true, 0.25, 0.05).point == QPointF(0.0, 0.25), "solo griglia");
            require(snapSegments(raw, segments, {}, false, false, 0.25, 0.05).point == raw, "nessun aggancio");
            require(snapSegments(QPointF(0.6, 0.5), segments, {}, true, false, 0.25, 0.05).point == QPointF(0.6, 0.5), "geometria senza griglia");
        }
        SketchFrame tilted;
        tilted.origin[0] = 2; tilted.normal[0] = std::sqrt(0.5); tilted.normal[2] = std::sqrt(0.5);
        QPointF point, direction;
        require(sketchPlaneReference(work, tilted, point, direction).isEmpty(), "traccia di un piano obliquo");
        const auto world = sketchToWorld(point, work);
        require(std::fabs((world.x() - 2) + world.z()) < 1e-10, "traccia sul piano esatto");
        require(sketchPlaneReference(work, SketchFrame(), point, direction).size() > 0, "piano coincidente rifiutato");

        SketchObject contactTarget;
        contactTarget.name = QStringLiteral("Sezione loft");
        contactTarget.plane = 0;
        SketchObject crossing;
        crossing.name = QStringLiteral("Guida loft");
        crossing.plane = kFacePlane;
        crossing.customFrame = true;
        crossing.frame.normal[0] = 0.0; crossing.frame.normal[1] = 1.0; crossing.frame.normal[2] = 0.0;
        crossing.frame.xAxis[0] = 1.0; crossing.frame.xAxis[1] = 0.0; crossing.frame.xAxis[2] = 0.0;
        crossing.segments.append({QPointF(0, -1), QPointF(0, 1)});
        crossing.constraints.append(-1); crossing.segmentLengths.append(0); crossing.segmentAngles.append(-1);
        require(appendSketchContactReferences(contactTarget, crossing).isEmpty(), "contatto tra schizzi");
        require(contactTarget.segments.size() == 1 && contactTarget.segments.at(0).first == contactTarget.segments.at(0).second,
                "punto di attraversamento come riferimento");
        SketchObject contactLineTarget;
        contactLineTarget.plane = 0;
        SketchObject touching = crossing;
        touching.segments[0] = {QPointF(-1, 0), QPointF(1, 0)};
        require(appendSketchContactReferences(contactLineTarget, touching).isEmpty(), "segmento comune tra piani di schizzo");
        require(contactLineTarget.segments.size() == 1
                && pointDistance(contactLineTarget.segments.at(0).first, contactLineTarget.segments.at(0).second) > 1.0,
                "segmento di contatto come costruzione");
        SketchObject oneContact;
        oneContact.plane = 0;
        touching.segments.append({QPointF(-2, 0), QPointF(2, 0)});
        touching.constraints.append(-1); touching.segmentLengths.append(0); touching.segmentAngles.append(-1);
        require(appendSketchContactReference(oneContact, touching, {0, 1}).isEmpty(), "singolo bordo di schizzo come riferimento");
        require(oneContact.segments.size() == 1, "la scelta grafica non importa tutto lo schizzo");
        contactTarget.segments.append({QPointF(0.4, 0.3), QPointF(1, 0)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::Coincident, {{0, 1, 0}, {0, 0, 0}}));
        require(solveSketch(contactTarget).ok, "coincidenza con contatto esterno");
        require(pointDistance(contactTarget.segments.at(1).first, QPointF()) < 1e-8, "nodo sul contatto esterno");
        contactTarget.segments.append({QPointF(-1, 0.5), QPointF(1, 0.5)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {0, 2, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su segmento");
        require(std::fabs(contactTarget.segments.at(2).first.y()) < 1e-8 && std::fabs(contactTarget.segments.at(2).second.y()) < 1e-8,
                "segmento passa per il contatto esterno");
        CurveObject contactCircle;
        contactCircle.tool = DrawingTool::Circle;
        contactCircle.controlPoints = {QPointF(2, 0), QPointF(3, 0)};
        recalculateCurve(contactCircle);
        contactTarget.curves.append(contactCircle);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {1, 0, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su curva");
        require(std::fabs(pointDistance(contactTarget.curves.at(0).controlPoints.at(0), QPointF())
                          - pointDistance(contactTarget.curves.at(0).controlPoints.at(0), contactTarget.curves.at(0).controlPoints.at(1))) < 1e-8,
                "curva passa per il contatto esterno");

        CurveObject linkedSpline;
        linkedSpline.tool = DrawingTool::Spline;
        linkedSpline.controlPoints = {QPointF(0, 0), QPointF(2, 1), QPointF(4, 0)};
        initializeTangentHandles(linkedSpline);
        linkedSpline.tangentLinked[1] = true;
        work.curves.append(linkedSpline);
        SketchObject handleSketch;
        handleSketch.curves.append(linkedSpline);
        SketchConstraint handleLength = makeConstraint(handleSketch, ConstraintType::Distance,
            {{1, 0, 1}, {1, 0, handlePoint(1, 1)}});
        handleLength.value *= 1.4;
        handleSketch.geometricConstraints.append(handleLength);
        require(solveSketch(handleSketch).ok, "quota della maniglia collegata");
        const CurveObject &solvedSpline = handleSketch.curves.first();
        const QPointF hin = solvedSpline.tangentHandles.at(1).first - solvedSpline.controlPoints.at(1);
        const QPointF hout = solvedSpline.tangentHandles.at(1).second - solvedSpline.controlPoints.at(1);
        require(std::fabs(hin.x() * hout.y() - hin.y() * hout.x()) < 1e-8, "tangenza delle maniglie collegate");

        SketchObject multiplePaths;
        multiplePaths.segments = {{QPointF(0, 0), QPointF(0, 3)}, {QPointF(8, 0), QPointF(8, 3)}};
        multiplePaths.constraints = {-1, -1}; multiplePaths.segmentLengths = {0, 0}; multiplePaths.segmentAngles = {-1, -1};
        QVector<SketchPathRef> components = sketchPathComponents(multiplePaths);
        require(components.size() == 2, "percorsi disconnessi nello stesso schizzo");
        std::vector<Kernel::PathSegment> partialPath;
        require(sketchPath(sketchPathSubset(multiplePaths, components.at(1)), partialPath, nullptr) && partialPath.size() == 1,
                "selezione parziale usabile come percorso");

        QVector<SketchObject> loftSections;
        for (int i = 0; i < 3; ++i) {
            SketchObject section;
            section.name = QStringLiteral("Loft UV %1").arg(i + 1);
            section.plane = kFacePlane;
            section.customFrame = true;
            section.frame.origin[2] = 2.0 * i;
            CurveObject circle;
            circle.tool = DrawingTool::Circle;
            circle.controlPoints = {QPointF(), QPointF(i == 1 ? 1.0 : 2.0, 0.0)};
            recalculateCurve(circle);
            section.curves.append(circle);
            loftSections.append(section);
        }
        QString loftError;
        const ForgeBody loftBody = forgeLoft(loftSections, false, &loftError);
        require(loftBody && loftError.isEmpty(), "loft per griglia UV");
        SketchObject mirroredGuides;
        mirroredGuides.name = QStringLiteral("Guide specchiate");
        mirroredGuides.plane = kFacePlane; mirroredGuides.customFrame = true;
        mirroredGuides.frame.normal[0] = 0; mirroredGuides.frame.normal[1] = 1; mirroredGuides.frame.normal[2] = 0;
        mirroredGuides.frame.xAxis[0] = 1; mirroredGuides.frame.xAxis[1] = 0; mirroredGuides.frame.xAxis[2] = 0;
        for (double sign : {-1.0, 1.0}) {
            CurveObject guide; guide.tool = DrawingTool::Spline;
            guide.controlPoints = {QPointF(2 * sign, 0), QPointF(sign, -2), QPointF(2 * sign, -4)};
            initializeTangentHandles(guide); recalculateCurve(guide); mirroredGuides.curves.append(guide);
        }
        QVector<SketchPathRef> guideComponents = sketchPathComponents(mirroredGuides);
        require(guideComponents.size() == 2, "guide specchiate selezionabili separatamente");
        const ForgeBody partialGuidedLoft = forgeLoft(loftSections,
            {sketchPathSubset(mirroredGuides, guideComponents.first())}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError);
        require(partialGuidedLoft && loftError.isEmpty(), "loft con una sola guida scelta dallo schizzo specchiato");
        SketchObject missedGuide = sketchPathSubset(mirroredGuides, guideComponents.first());
        missedGuide.name = QStringLiteral("Guida lontana");
        for (CurveObject &curve : missedGuide.curves) {
            for (QPointF &point : curve.controlPoints) point.rx() += 20.0;
            initializeTangentHandles(curve); recalculateCurve(curve);
        }
        require(!forgeLoft(loftSections, {missedGuide}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError),
                "guida che non incontra la sezione rifiutata");
        require(loftError.contains(loftSections.first().name) && loftError.contains(QStringLiteral("[[loft-section=0]]")),
                "errore loft con nome e indice della sezione mancata");
        v.preview_.definition.loftSketches = {sketch};
        const QString visibleLoftError = v.preparePreviewError(loftError);
        require(v.previewErrorSketch_ == sketch && !visibleLoftError.contains(QStringLiteral("[[loft-section=")),
                "sezione dell'errore evidenziabile nella vista");
        BodyDisplay loftDisplay;
        forgeTessellate(*loftBody, 0, loftDisplay);
        forgeSurfaceConstructionCurves(*loftBody, loftDisplay, 4);
        require(!loftDisplay.constructionCurves.isEmpty(), "curve UV dell'anteprima loft");
        const Kernel::Body previewBox = Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 4.0, 3.0, 2.0);
        BodyDisplay extrusionDisplay;
        forgeTessellate(previewBox, 0, extrusionDisplay);
        forgeSurfaceConstructionCurves(previewBox, extrusionDisplay, 4, true);
        require(!extrusionDisplay.constructionCurves.isEmpty(), "curve UV anche sulle facce piane dell'anteprima estrusione");
        {
            const Kernel::Body tool = Kernel::makeBox(
                Kernel::Frame3(Kernel::Vec3(3.0, 0.75, 0.5), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 2.0, 1.5, 1.0);
            const Kernel::Body merged = Kernel::booleanOperation(previewBox, tool, Kernel::BooleanOperation::Unite);
            BodyDisplay fullMerged, localExtrusion;
            forgeTessellate(merged, 0, fullMerged);
            forgeExtrusionPreviewDisplay({std::make_shared<const Kernel::Body>(previewBox)}, merged, 0, localExtrusion, 4);
            require(!localExtrusion.vertices.isEmpty() && !localExtrusion.constructionCurves.isEmpty()
                        && localExtrusion.vertices.size() < fullMerged.vertices.size(),
                    "l'anteprima dell'estrusione fusa contiene solo le facce che modificano la base");

            const Kernel::Body cylinder = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 4.0);
            const Kernel::Body bore = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(0, 0, -1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 1.0, 6.0);
            const Kernel::Body cut = Kernel::booleanOperation(cylinder, bore, Kernel::BooleanOperation::Subtract);
            BodyDisplay cuttingFaces, retainedFaces;
            forgeExtrusionPreviewDisplay({std::make_shared<const Kernel::Body>(cylinder)}, cut, 0, cuttingFaces, 4, &retainedFaces);
            bool annularCap = false;
            for (Kernel::FaceId face : cut.faces())
                if (cut.face(face).surface->type() == Kernel::SurfaceType::Plane && cut.face(face).loops.size() == 2) annularCap = true;
            require(annularCap && !cuttingFaces.vertices.isEmpty() && !cuttingFaces.constructionCurves.isEmpty()
                        && !retainedFaces.vertices.isEmpty(),
                    "la sottrazione mostra il corpo opaco gia' forato e la sola superficie di taglio trasparente");
        }
        const QVector3D rayOrigin(2.0f, 1.5f, 10.0f), rayDirection(0.0f, 0.0f, -1.0f);
        require(!CadViewport::edgeOccludedByMesh(extrusionDisplay, rayOrigin, rayDirection, QVector3D(0, 0, 2), 1e-5)
                    && CadViewport::edgeOccludedByMesh(extrusionDisplay, rayOrigin, rayDirection, QVector3D(0, 0, 0), 1e-5),
                "i bordi posteriori sono esclusi dalla selezione visibile");
        {
            const QVector<ExportBody> exportBox{{QStringLiteral("Blocco STL"),
                std::make_shared<const Kernel::Body>(previewBox), {}, QColor(80, 160, 220)}};
            StlExportOptions coarse;
            coarse.maxEdgeLength = 3.0; coarse.deflection = 1.0; coarse.angle = 90.0;
            const StlBuildResult coarseStl = buildBinaryStl(exportBox, coarse);
            StlExportOptions fine = coarse;
            fine.maxEdgeLength = 1.0;
            const StlBuildResult fineStl = buildBinaryStl(exportBox, fine);
            require(coarseStl.error.isEmpty() && fineStl.error.isEmpty()
                        && fineStl.triangleCount > coarseStl.triangleCount,
                    "STL: il lato massimo aumenta realmente il numero dei triangoli");
            require(fineStl.data.size() == 84 + 50 * qint64(fineStl.triangleCount),
                    "STL binario: 50 byte per triangolo");
            require(fineStl.preview.vertices.size() / 3 == qint64(fineStl.triangleCount)
                        && !fineStl.preview.edges.isEmpty() && !fineStl.previewLimited,
                    "STL: anteprima con facce e griglia triangolare completa");
            quint32 storedTriangles = 0;
            std::memcpy(&storedTriangles, fineStl.data.constData() + 80, sizeof(storedTriangles));
            require(qFromLittleEndian(storedTriangles) == fineStl.triangleCount,
                    "STL binario: conteggio triangoli nell'intestazione");
            QTemporaryDir stlTemporary;
            const QString stlPath = stlTemporary.filePath(QStringLiteral("mesh.stl"));
            require(stlTemporary.isValid() && saveBinaryStl(stlPath, fineStl.data).isEmpty()
                        && QFileInfo(stlPath).size() == fineStl.data.size(),
                    "scrittura atomica del file STL");
            const ObjBuildResult quadObj = buildQuadObj(exportBox, fine);
            require(quadObj.error.isEmpty() && quadObj.quadCount > 0
                        && quadObj.quadCount * 2 + quadObj.triangleCount <= fineStl.triangleCount,
                    "OBJ quadrangolare: triangoli adiacenti accoppiati");
            quint64 objFaces = 0, objQuads = 0;
            for (const QByteArray &line : quadObj.data.split('\n')) {
                if (!line.startsWith("f ")) continue;
                ++objFaces;
                if (line.count(' ') == 4) ++objQuads;
            }
            require(objFaces == quadObj.quadCount + quadObj.triangleCount && objQuads == quadObj.quadCount,
                    "OBJ quadrangolare: conteggi coerenti con le facce scritte");
            require(!quadObj.preview.vertices.isEmpty()
                        && quadObj.preview.edges.size() == qint64(quadObj.quadCount + quadObj.triangleCount)
                        && !quadObj.previewLimited,
                    "OBJ quadrangolare: anteprima con la stessa griglia quad/triangolo esportata");
            const QString objPath = stlTemporary.filePath(QStringLiteral("mesh.obj"));
            require(saveQuadObj(objPath, quadObj.data).isEmpty() && QFileInfo(objPath).size() == quadObj.data.size(),
                    "scrittura atomica del file OBJ quadrangolare");

            const Kernel::Body sphere = Kernel::makeSphere(Kernel::Frame3(), 5.0);
            const QVector<ExportBody> exportSphere{{QStringLiteral("Sfera STL"),
                std::make_shared<const Kernel::Body>(sphere), {}, QColor()}};
            StlExportOptions radialCoarse;
            radialCoarse.maxEdgeLength = 0.0; radialCoarse.deflection = 0.5; radialCoarse.angle = 90.0;
            StlExportOptions radialFine = radialCoarse;
            radialFine.deflection = 0.05;
            const StlBuildResult coarseSphere = buildBinaryStl(exportSphere, radialCoarse);
            const StlBuildResult fineSphere = buildBinaryStl(exportSphere, radialFine);
            require(coarseSphere.error.isEmpty() && fineSphere.error.isEmpty()
                        && fineSphere.triangleCount > coarseSphere.triangleCount,
                    "STL: lo scarto cordale aumenta l'approssimazione dei raggi");
            const ObjBuildResult sphereObj = buildQuadObj(exportSphere, radialFine);
            require(sphereObj.error.isEmpty() && sphereObj.quadCount > 0,
                    "OBJ quadrangolare anche sulle superfici curve");
        }

        DocumentState state = v.documentState();
        state.sketches[sketch] = work;
        state.extrusions[datumIndex].loftGuides = {sketch};
        state.extrusions[datumIndex].loftStartContinuity = 1;
        state.extrusions[datumIndex].loftEndContinuity = 2;
        state.extrusions[datumIndex].loftGuideInfluence = 0.65;
        state.extrusions[datumIndex].loftStartInfluence = 0.4;
        state.extrusions[datumIndex].loftEndInfluence = 0.8;
        state.extrusions[datumIndex].loftGuideContinuity = 1;
        state.extrusions[datumIndex].pathSketch = sketch;
        state.extrusions[datumIndex].pathSegments = {1};
        SketchPathRef savedGuide{sketch, {1}, {}};
        state.extrusions[datumIndex].loftGuidePaths = {savedGuide};
        state.lengthUnit = LengthUnit::Foot;
        state.lengthUnitSet = true;
        QTemporaryDir tmp;
        require(tmp.isValid(), "directory temporanea");
        const QString path = tmp.filePath(QStringLiteral("refs.prt"));
        require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio riferimenti e datum");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty(), "lettura riferimenti e datum");
        require(loaded.lengthUnitSet && loaded.lengthUnit == LengthUnit::Foot,
                "unita' lineare del documento salvata nel formato 26");
        CadViewport filePreview;
        filePreview.loadPreviewDocument(loaded);
        require(!filePreview.sketches_.isEmpty() && !filePreview.sceneGeometryPoints().isEmpty(),
                "schizzi visibili nell'anteprima della finestra Apri");
        require(loaded.extrusions.at(datumIndex).datum.size == resized, "dimensione datum nel documento");
        require(loaded.sketches.at(sketch).curves.last().tangentLinked.value(1), "collegamento maniglie nel documento");
        require(loaded.extrusions.at(datumIndex).pathSegments == QVector<int>{1}
                && loaded.extrusions.at(datumIndex).loftGuidePaths == QVector<SketchPathRef>{savedGuide},
                "percorsi parziali nel documento");
        const ExtrusionObject &roundTrip = loaded.extrusions.at(datumIndex);
        require(roundTrip.loftGuides == QVector<int>{sketch} && roundTrip.loftStartContinuity == 1 && roundTrip.loftEndContinuity == 2,
                "parametri loft nel documento");
        require(roundTrip.loftGuideInfluence == 0.65 && roundTrip.loftStartInfluence == 0.4 && roundTrip.loftEndInfluence == 0.8,
                "influenze loft nel documento");
        require(roundTrip.loftGuideContinuity == 1, "tangenza delle guide nel documento");
        require(solveSketch(loaded.sketches[sketch]).ok, "vincoli dopo riapertura");

        v.endSketchMode();
        PrimitiveParameters primitive;
        require(v.createPrimitive(primitive, QStringLiteral("Box rendering")).isEmpty(), "primitiva per rendering");
        const int body = int(v.extrusions_.size()) - 1;
        const auto &display = v.extrusions_.at(body).display;
        require(display.rayIndex && !display.faceEdges.isEmpty(), "indici di selezione preparati");
        for (const auto &edges : display.faceEdges)
            for (int e : edges) require(e >= 0 && e < display.edges.size(), "corrispondenza faccia-spigoli");
        v.fitAll();
        const auto &projection = v.projectedBodyEdges(body);
        require(projection.lines.size() == display.edges.size(), "cache proiezione spigoli");
        const auto oldPoint = projection.lines.first().first();
        v.panX_ += 1;
        require(v.projectedBodyEdges(body).lines.first().first() != oldPoint, "invalidazione cache sul pan");

        // La dimensione maggiore del corpo determina un unico quadrato comune:
        // i tre piani devono restare uguali anche per un corpo molto sottile.
        CadViewport thin;
        for (double &scale : thin.referencePlaneScales_) scale = 1.0;
        PrimitiveParameters plate;
        plate.size[0] = 100.0;
        plate.size[1] = 5.0;
        plate.size[2] = 2.0;
        require(thin.createPrimitive(plate, QStringLiteral("Piastra sottile")).isEmpty(), "primitiva sottile");
        const QSizeF xy = thin.referencePlaneExtents(0);
        const QSizeF xz = thin.referencePlaneExtents(1);
        const QSizeF yz = thin.referencePlaneExtents(2);
        require(xy.width() == xy.height(), "il piano XY e' quadrato");
        require(xy == xz && xz == yz, "i tre piani hanno la stessa dimensione");
        require(xy.width() >= 60.0, "la misura comune segue la dimensione maggiore del corpo");

        // Una seconda lavorazione piu' grande, adiacente a una gia' presente,
        // viene rigenerata dalla base comune in ordine di misura. Copre tutte
        // le combinazioni raccordo/smusso sulla zona d'intersezione.
        for (bool firstChamfer : {false, true})
            for (bool secondChamfer : {false, true}) {
                CadViewport sequential;
                PrimitiveParameters block;
                block.size[0] = 4.0; block.size[1] = 3.0; block.size[2] = 2.0;
                require(sequential.createPrimitive(block, QStringLiteral("Base raccordi consecutivi")).isEmpty(),
                        "base dei raccordi consecutivi");
                const QVector<EdgePoint> vertical{{4.0, 0.0, 1.0}};
                require(sequential.createBlend(0, vertical, 0.3, firstChamfer, QStringLiteral("Prima lavorazione")).isEmpty(),
                        "prima lavorazione piccola");
                const QVector<EdgePoint> topFront{{2.0, 0.0, 2.0}};
                require(sequential.createBlend(1, topFront, 0.55, secondChamfer, QStringLiteral("Seconda lavorazione")).isEmpty(),
                        "seconda lavorazione grande sulla zona gia' raccordata");
                require(sequential.extrusions_.back().forgeBody != nullptr, "rigenerazione delle lavorazioni intersecanti");
            }

        if (render) {
            CadViewport threadView;
            threadView.resize(720, 720);
            PrimitiveParameters shaft;
            shaft.kind = PrimitiveKind::Cylinder;
            shaft.size[0] = 4.0;
            shaft.size[1] = 12.0;
            require(threadView.createPrimitive(shaft, QStringLiteral("Albero")).isEmpty(), "albero per rendering del filetto");
            const ForgeBody shaftBody = threadView.extrusions_.first().forgeBody;
            Kernel::FaceId shaftSide;
            for (Kernel::FaceId face : shaftBody->faces())
                if (shaftBody->face(face).surface->type() == Kernel::SurfaceType::Cylinder) { shaftSide = face; break; }
            require(shaftSide.valid(), "faccia dell'albero da renderizzare");
            ExtrusionObject threadFeature;
            threadFeature.name = QStringLiteral("Filettatura esterna");
            threadFeature.feature = BodyFeature::Thread;
            threadFeature.firstBody = 0;
            threadFeature.thread.standard = 0;
            threadFeature.thread.designation = QStringLiteral("M8 x 1,25");
            threadFeature.thread.pitch = 1.25;
            threadFeature.thread.length = 10.0;
            const Kernel::FinId shaftFin = shaftBody->loop(shaftBody->face(shaftSide).loops.front()).first;
            threadFeature.thread.face = faceReference(*shaftBody, shaftSide, shaftBody->finPoint(shaftFin, 0.5));
            require(threadView.createBody(threadFeature, {0}).isEmpty(), "feature filettatura esterna da renderizzare");
            threadView.setViewPreset(4);
            threadView.show();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            threadView.fitAll();
            threadView.update();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            require(threadView.grabFramebuffer().save(QStringLiteral("/tmp/forgecad-thread.png")), "immagine del filetto esterno");
            threadView.hide();
            v.show();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            require(v.isValid(), "contesto OpenGL valido");
            require(v.context() && v.context()->format().profile() == QSurfaceFormat::CoreProfile
                        && v.context()->format().majorVersion() >= 3,
                    "contesto OpenGL 3 Core effettivo");
            DocumentFilePreview openPreview;
            openPreview.resize(360, 420);
            openPreview.setPath(path);
            auto *fileProgress = openPreview.findChild<QProgressBar *>(QStringLiteral("documentPreviewProgress"));
            require(fileProgress && !fileProgress->isHidden(), "barra durante il caricamento dell'anteprima file");
            openPreview.show();
            QEventLoop previewWait;
            QTimer::singleShot(500, &previewWait, &QEventLoop::quit);
            previewWait.exec();
            auto *previewViewport = dynamic_cast<CadViewport *>(
                openPreview.findChild<QOpenGLWidget *>(QStringLiteral("documentPreviewViewport")));
            auto *previewDetails = openPreview.findChild<QLabel *>(QStringLiteral("documentPreviewDetails"));
            require(previewViewport && !previewViewport->sketches_.isEmpty()
                        && previewDetails && previewDetails->text().contains(QStringLiteral("schizzi")),
                    "caricamento asincrono del riquadro nella finestra Apri");
            require(fileProgress->isHidden(), "barra nascosta al termine dell'anteprima file");
            openPreview.grab().save(QStringLiteral("/tmp/forgecad-open-preview.png"));
            openPreview.hide();
            PdfWindow progressWindow;
            progressWindow.show();
            QApplication::processEvents();
            require(progressWindow.openDocumentPath(path), "apertura del documento con avanzamento determinato");
            auto *openProgress = progressWindow.findChild<QProgressDialog *>(QStringLiteral("foregroundProgressDialog"));
            require(openProgress && openProgress->maximum() == 100 && openProgress->value() == 100 && openProgress->isHidden(),
                    "finestra di apertura arrivata al 100% e chiusa al termine");
            progressWindow.close();
            FunctionDialogPanel focusPanel(&v);
            focusPanel.setWindowTitle(QStringLiteral("Test focus pannello"));
            auto *focusSpin = new QDoubleSpinBox(&focusPanel);
            focusPanel.createScrollableForm()->addRow(QStringLiteral("Valore:"), focusSpin);
            focusPanel.show();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            // Wayland puo' negare l'attivazione della finestra di test quando
            // l'utente sta lavorando in un'altra applicazione. In quel caso il
            // widget resta comunque il destinatario predisposto per il focus.
            require(focusSpin->hasFocus() || focusPanel.focusWidget() == focusSpin,
                    "focus iniziale sul primo dato del pannello");
            require(focusPanel.size().width() <= v.width() - 32 && focusPanel.size().height() <= v.height() - 32,
                    "dimensione automatica limitata al viewport");
            focusPanel.hide();
            require(v.gpuGlassAvailable(), "shader OpenGL per la sfocatura dei pannelli");
            v.setGlassPanel(456, QRect(30, 30, 260, 180), 14, 18, true);
            v.setExportMeshPreview(display, true, false);
            v.update();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            const QImage screenshot = v.grabFramebuffer();
            require(!screenshot.isNull(), "rendering framebuffer");
            v.clearExportMeshPreview();
            v.removeGlassPanel(456);
            screenshot.save(QStringLiteral("/tmp/forgecad-viewport-test.png"));
            v.makeCurrent();
            while (glGetError() != GL_NO_ERROR) {}
            v.displayCache_.faces(display);
            v.displayCache_.edges(display);
            require(glGetError() == GL_NO_ERROR, "rendering VBO senza errori GL");
            {
                QOpenGLFramebufferObject framebuffer(160, 160, QOpenGLFramebufferObject::CombinedDepthStencil);
                require(framebuffer.isValid(), "framebuffer confronto VBO");
                const auto draw = [&]() {
                    framebuffer.bind();
                    glViewport(0, 0, 160, 160);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
                    QMatrix4x4 projection;
                    projection.ortho(-6.0f, 6.0f, -6.0f, 6.0f, -100.0f, 100.0f);
                    QMatrix4x4 modelView;
                    modelView.rotate(25.0f, 1.0f, 0.0f, 0.0f);
                    modelView.rotate(30.0f, 0.0f, 1.0f, 0.0f);
                    v.displayCache_.setMatrices(projection, modelView);
                    v.displayCache_.setLightingEnabled(false);
                    v.displayCache_.setColor(QVector4D(0.8f, 0.5f, 0.2f, 1.0f));
                    v.displayCache_.faces(display);
                    v.displayCache_.setColor(QVector4D(0.2f, 0.8f, 0.9f, 1.0f));
                    v.displayCache_.edges(display);
                    QVector<QMatrix4x4> instances(2);
                    instances[0].translate(-2.0f, 0.0f, 0.0f);
                    instances[1].translate(2.0f, 0.0f, 0.0f);
                    v.displayCache_.pickingInstancedFaces(display, instances, QVector4D(0.1f, 0.2f, 0.3f, 1.0f));
                    v.overlayRenderer_.setMatrices(projection, modelView);
                    v.overlayRenderer_.drawInstanced(GL_LINES,
                        QVector<QVector3D>{QVector3D(0, 0, 0), QVector3D(1, 0, 0)},
                        QVector4D(1.0f, 1.0f, 0.0f, 1.0f), instances);
                    QMatrix4x4 overlayView;
                    v.overlayRenderer_.setMatrices(projection, overlayView);
                    v.overlayRenderer_.drawWideLineStrip(
                        QVector<QVector3D>{QVector3D(-4, 4, 0), QVector3D(4, 4, 0)},
                        QVector4D(1.0f, 0.0f, 0.0f, 1.0f), 12.0f);
                    glFinish();
                    return framebuffer.toImage();
                };
                const QImage cached = draw();
                require(glGetError() == GL_NO_ERROR, "renderer Core non produce errori GL");
                bool colored = false;
                for (int y = 0; y < cached.height(); ++y)
                    for (int x = 0; x < cached.width(); ++x) colored |= (cached.pixel(x, y) & 0xffffff) != 0;
                require(colored, "confronto rendering non vuoto");
                int redBand = 0;
                for (int y = 0; y < cached.height(); ++y) {
                    const QColor pixel = cached.pixelColor(cached.width() / 2, y);
                    if (pixel.red() > 220 && pixel.green() < 50 && pixel.blue() < 50) ++redBand;
                }
                require(redBand >= 10, "linea evidenziata espansa realmente oltre il limite di glLineWidth");
                framebuffer.release();
            }
            v.displayCache_.clear();
            v.doneCurrent();
            v.close();
        }
        // Offset nello schizzo: catene chiuse verso l'esterno (o l'interno),
        // angoli vivi prolungati o raccordati, archi concentrici, spline
        // approssimate; i vincoli aggiunti sono soddisfatti.
        {
            const auto rectangle = [](double w, double h) {
                SketchObject sketch;
                const QVector<QPointF> c{{0, 0}, {w, 0}, {w, h}, {0, h}};
                for (int k = 0; k < 4; ++k) {
                    sketch.segments.append({c[k], c[(k + 1) % 4]});
                    sketch.constraints.append(-1);
                    sketch.segmentLengths.append(0.0);
                    sketch.segmentAngles.append(-1.0);
                }
                return sketch;
            };
            const auto all = [](const SketchObject &sketch) {
                QVector<ForgeCad::SketchEntity> entities;
                for (int k = 0; k < sketch.segments.size(); ++k) entities.append({0, k});
                for (int k = 0; k < sketch.curves.size(); ++k) entities.append({1, k});
                return entities;
            };
            const auto satisfied = [](const SketchObject &sketch) {
                for (const SketchConstraint &c : sketch.geometricConstraints)
                    if (ForgeCad::constraintError(sketch, c) > 1e-9) return false;
                return true;
            };
            const auto bounds = [](const SketchObject &sketch, int from) {
                QRectF box;
                for (int k = from; k < sketch.segments.size(); ++k) {
                    const QRectF r = QRectF(sketch.segments.at(k).first, sketch.segments.at(k).second).normalized();
                    box = box.isNull() ? r : box.united(r);
                }
                return box;
            };
            SketchObject outward = rectangle(4.0, 3.0);
            ForgeCad::SketchOffset offset;
            offset.distance = 0.5;
            QVector<ForgeCad::SketchEntity> created;
            require(ForgeCad::offsetSketchEntities(outward, all(outward), offset, &created).error.isEmpty() && created.size() == 4,
                    "offset del rettangolo verso l'esterno");
            const QRectF grown = bounds(outward, 4);
            require(std::fabs(grown.left() + 0.5) < 1e-12 && std::fabs(grown.right() - 4.5) < 1e-12 && std::fabs(grown.top() + 0.5) < 1e-12
                        && std::fabs(grown.bottom() - 3.5) < 1e-12 && satisfied(outward),
                    "rettangolo a distanza con gli angoli vivi");
            SketchObject inward = rectangle(4.0, 3.0);
            offset.reverse = true;
            require(ForgeCad::offsetSketchEntities(inward, all(inward), offset).error.isEmpty(), "offset del rettangolo verso l'interno");
            const QRectF shrunk = bounds(inward, 4);
            require(std::fabs(shrunk.left() - 0.5) < 1e-12 && std::fabs(shrunk.right() - 3.5) < 1e-12 && satisfied(inward), "rettangolo ristretto");
            offset.reverse = false;
            offset.roundCorners = true;
            SketchObject rounded = rectangle(4.0, 3.0);
            require(ForgeCad::offsetSketchEntities(rounded, all(rounded), offset, &created).error.isEmpty() && created.size() == 8
                        && rounded.curves.size() == 4 && satisfied(rounded),
                    "offset con gli archi negli angoli");
            offset.roundCorners = false;
            offset.bothSides = true;
            SketchObject both = rectangle(4.0, 3.0);
            require(ForgeCad::offsetSketchEntities(both, all(both), offset, &created).error.isEmpty() && created.size() == 8, "offset dalle due parti");
            offset.bothSides = false;
            // Cerchio, arco aperto e spline.
            SketchObject curves;
            CurveObject circle;
            circle.tool = DrawingTool::Circle;
            circle.controlPoints = {QPointF(0, 0), QPointF(2, 0)};
            curves.curves.append(circle);
            require(ForgeCad::offsetSketchEntities(curves, all(curves), offset, &created).error.isEmpty()
                        && curves.curves.last().tool == DrawingTool::Circle
                        && std::fabs(QLineF(curves.curves.last().controlPoints.at(0), curves.curves.last().controlPoints.at(1)).length() - 2.5) < 1e-12
                        && satisfied(curves),
                    "cerchio a distanza concentrico");
            offset.reverse = true;
            offset.distance = 3.0;
            SketchObject tooFar;
            tooFar.curves.append(circle);
            require(!ForgeCad::offsetSketchEntities(tooFar, all(tooFar), offset).error.isEmpty() && tooFar.curves.size() == 1,
                    "raggio negativo: errore e schizzo invariato");
            offset.reverse = false;
            offset.distance = 0.3;
            SketchObject free;
            CurveObject spline;
            spline.tool = DrawingTool::Nurbs;
            spline.controlPoints = {QPointF(0, 0), QPointF(2, 3), QPointF(5, -1), QPointF(7, 2), QPointF(9, 0)};
            free.curves.append(spline);
            require(ForgeCad::offsetSketchEntities(free, all(free), offset, &created).error.isEmpty() && created.size() == 1
                        && free.curves.last().tool == DrawingTool::Converted,
                    "NURBS a distanza come curva convertita");
            const auto original = ForgeCad::curveGeometry(free.curves.first()).front();
            const auto copy = ForgeCad::curveGeometry(free.curves.last()).front();
            double worst = 0.0;
            for (int k = 0; k <= 50; ++k) {
                const ForgeCad::Kernel::Vec2 p = copy.curve->point(copy.range.lo + copy.range.length() * k / 50.0);
                worst = std::max(worst, std::fabs(ForgeCad::Kernel::projectPoint(*original.curve, p, original.range).distance - 0.3));
            }
            require(worst < 1e-6, "distanza della copia della NURBS");
            // Rettangolo arrotondato (segmenti e archi tangenti): archi concentrici di raggio R - d.
            {
                SketchObject slot;
                const double r = 1.0;
                const auto seg = [&](QPointF a, QPointF b) {
                    slot.segments.append({a, b});
                    slot.constraints.append(-1);
                    slot.segmentLengths.append(0.0);
                    slot.segmentAngles.append(-1.0);
                };
                const auto arc = [&](QPointF c, QPointF a, QPointF b) {
                    CurveObject curve;
                    curve.tool = DrawingTool::Arc;
                    curve.controlPoints = {c, a, b};
                    slot.curves.append(curve);
                };
                seg({r, 0}, {5 - r, 0});
                arc({5 - r, r}, {5 - r, 0}, {5, r});
                seg({5, r}, {5, 3 - r});
                arc({5 - r, 3 - r}, {5, 3 - r}, {5 - r, 3});
                seg({5 - r, 3}, {r, 3});
                arc({r, 3 - r}, {r, 3}, {0, 3 - r});
                seg({0, 3 - r}, {0, r});
                arc({r, r}, {0, r}, {r, 0});
                ForgeCad::SketchOffset in;
                in.distance = 0.4;
                in.reverse = true;
                require(ForgeCad::offsetSketchEntities(slot, all(slot), in, &created).error.isEmpty() && created.size() == 8 && satisfied(slot),
                        "rettangolo arrotondato verso l'interno");
                const CurveObject &inner = slot.curves.at(4);
                require(inner.tool == DrawingTool::Arc && std::fabs(QLineF(inner.controlPoints.at(0), inner.controlPoints.at(1)).length() - 0.6) < 1e-12,
                        "arco concentrico di raggio R - d");
            }
            // Angolo concavo di una catena aperta (L): le copie si accorciano fino al punto comune.
            SketchObject corner;
            for (const SketchSegment &segment : {SketchSegment(QPointF(0, 0), QPointF(4, 0)), SketchSegment(QPointF(4, 0), QPointF(4, 3))}) {
                corner.segments.append(segment);
                corner.constraints.append(-1);
                corner.segmentLengths.append(0.0);
                corner.segmentAngles.append(-1.0);
            }
            offset.distance = 0.5;
            require(ForgeCad::offsetSketchEntities(corner, all(corner), offset).error.isEmpty() && corner.segments.size() == 4
                        && QLineF(corner.segments.at(2).second, QPointF(3.5, 0.5)).length() < 1e-12
                        && QLineF(corner.segments.at(3).first, QPointF(3.5, 0.5)).length() < 1e-12 && satisfied(corner),
                    "angolo concavo: le copie si incontrano");
        }
        offsetFacePicking();
        // Offset di superficie e cucitura: feature parametriche, salvate nel formato 32.
        {
            CadViewport surfaces;
            PrimitiveParameters ball;
            ball.kind = PrimitiveKind::Sphere;
            ball.size[0] = 2.0;
            require(surfaces.createPrimitive(ball, QStringLiteral("Sfera")).isEmpty(), "sfera per l'offset");
            ExtrusionObject offset;
            offset.feature = BodyFeature::SurfaceOffset;
            offset.firstBody = 0;
            offset.distance = 0.5;
            offset.name = QStringLiteral("Offset");
            require(surfaces.createBody(offset).isEmpty(), "offset di tutte le facce");
            const ExtrusionObject &skin = surfaces.extrusions_.at(1);
            require(skin.forgeBody && skin.forgeBody->isSheet() && !skin.solid, "l'offset e' una superficie");
            require(std::fabs(ForgeCad::Kernel::faceArea(*skin.forgeBody, skin.forgeBody->faces().front()) - 4.0 * M_PI * 6.25) < 1e-8,
                    "area della sfera a distanza");
            require(surfaces.extrusions_.at(0).visible, "il corpo di partenza dell'offset resta visibile");
            ExtrusionObject sew;
            sew.feature = BodyFeature::Sew;
            sew.firstBody = 1;
            sew.sewSolid = true;
            sew.name = QStringLiteral("Cucitura");
            require(surfaces.createBody(sew).isEmpty(), "cucitura in un solido");
            const ExtrusionObject &closed = surfaces.extrusions_.at(2);
            require(closed.solid && std::fabs(ForgeCad::Kernel::massProperties(*closed.forgeBody).volume - 4.0 / 3.0 * M_PI * 15.625) < 1e-7,
                    "volume del solido cucito");
            require(!surfaces.extrusions_.at(1).visible, "le superfici cucite si nascondono");
            // Offset di una faccia sola di un cilindro (il fianco) con il riferimento persistente.
            PrimitiveParameters can;
            can.kind = PrimitiveKind::Cylinder;
            can.size[0] = 1.0;
            can.size[1] = 3.0;
            require(surfaces.createPrimitive(can, QStringLiteral("Cilindro")).isEmpty(), "cilindro per l'offset");
            const int cylinder = int(surfaces.extrusions_.size()) - 1;
            const ForgeCad::Kernel::Body &canBody = *surfaces.extrusions_.at(cylinder).forgeBody;
            ForgeCad::Kernel::FaceId side;
            for (ForgeCad::Kernel::FaceId f : canBody.faces())
                if (canBody.face(f).surface->type() == ForgeCad::Kernel::SurfaceType::Cylinder) side = f;
            ExtrusionObject sideOffset;
            sideOffset.feature = BodyFeature::SurfaceOffset;
            sideOffset.firstBody = cylinder;
            sideOffset.distance = -0.25;
            sideOffset.offsetSew = false;
            sideOffset.offsetFaces = {ForgeCad::faceReference(canBody, side, ForgeCad::Kernel::Vec3(1.0, 0.0, 1.5))};
            sideOffset.name = QStringLiteral("Offset fianco");
            require(surfaces.createBody(sideOffset).isEmpty(), "offset del fianco del cilindro");
            const ExtrusionObject &band = surfaces.extrusions_.back();
            require(std::fabs(ForgeCad::Kernel::faceArea(*band.forgeBody, band.forgeBody->faces().front()) - 2.0 * M_PI * 0.75 * 3.0) < 1e-9,
                    "area del fianco a distanza verso l'interno");
            QTemporaryDir directory;
            const QString path = directory.filePath(QStringLiteral("superfici.prt"));
            require(saveDocumentFile(path, surfaces.currentDocument(), false).isEmpty(), "salvataggio di offset e cucitura");
            DocumentState reloaded;
            require(loadDocumentFile(path, reloaded).isEmpty(), "lettura di offset e cucitura");
            require(reloaded.extrusions.at(2).feature == BodyFeature::Sew && reloaded.extrusions.at(2).sewSolid
                        && reloaded.extrusions.back().feature == BodyFeature::SurfaceOffset && reloaded.extrusions.back().offsetFaces.size() == 1
                        && reloaded.extrusions.back().distance == -0.25
                        && !reloaded.extrusions.back().offsetSew && reloaded.extrusions.at(1).offsetSew,
                    "parametri di offset e cucitura nel documento");
            CadViewport again;
            again.loadDocument(reloaded);
            require(again.extrusions_.at(2).solid && again.extrusions_.back().forgeBody, "offset e cucitura rigenerati");
        }
        // Taglio di superfici: il bersaglio puo' essere anche la pelle di un
        // solido; gli utensili possono essere corpi, piani e schizzi.
        {
            using namespace ForgeCad;
            using namespace ForgeCad::Kernel;
            QString error;
            const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10.0));
            const ForgeBody cylinder = std::make_shared<const Body>(makeCylinder(
                Frame3(Vec3(1, 2, -3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.5, 6.0));
            const QVector<SheetPiece> cachedPieces = forgeSheetPieces(plate, cylinder, 0, &error);
            require(cachedPieces.size() == 2 && error.isEmpty(),
                    "taglio di una superficie con un corpo");
            require(cachedPieces.at(0).geometry && cachedPieces.at(1).geometry
                        && !cachedPieces.at(0).display.vertices.isEmpty() && !cachedPieces.at(1).display.vertices.isEmpty()
                        && forgeClosestSheetPiece(cachedPieces, cachedPieces.at(1).point) == 1,
                    "parti del taglio riutilizzabili e selezionabili nella vista");
            CadViewport cachedViewport;
            ExtrusionObject cachedPlate, cachedCylinder;
            cachedPlate.name = QStringLiteral("Lastra cache"); cachedPlate.forgeBody = plate;
            cachedCylinder.name = QStringLiteral("Cilindro cache"); cachedCylinder.forgeBody = cylinder; cachedCylinder.solid = true;
            cachedViewport.extrusions_ = {cachedPlate, cachedCylinder};
            ExtrusionObject cachedDefinition;
            cachedDefinition.feature = BodyFeature::SheetTrim;
            cachedDefinition.firstBody = 0;
            cachedDefinition.secondBody = 1;
            cachedDefinition.trimKeep = cachedPieces.at(0).point;
            cachedViewport.resize(800, 600);
            cachedViewport.setViewNormal(0);
            cachedViewport.setTrimPartPickCallback(0, cachedPieces, -1, {}, [](int, int, EdgePoint) {});
            const EdgePoint hoverPoint = cachedPieces.at(1).point;
            cachedViewport.updateHover(cachedViewport.projectWorldPoint(
                QVector3D(float(hoverPoint.x), float(hoverPoint.y), float(hoverPoint.z))).toPoint());
            require(cachedViewport.trimPartHover_ == 1, "evidenziazione della parte sotto il mouse");
            cachedViewport.showTrimPreview(cachedDefinition, cachedPieces.at(0).geometry, cachedPieces.at(0).display);
            const ForgeBody cachedGeometry = cachedPieces.at(0).geometry;
            require(cachedViewport.createSheetTrim(0, 1, 0, cachedPieces.at(0).point, QStringLiteral("Taglio da cache")).isEmpty()
                        && cachedViewport.extrusions_.back().forgeBody == cachedGeometry,
                    "conferma del taglio senza ripetere l'intersezione");

            error.clear();
            const ForgeBody vertical = forgeTrimPlaneTool(
                plate, Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), &error);
            require(vertical && forgeSheetPieces(plate, vertical, 0, &error).size() == 2 && error.isEmpty(),
                    "taglio di una superficie con un piano di lavoro");
            error.clear();
            const ForgeBody mutualTool = std::make_shared<const Body>(
                makePlaneSheet(Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 10.0));
            error.clear();
            const QVector<SheetPiece> mutualFirstPieces = forgeSheetPieces(plate, mutualTool, 0, &error);
            const QVector<SheetPiece> mutualSecondPieces = forgeSheetPieces(mutualTool, plate, 0, &error);
            int pickedSide = -1, pickedPart = -1;
            cachedViewport.setTrimPartPickCallback(0, mutualFirstPieces, 1, mutualSecondPieces,
                [&](int side, int part, EdgePoint) { pickedSide = side; pickedPart = part; });
            require(cachedViewport.trimPartPickDisplays_.size() == mutualFirstPieces.size() + mutualSecondPieces.size()
                        && cachedViewport.trimPartPickSides_.value(mutualFirstPieces.size()) == 1,
                    "hover simultaneo delle parti di entrambi i corpi");
            cachedViewport.trimPartPickFinished_(1, 0, mutualSecondPieces.at(0).point);
            require(pickedSide == 1 && pickedPart == 0, "il clic conserva corpo e indice della parte illuminata");
            const ForgeBody mutual = forgeTrimBoth(plate, mutualTool, EdgePoint{-5, 0, 0}, EdgePoint{2, 0, 5}, &error);
            if (!mutual || !error.isEmpty()) std::cout << "Mutual trim: " << error.toStdString() << std::endl;
            require(mutual && mutual->isSheet() && Kernel::checkBody(*mutual).empty() && error.isEmpty(),
                    "taglio reciproco conserva una parte di entrambe le superfici");
            CadViewport mutualViewport;
            ExtrusionObject firstSheet, secondSheet;
            firstSheet.name = QStringLiteral("Prima superficie");
            firstSheet.forgeBody = plate;
            firstSheet.visible = true;
            secondSheet.name = QStringLiteral("Seconda superficie");
            secondSheet.forgeBody = mutualTool;
            secondSheet.visible = true;
            mutualViewport.extrusions_.append(firstSheet);
            mutualViewport.extrusions_.append(secondSheet);
            require(mutualViewport.createSheetTrim(0, 1, 0, EdgePoint{-5, 0, 0}, QStringLiteral("Taglio reciproco"),
                                                   -1, true, EdgePoint{2, 0, 5}).isEmpty(),
                    "feature di taglio reciproco");
            require(!mutualViewport.extrusions_.at(0).visible && !mutualViewport.extrusions_.at(1).visible
                        && mutualViewport.extrusions_.back().trimBoth && mutualViewport.extrusions_.back().forgeBody,
                    "il taglio reciproco nasconde entrambi gli originali");
            QTemporaryDir mutualDirectory;
            const QString mutualPath = mutualDirectory.filePath(QStringLiteral("taglio-reciproco.prt"));
            require(saveDocumentFile(mutualPath, mutualViewport.currentDocument(), false).isEmpty(),
                    "salvataggio del taglio reciproco");
            DocumentState mutualDocument;
            require(loadDocumentFile(mutualPath, mutualDocument).isEmpty() && mutualDocument.extrusions.back().trimBoth
                        && std::fabs(mutualDocument.extrusions.back().trimToolKeep.z - 5.0) < 1e-12,
                    "persistenza del taglio reciproco");

            SketchObject line;
            line.name = QStringLiteral("Linea di taglio");
            line.segments.append({QPointF(-20, 0), QPointF(20, 0)});
            line.constraints.append(-1);
            line.segmentLengths.append(0.0);
            line.segmentAngles.append(-1.0);
            error.clear();
            const ForgeBody sketchTool = forgeTrimSketchTool(plate, line, &error);
            require(sketchTool && forgeSheetPieces(plate, sketchTool, 0, &error).size() == 2 && error.isEmpty(),
                    "taglio di una superficie con uno schizzo aperto");

            const ForgeBody box = std::make_shared<const Body>(makeBox(Frame3(), 4.0, 3.0, 2.0));
            error.clear();
            const ForgeBody boxPlane = forgeTrimPlaneTool(
                box, Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), &error);
            const QVector<SheetPiece> boxPieces = forgeSheetPieces(box, boxPlane, 0, &error);
            require(boxPlane && boxPieces.size() == 2 && error.isEmpty(),
                    "taglio della pelle di un solido con un piano");
        }
        // Superfici rigata, planare, loft e sweep di superfici (formato 24).
        {
            using namespace ForgeCad::Kernel;
            CadViewport surfaces;
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto circleSketch = [](const QString &name, double z, double r) {
                SketchObject sketch;
                sketch.name = name;
                sketch.plane = kFacePlane;
                sketch.frame.origin[2] = z;
                CurveObject circle;
                circle.tool = DrawingTool::Circle;
                circle.controlPoints = {QPointF(0, 0), QPointF(r, 0)};
                ForgeCad::recalculateCurve(circle, 1);
                sketch.curves.append(circle);
                return sketch;
            };
            const auto area = [](const ExtrusionObject &body) {
                double total = 0.0;
                for (FaceId f : body.forgeBody->faces()) total += faceArea(*body.forgeBody, f);
                return total;
            };
            // 0: rettangolo 4 x 3 con un foro di raggio 0.5 (piano XY); 1, 2: cerchi a z = 0 e 2; 3: percorso lungo Z.
            SketchObject plate;
            plate.name = QStringLiteral("Lastra");
            addSegment(plate, {0, 0}, {4, 0});
            addSegment(plate, {4, 0}, {4, 3});
            addSegment(plate, {4, 3}, {0, 3});
            addSegment(plate, {0, 3}, {0, 0});
            CurveObject hole;
            hole.tool = DrawingTool::Circle;
            hole.controlPoints = {QPointF(2, 1.5), QPointF(2.5, 1.5)};
            ForgeCad::recalculateCurve(hole, 1);
            plate.curves.append(hole);
            surfaces.sketches_.append(plate);
            surfaces.sketches_.append(circleSketch(QStringLiteral("Basso"), 0.0, 1.0));
            surfaces.sketches_.append(circleSketch(QStringLiteral("Alto"), 2.0, 2.0));
            SketchObject path;
            path.name = QStringLiteral("Percorso");
            path.plane = 1;  // XZ: la seconda coordinata e' Z
            addSegment(path, {0, 0}, {0, 3});
            surfaces.sketches_.append(path);

            ExtrusionObject planar;
            planar.feature = BodyFeature::PlanarSurface;
            planar.sketchIndex = 0;
            planar.name = QStringLiteral("Planare");
            require(surfaces.createBody(planar).isEmpty(), "superficie planare da uno schizzo");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - (12.0 - M_PI * 0.25)) < 1e-9,
                    "area della lastra con il foro");

            const double slant = std::sqrt(4.0 + 1.0), frustum = M_PI * 3.0 * slant;
            ExtrusionObject ruled;
            ruled.feature = BodyFeature::Ruled;
            GeometryRef low, high;
            low.kind = high.kind = 7;
            low.index = 1;
            high.index = 2;
            low.element = high.element = ConstraintRef{1, 0, -1};
            ruled.ruledFirst = {low};
            ruled.ruledSecond = {high};
            ruled.name = QStringLiteral("Rigata");
            require(surfaces.createBody(ruled).isEmpty(), "superficie rigata tra due cerchi di schizzi");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - frustum) < 1e-8,
                    "area del tronco di cono rigato");

            ExtrusionObject loft;
            loft.feature = BodyFeature::Loft;
            loft.loftSketches = {1, 2};
            loft.loftRuled = true;
            loft.loftSurface = true;
            loft.name = QStringLiteral("Tubo");
            require(surfaces.createBody(loft).isEmpty(), "loft di superfici");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - frustum) < 1e-8,
                    "loft di superfici: tubo senza coperchi");

            ExtrusionObject sweep;
            sweep.feature = BodyFeature::Sweep;
            sweep.sketchIndex = 1;
            sweep.pathSketch = 3;
            sweep.sweepSurface = true;
            sweep.name = QStringLiteral("Canna");
            require(surfaces.createBody(sweep).isEmpty(), "sweep di superfici");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - 2.0 * M_PI * 3.0) < 1e-8,
                    "sweep di superfici: cilindro senza coperchi");

            // Planare dai bordi superiori di un parallelepipedo (riferimenti agli spigoli).
            PrimitiveParameters block;
            block.size[0] = 4.0;
            block.size[1] = 3.0;
            block.size[2] = 2.0;
            require(surfaces.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco per la planare");
            const int blockIndex = int(surfaces.extrusions_.size()) - 1;
            const Body &blockBody = *surfaces.extrusions_.at(blockIndex).forgeBody;
            ExtrusionObject lid;
            lid.feature = BodyFeature::PlanarSurface;
            lid.sketchIndex = -1;
            for (EdgeId e : blockBody.edges()) {
                const Edge &edge = blockBody.edge(e);
                const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
                if (std::fabs(middle.z() - 2.0) > 1e-9) continue;
                GeometryRef ref;
                ref.kind = 4;
                ref.index = blockIndex;
                ref.featureId = surfaces.extrusions_.at(blockIndex).featureId;
                ref.point = ForgeCad::edgeReference(blockBody, e, middle);
                lid.planarRefs.append(ref);
            }
            require(lid.planarRefs.size() == 4, "quattro bordi superiori");
            lid.name = QStringLiteral("Coperchio");
            require(surfaces.createBody(lid).isEmpty(), "superficie planare dai bordi");
            require(std::fabs(area(surfaces.extrusions_.back()) - 12.0) < 1e-9, "area del coperchio");

            // Undo/Redo della creazione e salvataggio/lettura nel formato 24.
            const int count = int(surfaces.extrusions_.size());
            surfaces.undo();
            require(surfaces.extrusions_.size() == count - 1, "annulla la superficie planare");
            surfaces.redo();
            require(surfaces.extrusions_.size() == count && surfaces.extrusions_.back().forgeBody, "ripete la superficie planare");
            QTemporaryDir directory;
            const QString file = directory.filePath(QStringLiteral("superfici24.prt"));
            require(saveDocumentFile(file, surfaces.currentDocument(), false).isEmpty(), "salvataggio formato 24");
            DocumentState reloaded;
            require(loadDocumentFile(file, reloaded).isEmpty(), "lettura formato 24");
            require(reloaded.extrusions.at(1).ruledFirst.size() == 1 && reloaded.extrusions.at(1).ruledSecond.size() == 1
                        && reloaded.extrusions.at(2).loftSurface && reloaded.extrusions.at(3).sweepSurface
                        && reloaded.extrusions.back().planarRefs.size() == 4,
                    "campi delle superfici nel documento");
            CadViewport again;
            again.loadDocument(reloaded);
            for (int k = 0; k < again.extrusions_.size(); ++k)
                require(again.extrusions_.at(k).forgeBody != nullptr, "superfici rigenerate dopo la lettura");
            require(std::fabs(area(again.extrusions_.at(1)) - frustum) < 1e-8 && std::fabs(area(again.extrusions_.back()) - 12.0) < 1e-9,
                    "aree dopo la lettura");
        }
        // Rivoluzione di un profilo aperto (superficie) e bordo libero intero
        // di una superficie come loop per la planare.
        {
            using namespace ForgeCad::Kernel;
            CadViewport v;
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto area = [](const ExtrusionObject &body) {
                double total = 0.0;
                for (FaceId f : body.forgeBody->faces()) total += faceArea(*body.forgeBody, f);
                return total;
            };
            SketchObject profile;
            profile.name = QStringLiteral("Profilo");
            profile.plane = 1;
            addSegment(profile, {0, 0}, {0, 4});  // asse
            profile.constructionSegments.append(0);
            addSegment(profile, {1, 0}, {2, 0});  // corona piana
            addSegment(profile, {2, 0}, {2, 3});  // cilindro
            v.sketches_.append(profile);
            require(v.createRevolution(0, 0, 360.0, QStringLiteral("Vaso")).isEmpty(), "rivoluzione di un profilo aperto");
            const ExtrusionObject &vase = v.extrusions_.back();
            require(vase.forgeBody->isSheet() && !vase.solid && std::fabs(area(vase) - 15.0 * M_PI) < 1e-8, "superficie di rivoluzione: area 15 pi");
            require(v.createRevolution(0, 0, 90.0, QStringLiteral("Quarto")).isEmpty(), "rivoluzione parziale di un profilo aperto");
            require(std::fabs(area(v.extrusions_.back()) - 15.0 * M_PI / 4.0) < 1e-8, "superficie di rivoluzione parziale");
            // Rivoluzione con l'asse scelto nella vista (l'asse Z del modello,
            // nel piano XZ dello schizzo) e fusa con i solidi come l'estrusione.
            {
                CadViewport r;
                SketchObject ring;
                ring.name = QStringLiteral("Anello");
                ring.plane = 1;
                addSegment(ring, {1, 0}, {2, 0});
                addSegment(ring, {2, 0}, {2, 3});
                addSegment(ring, {2, 3}, {1, 3});
                addSegment(ring, {1, 3}, {1, 0});
                r.sketches_.append(ring);
                ExtrusionObject first;
                first.name = QStringLiteral("Anello");
                first.feature = BodyFeature::Revolution;
                first.sketchIndex = 0;
                first.plane = 1;
                first.revolveAxis = kRevolveAxisReference;
                first.revolveAxisRef.kind = 2;
                first.revolveAxisRef.index = 2;
                require(r.createBody(first).isEmpty(), "rivoluzione attorno a un asse del modello");
                require(std::fabs(massProperties(*r.extrusions_.back().forgeBody).volume - 9.0 * M_PI) < 1e-8, "volume dell'anello: 9 pi");
                ExtrusionObject outside = first;
                outside.revolveAxisRef.index = 1;  // l'asse Y e' normale al piano dello schizzo
                CadViewport probe;
                probe.sketches_ = r.sketches_;
                require(!probe.createBody(outside).isEmpty() && probe.extrusions_.isEmpty(), "un asse fuori dal piano dello schizzo e' un errore");
                SketchObject flange;
                flange.name = QStringLiteral("Flangia");
                flange.plane = 1;
                addSegment(flange, {0, 4}, {0, 5});
                flange.constructionSegments.append(0);
                addSegment(flange, {1.5, 1}, {3, 1});
                addSegment(flange, {3, 1}, {3, 2});
                addSegment(flange, {3, 2}, {1.5, 2});
                addSegment(flange, {1.5, 2}, {1.5, 1});
                r.sketches_.append(flange);
                ExtrusionObject second;
                second.name = QStringLiteral("Flangia");
                second.feature = BodyFeature::Revolution;
                second.sketchIndex = 1;
                second.plane = 1;
                second.revolveAxis = 0;
                second.mergeOperation = 1;
                second.mergeAuto = true;
                require(r.createBody(second).isEmpty(), "rivoluzione unita al solido toccato");
                const ExtrusionObject &united = r.extrusions_.back();
                require(united.mergeBodies == QVector<int>{0} && !r.extrusions_.first().visible, "il solido unito si nasconde");
                require(std::fabs(massProperties(*united.forgeBody).volume - 14.0 * M_PI) < 1e-8, "volume dell'unione: 14 pi");
                require(r.bodyOperands(united).contains(0), "il solido unito e' un operando");
                QTemporaryDir dir;
                const QString path = dir.filePath(QStringLiteral("rivoluzione.prt"));
                require(saveDocumentFile(path, r.currentDocument(), false).isEmpty(), "salvataggio delle rivoluzioni");
                DocumentState loaded;
                require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.size() == 2
                            && loaded.extrusions.first().revolveAxis == kRevolveAxisReference
                            && loaded.extrusions.first().revolveAxisRef.kind == 2 && loaded.extrusions.first().revolveAxisRef.index == 2
                            && loaded.extrusions.back().mergeOperation == 1,
                        "asse della vista e fusione nel documento");
            }
            // Il bordo del quarto e' un solo loop: da uno spigolo qualsiasi tutti gli spigoli di bordo.
            const Body &quarter = *v.extrusions_.back().forgeBody;
            int laminar = 0;
            EdgeId some;
            for (EdgeId e : quarter.edges())
                if (quarter.isLaminar(e)) ++laminar, some = e;
            require(ForgeCad::freeBoundaryLoop(quarter, some).size() == laminar, "loop del bordo libero del quarto");

            // Planare chiusa sul bordo esterno di una lastra scelto da un solo spigolo.
            SketchObject plate;
            plate.name = QStringLiteral("Lastra");
            addSegment(plate, {0, 0}, {4, 0});
            addSegment(plate, {4, 0}, {4, 3});
            addSegment(plate, {4, 3}, {0, 3});
            addSegment(plate, {0, 3}, {0, 0});
            v.sketches_.append(plate);
            ExtrusionObject planarPlate;
            planarPlate.feature = BodyFeature::PlanarSurface;
            planarPlate.sketchIndex = 1;
            planarPlate.name = QStringLiteral("Lastra piana");
            require(v.createBody(planarPlate).isEmpty(), "lastra piana");
            const int plateIndex = int(v.extrusions_.size()) - 1;
            const Body &plateBody = *v.extrusions_.at(plateIndex).forgeBody;
            EdgeId first = plateBody.edges().front();
            const QVector<EdgePoint> loop = ForgeCad::freeBoundaryLoop(plateBody, first);
            require(loop.size() == 4, "bordo libero della lastra: quattro spigoli");
            ExtrusionObject lid;
            lid.feature = BodyFeature::PlanarSurface;
            lid.sketchIndex = -1;
            for (const EdgePoint &point : loop) {
                GeometryRef ref;
                ref.kind = 4;
                ref.index = plateIndex;
                ref.featureId = v.extrusions_.at(plateIndex).featureId;
                ref.point = point;
                lid.planarRefs.append(ref);
            }
            lid.name = QStringLiteral("Chiusura");
            require(v.createBody(lid).isEmpty(), "planare dal loop del bordo libero");
            require(std::fabs(area(v.extrusions_.back()) - 12.0) < 1e-9, "area della chiusura dal loop");
        }
        // Eliminazione di facce (formato 25) e corpi offerti alle booleane.
        {
            using namespace ForgeCad::Kernel;
            // Il pannello non deve isolare/riaccendere i candidati: un corpo
            // nascosto che racchiude quello da modificare deve restare fuori
            // sia dal disegno sia dal picking. Il corpo deriva dalla faccia,
            // quindi nel pannello non compare alcuna combo di selezione.
            {
                QMainWindow window;
                auto *picker = new CadViewport(&window);
                window.setCentralWidget(picker);
                PrimitiveParameters outer;
                outer.size[0] = outer.size[1] = outer.size[2] = 4.0;
                PrimitiveParameters inner;
                inner.origin[0] = inner.origin[1] = inner.origin[2] = 1.0;
                inner.size[0] = inner.size[1] = inner.size[2] = 1.0;
                require(picker->createPrimitive(outer, QStringLiteral("Esterno")).isEmpty()
                            && picker->createPrimitive(inner, QStringLiteral("Interno")).isEmpty(),
                        "corpi per il pannello elimina facce");
                picker->setModelBodyVisible(0, false);
                ExtrusionObject definition;
                definition.feature = BodyFeature::DeleteFace;
                definition.firstBody = 0;
                bool inspected = false, noBodySelector = false, visibilityPreserved = false, ownerFromFace = false;
                QTimer::singleShot(0, &window, [&] {
                    auto *panel = dynamic_cast<FunctionDialogPanel *>(window.findChild<QDialog *>());
                    inspected = panel != nullptr;
                    if (!panel) return;
                    noBodySelector = panel->findChildren<QComboBox *>().isEmpty();
                    visibilityPreserved = !picker->extrusions_.at(0).visible && picker->extrusions_.at(1).visible
                        && picker->pickBodies_.isEmpty() && !picker->referenceBodyEligible(0) && picker->referenceBodyEligible(1);
                    const Body &innerBody = *picker->extrusions_.at(1).forgeBody;
                    const FaceId face = innerBody.faces().front();
                    const FinId fin = innerBody.loop(innerBody.face(face).loops.front()).first;
                    GeometryRef ref;
                    ref.kind = 5;
                    ref.index = 1;
                    ref.point = ForgeCad::faceReference(innerBody, face, innerBody.finPoint(fin, 0.5));
                    picker->refPickFinished_(true, ref);
                    ownerFromFace = picker->refMarks_.size() == 1 && picker->refMarks_.first().index == 1;
                    panel->reject();
                });
                require(!offsetDialog(&window, picker, QStringLiteral("Elimina facce"), -1, definition,
                                      [](const ExtrusionObject &) { return QString(); }),
                        "annullamento del pannello elimina facce");
                require(inspected && noBodySelector && visibilityPreserved && ownerFromFace,
                        "elimina facce conserva la visibilita' e sceglie il corpo dalla faccia");
            }
            CadViewport v;
            PrimitiveParameters block;
            block.size[0] = 4.0;
            block.size[1] = 3.0;
            block.size[2] = 2.0;
            require(v.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco da aprire");
            const Body &solid = *v.extrusions_.at(0).forgeBody;
            ExtrusionObject open;
            open.feature = BodyFeature::DeleteFace;
            open.firstBody = 0;
            for (FaceId f : solid.faces()) {
                const SurfaceProjection top = projectPoint(*solid.face(f).surface, Vec3(2.0, 1.5, 2.0));
                if (distance(solid.face(f).surface->point(top.u, top.v), Vec3(2.0, 1.5, 2.0)) < 1e-9)
                    open.offsetFaces = {ForgeCad::faceReference(solid, f, Vec3(2.0, 1.5, 2.0))};
            }
            require(open.offsetFaces.size() == 1, "faccia superiore del blocco");
            open.name = QStringLiteral("Aperto");
            require(v.createBody(open).isEmpty(), "eliminazione della faccia superiore");
            const ExtrusionObject &sheet = v.extrusions_.back();
            double total = 0.0;
            for (FaceId f : sheet.forgeBody->faces()) total += faceArea(*sheet.forgeBody, f);
            require(sheet.forgeBody->isSheet() && !sheet.solid && sheet.forgeBody->faces().size() == 5 && std::fabs(total - 40.0) < 1e-9,
                    "il solido senza una faccia diventa una superficie di area 40");
            require(sheet.modelBodyId == v.extrusions_.at(0).modelBodyId && sheet.visible && !v.extrusions_.at(0).visible,
                    "l'eliminazione e' una feature dello stesso corpo");
            // Se il risultato ha shell disconnesse, il comando della GUI le
            // espone come corpi logici distinti (non come un solo multi-body).
            {
                CadViewport separated;
                PrimitiveParameters left;
                left.size[0] = left.size[1] = left.size[2] = 1.0;
                PrimitiveParameters right = left;
                right.origin[0] = 3.0;
                require(separated.createPrimitive(left, QStringLiteral("Sinistro")).isEmpty()
                            && separated.createPrimitive(right, QStringLiteral("Destro")).isEmpty(),
                        "scatole disgiunte per elimina facce");
                require(separated.createBoolean(::BooleanOperation::Union, 0, {1}, QStringLiteral("Due componenti")).isEmpty(),
                        "unione disgiunta per elimina facce");
                const Body &joined = *separated.extrusions_.at(2).forgeBody;
                const FaceId removed = joined.faces().front();
                const FinId sample = joined.loop(joined.face(removed).loops.front()).first;
                ExtrusionObject deletion;
                deletion.feature = BodyFeature::DeleteFace;
                deletion.firstBody = 2;
                deletion.offsetFaces = {ForgeCad::faceReference(joined, removed, joined.finPoint(sample, 0.5))};
                deletion.name = QStringLiteral("Separato");
                require(separated.createDeleteFaces(deletion).isEmpty(), "elimina facce separa le componenti");
                require(separated.extrusions_.size() == 5
                            && separated.extrusions_.at(3).modelBodyId != separated.extrusions_.at(4).modelBodyId
                            && separated.extrusions_.at(3).visible && separated.extrusions_.at(4).visible
                            && !separated.extrusions_.at(2).visible,
                        "le due componenti sono due corpi visibili di storyboard");
                require(separated.extrusions_.at(3).deleteComponent == 0 && separated.extrusions_.at(4).deleteComponent == 1
                            && !separated.extrusions_.at(3).display.faceIds.isEmpty()
                            && separated.extrusions_.at(3).display.faceIds.size() == separated.extrusions_.at(3).display.faceLabelPoints.size(),
                        "componenti e ID topologici della vista");
                QTemporaryDir directory;
                const QString file = directory.filePath(QStringLiteral("delete-components.prt"));
                require(saveDocumentFile(file, separated.currentDocument(), true).isEmpty(), "salvataggio elimina facce multi-corpo");
                DocumentState reloaded;
                require(loadDocumentFile(file, reloaded).isEmpty() && reloaded.extrusions.at(3).deleteComponent == 0
                            && reloaded.extrusions.at(4).deleteComponent == 1
                            && reloaded.extrusions.at(3).display.faceIds.size() == reloaded.extrusions.at(3).display.faceLabelPoints.size()
                            && !reloaded.extrusions.at(3).display.faceIds.isEmpty(),
                        "lettura componenti e ID topologici");
            }
            // Guscio del blocco 4 x 3 x 2 aperto in alto, spessore 0.25: cavita' 3.5 x 2.5 x 1.75.
            {
                CadViewport g;
                require(g.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco da svuotare");
                ExtrusionObject shell = open;
                shell.feature = BodyFeature::Shell;
                shell.distance = 0.25;
                shell.name = QStringLiteral("Guscio");
                // Anteprima della stessa definizione: OK ne riusa il B-rep, senza ricalcolare.
                g.requestPreview(shell, -1);
                QElapsedTimer previewTimeout;
                previewTimeout.start();
                while (!(g.preview_.valid || !g.preview_.error.isEmpty()) && previewTimeout.elapsed() < 60000) QApplication::processEvents();
                require(g.preview_.valid && g.preview_.geometry, "anteprima del guscio");
                const ForgeBody shellPreview = g.preview_.geometry;
                require(g.createBody(shell).isEmpty(), "guscio del blocco");
                require(g.extrusions_.back().forgeBody == shellPreview && !g.extrusions_.back().display.vertices.isEmpty(),
                        "la conferma del guscio riusa il B-rep e la tassellazione dell'anteprima");
                // Modifica con un altro spessore mentre l'anteprima e' ancora in corso: si aspetta quella.
                ExtrusionObject thicker = g.extrusions_.back();
                thicker.distance = 0.3;
                g.requestPreview(thicker, 1);
                require(g.updateBody(1, thicker).isEmpty() && g.preview_.valid && g.extrusions_.at(1).forgeBody == g.preview_.geometry,
                        "la modifica aspetta l'anteprima in corso e ne riusa il risultato");
                require(std::fabs(massProperties(*g.extrusions_.at(1).forgeBody).volume - (24.0 - 3.4 * 2.4 * 1.7)) < 1e-8, "guscio piu' spesso");
                thicker.distance = 0.25;
                require(g.updateBody(1, thicker).isEmpty(), "spessore di partenza");
                g.clearPreview();
                const ExtrusionObject &hollow = g.extrusions_.back();
                require(hollow.solid && std::fabs(massProperties(*hollow.forgeBody).volume - (24.0 - 3.5 * 2.5 * 1.75)) < 1e-8
                            && hollow.modelBodyId == g.extrusions_.at(0).modelBodyId,
                        "guscio: volume e stesso corpo");
                QTemporaryDir shellDirectory;
                const QString shellFile = shellDirectory.filePath(QStringLiteral("guscio25.prt"));
                require(saveDocumentFile(shellFile, g.currentDocument(), false).isEmpty(), "salvataggio del guscio");
                DocumentState shellReloaded;
                require(loadDocumentFile(shellFile, shellReloaded).isEmpty() && shellReloaded.extrusions.back().feature == BodyFeature::Shell
                            && shellReloaded.extrusions.back().distance == 0.25,
                        "lettura del guscio");
            }
            // Superficie tra curve sul bordo libero della scatola aperta (scelto da un solo spigolo).
            const int openIndex = int(v.extrusions_.size()) - 1;
            EdgeId rim;
            for (EdgeId e : sheet.forgeBody->edges())
                if (sheet.forgeBody->isLaminar(e)) rim = e;
            ExtrusionObject lid;
            lid.feature = BodyFeature::BoundarySurface;
            for (const EdgePoint &point : ForgeCad::freeBoundaryLoop(*sheet.forgeBody, rim)) {
                GeometryRef ref;
                ref.kind = 4;
                ref.index = openIndex;
                ref.featureId = v.extrusions_.at(openIndex).featureId;
                ref.point = point;
                lid.planarRefs.append(ref);
            }
            lid.name = QStringLiteral("Coperchio");
            require(lid.planarRefs.size() == 4 && v.createBody(lid).isEmpty(), "superficie tra curve sul bordo libero");
            double lidArea = 0.0;
            for (FaceId f : v.extrusions_.back().forgeBody->faces()) lidArea += faceArea(*v.extrusions_.back().forgeBody, f);
            require(v.extrusions_.back().forgeBody->isSheet() && std::fabs(lidArea - 12.0) < 1e-8, "superficie tra curve: area del coperchio");
            QTemporaryDir directory;
            const QString file = directory.filePath(QStringLiteral("facce25.prt"));
            require(saveDocumentFile(file, v.currentDocument(), false).isEmpty(), "salvataggio formato 25");
            DocumentState reloaded;
            require(loadDocumentFile(file, reloaded).isEmpty() && reloaded.extrusions.at(1).feature == BodyFeature::DeleteFace
                        && reloaded.extrusions.at(1).offsetFaces.size() == 1 && reloaded.extrusions.back().feature == BodyFeature::BoundarySurface
                        && reloaded.extrusions.back().planarRefs.size() == 4,
                    "lettura dell'eliminazione delle facce e della superficie tra curve");

            // Booleane: solo i corpi che esistono in quel punto della storia.
            CadViewport b;
            PrimitiveParameters second = block;
            second.origin[0] = 2.0;
            require(b.createPrimitive(block, QStringLiteral("A")).isEmpty() && b.createScale(0, 1.0, 0, {}, QStringLiteral("Scala A")).isEmpty()
                        && b.createPrimitive(second, QStringLiteral("B")).isEmpty(),
                    "due corpi, il primo con due stadi");
            require(b.resultBodiesBefore(-1) == QVector<int>({1, 2}), "corpi risultanti: lo stadio finale di A e B");
            require(b.createBoolean(::BooleanOperation::Union, 1, {2}, QStringLiteral("Unione")).isEmpty(), "unione dei due corpi");
            require(b.resultBodiesBefore(-1) == QVector<int>({3}), "dopo l'unione resta un solo corpo");
            require(b.resultBodiesBefore(3) == QVector<int>({1, 2}), "prima dell'unione i due corpi");
        }
        // Revisione delle superfici: riferimenti alle entita' dopo un'eliminazione
        // nello schizzo, feature che si staccano dal corpo di cui erano uno stadio.
        {
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto minY = [](const ExtrusionObject &body) {
                double y = 1e300;
                for (auto v : body.forgeBody->vertices()) y = std::min(y, body.forgeBody->vertex(v).point.y());
                return y;
            };
            {
                CadViewport v;
                SketchObject s;
                s.name = QStringLiteral("S");
                addSegment(s, {10, 10}, {11, 10});  // 0: estraneo
                addSegment(s, {0, 0}, {4, 0});      // 1
                addSegment(s, {0, 3}, {4, 3});      // 2
                addSegment(s, {0, 6}, {4, 6});      // 3
                v.sketches_.append(s);
                ExtrusionObject r;
                r.feature = BodyFeature::Ruled;
                r.name = QStringLiteral("R");
                GeometryRef a, b;
                a.kind = b.kind = 7;
                a.index = b.index = 0;
                a.element = ConstraintRef{0, 1, -1};
                b.element = ConstraintRef{0, 2, -1};
                r.ruledFirst = {a};
                r.ruledSecond = {b};
                require(v.createBody(r).isEmpty(), "rigata tra due segmenti di uno schizzo");
                v.activeSketch_ = 0;
                v.sketchMode_ = true;
                v.sketchSelections_ = {SketchElementSelection{0, 0}};
                v.deleteSketchElements();
                const ExtrusionObject &after = v.extrusions_.back();
                require(after.ruledFirst.at(0).element.element == 0 && after.ruledSecond.at(0).element.element == 1 && after.forgeBody
                            && std::fabs(minY(after)) < 1e-12,
                        "la rigata segue le entita' rinumerate");
                v.sketchSelections_ = {SketchElementSelection{0, 0}};
                v.deleteSketchElements();
                require(v.extrusions_.back().ruledFirst.at(0).element.element == -1 && !v.extrusions_.back().error.isEmpty(),
                        "entita' eliminata: la rigata va in errore invece di passare a un'altra");
            }
            {
                CadViewport v;
                SketchObject circle;
                circle.name = QStringLiteral("C");
                circle.plane = kFacePlane;
                CurveObject c;
                c.tool = DrawingTool::Circle;
                c.controlPoints = {QPointF(0, 0), QPointF(0.5, 0)};
                ForgeCad::recalculateCurve(c, 1);
                circle.curves.append(c);
                v.sketches_.append(circle);
                SketchObject path;
                path.name = QStringLiteral("P");
                path.plane = 1;
                addSegment(path, {0, 0}, {0, 3});
                v.sketches_.append(path);
                PrimitiveParameters box;
                box.size[0] = 4;
                box.size[1] = 3;
                box.size[2] = 1;
                box.origin[0] = -2;
                box.origin[1] = -1.5;
                box.origin[2] = 0.7;
                require(v.createPrimitive(box, QStringLiteral("Box")).isEmpty(), "blocco per la sweep fusa");
                ExtrusionObject sw;
                sw.feature = BodyFeature::Sweep;
                sw.sketchIndex = 0;
                sw.pathSketch = 1;
                sw.name = QStringLiteral("Sw");
                sw.mergeOperation = 1;
                sw.mergeAuto = false;
                sw.mergeBodies = {0};
                require(v.createBody(sw).isEmpty() && !v.extrusions_.at(0).visible, "sweep fusa nel blocco");
                ExtrusionObject edited = v.extrusions_.at(1);
                edited.sweepSurface = true;
                edited.mergeOperation = 0;
                edited.mergeBodies.clear();
                require(v.updateBody(1, edited).isEmpty(), "sweep resa superficie");
                require(v.extrusions_.at(0).visible && v.extrusions_.at(1).visible
                            && v.extrusions_.at(0).modelBodyId != v.extrusions_.at(1).modelBodyId,
                        "il blocco torna un corpo visibile, la superficie un corpo a parte");
            }
            {
                CadViewport v;
                for (int k = 0; k < 3; ++k) {
                    SketchObject s;
                    s.name = QStringLiteral("Q%1").arg(k);
                    const double x = 3.0 * k;
                    addSegment(s, {x, 0}, {x + 1, 0});
                    addSegment(s, {x + 1, 0}, {x + 1, 1});
                    addSegment(s, {x + 1, 1}, {x, 1});
                    addSegment(s, {x, 1}, {x, 0});
                    v.sketches_.append(s);
                    ExtrusionObject p;
                    p.feature = BodyFeature::PlanarSurface;
                    p.sketchIndex = k;
                    p.name = QStringLiteral("PL%1").arg(k);
                    require(v.createBody(p).isEmpty(), "planare per la cucitura");
                }
                ExtrusionObject sew;
                sew.feature = BodyFeature::Sew;
                sew.firstBody = 0;
                sew.booleanTools = {1, 2};
                sew.sewSolid = false;
                sew.name = QStringLiteral("Sew");
                require(v.createBody(sew).isEmpty(), "cucitura di tre superfici");
                ExtrusionObject edited = v.extrusions_.at(3);
                edited.firstBody = 1;
                edited.booleanTools = {2};
                require(v.updateBody(3, edited).isEmpty(), "cucitura senza la prima superficie");
                require(v.extrusions_.at(0).visible && !v.extrusions_.at(1).visible
                            && v.extrusions_.at(3).modelBodyId == v.extrusions_.at(1).modelBodyId,
                        "la superficie tolta torna visibile, la cucitura passa al corpo della nuova prima");
            }
        }
        // Calcolo lungo dei corpi: va in un thread, il ciclo di eventi continua
        // (timer e ridisegni) e la finestra di avanzamento compare dopo il
        // ritardo e si chiude alla fine; le eccezioni tornano al chiamante.
        {
            CadViewport responsive;
            responsive.resize(320, 240);
            responsive.show();
            int starts = 0, ends = 0;
            QString shownMessage;
            responsive.setWorkCallback([&](bool begin, const QString &message, bool background) {
                if (background) return;
                (begin ? starts : ends)++;
                if (begin) shownMessage = message;
            });
            int ticks = 0;
            QTimer ticker;
            QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
            ticker.start(20);
            {
                CadViewport::DeferredWork work(&responsive, QStringLiteral("Rigenerazione di prova..."));
                responsive.runWhileResponsive([] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
                require(starts == 1 && ends == 0 && shownMessage == QStringLiteral("Rigenerazione di prova..."),
                        "avanzamento mostrato durante un calcolo lungo");
            }
            require(ends == 1, "avanzamento chiuso alla fine del calcolo");
            require(ticks >= 5, "ciclo di eventi attivo durante il calcolo nel thread");
            {
                CadViewport::DeferredWork work(&responsive, QStringLiteral("Breve"));
                responsive.runWhileResponsive([] {});
            }
            require(starts == 1, "nessun avanzamento per i calcoli brevi");
            bool thrown = false;
            try {
                responsive.runWhileResponsive([] { throw std::runtime_error("prova"); });
            } catch (const std::runtime_error &) {
                thrown = true;
            }
            require(thrown, "eccezione del thread riportata al chiamante");
            ticker.stop();
            responsive.close();
        }
        // Regressione del documento Applicatore: la sottrazione dello sweep
        // attraversa piu' volte la cucitura parametrica del cilindro. La mesh
        // non deve riempire il lato opposto del trim ne' scambiare i salti di
        // piu' periodi per lati collassati.
        {
            DocumentState applicator;
            const QString path = QString::fromUtf8(FORGECAD_SOURCE_DIR) + QStringLiteral("/File_Esempio/Applicatore.prt");
            require(loadDocumentFile(path, applicator).isEmpty() && !applicator.extrusions.isEmpty(), "lettura di Applicatore.prt");
            const ForgeBody result = applicator.extrusions.back().forgeBody;
            require(bool(result), "B-rep finale di Applicatore nello snapshot");
            Kernel::TessellationOptions options;
            options.deflection = 0.02;
            options.angle = 0.25;
            const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
            require(mesh.failedFaces == 0, "tassellazione del risultato di Applicatore");
            int sampled = 0;
            for (const Kernel::FaceMesh &face : mesh.faces) {
                const Kernel::Surface &surface = *result->face(face.face).surface;
                for (std::size_t i = 0; i < face.points.size(); ++i) {
                    const Kernel::Vec2 uv = face.parameters[i];
                    require(Kernel::distance(face.points[i], surface.point(uv.x(), uv.y())) <= 1e-5,
                            "Applicatore: vertice della mesh fuori dalla superficie");
                }
                if (surface.type() != Kernel::SurfaceType::Cylinder) continue;
                for (std::size_t i = 0; i < face.triangles.size(); ++i) {
                    const std::array<int, 3> &triangle = face.triangles[i];
                    double longest = 0.0;
                    for (int k = 0; k < 3; ++k)
                        longest = std::max(longest, Kernel::distance(face.points[std::size_t(triangle[k])],
                            face.points[std::size_t(triangle[(k + 1) % 3])]));
                    if (i % 200 != 0 && longest <= 1.0) continue;
                    const int divisions = longest > 1.0 ? 8 : 3;
                    for (int a = 1; a < divisions; ++a)
                        for (int b = 1; a + b < divisions; ++b) {
                            const int c = divisions - a - b;
                            const Kernel::Vec3 point = (a * face.points[std::size_t(triangle[0])]
                                                      + b * face.points[std::size_t(triangle[1])]
                                                      + c * face.points[std::size_t(triangle[2])]) / double(divisions);
                            const Kernel::SurfaceProjection on = Kernel::projectPoint(surface, point);
                            if (Kernel::classifyPointOnFace(*result, face.face, on.point, 1e-6) == Kernel::PointLocation::Outside)
                                throw std::runtime_error("Applicatore: triangolo fuori dal trim della faccia "
                                                         + std::to_string(face.face.index));
                            ++sampled;
                        }
                }
            }
            require(sampled > 0, "facce di Applicatore campionate");
        }
        // Regressione del flacone: il contatto terminale del raccordo cade
        // presso una cucitura della superficie e Newton, con un solo seme,
        // sceglieva il ramo sbagliato della curva chiusa.
        {
            DocumentState bottleDocument;
            const QString bottlePath = QString::fromUtf8(FORGECAD_SOURCE_DIR) + QStringLiteral("/File_Esempio/flacone.prt");
            require(loadDocumentFile(bottlePath, bottleDocument).isEmpty(), "lettura di flacone.prt");
            CadViewport bottle;
            bottle.loadDocument(bottleDocument);
            int sourceBody = -1;
            Kernel::EdgeId singular;
            for (int index = bottle.extrusions_.size() - 1; index >= 0 && sourceBody < 0; --index) {
                const ForgeBody &candidate = bottle.extrusions_.at(index).forgeBody;
                if (!candidate) continue;
                singular = Kernel::nearestEdge(*candidate, Kernel::Vec3(0.0, 13.6, 105.285), 1e-2);
                if (singular.valid()) sourceBody = index;
            }
            require(sourceBody >= 0, "corpo del flacone prima dei raccordi");
            const Kernel::Body &base = *bottle.extrusions_.at(sourceBody).forgeBody;
            require(singular.valid(), "bordo singolare del flacone");
            const Kernel::Body rounded = Kernel::blendEdges(base, {singular}, 0.6, false);
            require(Kernel::checkBody(rounded).empty() && rounded.faces().size() == 34 && rounded.edges().size() == 48,
                    "raccordo da 0,6 mm sulla cucitura del flacone");
            const Kernel::EdgeId threadEnd = Kernel::nearestEdge(base, Kernel::Vec3(0.0, 13.6, 108.034), 1e-2);
            require(threadEnd.valid(), "bordo del fine-filetto del flacone");
            const Kernel::Body joined = Kernel::blendEdges(base, {singular, threadEnd}, 0.3, false);
            require(Kernel::checkBody(joined).empty() && joined.faces().size() == 37 && joined.edges().size() == 53,
                    "raccordo congiunto da 0,3 mm su elica e fine-filetto");
            const Kernel::EdgeId otherThreadEnd = Kernel::nearestEdge(base, Kernel::Vec3(0.0, 13.6, 101.678), 1e-2);
            require(otherThreadEnd.valid(), "secondo bordo del fine-filetto del flacone");
            const Kernel::Body joinedEnds = Kernel::blendEdges(base, {singular, threadEnd, otherThreadEnd}, 0.3, false);
            require(Kernel::checkBody(joinedEnds).empty(), "raccordo congiunto sui due estremi del filetto");
            const auto edgeNear = [&](const Kernel::Vec3 &point, const char *message) {
                const Kernel::EdgeId edge = Kernel::nearestEdge(base, point, 1e-2);
                require(edge.valid(), message);
                return edge;
            };
            const Kernel::EdgeId lowerHelix = edgeNear(Kernel::Vec3(0.0, 13.6, 104.427), "elica inferiore del filetto");
            const Kernel::EdgeId lowerTrimA = edgeNear(Kernel::Vec3(-2.506, 12.987, 107.704), "primo trim inferiore del filetto");
            const Kernel::EdgeId lowerTrimB = edgeNear(Kernel::Vec3(2.528, 12.967, 101.153), "secondo trim inferiore del filetto");
            const Kernel::Body lower = Kernel::blendEdges(base, {lowerHelix, lowerTrimA, lowerTrimB}, 0.3, false);
            require(Kernel::checkBody(lower).empty() && lower.faces().size() == 34 && lower.edges().size() == 50,
                    "raccordo del bordo inferiore attraverso i trim del collo");
            const Kernel::EdgeId upperTrimA = edgeNear(Kernel::Vec3(-2.478, 12.984, 108.560), "primo trim superiore del filetto");
            const Kernel::EdgeId upperTrimB = edgeNear(Kernel::Vec3(2.555, 12.969, 102.010), "secondo trim superiore del filetto");
            const Kernel::Body upper = Kernel::blendEdges(base, {singular, upperTrimA, upperTrimB}, 0.3, false);
            require(Kernel::checkBody(upper).empty() && upper.faces().size() == 34 && upper.edges().size() == 50,
                    "raccordo del bordo superiore attraverso i trim del collo");

            // La parete del collo e' un cilindro con piu' loop e bordi,
            // trimmato dalle filettature. Una nuova faccia cilindrica nasce
            // invece con due soli cerchi: la cucitura deve imprimerle i loop
            // liberi dell'apertura prima di tentare di chiudere il solido.
            Kernel::FaceId neck;
            int mostEdges = 0;
            for (Kernel::FaceId f : base.faces()) {
                if (base.face(f).surface->type() != Kernel::SurfaceType::Cylinder) continue;
                int count = 0;
                for (Kernel::LoopId loop : base.face(f).loops) count += int(base.loopFins(loop).size());
                if (count > mostEdges) mostEdges = count, neck = f;
            }
            require(neck.valid() && mostEdges > 2, "parete cilindrica trimmata del collo");
            std::vector<Kernel::FaceId> withoutNeck;
            for (Kernel::FaceId f : base.faces()) if (f != neck) withoutNeck.push_back(f);
            const Kernel::Body openBottle = Kernel::facesAsSheet(base, withoutNeck);
            const auto *cylinder = dynamic_cast<const Kernel::CylindricalSurface *>(base.face(neck).surface.get());
            require(cylinder != nullptr, "supporto cilindrico del collo");
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
            for (Kernel::LoopId loop : base.face(neck).loops)
                for (Kernel::FinId fin : base.loopFins(loop))
                    for (Kernel::VertexId vertex : {base.finStart(fin), base.finEnd(fin)}) {
                        const double z = Kernel::dot(base.vertex(vertex).point - cylinder->frame().origin(), cylinder->frame().zDir());
                        lo = std::min(lo, z);
                        hi = std::max(hi, z);
                    }
            const Kernel::Frame3 replacementFrame(cylinder->frame().origin() + lo * cylinder->frame().zDir(),
                                                   cylinder->frame().zDir(), cylinder->frame().yDir());
            const Kernel::Body replacementSolid = Kernel::makeCylinder(replacementFrame, cylinder->radius(), hi - lo);
            std::vector<Kernel::FaceId> replacementFaces;
            for (Kernel::FaceId f : replacementSolid.faces())
                if (replacementSolid.face(f).surface->type() == Kernel::SurfaceType::Cylinder) replacementFaces.push_back(f);
            const Kernel::Body replacement = Kernel::facesAsSheet(replacementSolid, replacementFaces);
            const Kernel::SewResult repaired = Kernel::sewSheets({&openBottle, &replacement}, 1e-5, true);
            require(repaired.closed && repaired.solid && repaired.freeEdges == 0 && Kernel::checkBody(repaired.body).empty(),
                    "ricostruzione del collo cilindrico trimmato e cucitura in solido");
        }
        // L'anteprima del raccordo conserva il B-rep esatto e la
        // tassellazione: OK deve promuoverli senza eseguire di nuovo il kernel.
        {
            CadViewport cachedBlend;
            int foregroundStarts = 0, foregroundEnds = 0, backgroundStarts = 0, backgroundEnds = 0;
            cachedBlend.setWorkCallback([&](bool begin, const QString &, bool background) {
                if (background) (begin ? backgroundStarts : backgroundEnds)++;
                else (begin ? foregroundStarts : foregroundEnds)++;
            });
            PrimitiveParameters cachedBlock;
            cachedBlock.size[0] = 4.0; cachedBlock.size[1] = 3.0; cachedBlock.size[2] = 2.0;
            require(cachedBlend.createPrimitive(cachedBlock, QStringLiteral("Base anteprima raccordo")).isEmpty(),
                    "base dell'anteprima raccordo");
            require(!forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, cachedBlend.extrusions_.first().forgeBody),
                    "un risultato identico non e' un raccordo riuscito");
            const ForgeBody sameTopologyChanged = std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(), 5.0, 3.0, 2.0));
            require(forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, sameTopologyChanged),
                    "una modifica geometrica resta riconosciuta anche a topologia invariata");
            require(foregroundStarts == foregroundEnds, "notifiche bilanciate per il calcolo sul thread principale");
            CadViewport loadedWithProgress;
            int loadedBodies = -1, totalBodies = -1;
            loadedWithProgress.loadDocument(cachedBlend.currentDocument(), [&](int completed, int total, const QString &) {
                loadedBodies = completed;
                totalBodies = total;
            });
            require(loadedBodies == totalBodies && totalBodies == 1,
                    "avanzamento determinato fino all'ultimo corpo durante l'apertura");

            // Uno snapshot della stessa definizione resta apribile dopo un
            // aggiornamento del kernel: la prima modifica lo rigenerera'.
            QTemporaryDir staleCacheDir;
            const QString staleCachePath = staleCacheDir.filePath(QStringLiteral("cache-precedente.prt"));
            require(saveDocumentFile(staleCachePath, cachedBlend.currentDocument(), true).isEmpty(),
                    "salvataggio del documento con cache 3D");
            QFile staleFile(staleCachePath);
            require(staleFile.open(QIODevice::ReadOnly), "lettura del documento con cache 3D");
            QByteArray staleData = staleFile.readAll();
            staleFile.close();
            QDataStream staleHeader(staleData);
            staleHeader.skipRawData(4);
            quint16 staleVersion = 0;
            quint8 staleCompression = 0;
            quint32 payloadSize = 0;
            staleHeader >> staleVersion >> staleCompression >> payloadSize;
            const qsizetype cacheOffset = 11 + qsizetype(payloadSize);
            QByteArray staleCache = qUncompress(staleData.mid(cacheOffset));
            require(staleVersion >= 14 && staleCompression == 1 && staleCache.size() > 68,
                    "blocco cache 3D presente nel documento");
            staleCache[4] = staleCache.at(4) == '0' ? '1' : '0'; // altera soltanto l'impronta dei sorgenti
            staleData.replace(cacheOffset, staleData.size() - cacheOffset, qCompress(staleCache, 6));
            require(staleFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                        && staleFile.write(staleData) == staleData.size(),
                    "scrittura della cache con impronta precedente");
            staleFile.close();
            DocumentState strictCache, previewCache;
            require(loadDocumentFile(staleCachePath, strictCache).isEmpty()
                        && loadDocumentFile(staleCachePath, previewCache, true).isEmpty(),
                    "lettura della cache precedente nei due modi");
            require(strictCache.extrusions.first().forgeBody && strictCache.extrusions.first().cachedGeometry
                        && previewCache.extrusions.first().forgeBody
                        && !strictCache.extrusions.first().display.vertices.isEmpty(),
                    "snapshot precedente disponibile senza ricalcolo e con mesh salvata");
            const QVector<EdgePoint> cachedEdge{{2.0, 0.0, 2.0}};
            cachedBlend.requestBlendPreview(0, cachedEdge, 0.25, false);
            cachedBlend.startPreviewJob();
            QElapsedTimer previewTimeout;
            previewTimeout.start();
            while (cachedBlend.previewRunning_ && previewTimeout.elapsed() < 10000) QApplication::processEvents();
            require(backgroundStarts == 1 && backgroundEnds == 1,
                    "notifiche bilanciate per il calcolo dell'anteprima in background");
            require(cachedBlend.preview_.valid && cachedBlend.preview_.geometry,
                    "B-rep esatto conservato dall'anteprima raccordo");
            require(!cachedBlend.preview_.display.vertices.isEmpty(),
                    "patch locale presente nell'anteprima raccordo");
            require(cachedBlend.preview_.resultDisplay.vertices.isEmpty() && cachedBlend.preview_.resultDisplay.edges.isEmpty(),
                    "l'anteprima nuova non tassella le facce estranee alla patch");
            require(!cachedBlend.preview_.display.constructionCurves.isEmpty(),
                    "curve U/V presenti nell'anteprima raccordo");
            for (const QVector<QVector3D> &curve : cachedBlend.preview_.display.constructionCurves)
                require(curve.size() >= 2 && (curve.first() - curve.last()).length() > 1e-5f,
                        "le curve U/V del raccordo sono trimmate sulla patch e non sono cerchi completi");
            require(cachedBlend.preview_.replaced.isEmpty(),
                    "la base opaca non viene sostituita dalla patch del raccordo");
            const ForgeBody previewGeometry = cachedBlend.preview_.geometry;
            const int patchTriangles = cachedBlend.preview_.display.vertices.size();
            require(cachedBlend.createBlend(0, cachedEdge, 0.25, false, QStringLiteral("Raccordo da anteprima")).isEmpty(),
                    "conferma dell'anteprima raccordo");
            require(cachedBlend.extrusions_.back().forgeBody == previewGeometry,
                    "la conferma riusa il B-rep dell'anteprima");
            require(cachedBlend.extrusions_.back().display.vertices.size() > patchTriangles,
                    "la conferma completa la tassellazione dell'intero risultato");
            require(forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, cachedBlend.extrusions_.back().forgeBody),
                    "il raccordo confermato modifica realmente la base");

            DocumentState noOpSnapshot = cachedBlend.currentDocument();
            noOpSnapshot.extrusions.back().forgeBody = noOpSnapshot.extrusions.first().forgeBody;
            noOpSnapshot.extrusions.back().display = noOpSnapshot.extrusions.first().display;
            noOpSnapshot.extrusions.back().cachedGeometry = true;
            CadViewport repairedSnapshot;
            repairedSnapshot.loadDocument(noOpSnapshot);
            require(repairedSnapshot.extrusions_.back().error.isEmpty()
                        && forgeBlendHasEffect(repairedSnapshot.extrusions_.first().forgeBody,
                                               repairedSnapshot.extrusions_.back().forgeBody),
                    "una cache no-op del raccordo viene scartata e rigenerata");

            // Modifica senza cambiare valori: l'anteprima mostra la patch con le
            // curve U/V dal body esistente (niente kernel) e la conferma lascia
            // il corpo com'e'.
            const int blendIndex = int(cachedBlend.extrusions_.size()) - 1;
            const ExtrusionObject existing = cachedBlend.extrusions_.at(blendIndex);
            require(cachedBlend.unchangedFeature(existing, blendIndex), "funzione non modificata riconosciuta");
            cachedBlend.requestBlendPreview(existing.firstBody, existing.blendEdges, existing.blendSize, existing.blendChamfer,
                                            blendIndex, existing.chamferSpec);
            cachedBlend.startPreviewJob();
            previewTimeout.restart();
            while (cachedBlend.previewRunning_ && previewTimeout.elapsed() < 10000) QApplication::processEvents();
            require(cachedBlend.preview_.valid && cachedBlend.preview_.geometry == existing.forgeBody,
                    "l'anteprima della modifica riusa il B-rep del corpo");
            require(!cachedBlend.preview_.display.vertices.isEmpty() && !cachedBlend.preview_.display.constructionCurves.isEmpty()
                        && cachedBlend.preview_.display.vertices.size() < existing.display.vertices.size(),
                    "patch in trasparenza con le curve U/V senza ricalcolo");
            require(cachedBlend.updateBody(blendIndex, existing).isEmpty()
                        && cachedBlend.extrusions_.at(blendIndex).forgeBody == existing.forgeBody,
                    "conferma senza modifiche: corpo invariato");
            // La finestra passa la seconda distanza dello smusso anche per un
            // raccordo: per i raccordi non conta.
            ExtrusionObject fromDialog = existing;
            fromDialog.chamferSpec.second = existing.blendSize + 1.0;
            fromDialog.chamferSpec.flip = true;
            require(cachedBlend.unchangedFeature(fromDialog, blendIndex), "la seconda distanza non conta per un raccordo");
            // Gli stessi spigoli scelti di nuovo nella vista (punti diversi).
            require(cachedBlend.beginBlendEdit(blendIndex, existing.blendSize, existing.blendChamfer).isEmpty(),
                    "modifica degli spigoli del raccordo");
            ExtrusionObject repicked = existing;
            repicked.blendEdges = cachedBlend.pickedEdgePoints();
            require(cachedBlend.unchangedFeature(repicked, blendIndex), "gli stessi spigoli scelti di nuovo non richiedono il calcolo");
            // Durante la modifica degli spigoli l'anteprima resta quella del
            // raccordo attuale: togliere o aggiungere spigoli non la ricalcola.
            const QString existingKey = CadViewport::previewKey(existing, blendIndex);
            require(cachedBlend.preview_.key == existingKey, "la modifica degli spigoli mostra il raccordo attuale");
            cachedBlend.pickedEdges_.clear();
            cachedBlend.pickedEdges_.append(0);
            cachedBlend.edgePicked();
            require(cachedBlend.preview_.key == existingKey, "un clic nella modifica degli spigoli non ricalcola");
            cachedBlend.cancelEdgePick();
            ExtrusionObject changed = existing;
            changed.blendSize = 0.3;
            require(!cachedBlend.unchangedFeature(changed, blendIndex), "un valore cambiato richiede il calcolo");
            cachedBlend.clearPreview();
        }
        std::cout << "Viewport: assi, piani, datum, Undo/Redo, riferimenti, vincoli, salvataggio e cache OK" << std::endl;
    }
};
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL); format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Viewport"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    if (app.arguments().contains(QStringLiteral("--automatic-snaps"))) {
        try { ViewportInteractionTest::automaticSnapConstraints(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS automatic snap constraints" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--micro-features"))) {
        try { ViewportInteractionTest::microFeatureMarksTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS micro features" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--ellipse-trim"))) {
        try { ViewportInteractionTest::ellipseTrim(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS ellipse trim" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--edge-pick-deletion"))) {
        try { ViewportInteractionTest::deletionDuringEdgePick(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    const int benchWindow = int(app.arguments().indexOf(QStringLiteral("--bench-window")));
    if (benchWindow > 0) {
        // Le impostazioni dell'utente, copiate: la finestra non tocca le sue.
        QDir().mkpath(settings.path() + QStringLiteral("/ForgeCADTests"));
        QFile::copy(QDir::homePath() + QStringLiteral("/.config/ForgeCAD/ForgeCAD.conf"),
                    settings.path() + QStringLiteral("/ForgeCADTests/Viewport.ini"));
        try { ViewportInteractionTest::benchWindow(app.arguments().mid(benchWindow + 1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().indexOf(QStringLiteral("--time-offset")) > 0) {
        DocumentState state;
        ForgeCad::loadDocumentFile(app.arguments().last(), state);
        CadViewport v;
        v.loadDocument(state);
        for (int i = 0; i < v.extrusions().size(); ++i) {
            const ExtrusionObject &e = v.extrusions().at(i);
            if (e.feature != BodyFeature::SurfaceOffset) continue;
            const ExtrusionObject &base = v.extrusions().at(e.firstBody);
            QElapsedTimer t; t.start();
            QString error;
            auto r = ForgeCad::forgeOffsetFaces(base.forgeBody, e.offsetFaces, e.distance, &error);
            std::cout << e.name.toStdString() << ": facce " << (e.offsetFaces.isEmpty() ? base.forgeBody->faces().size() : std::size_t(e.offsetFaces.size()))
                      << " di " << base.forgeBody->faces().size() << ", " << t.elapsed() << " ms " << error.toStdString() << std::endl;
            // Tutte le facce del corpo.
            t.restart();
            r = ForgeCad::forgeOffsetFaces(base.forgeBody, {}, e.distance, &error);
            std::cout << "  tutte le facce: " << t.elapsed() << " ms " << error.toStdString() << std::endl;
        }
        return 0;
    }
    try {
        const int step = int(app.arguments().indexOf(QStringLiteral("--render-step")));
        const int owner = int(app.arguments().indexOf(QStringLiteral("--pick-owner")));
        if (owner > 0) { ViewportInteractionTest::pickOwner(app.arguments().mid(owner + 1)); return 0; }
        const int load = int(app.arguments().indexOf(QStringLiteral("--bench-load")));
        if (load > 0) { ViewportInteractionTest::benchLoad(app.arguments().mid(load + 1)); return 0; }
        const int bench = int(app.arguments().indexOf(QStringLiteral("--bench-view")));
        if (bench > 0) { ViewportInteractionTest::benchView(app.arguments().mid(bench + 1)); return 0; }
        const int edit = int(app.arguments().indexOf(QStringLiteral("--render-edit")));
        const int displayStatsArg = int(app.arguments().indexOf(QStringLiteral("--display-stats")));
        if (displayStatsArg > 0) { ViewportInteractionTest::displayStats(app.arguments().value(displayStatsArg + 1)); return 0; }
        const int meshStats = int(app.arguments().indexOf(QStringLiteral("--mesh-stats")));
        if (meshStats > 0) { ViewportInteractionTest::meshStats(app.arguments().mid(meshStats + 1)); return 0; }
        if (edit > 0) ViewportInteractionTest::renderEdit(app.arguments().mid(edit + 1));
        else if (step > 0) ViewportInteractionTest::renderStep(app.arguments().mid(step + 1));
        else if (app.arguments().contains(QStringLiteral("--extend-contour-pick")))
            ViewportInteractionTest::extensionContourPicking();
        else if (app.arguments().contains(QStringLiteral("--trim-pick")))
            ViewportInteractionTest::trimPartPicking();
        else if (app.arguments().contains(QStringLiteral("--offset-pick")))
            ViewportInteractionTest::offsetFacePicking();
        else if (app.arguments().contains(QStringLiteral("--offset-loft-sides"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--offset-loft-sides"));
            ViewportInteractionTest::offsetLoftSides(app.arguments().value(argument + 1), app.arguments().value(argument + 2, QStringLiteral("1")).toDouble(), app.arguments().contains(QStringLiteral("--rebuild-lofts")),
                app.arguments().contains(QStringLiteral("--save-rebuilt-loft"))
                    ? app.arguments().value(app.arguments().indexOf(QStringLiteral("--save-rebuilt-loft")) + 1) : QString());
        }
        else if (app.arguments().contains(QStringLiteral("--extend-topology-face"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--extend-topology-face"));
            ViewportInteractionTest::extendTopologyFace(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt(),
                app.arguments().value(argument + 4, QStringLiteral("1")).toDouble());
        }
        else if (app.arguments().contains(QStringLiteral("--blend-document-faces"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--blend-document-faces"));
            ViewportInteractionTest::blendDocumentFaces(app.arguments().mid(argument + 1));
        }
        else if (app.arguments().contains(QStringLiteral("--repair-extension-contours"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--repair-extension-contours"));
            ViewportInteractionTest::repairExtensionContours(app.arguments().value(argument + 1), app.arguments().value(argument + 2),
                                                            app.arguments().mid(argument + 3));
        }
        else if (app.arguments().contains(QStringLiteral("--audit-sheet-history"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--audit-sheet-history"));
            ViewportInteractionTest::auditSheetHistory(app.arguments().value(argument + 1), app.arguments().value(argument + 2));
        }
        else if (app.arguments().contains(QStringLiteral("--verify-mutual-trim"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--verify-mutual-trim"));
            ViewportInteractionTest::trimTopologyBodies(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt(), true);
        }
        else if (app.arguments().contains(QStringLiteral("--trim-topology-bodies"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--trim-topology-bodies"));
            ViewportInteractionTest::trimTopologyBodies(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt());
        }
        else if (app.arguments().contains(QStringLiteral("--offset-lofts")))
            ViewportInteractionTest::offsetLofts(app.arguments().mid(app.arguments().indexOf(QStringLiteral("--offset-lofts")) + 1));
        else if (app.arguments().contains(QStringLiteral("--loft-corner")))
            ViewportInteractionTest::loftCorner(QStringLiteral("File_Esempio/prova con loft.prt"));
        else ViewportInteractionTest::run(app.arguments().contains(QStringLiteral("--gl")));
    }
    catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
}
