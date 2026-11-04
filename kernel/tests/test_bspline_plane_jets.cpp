#include "fk_bspline_surface.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
}
// Un piano X=costante resta un piano anche con grado elevato, pesi
// razionali e intervalli UV stretti: tutti i jet X sono esattamente nulli.
int main(){try{
    for(int degree:{3,34})for(bool rational:{false,true})for(double span:{1.,1e-8}) {
        std::vector<double> u(degree+1,1),v(6,0);u.insert(u.end(),degree+1,1+span);v.insert(v.end(),6,1);
        std::vector<Vec3> poles;std::vector<double> weights;
        for(int i=0;i<=degree;++i)for(int j=0;j<=5;++j){
            poles.push_back({200000.,20.*i/degree,6.*j});
            if(rational)weights.push_back(1+.2*std::sin(double(i+j)));
        }
        BSplineSurface surface(degree,5,u,v,degree+1,6,poles,weights);
        for(double a:{.125,.5,.875})for(double b:{.2,.6,.9})for(int order=0;order<=3;++order){
            Vec3 jet[16];surface.evaluate(1+a*span,b,order,jet);
            require(jet[0].x()==200000.,"constant plane coordinate retained");
            for(int k=0;k<=order;++k)for(int l=0;k+l<=order;++l){
                const auto value=jet[Surface::derivativeIndex(k,l,order)];
                require(isFinite(value),"finite surface jets");
                if(k+l)require(value.x()==0.,"constant plane coordinate has zero derivatives");
            }
            const auto normal=surface.normal(1+a*span,b);
            require(norm(cross(normal,Vec3(1,0,0)))==0.,"plane normal does not depend on world coordinates or degree");
        }
    }
    std::cout<<"PASS B-spline plane jets, degrees 3/34, rational weights and narrow UV\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
