#include "fk_marching.h"
#include "fk_bspline_surface.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
BSplineSurface tiltedPlane(int nu, int nv, double offset) {
    std::vector<double> u{0}, v{0}, weights;
    std::vector<Vec3> poles;
    for (int i = 0; i < nu; ++i) u.push_back(double(i) / (nu - 1));
    for (int j = 0; j < nv; ++j) v.push_back(double(j) / (nv - 1));
    u.push_back(1); v.push_back(1);
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            const double x = -1 + 2. * i / (nu - 1), y = -1 + 2. * j / (nv - 1);
            poles.emplace_back(x, y, .5 * x + .7 * y + offset);
            weights.push_back(1. + .1 * ((i + j) % 3));
        }
    return BSplineSurface(1, 1, u, v, nu, nv, poles, weights);
}

}
int main() {
    try {
        // 100701 pezze contro quattro del cilindro: il prodotto cartesiano
        // supera il vecchio budget, ma quasi tutte le coppie sono disgiunte.
        const int nu = 202, nv = 502;
        std::vector<double> u{0}, v{0};
        std::vector<Vec3> poles;
        for (int i = 0; i < nu; ++i) u.push_back(double(i) / (nu - 1));
        for (int j = 0; j < nv; ++j) v.push_back(double(j) / (nv - 1));
        u.push_back(1); v.push_back(1);
        for (int i = 0; i < nu; ++i)
            for (int j = 0; j < nv; ++j)
                poles.emplace_back(-100 + 200. * i / (nu - 1), -100 + 200. * j / (nv - 1), 0);
        const BSplineSurface plane(1, 1, u, v, nu, nv, poles);
        const CylindricalSurface cylinder(Frame3(), 1);
        const Box bounds{Vec3(-2,-2,-2), Vec3(2,2,2)};
        for (bool reverse : {false, true}) {
            const Surface &a = reverse ? static_cast<const Surface &>(cylinder) : plane;
            const Surface &b = reverse ? static_cast<const Surface &>(plane) : cylinder;
            const auto result = intersectSurfaces(a, b, bounds, {});
            require(!result.coincident && result.curves.size() == 1, "cerchio completo non trovato");
            const auto &curve = result.curves.front();
            require(distance(curve.curve->point(curve.range.lo), curve.curve->point(curve.range.hi)) < 1e-8, "curva non chiusa");
            require(curve.pcurves[0] && curve.pcurves[1], "SP-curve mancanti");
            for (int i = 0; i <= 200; ++i) {
                const double t = curve.range.lo + curve.range.length() * i / 200.;
                const Vec3 p = curve.curve->point(t);
                require(std::fabs(p.z()) < 2e-9 && std::fabs(p.x()*p.x() + p.y()*p.y() - 1) < 2e-9,
                        "intersezione diversa dal cerchio analitico");
                const Vec2 uv = curve.pcurves[0]->point(t), st = curve.pcurves[1]->point(t);
                require(distance(a.point(uv.x(),uv.y()),p) < 3e-9 && distance(b.point(st.x(),st.y()),p) < 3e-9,
                        "SP-curve incoerenti con le superfici esatte");
            }
        }

        // I box assiali si sovrappongono, ma gli inviluppi dei poli stanno
        // su piani paralleli distinti. La griglia molto diversa riproduce
        // il confronto di un carrier originale con la sua superficie offset.
        const BSplineSurface coarse = tiltedPlane(2, 2, 0), dense = tiltedPlane(202, 502, .01);
        const Box tiltedBounds{Vec3(-2,-2,-3), Vec3(2,2,3)};
        for (bool reverse : {false, true}) {
            const Surface &a = reverse ? dense : coarse, &b = reverse ? coarse : dense;
            const auto result = intersectSurfaces(a, b, tiltedBounds, {});
            require(!result.coincident && result.curves.empty() && result.tangentCurves.empty() && result.tangentPoints.empty(),
                    "piani NURBS paralleli separati riconosciuti come intersecanti");
        }
        // Il filtro non deve eliminare la coincidenza, neanche quando i
        // carrier sono distinti ma entro la tolleranza gia' prevista.
        const double tolerance = SurfaceIntersectionOptions{}.tolerance;
        for (double offset : {0.0, .5 * tolerance}) {
            const BSplineSurface nearby = tiltedPlane(2, 2, offset);
            for (bool reverse : {false, true}) {
                const auto result = intersectSurfaces(reverse ? nearby : coarse, reverse ? coarse : nearby, tiltedBounds, {});
                require(result.coincident, "coincidenza NURBS entro tolleranza persa");
            }
        }
        // Un'intersezione reale tra NURBS resta al percorso ordinario:
        // x=0 taglia il piano z=.5x+.7y lungo la retta analitica z=.7y.
        const BSplineSurface vertical(1,1,{0,0,1,1},{0,0,1,1},2,2,
            {Vec3(0,-1,-2),Vec3(0,1,-2),Vec3(0,-1,2),Vec3(0,1,2)});
        for (bool reverse : {false, true}) {
            const Surface &a = reverse ? vertical : coarse, &b = reverse ? coarse : vertical;
            const auto result = intersectSurfaces(a, b, tiltedBounds, {});
            require(!result.coincident && result.curves.size() == 1, "intersezione trasversale NURBS persa");
            const auto &curve = result.curves.front();
            for (int i = 0; i <= 100; ++i) {
                const double t = curve.range.lo + curve.range.length() * i / 100.;
                const Vec3 p = curve.curve->point(t);
                require(std::fabs(p.x()) < 2e-9 && std::fabs(p.z() - .7 * p.y()) < 2e-9,
                        "retta analitica NURBS errata");
                for (int side = 0; side < 2; ++side) {
                    require(bool(curve.pcurves[side]), "SP-curve trasversale assente");
                    const Vec2 uv = curve.pcurves[side]->point(t);
                    require(distance((side == 0 ? a : b).point(uv.x(),uv.y()),p) < 3e-9, "SP-curve trasversale incoerente");
                }
            }
        }
        std::cout << "PASS ricerca semi su griglia NURBS fitta: cerchio analitico, completezza, SP-curve, carrier separati razionali, tagli trasversali e ordini inversi\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
