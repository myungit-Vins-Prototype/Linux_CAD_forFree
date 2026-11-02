#include "fk_sketch_projection.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace ForgeCad::Kernel;
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static CurvePtr<3> bezier(std::vector<Vec3> poles){const int n=int(poles.size());std::vector<double> knots(std::size_t(n),0);knots.insert(knots.end(),std::size_t(n),1);return std::make_shared<BSplineCurve<3>>(n-1,knots,poles);}
int main(){
    try {
        const auto a=bezier({{0,0,0},{0,2,0},{4,3,0}}),b=bezier({{0,0,1},{1,0,4},{4,0,2}});
        const auto curves=projectSketchOnSketch({{a,a->domain(),"A"}},{0,0,1},{{b,b->domain(),"B"}},{0,1,0});
        require(curves.size()==1 && isImplicitSketchProjection(*curves[0]),"fixture implicita");
        const auto curve=curves[0];const auto range=curve->domain();int cases=0;
        for(const auto &plane:std::vector<std::pair<Vec3,double>>{{{1,0,0},1},{{0,0,1},2},{{0,0,1},2.8},{{1,0,0},0},{{1,0,0},4}}) {
            const auto roots=planeRoots<3>(*curve,range,plane.first,plane.second,1e-9);
            require(!roots.parameters.empty(),"radici presenti sui piani di coordinate e tangente");
            for(double t:roots.parameters)require(std::abs(dot(plane.first,curve->point(t))-plane.second)<=1e-9,"residuo sul piano");
            ++cases;
        }
        std::mt19937 generator(8121);std::uniform_real_distribution<double> component(-1,1),parameter(.05,range.hi-.05);
        for(int i=0;i<30;++i) {
            const Vec3 normal=normalized(Vec3(component(generator),component(generator),component(generator)));
            const auto point=curve->point(parameter(generator));const double offset=dot(normal,point);
            const auto roots=planeRoots<3>(*curve,range,normal,offset,1e-9);
            double nearest=1e30;for(double t:roots.parameters) {const auto p=curve->point(t);nearest=std::min(nearest,distance(p,point));require(std::abs(dot(normal,p)-offset)<=1e-9,"residuo obliquo");}
            require(nearest<1e-6,"nessuna radice persa sul piano obliquo");++cases;
        }
        const Frame3 frame(Vec3(1,2,3),normalized(Vec3(.2,.8,.4)),Vec3(1,.1,0));
        const auto image=std::make_shared<SketchProjectionImage>(curve,range,frame);
        for(int i=0;i<=30;++i) {
            const double t=range.lo+range.length()*i/30;const auto p=frame.toLocal(curve->point(t));
            require(distance(image->point(t),Vec2(p.x(),p.y()))<1e-12,"immagine affine esatta");
        }
        const double t=.7;const auto p=image->point(t);
        const auto roots2=planeRoots<2>(*image,range,Vec2(.8,.6),dot(Vec2(.8,.6),p),1e-9);
        double nearest=1e30;for(double u:roots2.parameters)nearest=std::min(nearest,distance(image->point(u),p));
        require(nearest<1e-7,"radici esatte di retta nell'immagine2D");++cases;
        const auto embedded=embedCurve(image,frame);
        const auto coincident=planeRoots<3>(*embedded,range,frame.zDir(),dot(frame.zDir(),frame.origin()),1e-9);
        require(!coincident.coincident.empty() && coincident.parameters.empty(),"riferimento immerso complanare");++cases;
        const auto absent=planeRoots<3>(*curve,range,Vec3(1,0,0),20,1e-9);
        require(absent.parameters.empty() && absent.coincident.empty(),"piano senza contatto");++cases;
        const auto x0=bezier({{0,0,0},{4,0,0}}),x1=bezier({{0,10,0},{4,10,0}}),z=bezier({{0,0,0},{4,0,4}});
        std::vector<std::string> keys1,keys2;
        const auto first=projectSketchOnSketch({{x0,x0->domain(),"seg0"},{x1,x1->domain(),"seg1"}},{0,0,1},{{z,z->domain(),"other"}},{0,1,0},&keys1);
        const auto second=projectSketchOnSketch({{x1,x1->domain(),"seg1"},{x0,x0->domain(),"seg0"}},{0,0,1},{{z,z->domain(),"other"}},{0,1,0},&keys2);
        require(first.size()==2 && second.size()==2 && keys1[0]==keys2[1] && keys1[1]==keys2[0],"genealogia dei rami indipendente dall'ordine input");++cases;
        std::cout<<"PASS "<<cases<<" casi kernel riferimenti esatti, piano, immagine2D e genealogia\n";return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
