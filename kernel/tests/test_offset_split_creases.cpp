#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_offset.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
// Inserimento esatto di nodi V lisci, anche a distanza molto piccola:
// la geometria e gli edge del B-rep restano quelli della fixture.
SurfacePtr refined(const BSplineSurface &s) {
    std::vector<Vec3> poles; std::vector<double> weights, knots; int count = 0;
    const auto domain = s.vDomain();
    for (int u = 0; u < s.uPoleCount(); ++u) {
        std::vector<Vec3> row; std::vector<double> w;
        for (int v = 0; v < s.vPoleCount(); ++v) { row.push_back(s.pole(u,v)); w.push_back(s.weight(u,v)); }
        BSplineCurve<3> curve(s.vDegree(), s.vKnots(), row, w);
        for (double fraction : {.2,.2+1e-10,.65,.8}) curve = curve.insertKnot(domain.lo + fraction * domain.length());
        knots = curve.knots(); count = curve.poleCount();
        poles.insert(poles.end(), curve.poles().begin(), curve.poles().end());
        for (int v = 0; v < count; ++v) weights.push_back(curve.weight(v));
    }
    return std::make_shared<BSplineSurface>(s.uDegree(),s.vDegree(),s.uKnots(),knots,
        s.uPoleCount(),count,poles,s.isRational()?weights:std::vector<double>{});
}
}
int main() {
    try {
        std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/offset_loft.body", std::ios::binary);
        require(bool(file), "fixture disponibile"); std::ostringstream bytes; bytes << file.rdbuf();
        const Body original = readBodyBinary(bytes.str()); Body input = original;
        std::vector<FaceId> faces;
        for (auto f : input.faces()) if (input.face(f).surface->type() == SurfaceType::BSpline) {
            faces.push_back(f); input.face(f).surface = refined(static_cast<const BSplineSurface &>(*input.face(f).surface));
            const auto &s = *input.face(f).surface, &before = *original.face(f).surface;
            for (double u : {.1,.4,.8}) for (double v : {.1,.3,.7,.9}) {
                const double a = s.uDomain().lo + u*s.uDomain().length(), b = s.vDomain().lo + v*s.vDomain().length();
                require(distance(s.point(a,b),before.point(a,b)) < 1e-12, "raffinamento conserva la geometria esatta");
            }
        }
        require(faces.size()==2 && checkBody(input,{true,true}).empty(), "B-rep raffinato valido");
        const auto frozen = writeBodyBinary(input);
        for (double d : {1.,-1.}) for (bool sew : {false,true}) {
            const auto reference = offsetFaces(original,faces,d,1e-7,sew);
            const auto result = offsetFaces(input,faces,d,1e-7,sew);
            require(result.body.faces().size()==reference.body.faces().size(), "nodi lisci non moltiplicano le facce dell'offset");
            require(result.shells==reference.shells && checkBody(result.body,{true,true}).empty(), "topologia e validita dell'offset preservate");
            // Le superfici cucite sono prolungate per intersecarle: un UV
            // del dominio esteso puo' cadere oltre una piega del genitore.
            // Misura sulle campate originali interne, sul rispettivo offset.
            for (auto parentId : faces) {
                const auto &parent = original.face(parentId);
                const auto &source = static_cast<const BSplineSurface &>(*parent.surface);
                for (const auto &patch : *source.cachedBezierPatches())
                    for (double u : {.2,.5,.8}) for (double v : {.15,.45,.75}) {
                        const double a=patch.uDomain().lo+u*patch.uDomain().length(), b=patch.vDomain().lo+v*patch.vDomain().length();
                        const auto expected=patch.point(a,b)+(parent.sense?d:-d)*patch.normal(a,b);
                        double error=1e300;
                        for (auto f : result.body.faces()) {
                            const auto &origins=result.history.origins(EntityKind::Face,f.index);
                            require(origins.size()==1&&origins[0].kind==EntityKind::Face,"genealogia della faccia offset");
                            if(origins[0].index!=parentId.index)continue;
                            error=std::min(error,distance(result.body.face(f).surface->point(a,b),expected));
                        }
                        require(error<2e-6,"offset esatto nelle campate del profilo originale");
                    }
            }
            require(writeBodyBinary(input)==frozen,"input immutato");
        }
        std::cout << "PASS offset: pieghe reali, nodi lisci stretti, distanze, genealogia e input immutato\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
