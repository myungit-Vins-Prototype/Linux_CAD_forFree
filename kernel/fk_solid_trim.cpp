#include "fk_solid_trim.h"
#include "fk_history.h"
#include "fk_surface_algo.h"

#include <cmath>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
bool toward(const Body &face,const Vec3 &other,double tolerance) {
    const auto &f=face.face(face.faces().front());
    const auto p=projectPoint(*f.surface,other);
    const auto n=f.surface->normal(p.u,p.v)*(f.sense?1.:-1.);
    const double side=dot(other-p.point,n);
    if(std::fabs(side)<=tolerance)throw std::domain_error("Le superfici limite coincidono o non definiscono un intervallo: controllare le facce.");
    return side>0;
}
}
Body trimSolidBetweenFaces(const Body &solid,const Body &first,const Body &second,bool reverseFirst,bool reverseSecond,
                          const BooleanOptions &options,BodyHistory *history) {
    if(history)*history={};
    if(first.faces().size()!=1||second.faces().size()!=1||!first.isSheet()||!second.isSheet())
        throw std::domain_error("Scegliere esattamente due facce limite.");
    Vec3 onFirst;
    if(!faceInteriorPoint(first,first.faces().front(),onFirst))
        throw std::domain_error("Impossibile trovare un punto interno alla prima faccia limite.");
    const auto onSecond=projectPoint(*second.face(second.faces().front()).surface,onFirst).point;
    const bool sideA=toward(first,onSecond,options.tolerance)!=reverseFirst;
    const bool sideB=toward(second,onFirst,options.tolerance)!=reverseSecond;
    BodyHistory firstHistory,secondHistory;
    auto result=clipSolidByFace(solid,first,sideA,options,history?&firstHistory:nullptr);
    if(result.faces().empty())throw std::domain_error("La prima superficie elimina tutti i solidi: invertire il lato.");
    result=clipSolidByFace(result,second,sideB,options,history?&secondHistory:nullptr);
    if(result.faces().empty())throw std::domain_error("Nessun volume tra le superfici: controllare i limiti o invertire il lato.");
    if(history)*history=BodyHistory::compose(firstHistory,secondHistory,0);
    return result;
}
}
