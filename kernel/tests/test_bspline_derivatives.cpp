#include "fk_test.h"
#include "fk_bspline.h"
#include "fk_bspline_basis.h"
#include "fk_curve_length_cache.h"
#include <algorithm>
#include <stdexcept>

using namespace ForgeCad::Kernel;

namespace {
// Riferimento indipendente: differenze successive dei poli Bernstein,
// poi de Casteljau; non usa le derivate delle funzioni di base.
template<int N>
Vec<N> bezierDerivative(std::vector<Vec<N>> poles, int order, double t, double span) {
    for (int k=0;k<order;++k) {
        if (poles.size()<2) return {};
        const double factor=double(poles.size()-1)/span;
        for(std::size_t i=0;i+1<poles.size();++i) poles[i]=factor*(poles[i+1]-poles[i]);
        poles.pop_back();
    }
    while(poles.size()>1) {
        for(std::size_t i=0;i+1<poles.size();++i) poles[i]=(1-t)*poles[i]+t*poles[i+1];
        poles.pop_back();
    }
    return poles.front();
}

template<int N>
void checkTranslatedBezier() {
    for(int degree:{1,2,3,5,9}) for(double span:{1.,.125}) {
        std::vector<Vec<N>> poles;
        for(int i=0;i<=degree;++i) {
            Vec<N> p;
            for(int c=0;c<N;++c) p[c]=(c+1)*1048576.+((i*i+3*i+c*i)%23)/4096.;
            poles.push_back(p);
        }
        std::vector<double> knots(degree+1,911.);
        knots.insert(knots.end(),degree+1,911.+span);
        const BSplineCurve<N> curve(degree,knots,poles);
        for(double fraction:{-.25,0.,.03125,.25,.5,.875,1.,1.25}) {
            const double t=911.+span*fraction;
            std::vector<Vec<N>> values(degree+3);
            curve.evaluate(t,degree+2,values.data());
            for(int k=1;k<=degree+2;++k) {
                const auto expected=bezierDerivative(poles,k,fraction,span);
                FK_CHECK(norm(values[k]-expected)<=2e-12*std::max(1.,norm(expected)));
            }
            // Il cambiamento alle derivate non deve cambiare la posizione.
            std::vector<double> basis(degree+1);
            detail::basisFunctionDerivatives(knots,degree,t,degree,0,basis.data());
            Vec<N> point;
            for(int i=0;i<=degree;++i) point+=basis[i]*poles[i];
            FK_CHECK(norm(values[0]-point)==0.);
        }
    }
}

class CountedCurve final : public Curve<3> {
public:
    explicit CountedCurve(BSplineCurve<3> curve):curve_(std::move(curve)) {}
    CurveType type() const override {return CurveType::Other;}
    Interval domain() const override {return curve_.domain();}
    std::vector<double> breakpoints(const Interval &range) const override {return curve_.breakpoints(range);}
    void evaluate(double t,int order,Vec3 *out) const override {
        if(++calls>100000) throw std::runtime_error("quadratura dominata dal rumore delle derivate");
        curve_.evaluate(t,order,out);
    }
    mutable int calls=0;
private:
    BSplineCurve<3> curve_;
};
}

FK_TEST(BSplinePolynomialDerivativesRemainAccurateAfterTranslation) {
    checkTranslatedBezier<2>();
    checkTranslatedBezier<3>();
}

FK_TEST(BSplinePolynomialDerivativesPreserveOneSidedKnotLimits) {
    const std::vector<Vec3> poles{{64,32,16},{64.125,32,16},{64.25,32.125,16},{64.375,32.25,16},
                                 {64.375,32.5,16.125},{64.5,32.75,16.25},{64.75,33,16.5}};
    BSplineCurve<3> curve(3,{0,0,0,0,1,1,1,2,2,2,2},poles);
    Vec3 left[6],right[6];curve.evaluateLeft(1,5,left);curve.evaluate(1,5,right);
    for(int k=1;k<=5;++k) {
        const auto a=bezierDerivative(std::vector<Vec3>(poles.begin(),poles.begin()+4),k,1.,1.);
        const auto b=bezierDerivative(std::vector<Vec3>(poles.begin()+3,poles.end()),k,0.,1.);
        FK_CHECK(norm(left[k]-a)<1e-13);
        FK_CHECK(norm(right[k]-b)<1e-13);
    }
    FK_CHECK(norm(left[1]-right[1])>.1);
}

FK_TEST(BSplineDenseTranslatedLengthConvergesWithoutChangingTolerance) {
    // Retta rappresentata da 800 pezze cubiche: caso sintetico indipendente
    // dal CAD utente. Il vecchio valutatore supera il limite di 100k chiamate.
    const int spans=800;
    std::vector<double> knots(4,0.);
    for(int i=1;i<spans;++i) knots.insert(knots.end(),3,i);
    knots.insert(knots.end(),4,spans);
    std::vector<Vec3> poles;
    for(int i=0;i<=3*spans;++i) poles.push_back({100+i/4096.,-20+i/8192.,93-i/16384.});
    CountedCurve curve(BSplineCurve<3>(3,knots,poles));
    detail::CurveLengthCache cache;
    const double length=cache.get(curve,curve.domain(),1e-13);
    FK_CHECK_NEAR(length,distance(poles.front(),poles.back()),1e-13);
    FK_CHECK(curve.calls<120*spans);
    const int calls=curve.calls;
    FK_CHECK_NEAR(cache.get(curve,curve.domain(),1e-13),length,0.);
    FK_CHECK(curve.calls==calls);
}
