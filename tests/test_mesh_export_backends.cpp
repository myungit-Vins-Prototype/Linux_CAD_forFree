// Export reali STL/OBJ: lettura indipendente dei file, CPU/CUDA e fallback.
#include "cad_export.h"
#include "cad_cuda_tessellation.h"
#include "fk_body_io.h"
#include "kernel/tests/surface_tools_fixture.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;

namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
struct Mesh {
    std::vector<Vec3> points, normals;
    std::vector<std::vector<std::pair<int,int>>> polygons;
};
Vec3 readVector(QDataStream &stream) {
    float x, y, z;
    stream >> x >> y >> z;
    return Vec3(x,y,z);
}
void validate(const Mesh &mesh,const char *format) {
    require(!mesh.points.empty() && !mesh.polygons.empty(), "file mesh vuoto");
    for (const auto &p:mesh.points) require(isFinite(p), "coordinate esportate non finite");
    for (const auto &n:mesh.normals)
        require(isFinite(n) && std::abs(norm(n)-1)<2e-6, "normale esportata non unitaria");
    for (const auto &polygon:mesh.polygons) {
        require(polygon.size()==3 || polygon.size()==4, "poligono STL/OBJ non valido");
        for (const auto &[v,n]:polygon)
            require(v>=0 && std::size_t(v)<mesh.points.size() && n>=0 && std::size_t(n)<mesh.normals.size(), "indice OBJ fuori intervallo");
        for (std::size_t i=0;i<polygon.size();++i)
            for (std::size_t j=i+1;j<polygon.size();++j) {
                const auto &p=mesh.points[polygon[i].first], &q=mesh.points[polygon[j].first];
                require(p[0]!=q[0] || p[1]!=q[1] || p[2]!=q[2], "vertici esportati coincidenti nello stesso poligono");
            }
        const auto &a=mesh.points[polygon[0].first], &b=mesh.points[polygon[1].first], &c=mesh.points[polygon[2].first];
        const Vec3 oriented=cross(b-a,c-a);
        if(!(norm(oriented)>0)) {
            std::fprintf(stderr,"%s area nulla, vertici=%zu\n",format,polygon.size());
            require(false,"poligono esportato degenere");
        }
        if(dot(oriented,mesh.normals[polygon[0].second])<=0) {
            std::fprintf(stderr,"%s orientamento: area=%.12g dot=%.12g normale=(%.9g %.9g %.9g)\n",format,norm(oriented),
                dot(oriented,mesh.normals[polygon[0].second]),mesh.normals[polygon[0].second][0],mesh.normals[polygon[0].second][1],mesh.normals[polygon[0].second][2]);
            for(const auto &[v,n]:polygon)std::fprintf(stderr,"v%d=(%.12g %.12g %.12g) n%d\n",v,mesh.points[v][0],mesh.points[v][1],mesh.points[v][2],n);
            require(false,"orientamento e normale discordi");
        }
    }
}
Mesh readStl(const QByteArray &data, quint64 expected) {
    require(data.size()>=84,"STL troncato");
    const auto count=qFromLittleEndian<quint32>(data.constData()+80);
    require(count==expected && data.size()==84+50*qint64(count), "conteggio o lunghezza STL errati");
    QDataStream stream(data.mid(84));
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    Mesh mesh;
    for (quint32 i=0;i<count;++i) {
        mesh.normals.push_back(readVector(stream));
        std::vector<std::pair<int,int>> polygon;
        for (int k=0;k<3;++k) {
            polygon.emplace_back(int(mesh.points.size()),int(i));
            mesh.points.push_back(readVector(stream));
        }
        quint16 attribute=0;stream>>attribute;
        require(attribute==0,"attributo STL inatteso");
        mesh.polygons.push_back(std::move(polygon));
    }
    require(stream.status()==QDataStream::Ok && stream.atEnd(),"STL incompleto");
    validate(mesh,"STL");
    return mesh;
}
Mesh readObj(const QByteArray &data, quint64 quads, quint64 triangles) {
    Mesh mesh;std::istringstream input(data.toStdString());std::string line;
    quint64 actualQuads=0,actualTriangles=0;
    while (std::getline(input,line)) {
        std::istringstream row(line);std::string type;row>>type;
        if(type=="v" || type=="vn") {
            double x,y,z;require(bool(row>>x>>y>>z),"coordinata OBJ incompleta");
            (type=="v"?mesh.points:mesh.normals).emplace_back(x,y,z);
        } else if(type=="f") {
            std::vector<std::pair<int,int>> polygon;std::string token;
            while(row>>token) {
                const auto separator=token.find("//");
                require(separator!=std::string::npos,"riferimento normale OBJ assente");
                polygon.emplace_back(std::stoi(token.substr(0,separator))-1,std::stoi(token.substr(separator+2))-1);
            }
            if(polygon.size()==4)++actualQuads;else if(polygon.size()==3)++actualTriangles;
            mesh.polygons.push_back(std::move(polygon));
        }
    }
    require(actualQuads==quads && actualTriangles==triangles,"conteggio OBJ errato");
    validate(mesh,"OBJ");return mesh;
}
// Confronto simmetrico dei campioni geometrici, indipendente dall'ordine e
// dall'accoppiamento dei triangoli in quad. Celle adiacenti coprono i bordi.
double nearestDifference(const std::vector<Vec3> &a,const std::vector<Vec3> &b,double tolerance) {
    using Cell=std::array<long long,3>;
    const auto cell=[&](const Vec3 &p){return Cell{std::llround(p[0]/tolerance),std::llround(p[1]/tolerance),std::llround(p[2]/tolerance)};};
    std::map<Cell,std::vector<Vec3>> bins;
    for(const auto &p:b)bins[cell(p)].push_back(p);
    double worst=0;
    for(const auto &p:a) {
        const auto c=cell(p);double nearest=std::numeric_limits<double>::infinity();
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
            const auto it=bins.find({c[0]+x,c[1]+y,c[2]+z});
            if(it!=bins.end())for(const auto &q:it->second)nearest=std::min(nearest,distance(p,q));
        }
        require(nearest<=tolerance,"campioni CPU/CUDA non equivalenti entro la precisione del formato");
        worst=std::max(worst,nearest);
    }
    return worst;
}
void compare(const Mesh &a,const Mesh &b,double tolerance,const char *format) {
    require(a.polygons.size()==b.polygons.size(),"numero poligoni CPU/CUDA diverso nel fixture");
    const double points=std::max(nearestDifference(a.points,b.points,tolerance),nearestDifference(b.points,a.points,tolerance));
    const double normals=std::max(nearestDifference(a.normals,b.normals,2e-6),nearestDifference(b.normals,a.normals,2e-6));
    std::printf("%s: poligoni=%zu scarto_punti=%.9g scarto_normali=%.9g\n",format,a.polygons.size(),points,normals);
}
// La proiezione XY del fixture e' regolare: verifica un campione coperto e
// il centro del trim, senza usare il tessellatore come oracolo del parser.
bool coversXY(const Mesh &mesh,const Vec3 &point) {
    const auto side=[](const Vec3 &a,const Vec3 &b,const Vec3 &p){return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]);};
    for(const auto &polygon:mesh.polygons)for(std::size_t i=1;i+1<polygon.size();++i) {
        const auto &a=mesh.points[polygon[0].first],&b=mesh.points[polygon[i].first],&c=mesh.points[polygon[i+1].first];
        const auto area=side(a,b,c);if(area==0)continue;
        if(side(a,b,point)/area>=-1e-10 && side(b,c,point)/area>=-1e-10 && side(c,a,point)/area>=-1e-10)return true;
    }
    return false;
}
void validateTrim(const Mesh &mesh,const Surface &surface,bool hole) {
    require(coversXY(mesh,surface.point(.1,.1)),"campione della patch omesso nell'export");
    require(coversXY(mesh,surface.point(.5,.5))!=hole,"foro NURBS non conservato nell'export");
}
QByteArray readFile(const QString &path) {
    QFile file(path);require(file.open(QIODevice::ReadOnly),"rilettura export fallita");return file.readAll();
}
void runCase(bool rational, bool withGpu, const QString &directory) {
    auto source=surface_fixture::patch(rational); // foro nel caso razionale
    source=editSheetPoles(source,{{1,1,Vec3(0,0,2),rational?1.3:1.0},
        {1,2,Vec3(0,0,-1),1.0},{2,1,Vec3(0,0,1),1.0},{2,2,Vec3(0,0,3),rational?1.5:1.0}});
    auto &face=source.face(source.faces().front());
    const auto *spline=dynamic_cast<const BSplineSurface *>(face.surface.get());
    require(spline,"fixture B-spline");
    if(!rational) {
        // editSheetPoles conserva pesi unitari espliciti; esercita anche la
        // rappresentazione polinomiale senza array dei pesi.
        std::vector<Vec3> poles;
        for(int i=0;i<spline->uPoleCount();++i)for(int j=0;j<spline->vPoleCount();++j)poles.push_back(spline->pole(i,j));
        face.surface=std::make_shared<BSplineSurface>(spline->uDegree(),spline->vDegree(),spline->uKnots(),spline->vKnots(),
            spline->uPoleCount(),spline->vPoleCount(),std::move(poles));
        spline=dynamic_cast<const BSplineSurface *>(face.surface.get());
    }
    require(spline && spline->isRational()==rational,"fixture B-spline polinomiale/razionale");
    const auto original=writeBodyBinary(source);
    const QVector<ExportBody> bodies{{QStringLiteral("Patch NURBS"),std::make_shared<const Body>(source),{},QColor()}};
    StlExportOptions options;options.maxEdgeLength=0;options.deflection=.001;options.angle=5;
    setCudaTessellationEnabled(false);
    const auto beforeCpu=cudaTessellationPoints();
    const auto cpuStl=buildBinaryStl(bodies,options);const auto cpuObj=buildQuadObj(bodies,options);
    require(cpuStl.error.isEmpty() && cpuObj.error.isEmpty(),"export CPU non riuscito");
    require(cudaTessellationPoints()==beforeCpu,"CPU usa solo CPU");
    const auto roundTrip=[&](const StlBuildResult &stl,const ObjBuildResult &obj,const QString &label) {
        const auto stlPath=directory+label+QStringLiteral(".stl"),objPath=directory+label+QStringLiteral(".obj");
        require(saveBinaryStl(stlPath,stl.data).isEmpty() && saveQuadObj(objPath,obj.data).isEmpty(),"salvataggio export fallito");
        const auto stlBytes=readFile(stlPath),objBytes=readFile(objPath);
        require(stlBytes==stl.data && objBytes==obj.data,"scrittura file incompleta");
        return std::make_pair(readStl(stlBytes,stl.triangleCount),readObj(objBytes,obj.quadCount,obj.triangleCount));
    };
    const auto cpu=roundTrip(cpuStl,cpuObj,QStringLiteral("/cpu"));
    validateTrim(cpu.first,*spline,rational);validateTrim(cpu.second,*spline,rational);
    if(withGpu) {
        setCudaTessellationEnabled(true);
        auto points=cudaTessellationPoints();const auto gpuStl=buildBinaryStl(bodies,options);
        require(cudaTessellationPoints()>points,"STL non ha esercitato CUDA");
        std::printf("STL punti CUDA=%llu\n",static_cast<unsigned long long>(cudaTessellationPoints()-points));
        points=cudaTessellationPoints();const auto gpuObj=buildQuadObj(bodies,options);
        require(cudaTessellationPoints()>points,"OBJ non ha esercitato CUDA");
        std::printf("OBJ punti CUDA=%llu\n",static_cast<unsigned long long>(cudaTessellationPoints()-points));
        require(gpuStl.error.isEmpty() && gpuObj.error.isEmpty(),"export CUDA non riuscito");
        const auto gpu=roundTrip(gpuStl,gpuObj,QStringLiteral("/cuda"));
        validateTrim(gpu.first,*spline,rational);validateTrim(gpu.second,*spline,rational);
        // STL usa float: 8 epsilon alla scala 10 mm. OBJ usa 12 decimali.
        compare(cpu.first,gpu.first,8*std::numeric_limits<float>::epsilon()*10,"STL");
        compare(cpu.second,gpu.second,1e-9,"OBJ");
    }
    require(writeBodyBinary(*bodies.front().body)==original,"export ha modificato il B-rep");
    std::printf("PASS export B-spline razionale=%d foro=%d CUDA=%d\n",rational,rational,withGpu);
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const bool gpu=app.arguments().contains(QStringLiteral("--require-gpu"));
    if(gpu && !cudaTessellationAvailable()) {std::puts("CUDA non disponibile: SKIP export GPU");return 77;}
    try {
        QTemporaryDir directory;require(directory.isValid(),"directory temporanea non disponibile");
        runCase(false,gpu,directory.path());runCase(true,gpu,directory.path());
        return 0;
    } catch(const std::exception &error) {std::fprintf(stderr,"FAIL export: %s\n",error.what());return 1;}
}
