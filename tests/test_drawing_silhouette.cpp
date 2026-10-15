#include "cad_drawing_silhouette.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_bspline_surface.h"
#include "fk_pcurve.h"
#include "fk_classify.h"
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
DrawingSourceBody source(Body b){auto body=std::make_shared<Body>(std::move(b));return {1,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"silhouette"))};}
Body dome(bool hole=false){
 auto surface=std::make_shared<BSplineSurface>(2,2,std::vector<double>{0,0,0,1,1,1},std::vector<double>{0,0,0,1,1,1},3,3,std::vector<Vec3>{{-5,-4,0},{-5,0,4},{-5,4,0},{0,-4,0},{0,0,12},{0,4,0},{5,-4,0},{5,0,6},{5,4,0}});
 std::vector<Vec3> vertices;std::vector<Body::BuildEdge> edges;Body::BuildFace face;face.surface=surface;
 const auto rectangle=[&](double lo,double hi,bool inner){int vertex=int(vertices.size()),edge=int(edges.size());for(auto uv:{Vec2(lo,lo),Vec2(hi,lo),Vec2(hi,hi),Vec2(lo,hi)})vertices.push_back(surface->point(uv.x(),uv.y()));
  edges.push_back({vertex,vertex+1,surface->vIso(lo),{lo,hi},0});edges.push_back({vertex+1,vertex+2,surface->uIso(hi),{lo,hi},0});edges.push_back({vertex+3,vertex+2,surface->vIso(hi),{lo,hi},0});edges.push_back({vertex,vertex+3,surface->uIso(lo),{lo,hi},0});
  auto bottom=std::make_shared<Line<2>>(Vec2(0,lo),Vec2(1,0)),right=std::make_shared<Line<2>>(Vec2(hi,0),Vec2(0,1)),top=std::make_shared<Line<2>>(Vec2(0,hi),Vec2(1,0)),left=std::make_shared<Line<2>>(Vec2(lo,0),Vec2(0,1));
  if(inner)face.loops.push_back({{edge+3,true,left,0},{edge+2,true,top,0},{edge+1,false,right,0},{edge,false,bottom,0}});else face.loops.push_back({{edge,true,bottom,0},{edge+1,true,right,0},{edge+2,false,top,0},{edge+3,false,left,0}});};
 rectangle(0,1,false);if(hole)rectangle(.4,.6,true);return Body::buildSheet(vertices,edges,{face});
}
int main(){try{
 DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=Frame3(Vec3(),Vec3(0,-1,0),Vec3(1,0,0));DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(makeSphere(Frame3(),5))}};DrawingSilhouetteCache cache;DrawingSilhouetteResult contours;auto error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty(),qPrintable(error));require(!contours.paths.empty(),"silhouette della sfera");double height=-100;for(const auto &path:contours.paths)for(auto p:path.points){require(std::abs(norm(p)-5)<1e-7&&std::abs(p.y())<1e-6,"punti sulla superficie e sul contorno tangente");height=std::max(height,p.z());}require(height>4.99,"profilo superiore della sfera senza bordo topologico");
 const auto *firstMesh=cache.seeds(model.bodies[0]).get();view.scale=3;error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&cache.seeds(model.bodies[0]).get()==firstMesh,"seed riusati tra scale e viste");
 model.bodies={source(makeBox(Frame3(),10,6,4))};error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&contours.paths.empty(),"facce piane non aggiungono false silhouette");
 model.bodies={source(makeCylinder(Frame3(),3,8))};error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&!contours.paths.empty(),"generatrici apparenti del cilindro");view.plane=Frame3();error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&contours.paths.empty(),"cilindro visto lungo l'asse senza contorni degeneri");
 view.plane=Frame3(Vec3(),Vec3(0,-1,0),Vec3(1,0,0));model.bodies={source(dome())};error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&!contours.paths.empty(),"contorno interno alla superficie NURBS");height=-100;
 for(const auto &path:contours.paths)for(auto point:path.points){height=std::max(height,point.z());require(std::abs(point.y())<1e-6,"tangenza della superficie NURBS nella direzione di vista");}
 require(height>4.28&&height<4.286,"la cima interna supera i bordi di altezza 3 mm");
 model.bodies={source(dome(true))};error=drawingViewSilhouettes(model,view,contours,cache);require(error.isEmpty()&&contours.paths.size()>=2,"il foro interrompe il contorno apparente");for(const auto &path:contours.paths)for(auto point:path.points)require(classifyPointOnFace(*model.bodies[0].body,path.sourceFace,point,1e-5)!=PointLocation::Outside,"i contorni non attraversano il foro della superficie");
 const auto count=contours.paths.size();auto invalid=model;invalid.bodies.push_back(invalid.bodies[0]);require(!drawingViewSilhouettes(invalid,view,contours,cache).isEmpty()&&contours.paths.size()==count,"sorgente incoerente rifiutata atomicamente");
 std::cout<<"PASS surface silhouettes, sphere/cylinder bounds, planar degeneracy and seed cache\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
