#include "cad_drawing_editor.h"
#include "fk_boolean.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_curve_algo.h"
#include "fk_bspline_surface.h"
#include "fk_pcurve.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTimer>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why){require(std::isfinite(a)&&std::abs(a-b)<1e-7,why);}
DrawingSourceBody source(Body body){auto p=std::make_shared<Body>(std::move(body));auto names=std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"projected"));return {1,p,names};}
std::vector<DrawingCurve> curves(const DrawingModelSnapshot &model,const DrawingView &view){std::vector<DrawingCurve> result;ok(drawingViewCurves(model,view,result));return result;}
DrawingDocument dimension(const DrawingModelSnapshot &model,const DrawingView &view,const DrawingCurve &curve,DrawingMeasure measure){DrawingDocument doc;doc.model=model.version;doc.views={view};doc.dimensions={dimensionOfCurve(view,curve,measure)};return doc;}
double value(const DrawingDocument &doc,const DrawingModelSnapshot &model){const auto result=evaluateDrawingDimension(doc,model,doc.dimensions[0].id);require(result.status==DrawingDimensionStatus::Ready,"quota pronta");return result.value;}
void select(DrawingSheetEditor &editor,const QUuid &view,int index){for(auto *item:editor.canvas()->scene()->items())item->setSelected(item->data(1).toUuid()==view&&item->data(2).toInt()==index&&(item->data(0).toString()=="projection"||item->data(0).toString()=="section"));}
Body patch(){
    auto surface=std::make_shared<BSplineSurface>(2,1,std::vector<double>{0,0,0,1,1,1},std::vector<double>{0,0,1,1},3,2,
        std::vector<Vec3>{{-5,-4,0},{-5,4,0},{0,-4,3},{0,4,3},{5,-4,0},{5,4,0}},std::vector<double>{1,1,.7,.7,1,1});
    const std::vector<Vec3> points{surface->point(0,0),surface->point(1,0),surface->point(1,1),surface->point(0,1)};
    const std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(0),{0,1},0},{1,2,surface->uIso(1),{0,1},0},{3,2,surface->vIso(1),{0,1},0},{0,3,surface->uIso(0),{0,1},0}};
    Body::BuildFace face;face.surface=surface;face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
    auto body=Body::buildSheet(points,edges,{face});computePCurves(body);return body;
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    try{
        DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(makeBox(Frame3(),10,6,4))}};
        DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=Frame3(Vec3(0,0,100),Vec3(0,0,1),Vec3(1,0,0));
        auto projected=curves(model,view);require(projected.size()==8,"bordi paralleli alla normale omessi, bordi posteriori mantenuti");
        for(const auto &curve:projected){require(curve.kind==DrawingCurveKind::ProjectedEdge&&curve.sourceEdge.valid()&&!curve.sourceFace.valid()&&!curve.edgeName.empty(),"provenienza del bordo proiettato");near(view.plane.toLocal(curve.curve->point(curve.range.lo)).z(),0,"immagine nel piano, anche fuori dal corpo");}
        auto horizontal=std::find_if(projected.begin(),projected.end(),[](const auto &c){return std::abs(distance(c.curve->point(c.range.lo),c.curve->point(c.range.hi))-10)<1e-7;});require(horizontal!=projected.end(),"bordo orizzontale");
        auto doc=dimension(model,view,*horizontal,DrawingMeasure::HorizontalDistance);near(value(doc,model),10,"quota orizzontale proiettata");doc.views[0].scale=7;near(value(doc,model),10,"misura indipendente dalla scala");
        auto legacy=doc;legacy.views[0].kind=DrawingViewKind::Section;legacy.views[0].plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));
        auto mixed=view;mixed.plane=legacy.views[0].plane;mixed.sectionReferences=true;const auto both=curves(model,mixed);
        require(both.size()==12,"proiezione e quattro sezioni del piano");const auto section=std::find_if(both.begin(),both.end(),[](const auto &c){return c.kind==DrawingCurveKind::Section&&std::abs(arcLength(*c.curve,c.range)-10)<1e-7;});
        require(section!=both.end(),"sezione quotabile nella vista ordinaria");const auto cutDimension=dimension(model,mixed,*section,DrawingMeasure::HorizontalDistance);near(value(cutDimension,model),10,"quota sulla faccia intersecata dal piano");
        // Su una lamina inclinata la distanza verticale e' quella nel piano, non quella spaziale.
        DrawingModelSnapshot sheet{model.version,{source(makePlaneSheet(Frame3(Vec3(),normalized(Vec3(0,1,1)),Vec3(1,0,0)),5))}};
        auto image=curves(sheet,view);const auto slanted=std::find_if(image.begin(),image.end(),[](const auto &c){return std::abs(arcLength(*c.curve,c.range)-10/std::sqrt(2.0))<1e-7;});require(slanted!=image.end(),"bordo inclinato proiettato");
        near(value(dimension(sheet,view,*slanted,DrawingMeasure::VerticalDistance),sheet),10/std::sqrt(2.0),"quota verticale nel piano");
        DrawingModelSnapshot generic=sheet;auto proxy=std::make_shared<Body>(*sheet.bodies[0].body);
        for(auto edge:proxy->edges())proxy->edge(edge).curve=std::make_shared<TransformedCurve>(proxy->edge(edge).curve,Transform3());
        generic.bodies={source(std::move(*proxy))};
        require(curves(generic,view).size()==4,"curve trasformate generiche proiettate senza approssimare il bordo sorgente");
        auto trimmed=*generic.bodies[0].body;for(auto edge:trimmed.edges())trimmed.edge(edge).curve=std::make_shared<TrimmedCurve<3>>(trimmed.edge(edge).curve,trimmed.edge(edge).range.lo,trimmed.edge(edge).range.hi);
        generic.bodies={source(std::move(trimmed))};require(curves(generic,view).size()==4,"curve generiche limitate mantenute");
        DrawingView oblique=view;oblique.plane=Frame3(Vec3(),normalized(Vec3(0,1,1)),Vec3(1,0,0));
        DrawingModelSnapshot cylinder{model.version,{source(makeCylinder(Frame3(),3,8))}};const auto ellipses=curves(cylinder,oblique);require(ellipses.size()==2,"cerchi proiettati come ellissi");
        double expected=0;const int samples=4096;for(int i=0;i<samples;++i){const double t=kTwoPi*(i+.5)/samples;expected+=3*std::sqrt(std::sin(t)*std::sin(t)+.5*std::cos(t)*std::cos(t))*kTwoPi/samples;}
        near(value(dimension(cylinder,oblique,ellipses[0],DrawingMeasure::SectionLength),cylinder),expected,"lunghezza ellittica, non circonferenza 3D");
        DrawingModelSnapshot rational{model.version,{source(patch())}};const auto nurbs=curves(rational,oblique);require(nurbs.size()==4,"bordi NURBS razionali proiettati");
        for(const auto &curve:nurbs){const auto &original=rational.bodies[0].body->edge(curve.sourceEdge);for(int i=0;i<=12;++i){const double t=original.range.lo+original.range.length()*i/12;const auto a=oblique.plane.toLocal(original.curve->point(t)),b=oblique.plane.toLocal(curve.curve->point(t));near(a.x(),b.x(),"coordinate esatte NURBS x");near(a.y(),b.y(),"coordinate esatte NURBS y");}}
        QByteArray bytes;ok(encodeDrawingDocument(doc,bytes));require(QJsonDocument::fromJson(bytes).object()["schema"].toInt()==2,"schema esteso per le nuove viste");DrawingDocument reloaded;ok(decodeDrawingDocument(bytes,reloaded));near(value(reloaded,model),10,"quote proiettate persistenti");
        auto bad=QJsonDocument::fromJson(bytes).object();bad["schema"]=1;const auto retained=reloaded.version.documentId;require(!decodeDrawingDocument(QJsonDocument(bad).toJson(),reloaded).isEmpty()&&reloaded.version.documentId==retained,"schema declassato rifiutato atomicamente");
        auto old=dimension(model,legacy.views[0],*section,DrawingMeasure::SectionLength); // stesso ID vista
        ok(encodeDrawingDocument(old,bytes));require(QJsonDocument::fromJson(bytes).object()["schema"].toInt()==1,"codec legacy invariato per sole sezioni");
        auto removed=doc;removed.dimensions[0].section.edgeName="assente";require(evaluateDrawingDimension(removed,model,removed.dimensions[0].id).status==DrawingDimensionStatus::Missing,"bordo cancellato non sostituito per indice");
        auto names=std::make_shared<ElementNames>(*model.bodies[0].names);const auto edge=horizontal->sourceEdge;int other=-1;
        for(const auto &curve:projected)if(curve.sourceEdge!=edge&&distance(curve.curve->point(.5*(curve.range.lo+curve.range.hi)),horizontal->curve->point(.5*(horizontal->range.lo+horizontal->range.hi)))<1e-7){other=curve.sourceEdge.index;break;}
        require(other>=0,"bordi sovrapposti da profondita' diverse");names->edges[other].push_back(horizontal->edgeName);auto ambiguous=model;ambiguous.bodies[0].names=names;
        require(evaluateDrawingDimension(doc,ambiguous,doc.dimensions[0].id).status==DrawingDimensionStatus::Ambiguous,"alias proiettati sovrapposti non risolti arbitrariamente");
        auto duplicate=model;duplicate.bodies.push_back(duplicate.bodies[0]);const auto count=projected.size();require(!projectDrawingEdges(duplicate,view,projected).isEmpty()&&projected.size()==count,"sorgente duplicata rifiutata senza risultati parziali");
        // Dialogo reale, selezione, nuove quote e schema del foglio.
        DrawingSheet page;page.drawing.model=model.version;DrawingEditorModel editorModel{model,{{1,"Corpo"}},{}};DrawingSheetEditor editor(page,editorModel);editor.resize(1450,850);editor.show();QApplication::processEvents();bool dialogSeen=false;
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;dialog->findChild<QComboBox *>("drawingViewPlane")->setCurrentIndex(0);dialog->findChild<QCheckBox *>("drawingSectionReferences")->setChecked(true);dialog->findChild<QDoubleSpinBox *>("drawingViewDepth")->setValue(2);dialog->findChild<QDoubleSpinBox *>("drawingViewScale")->setValue(4);dialogSeen=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor.findChild<QAction *>("drawingAddProjectionAction")->trigger();require(dialogSeen&&editor.hasViewPlacement()&&editor.sheet().drawing.views.empty(),"dialogo proiezione prepara l'anteprima");
        {auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(QPointF(140,100)),global=canvas->viewport()->mapToGlobal(local.toPoint());QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&press);QApplication::sendEvent(canvas->viewport(),&release);}
        require(!editor.hasViewPlacement()&&editor.sheet().drawing.views.size()==1,"clic conferma la proiezione");const auto id=editor.sheet().drawing.views[0].id;
        require(editor.curves(id).size()==12&&editor.sections(id).size()==4,"sezioni e bordi distinti nell'editor");int index=-1;const auto &display=editor.curves(id);
        for(std::size_t i=0;i<display.size();++i)if(display[i].kind==DrawingCurveKind::ProjectedEdge&&std::abs(arcLength(*display[i].curve,display[i].range)-10)<1e-7){index=int(i);break;}
        select(editor,id,index);editor.findChild<QAction *>("drawingHorizontalDimensionAction")->trigger();require(editor.sheet().drawing.dimensions.size()==1,"quota orizzontale dalla toolbar");const auto dimensionId=editor.sheet().drawing.dimensions[0].id;near(editor.dimensionResult(dimensionId).value,10,"quota GUI nel piano");
        DrawingEditorModel changed=editorModel;changed.snapshot.bodies={source(makeBox(Frame3(),14,6,4))};ok(editor.updateModel(changed));near(editor.dimensionResult(dimensionId).value,14,"bordo proiettato associativo dopo modifica");
        ok(encodeDrawingSheet(editor.sheet(),bytes));DrawingSheet restored;ok(decodeDrawingSheet(bytes,restored));DrawingSheetEditor reopened(restored,changed);near(reopened.dimensionResult(dimensionId).value,14,"riapertura della vista proiettata e quota");
        int verticalIndex=-1,sectionIndex=-1;const auto &loadedCurves=reopened.curves(id);
        for(std::size_t i=0;i<loadedCurves.size();++i){const auto &c=loadedCurves[i];const auto delta=c.curve->point(c.range.hi)-c.curve->point(c.range.lo);
            if(c.kind==DrawingCurveKind::ProjectedEdge&&std::abs(std::abs(delta.y())-6)<1e-7)verticalIndex=int(i);
            if(c.kind==DrawingCurveKind::Section&&std::abs(arcLength(*c.curve,c.range)-14)<1e-7)sectionIndex=int(i);}
        require(verticalIndex>=0&&sectionIndex>=0,"riferimenti verticali e sezioni nella vista riaperta");
        select(reopened,id,verticalIndex);reopened.findChild<QAction *>("drawingVerticalDimensionAction")->trigger();
        require(reopened.sheet().drawing.dimensions.size()==2,"quota verticale dalla toolbar");near(reopened.dimensionResult(reopened.sheet().drawing.dimensions.back().id).value,6,"quota verticale GUI");
        select(reopened,id,sectionIndex);reopened.findChild<QAction *>("drawingHorizontalDimensionAction")->trigger();
        require(reopened.sheet().drawing.dimensions.size()==3&&reopened.sheet().drawing.dimensions.back().section.kind==DrawingCurveKind::Section,"quota della sezione sullo stesso foglio proiettato");
        near(reopened.dimensionResult(reopened.sheet().drawing.dimensions.back().id).value,14,"sezione quotata nella vista ordinaria");
        ok(encodeDrawingSheet(reopened.sheet(),bytes));DrawingSheet mixedReferences;ok(decodeDrawingSheet(bytes,mixedReferences));require(mixedReferences.drawing.dimensions.back().section.kind==DrawingCurveKind::Section,"riferimenti misti salvati senza confondere facce e bordi");
        DrawingView side;side.kind=DrawingViewKind::ProjectedEdges;side.plane=Frame3(Vec3(),normalized(Vec3(1,-1,1)),normalized(Vec3(1,1,0)));side.scale=4;ok(editor.insertView(side,{side.id,"Isometrica",QPointF(220,110),{1}}));
        editor.setReadOnly(true);require(!editor.findChild<QAction *>("drawingAddProjectionAction")->isEnabled()&&!editor.findChild<QAction *>("drawingVerticalDimensionAction")->isEnabled(),"nuovi comandi rispettano sola lettura");editor.setReadOnly(false);
        const auto preview=qEnvironmentVariable("FORGECAD_PROJECTED_PREVIEW");if(!preview.isEmpty()){QApplication::processEvents();require(editor.grab().save(preview),"anteprima delle proiezioni");}
        std::cout<<"PASS projected edges, planar dimensions, section references, codec and GUI\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
