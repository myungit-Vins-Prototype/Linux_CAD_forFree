#include "fk_solid_trim.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_revolve.h"
#include "fk_extrude.h"
#include "fk_primitives.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_history.h"
#include "fk_mass.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
void require(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
void volume(const Body &body,double expected){require(checkBody(body,{true,true}).empty(),"B-rep e loop validi");require(std::fabs(massProperties(body).volume-expected)<1e-7*std::max(1.,expected),"volume esatto conservato");}
int main(){try{
    const auto low=makePlaneSheet(Frame3({0,0,2},{0,0,1},{1,0,0}),30);
    const auto high=makePlaneSheet(Frame3({0,0,7},{0,0,1},{1,0,0}),30);
    const auto box=makeBox(Frame3(),2,2,10);
    BodyHistory h;
    auto result=trimSolidBetweenFaces(box,low,high,false,false,{},&h);volume(result,20);
    require(h.operandCount()==3,"tre operandi nella genealogia");
    for(int operand=1;operand<=2;++operand){bool found=false;for(auto f:result.faces())for(auto o:h.origins(EntityKind::Face,f.index))found=found||o.operand==operand;require(found,"chiusure provenienti dalle due facce limite");}
    volume(trimSolidBetweenFaces(box,high,low),20);
    volume(trimSolidBetweenFaces(box,low,high,true,false),8);
    const auto other=makeBox(Frame3({5,0,0},{0,0,1},{1,0,0}),2,2,10);
    const auto collection=Body::collect({&box,&other});
    result=trimSolidBetweenFaces(collection,low,high);volume(result,40);
    int solidShells=0;for(auto s:result.shells())if(result.shell(s).region!=result.exteriorRegion())++solidShells;
    require(solidShells==2,"conservate tutte le componenti disconnesse");
    const auto cavity=makeBox(Frame3({.5,.5,0},{0,0,1},{1,0,0}),1,1,10);
    const auto hollow=booleanOperation(box,cavity,BooleanOperation::Subtract);
    volume(trimSolidBetweenFaces(hollow,low,high),15);
    const auto cylinder=makeCylinder(Frame3(),4,10);
    volume(trimSolidBetweenFaces(cylinder,low,high),80*kPi);
    const auto inner=makeCylinder(Frame3({0,0,-1},{0,0,1},{1,0,0}),2,12);
    const auto outer=makeCylinder(Frame3({0,0,-1},{0,0,1},{1,0,0}),3,12);
    const auto side=[](const Body &b){for(auto f:b.faces())if(b.face(f).surface->type()==SurfaceType::Cylinder)return facesAsSheet(b,{f});throw std::runtime_error("cilindro");};
    volume(trimSolidBetweenFaces(cylinder,side(inner),side(outer)),50*kPi);
    const auto atBase=makePlaneSheet(Frame3(),30);
    volume(clipSolidByFace(box,atBase,true),40);
    require(clipSolidByFace(box,atBase,false).faces().empty(),"contatto senza volume escluso");
    const auto shortTool=makePlaneSheet(Frame3({1,1,2},{0,0,1},{1,0,0}),.2);
    bool rejected=false;try{clipSolidByFace(box,shortTool,true);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"strumento finito che non attraversa il solido rifiutato");
    BooleanOptions sequential;sequential.threads=1;BooleanOptions parallel;parallel.threads=4;
    require(writeBodyBinary(trimSolidBetweenFaces(collection,low,high,false,false,sequential))==writeBodyBinary(trimSolidBetweenFaces(collection,low,high,false,false,parallel)),"determinismo dei thread");
    std::cout<<"PASS tagli piani/cilindrici, componenti, cavita, contatti, genealogia e determinismo\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
