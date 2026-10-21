#pragma once
namespace {
QPointF allDimensionMiddle(DrawingSheetEditor &e,const DrawingDimension &d,QPointF anchor){
 const auto r=e.dimensionResult(d.id);const auto &v=e.sheet().drawing.views[0];const auto origin=e.sheet().views[0].position;const auto paperAt=[&](Vec2 p){return origin+v.scale*QPointF(p.x(),-p.y());};
 if(d.measure==DrawingMeasure::SectionLength)return paperAt({6,0});
 auto a=paperAt(r.first),b=paperAt(r.second);
 if(d.references.empty()){a=paperAt({0,0});b=paperAt({12,0});}
 if(r.geometry==DrawingDimensionResult::Geometry::Radius)return (a+b)/2;
 if(r.geometry==DrawingDimensionResult::Geometry::Diameter)return a;
 if(d.measure==DrawingMeasure::HorizontalDistance)return {(a.x()+b.x())/2,anchor.y()};
 if(d.measure==DrawingMeasure::VerticalDistance)return {anchor.x(),(a.y()+b.y())/2};
 const auto t=b-a;const double length=QLineF(a,b).length();if(length<1e-10)return (a+b)/2;const QPointF n(-t.y()/length,t.x()/length);const auto mid=(a+b)/2;return mid+QPointF::dotProduct(anchor-mid,n)*n;
}
void drawingAllDimensionCenterCase(DrawingMeasure type,bool legacy=false){
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6),1),source(makeCylinder(Frame3(Vec3(28,0,0),Vec3(0,0,1),Vec3(1,0,0)),5,6),2)};model.choices={{1,"Blocco"},{2,"Cilindro"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;ok(e.insertView(v,{v.id,drawingDimensionName(type),{75,120},{1,2}}));e.fitSheet();QApplication::processEvents();

 DrawingDimension d;
 if(legacy){
  bool found=false;for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="projection"&&item->data(1).toUuid()==v.id){const auto &c=e.curves(v.id).at(std::size_t(item->data(2).toInt()));const auto a=v.plane.toLocal(c.curve->point(c.range.lo)),b=v.plane.toLocal(c.curve->point(c.range.hi));if(c.featureId==1&&std::abs(a.y())<1e-8&&std::abs(b.y())<1e-8&&std::abs(a.x()-b.x())>11){item->setSelected(true);found=true;break;}}require(found,"curva legacy orizzontale visibile");ok(e.addSelectedDimension(type));d=last(e);
 }else{
  mode(e,int(type));
  if(type==DrawingMeasure::Radius||type==DrawingMeasure::Diameter)click(e,paper({33,0}));
  else if(type==DrawingMeasure::SectionLength)click(e,paper({4,0}),Qt::ShiftModifier);
  else if(type==DrawingMeasure::AxisRadius||type==DrawingMeasure::AxisDiameter){click(e,paper({12,8}));e.findChild<QAction *>("drawingAxisYReferenceAction")->trigger();}
  else{click(e,paper({0,0}));click(e,paper({12,8}));}
  const QPointF cursor(145,65);move(e,cursor);QGraphicsItem *preview=nullptr;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionPreview")preview=i;require(preview,"anteprima di ogni tipo di quota centrata");const auto previewAnchor=preview->data(6).toPointF();click(e,cursor);d=last(e);QApplication::processEvents();near(QLineF(dimensionTextItem(e,d.id)->data(6).toPointF(),previewAnchor).length(),0,"centro identico in anteprima e inserimento per ogni tipo");e.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);
 }
 QApplication::processEvents();const double nominal=e.dimensionResult(d.id).value;const auto initial=dimensionTextItem(e,d.id)->data(6).toPointF();near(QLineF(initial,allDimensionMiddle(e,d,initial)).length(),0,"nuova quota centrata per ogni tipologia e legacy");
 auto style=e.sheet().style;
 for(double factor:{1.,4.}){e.fitSheet();e.canvas()->scale(factor,factor);const double zoom=std::abs(e.canvas()->transform().m11());
  for(int appearance=0;appearance<3;++appearance){style.dimensionTextOrientation=appearance?DrawingTextOrientation::Aligned:DrawingTextOrientation::Horizontal;style.dimensionTextPosition=appearance==1?DrawingTextPosition::Above:appearance==2?DrawingTextPosition::Below:DrawingTextPosition::Centered;ok(e.setDrawingStyle(style));QApplication::processEvents();auto *label=dimensionTextItem(e,d.id);auto anchor=label->data(6).toPointF();
   // Allontana prima il valore dal centro per tutti i tipi, comprese quote
   // senza due estensioni (raggi/diametri e lunghezze con un solo richiamo).
   const auto begin=label->sceneBoundingRect().center();const QPointF away=begin+QPointF(24,21)/zoom;const auto freeRaw=anchor+angularMapped(e,away)-angularMapped(e,begin);const auto before=encoded(e);angularMouse(e,begin,0);angularMouse(e,away,1,Qt::AltModifier);near(QLineF(label->data(6).toPointF(),freeRaw).length(),0,"Alt permette spostamento libero per ogni tipologia");angularMouse(e,away,2,Qt::AltModifier);QApplication::processEvents();label=dimensionTextItem(e,d.id);near(QLineF(label->data(6).toPointF(),freeRaw).length(),0,"rilascio libero senza salto per ogni tipo");require(encoded(e)!=before,"spostamento libero registrato");ok(e.undo());require(encoded(e)==before,"Undo dello spostamento libero");ok(e.redo());QApplication::processEvents();label=dimensionTextItem(e,d.id);anchor=label->data(6).toPointF();
   const auto middle=allDimensionMiddle(e,d,anchor),start=label->sceneBoundingRect().center();const QPointF target=start+middle-anchor+QPointF(2,1)/zoom;const auto raw=anchor+angularMapped(e,target)-angularMapped(e,start);const auto expected=allDimensionMiddle(e,d,raw);const auto stored=encoded(e);angularMouse(e,start,0);angularMouse(e,target,1);const auto snapped=label->data(6).toPointF();near(QLineF(snapped,expected).length(),0,"aggancio al centro geometrico entro8pixel");require(encoded(e)==stored,"snap live senza mutazioni");const auto previewCenter=label->sceneBoundingRect().center();angularMouse(e,target,2);QApplication::processEvents();label=dimensionTextItem(e,d.id);near(QLineF(label->data(6).toPointF(),snapped).length(),0,"centro salvato senza salto logico");near(QLineF(label->sceneBoundingRect().center(),previewCenter).length(),0,"centro salvato senza salto visivo ruotato e sopra/sotto");near(e.dimensionResult(d.id).value,nominal,"centraggio conserva la misura");
  }
 }
 const auto saved=encoded(e);DrawingSheet restored;ok(decodeDrawingSheet(saved,restored));DrawingSheetEditor reopened(restored,model);require(encoded(reopened)==saved,"quote centrate e libere persistono senza ricentratura implicita");near(QLineF(dimensionTextItem(reopened,d.id)->data(6).toPointF(),dimensionTextItem(e,d.id)->data(6).toPointF()).length(),0,"stessa posizione dopo riapertura");
 const auto output=qEnvironmentVariable("FORGECAD_DIMENSION_CENTER_OUTPUT");if(!output.isEmpty()){QDir().mkpath(output);e.fitSheet();QApplication::processEvents();const auto prefix=output+QStringLiteral("/quota-%1-%2").arg(int(type)).arg(legacy);require(e.grab().save(prefix+".png"),"QA tipo quota centrata");DrawingPdfReport report;ok(e.exportPdf(prefix+".pdf",report));if(!report.warnings.isEmpty())throw std::runtime_error((QStringLiteral("PDF tipo%1: ").arg(int(type))+report.warnings.join("; ")).toStdString());}
}
void drawingAllDimensionCenterTests(){
 for(auto type:{DrawingMeasure::Distance,DrawingMeasure::HorizontalDistance,DrawingMeasure::VerticalDistance,DrawingMeasure::Radius,DrawingMeasure::Diameter,DrawingMeasure::AxisRadius,DrawingMeasure::AxisDiameter,DrawingMeasure::SectionLength})drawingAllDimensionCenterCase(type);
 drawingAllDimensionCenterCase(DrawingMeasure::EndpointDistance,true);drawingAllDimensionCenterCase(DrawingMeasure::SectionLength,true);
 std::cout<<"PASS centro/snap tutte le quote:10 fixture,60 combinazioni zoom/stile, anteprima/inserimento, legacy, Alt, drag/rilascio, Undo/Redo, codec e PDF\n";
}
}
