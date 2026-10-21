#include "cad_shape_analysis.h"
#include "fk_primitives.h"
#include "fk_extrude.h"
#include "fk_tessellate.h"
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
static void require(bool ok, const char *message) { if(!ok) throw std::runtime_error(message); }
static bool near(double a,double b) { return std::abs(a-b)<1e-9; }
// z = u*v: curvatura gaussiana negativa nell'origine.
class Saddle final : public Surface {
public:
    SurfaceType type() const override { return SurfaceType::BSpline; }
    Interval uDomain() const override { return {-1,1}; }
    Interval vDomain() const override { return {-1,1}; }
    void evaluate(double u,double v,int order,Vec3 *out) const override {
        for(int i=0;i<(order+1)*(order+1);++i) out[i]={};
        out[0]={u,v,u*v};
        if(order>0) { out[derivativeIndex(1,0,order)]={1,0,v}; out[derivativeIndex(0,1,order)]={0,1,u}; }
        if(order>1) out[derivativeIndex(1,1,order)]={0,0,1};
    }
};
// Curva piana con flesso nell'origine: (t,t^3,0).
class Inflection final : public Curve<3> {
public:
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return {-1,1}; }
    void evaluate(double t,int order,Vec3 *out) const override {
        out[0]={t,t*t*t,0}; if(order>0) out[1]={1,3*t*t,0}; if(order>1) out[2]={0,6*t,0};
    }
};
int main() {
    try {
        SurfaceCurvature k;
        Plane plane(Frame3{});
        require(surfaceCurvature(plane,0,0,k) && near(k.minimum,0) && near(k.maximum,0),"plane");
        Saddle saddle;
        require(surfaceCurvature(saddle,0,0,k) && near(k.minimum,-1) && near(k.maximum,1),"saddle principal curvature");
        CylindricalSurface cylinder(Frame3{},10);
        require(surfaceCurvature(cylinder,.6,2,k) && near(k.minimum,-.1) && near(k.maximum,0),"cylinder");
        SphericalSurface sphere(Frame3{},5);
        require(surfaceCurvature(sphere,.6,.3,k) && near(k.minimum,-.2) && near(k.maximum,-.2),"sphere");
        double tensorNorm=0; for(auto &row:k.tensor) for(double v:row) tensorNorm+=v*v;
        require(near(tensorNorm,.08),"world tensor rotation invariance");
        ConicalSurface cone(Frame3{},.4,0);
        require(!surfaceCurvature(cone,0,0,k),"singular cone");
        Circle<3> circle(Vec3{},Vec3(1,0,0),Vec3(0,1,0),10);
        Vec3 p,c;
        require(curveCurvature(circle,0,p,c) && near(norm(c),.1) && c.x()<0,"circle curvature");
        Line<3> line(Vec3{},Vec3(1,0,0));
        require(curveCurvature(line,1,p,c) && near(norm(c),0),"line curvature");
        Inflection inflection;
        require(curveCurvature(inflection,0,p,c) && near(norm(c),0),"zero at inflection");
        require(curveCurvature(inflection,-.2,p,c) && c.y()<0,"left of inflection");
        require(curveCurvature(inflection,.2,p,c) && c.y()>0,"right of inflection");
        ExtrusionObject object;
        object.curve=std::make_shared<Circle<3>>(circle);
        auto comb=analyzeShape(object,4,100,.1,.001);
        require(!comb.lines.empty(),"comb");
        require(near((comb.lines[1].position-comb.lines[0].position).length(),10),"comb scale");
        const auto automatic=analyzeShape(object,4,0,.1,.001);
        require((automatic.lines[1].position-automatic.lines[0].position).length()<5,"automatic comb bounded");
        require(automatic.lines[1].position.length()>automatic.lines[0].position.length(),"comb outside bend");
        object.curve.reset(); object.forgeBody=std::make_shared<Body>(makeBox(Frame3{},10,20,30));
        auto joins=analyzeShape(object,3,1,.1,.001);
        require(joins.borders.size()==12,"box edges");
        for(const auto &b:joins.borders) require(b.color.x()>.9 && b.color.y()<.2,"box sharp boundaries");
        const auto box=object.forgeBody;
        ProfileLoop chain;
        chain.segments.push_back({std::make_shared<Line<2>>(Vec2(0,0),Vec2(1,0)),{0,10}});
        chain.segments.push_back({std::make_shared<Circle<2>>(Vec2(10,10),Vec2(1,0),Vec2(0,1),10),{-kHalfPi,0}});
        object.forgeBody=std::make_shared<Body>(makeSheetExtrusion(Frame3{}, {chain},5));
        joins=analyzeShape(object,3,1,.1,.001);
        int amber=0,gray=0;
        for(const auto &border:joins.borders) { amber+=border.color.x()==1 && border.color.y()>.6; gray+=border.color.x()==.5f; }
        require(amber==1 && gray==6,"tangent plane-cylinder is curvature jump, free borders gray");
        chain.segments[1]={std::make_shared<Line<2>>(Vec2(10,0),Vec2(1,0)),{0,10}};
        object.forgeBody=std::make_shared<Body>(makeSheetExtrusion(Frame3{}, {chain},5));
        joins=analyzeShape(object,3,1,.1,.001);
        int green=0;
        for(const auto &border:joins.borders) green+=border.color.y()>.85f;
        require(green==1,"coplanar joined faces green");
        object.forgeBody=box;
        Body smooth=*object.forgeBody;
        for(auto id:smooth.faces()) smooth.face(id).surface=std::make_shared<Plane>(Frame3{});
        object.forgeBody=std::make_shared<Body>(smooth);
        object.display.vertices={{0,0,0},{1,0,0},{0,1,0}};
        object.display.triangleFaces={smooth.faces().front().index};
        auto map=analyzeShape(object,1,.1,.1,.001);
        require(map.triangles.size()==3 && map.triangles[0].color.z()==1,"plane curvature map");
        object.display.triangleFaces={-1};
        map=analyzeShape(object,1,.1,.1,.001);
        require(map.triangles[0].color.x()==.5f,"unknown face gray");
        // Curvatura normale della superficie e curvatura della curva UV non coincidono sempre.
        require(surfaceIsoCurvature(cylinder,.3,2,true,true,p,c) && near(norm(c),.1),"cylinder U direction normal curvature");
        require(surfaceIsoCurvature(cylinder,.3,2,false,true,p,c) && near(norm(c),0),"cylinder V direction straight");
        require(surfaceIsoCurvature(sphere,.3,kPi/3,true,true,p,c) && near(norm(c),.2),"sphere normal curvature constant");
        require(surfaceIsoCurvature(sphere,.3,kPi/3,true,false,p,c) && near(norm(c),.4),"sphere latitude curve curvature");
        require(!surfaceIsoCurvature(cone,0,0,true,true,p,c),"singular surface comb omitted");
        object.forgeBody=std::make_shared<Body>(makeCylinder(Frame3{},10,8));
        SurfaceCombOptions options; options.uLines=3; options.vLines=4; options.samples=12;
        const auto grid=analyzeSurfaceComb(object,0,options);
        require(!grid.grid.empty() && !grid.lines.empty(),"surface UV grid and teeth");
        for(const auto &vertex:grid.lines) require(std::isfinite(vertex.position.x()) && std::isfinite(vertex.position.y()) && std::isfinite(vertex.position.z()),"finite surface comb");
        for(qsizetype i=0;i+1<grid.grid.size();i+=2)
            require((grid.grid[i+1].position-grid.grid[i].position).length()>1e-6f,"no collapsed grid intervals at periodic seam");
        options.uLines=0; options.vLines=1; options.samples=32;
        const auto singleRing=analyzeSurfaceComb(object,0,options);
        int ringSegments=0;
        for(qsizetype i=0;i+1<singleRing.grid.size();i+=2) {
            const auto a=singleRing.grid[i].position;
            if(a.z()>1e-5 && a.z()<8-1e-5) ++ringSegments;
        }
        require(ringSegments>=30 && ringSegments<=40,"uniform tooth density across periodic pieces");
        options.uLines=0; options.vLines=0;
        const auto empty=analyzeSurfaceComb(object,0,options);
        require(empty.grid.empty() && empty.lines.empty(),"both UV families disabled");
        options.uLines=5; options.vLines=5;
        object.forgeBody=std::make_shared<Body>(makePrism(Frame3{},{{0,0},{10,0},{10,10},{0,10}},
            {{{3,3},{7,3},{7,7},{3,7}}},2));
        const auto perforated=analyzeSurfaceComb(object,1,options);
        require(!perforated.grid.empty(),"trimmed face grid");
        int capSegments=0;
        for(qsizetype i=0;i+1<perforated.grid.size();i+=2) {
            const auto a=perforated.grid[i].position, b=perforated.grid[i+1].position;
            if(std::abs(a.z()-2)<1e-5 && std::abs(b.z()-2)<1e-5) {
                ++capSegments; const auto mid=(a+b)*.5f;
                require(!(mid.x()>3.00001 && mid.x()<6.99999 && mid.y()>3.00001 && mid.y()<6.99999),"UV grid does not bridge hole");
            }
        }
        require(capSegments>0,"grid on cap with hole");
        object.forgeBody=std::make_shared<Body>(makeCylinder(Frame3{},10,8));
        object.display={};
        int cap=-1;
        for(const auto &fm:tessellate(*object.forgeBody,{}).faces) {
            if(object.forgeBody->face(fm.face).surface->type()==SurfaceType::Plane) cap=fm.face.index;
            for(const auto &tri:fm.triangles) {
                for(int index:tri) {
                    const auto p=fm.points[index],n=fm.normals[index];
                    object.display.vertices.append({float(p.x()),float(p.y()),float(p.z())});
                    object.display.normals.append({float(n.x()),float(n.y()),float(n.z())});
                }
                object.display.triangleFaces.append(fm.face.index);
            }
        }
        require(cap>=0,"cap face available");
        const auto faceDisplay=analysisFaceDisplay(object.display,cap);
        require(!faceDisplay.vertices.empty() && faceDisplay.vertices.size()<object.display.vertices.size(),"single face display filtering");
        const auto faceMap=analyzeShape(object,1,.1,.1,.001,cap);
        require(faceMap.triangles.size()==faceDisplay.vertices.size(),"curvature map face filtering");
        const auto faceJoins=analyzeShape(object,3,1,.1,.001,cap);
        require(faceJoins.borders.size()==1 && faceJoins.borders[0].color.x()==1,"single cap edge evaluated against adjacent cylinder");
        const auto faceComb=analyzeShape(object,4,1,.1,.001,cap);
        require(!faceComb.lines.empty(),"face boundary comb");
        options.faceId=cap; options.uLines=3; options.vLines=3;
        const auto faceGrid=analyzeSurfaceComb(object,1,options);
        require(!faceGrid.grid.empty(),"single face UV grid");
        const auto surface=object.forgeBody->face(FaceId(cap)).surface;
        const Vec3 origin=surface->point(0,0),normal=surface->normal(0,0);
        for(const auto &v:faceGrid.grid) require(std::abs(dot(Vec3(v.position.x(),v.position.y(),v.position.z())-origin,normal))<1e-5,"UV points confined to selected cap");
        bool rejected=false;
        try { analyzeShape(object,1,1,.1,.001,999999); } catch(const std::invalid_argument &) { rejected=true; }
        require(rejected,"invalid face rejected");
        std::cout<<"Shape analysis: PASS\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
