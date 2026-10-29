#include "cad_drawing_preparation.h"
#include "fk_primitives.h"
#include "fk_parallel.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
void ok(QString error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
DrawingSourceBody source(quint64 id,Body value){auto body=std::make_shared<Body>(std::move(value));return {id,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"preparation"+std::to_string(id)))};}
QByteArray state(const DrawingSheet &sheet){QByteArray bytes;ok(encodeDrawingSheet(sheet,bytes));return bytes;}
QByteArray result(const PreparedDrawingSheet &prepared){
 QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream<<state(prepared.sheet);
 for(const auto &view:prepared.sheet.drawing.views){
  const auto &curves=prepared.curves.at(view.id);stream<<view.id<<quint64(curves.size());
  for(const auto &curve:curves){stream<<int(curve.kind)<<quint64(curve.featureId)<<QString::fromStdString(curve.faceName)<<QString::fromStdString(curve.edgeName)<<curve.range.lo<<curve.range.hi<<curve.coincident<<curve.multipleBranches<<curve.tangent;
   for(int i=0;i<=8;++i){const auto p=curve.curve->point(curve.range.lo+curve.range.length()*i/8);stream<<p.x()<<p.y()<<p.z();}}
  const auto sections=prepared.sections.find(view.id);stream<<quint64(sections==prepared.sections.end()?0:sections->second.size());
  const auto &contours=prepared.silhouettes.at(view.id);stream<<contours.warnings<<quint64(contours.paths.size());for(const auto &path:contours.paths){stream<<quint64(path.featureId)<<path.refinementLimited<<quint64(path.points.size());for(const auto &p:path.points)stream<<p.x()<<p.y()<<p.z();}
  const auto &v=*prepared.visibility.at(view.id);for(const auto *paths:{&v.curves,&v.silhouettes}){stream<<quint64(paths->size());for(const auto &p:*paths)stream<<p.visible<<p.hidden;}
  stream<<v.sectionHatch<<v.sectionMaterial<<v.visibleCylinderAxes<<v.allCylinderAxes<<v.shadedImage<<v.shadedBounds<<v.meshPrepared<<v.visibleCenters.axes<<v.visibleCenters.crosses<<v.allCenters.axes<<v.allCenters.crosses<<v.warnings<<v.breakMarks;
 }
 for(const auto &[id,d]:prepared.dimensions)stream<<id<<int(d.status)<<d.message<<d.value<<int(d.geometry)<<d.first.x()<<d.first.y()<<d.second.x()<<d.second.y()<<d.vertex.x()<<d.vertex.y()<<d.firstDirection.x()<<d.firstDirection.y()<<d.secondDirection.x()<<d.secondDirection.y()<<d.diametral<<d.sweep;
 return bytes;
}
void checks(){
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};
 model.snapshot.bodies={source(1,makeCylinder(Frame3(),3,8)),source(2,makeBox(Frame3(Vec3(1,0,1),Vec3(0,0,1),Vec3(1,0,0)),3,5,6))};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;
 for(int i=0;i<11;++i){DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=i<3?1:4;
  if(i==4||i==5||i==6){v.kind=DrawingViewKind::Section;v.sectionStyle=i==6?DrawingSectionStyle::Removed:DrawingSectionStyle::Full;v.plane=Frame3(Vec3(0,0,4),Vec3(0,0,1),Vec3(1,0,0));}
  DrawingViewLayout layout{v.id,QStringLiteral("Vista %1").arg(i),{30.+20*(i%5),40.+50*(i/5)},{quint64(i<8?1:2)}};layout.showMesh=i==7;
  if(i==10)layout.features={1,2};
  sheet.drawing.views.push_back(v);sheet.views.append(layout);
 }
 const auto original=state(sheet);auto serial=prepareDrawingSheet(sheet,model,[](int){},1);
 const auto &root=sheet.drawing.views.front();const auto &curves=serial.curves.at(root.id);auto circle=std::find_if(curves.begin(),curves.end(),[&](const auto &c){return drawingPrimitive(root,c).circular;});require(circle!=curves.end(),"cerchio analitico della base assente");
 auto dimension=drawingDimensionBetween(root,model.snapshot,curves,{drawingAttachment(root,*circle,model.snapshot,DrawingPointRole::Curve)},DrawingMeasure::Radius,{6,6});sheet.drawing.dimensions.push_back(dimension);sheet.dimensions.append({dimension.id,{50,20}});
 serial=prepareDrawingSheet(sheet,model,[](int){},1);const auto expected=result(serial);require(serial.dimensions.at(dimension.id).status==DrawingDimensionStatus::Ready&&std::abs(serial.dimensions.at(dimension.id).value-3)<1e-10,"raggio analitico diverso da3mm");
 require(serial.visibility.at(sheet.drawing.views[7].id)->meshPrepared,"mesh ombreggiata assente");
 const auto input=state(sheet);
 for(int threads:{0,2,4,8}){int previous=-1;std::atomic<int> callbacks{0};
  auto prepared=prepareDrawingSheet(sheet,model,[&](int phase){require(callbacks.fetch_add(1)==0,"callback concorrenti");require(phase>=previous&&phase>=0&&phase<=5,"avanzamento non monotono");previous=phase;callbacks.fetch_sub(1);},threads);
  require(previous==5&&result(prepared)==expected,"geometria/tracciati/quote diversi fra seriale e parallelo");
  for(const auto &body:model.snapshot.bodies){const auto a=serial.cache.seeds(body),b=prepared.cache.seeds(body);require(a->faces.size()==b->faces.size(),"cache finale diversa");for(std::size_t i=0;i<a->faces.size();++i)require(a->faces[i].points.size()==b->faces[i].points.size()&&a->faces[i].triangles==b->faces[i].triangles,"raffinamento finale diverso");}
 }
 require(state(sheet)==input,"foglio di ingresso modificato");
 DrawingSilhouetteCache cache;require(!cache.canReuse(model.snapshot.bodies.front(),1),"cache vuota riusabile");const auto coarse=cache.seeds(model.snapshot.bodies.front());require(cache.canReuse(model.snapshot.bodies.front(),1)&&!cache.canReuse(model.snapshot.bodies.front(),1e-9),"query cache non conservativa");const auto copied=cache;cache.seeds(model.snapshot.bodies.front(),1e-3);require(copied.canReuse(model.snapshot.bodies.front(),1)&&!copied.canReuse(model.snapshot.bodies.front(),1e-3),"raffinamento modifica la copia");(void)coarse;
 for(int cancelPhase:{-1,1,3}){auto token=std::make_shared<std::atomic<bool>>(cancelPhase<0);bool cancelled=false;CancellationScope scope(token);auto unchanged=serial;
  try{unchanged=prepareDrawingSheet(sheet,model,[&](int phase){if(phase>=cancelPhase)token->store(true);},4);}catch(const OperationCancelled &){cancelled=true;}
  token->store(false);require(cancelled&&result(unchanged)==expected&&state(sheet)==input,"annullamento pubblica un risultato parziale");
 }
 bool failed=false;auto unchanged=serial;try{unchanged=prepareDrawingSheet(sheet,model,[](int phase){if(phase>=1)throw std::runtime_error("errore nel worker");},4);}catch(const std::runtime_error &){failed=true;}require(failed&&result(unchanged)==expected,"errore pubblica un risultato parziale");
 auto a=std::async(std::launch::async,[&]{return result(prepareDrawingSheet(sheet,model,[](int){},2));});auto b=std::async(std::launch::async,[&]{return result(prepareDrawingSheet(sheet,model,[](int){},2));});require(a.get()==expected&&b.get()==expected,"richieste concorrenti divergenti");
 parallelFor(2,2,[&](std::size_t){require(result(prepareDrawingSheet(sheet,model,[](int){},8))==expected,"parallelismo annidato divergente");});
 DrawingSheet empty;empty.drawing.model=model.snapshot.version;require(prepareDrawingSheet(empty,model,[](int){},4).curves.empty(),"foglio vuoto");
 auto unavailable=model;unavailable.snapshot.bodies.clear();require(prepareDrawingSheet(sheet,unavailable,[](int){},4).visibility.size()==sheet.drawing.views.size(),"modello non disponibile");
 require(!original.isEmpty(),"fixture vuota");
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{checks();std::cout<<"PASS preparazione viste: equivalenza, cache, quote, cancellazione e concorrenza\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}catch(const OperationCancelled &){return 2;}return 0;}
