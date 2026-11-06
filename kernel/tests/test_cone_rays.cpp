#include "fk_boolean.h"
#include "fk_body_check.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
ProfileSegment segment(Vec2 a, Vec2 b) { return {std::make_shared<Line<2>>(a,b-a),{0,distance(a,b)}}; }
Body cappedCylinder(double slope) {
    std::vector<Vec2> p{{0,0},{50,50*slope},{50,100-50*slope},{0,100}};
    std::vector<ProfileSegment> segments;
    for (int i=0;i<4;++i) segments.push_back(segment(p[i],p[(i+1)%4]));
    return makeRevolution(Frame3(),buildProfile(segments,1e-9).regions.front());
}
int main() { try {
    // Due falde distanti meno della tolleranza devono conservare entrambe
    // le radici: e' il contorno della faccia a decidere quale appartiene al solido.
    for (double angle : {1e-9,-1e-9,0.4,-0.4,kHalfPi-1e-10,-kHalfPi+1e-10})
        for (const Frame3 &frame : {Frame3(),Frame3({120,-33,200},{1,2,3},{2,-1,0})}) {
            ConicalSurface cone(frame,angle,5);
            const auto point=cone.point(1.8,2.3), direction=normalized(frame.directionToGlobal({.2,.3,1}));
            const auto origin=point-.7*direction;
            bool grazing=false; const auto hits=intersectLineSurface(origin,direction,cone,grazing,1e-7);
            bool found=false;
            for (double t:hits) {
                found=found||std::fabs(t-.7)<1e-8;
                require(projectPoint(cone,origin+t*direction).distance<1e-7,"radice fuori dalla superficie conica");
            }
            require(found&&!grazing,"radice trasversale persa su cono quasi piano");
        }
    // Casi senza radici, tangenza e generatrice coincidente.
    ConicalSurface regular(Frame3(),kPi/4,0);
    bool grazing=false;
    require(intersectLineSurface({2,0,1},{0,1,0},regular,grazing,1e-7).empty()&&!grazing,"retta esterna al cono");
    intersectLineSurface({1,0,1},{0,1,0},regular,grazing,1e-7);
    require(grazing,"tangenza al cono non riconosciuta");
    intersectLineSurface({1,0,1},normalized(Vec3(1,0,1)),regular,grazing,1e-7);
    require(grazing,"generatrice coincidente non riconosciuta");
    for(double slope : {2e-10,-2e-10}) {
        const auto base=cappedCylinder(slope);
        require(checkBody(base,{true,true}).empty(),"fixture conica non valida");
        const SolidClassifier classifier(base,1e-6), prepared(base,1e-6,true);
        for(int angle=0;angle<23;++angle) for(double z:{10.,50.,90.}) for(double radius:{49.,51.}) {
            const double t=kTwoPi*angle/23.;const Vec3 p(radius*std::cos(t),radius*std::sin(t),z);
            const auto expected=radius<50?PointLocation::Inside:PointLocation::Outside;
            require(classifier.classify(p)==expected&&prepared.classify(p)==expected,"parita errata con chiusure coniche quasi piane");
        }
        const auto tool=makeBox(Frame3({49,-2,60},{0,0,1},{1,0,0}),3,4,5);
        const double a=massProperties(base).volume,b=massProperties(tool).volume;
        double volume=-1;
        for(bool reversed:{false,true})for(int threads:{1,4}) {
            BooleanOptions options;options.threads=threads;
            const auto united=booleanOperation(reversed?tool:base,reversed?base:tool,BooleanOperation::Unite,options);
            require(united.shells().size()==1&&checkBody(united,{true,true}).empty(),"unione con chiusure coniche non valida");
            const double v=massProperties(united).volume;
            require(v>a&&v<a+b,"volume unione fuori intervallo");
            if(volume>=0)require(std::fabs(v-volume)<1e-7,"unione dipende da ordine/thread");
            volume=v;
        }
    }
    std::cout<<"PASS coni quasi piani: raggi, falde, tangenza, classificazione e unione\n";return 0;
} catch(const std::exception &error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;} }
