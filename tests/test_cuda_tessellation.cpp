// Valutazione delle superfici B-spline/NURBS sulla GPU (cuda_support.cu,
// cad_cuda_tessellation) contro il kernel sulla CPU, e tassellazione con
// l'acceleratore. Senza GPU CUDA il test lo dice e passa.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "cad_cuda_tessellation.h"
#include "cuda_support.h"
#include "fk_bspline_surface.h"
#include "fk_loft.h"
#include "fk_tessellate.h"

using namespace ForgeCad::Kernel;

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FALLITO: %s\n", what);
    }
}

// Nodi espansi clamped con nodi interni anche multipli.
std::vector<double> knots(std::mt19937 &rng, int degree, int poles) {
    std::vector<double> result(std::size_t(degree + 1), 0.0);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> inner;
    while (int(inner.size()) < poles - degree - 1) {
        const double k = d(rng);
        const int multiplicity = std::min<int>(1 + int(rng() % std::size_t(degree)), poles - degree - 1 - int(inner.size()));
        for (int m = 0; m < multiplicity; ++m) inner.push_back(k);
    }
    std::sort(inner.begin(), inner.end());
    for (double k : inner) result.push_back(2.0 + 3.0 * k);
    for (double &k : result) k = k == 0.0 ? 2.0 : k;
    result.insert(result.end(), std::size_t(degree + 1), 5.0);
    return result;
}

std::shared_ptr<BSplineSurface> randomSurface(std::mt19937 &rng, int pu, int pv, bool rational) {
    const int nu = pu + 1 + int(rng() % 6), nv = pv + 1 + int(rng() % 6);
    std::uniform_real_distribution<double> d(-10.0, 10.0), w(0.3, 3.0);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            poles.push_back(Vec3(3.0 * i + 0.3 * d(rng), 3.0 * j + 0.3 * d(rng), d(rng)));
            if (rational) weights.push_back(w(rng));
        }
    return std::make_shared<BSplineSurface>(pu, pv, knots(rng, pu, nu), knots(rng, pv, nv), nu, nv, poles, weights);
}

}

int main() {
    if (!ForgeCad::cudaTessellationAvailable()) {
        std::printf("GPU CUDA non disponibile: test saltato\n");
        return 0;
    }
    std::unique_ptr<SurfaceBatchEvaluator> gpu = ForgeCad::makeTessellationAccelerator();
    check(gpu != nullptr, "acceleratore creato");
    if (!gpu) return 1;

    // Superfici a caso: gradi da 1 a 7 e 15, polinomiali e razionali, nodi
    // multipli; parametri a caso, ai bordi e sui nodi.
    std::mt19937 rng(20261002);
    std::vector<std::shared_ptr<BSplineSurface>> surfaces;
    for (int pu : {1, 2, 3, 5, 7, 15})
        for (int pv : {1, 3, 4})
            for (bool rational : {false, true}) surfaces.push_back(randomSurface(rng, pu, pv, rational));
    double worstPoint = 0.0, worstNormal = 0.0;
    for (const auto &surface : surfaces) {
        const Interval u = surface->uDomain(), v = surface->vDomain();
        std::vector<Vec2> uv;
        std::uniform_real_distribution<double> d(0.0, 1.0);
        for (int i = 0; i < 3000; ++i) uv.push_back(Vec2(u.lo + d(rng) * u.length(), v.lo + d(rng) * v.length()));
        for (double a : surface->uKnots())
            for (double b : surface->vKnots()) uv.push_back(Vec2(a, b));
        std::vector<Vec3> points(uv.size()), normals(uv.size());
        check(gpu->evaluate(*surface, uv.data(), uv.size(), points.data(), normals.data()), "superficie valutata sulla GPU");
        double scale = 0.0;
        for (int i = 0; i < surface->uPoleCount(); ++i)
            for (int j = 0; j < surface->vPoleCount(); ++j) scale = std::max(scale, norm(surface->pole(i, j)));
        for (std::size_t i = 0; i < uv.size(); ++i) {
            worstPoint = std::max(worstPoint, distance(points[i], surface->point(uv[i][0], uv[i][1])) / scale);
            if (squaredNorm(normals[i]) == 0.0) continue;
            try {
                worstNormal = std::max(worstNormal, distance(normals[i], surface->normal(uv[i][0], uv[i][1])));
            } catch (const std::exception &) {
                // singolare sulla CPU ma non sulla GPU: entro l'arrotondamento
            }
        }
    }
    std::printf("scarto massimo GPU-CPU: punti %.3g (relativo), normali %.3g\n", worstPoint, worstNormal);
    check(worstPoint < 1e-13, "punti come sulla CPU");
    check(worstNormal < 1e-9, "normali come sulla CPU");

    // Grado oltre il limite: resta alla CPU.
    {
        auto high = randomSurface(rng, kForgeCudaMaxDegree + 1, 2, false);
        Vec2 uv(3.0, 3.0);
        Vec3 p, n;
        check(!gpu->evaluate(*high, &uv, 1, &p, &n), "grado troppo alto rifiutato");
    }

    // Tassellazione di un loft liscio (facce B-spline) con e senza GPU.
    std::vector<LoftSection> sections;
    for (int k = 0; k < 4; ++k) {
        LoftSection s;
        s.frame = Frame3(Vec3(0.4 * k, 0, 3.0 * k), Vec3(0, 0, 1), Vec3(1, 0, 0));
        const double r = 4.0 + std::sin(double(k));
        s.loop.segments = {ProfileSegment{std::make_shared<Circle<2>>(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), r), Interval{0.0, kTwoPi}}};
        sections.push_back(s);
    }
    const Body loft = loftSolid(sections, false);
    TessellationOptions options;
    options.deflection = 1e-3;
    options.angle = 0.1;
    const Tessellation cpu = tessellate(loft, options);
    const std::uint64_t before = ForgeCad::cudaTessellationPoints();
    options.accelerator = gpu.get();
    options.acceleratorMinimumBatch = 1;
    const Tessellation accelerated = tessellate(loft, options);
    check(ForgeCad::cudaTessellationPoints() > before, "punti del loft valutati sulla GPU");
    check(cpu.failedFaces == 0 && accelerated.failedFaces == 0, "facce tassellate");
    // Scarto dei vertici dalla superficie (quelli del bordo vengono dagli edge,
    // che vi stanno entro la loro tolleranza): uguale con e senza GPU.
    const auto measure = [&](const Tessellation &mesh, std::size_t &triangles) {
        double deviation = 0.0;
        for (const FaceMesh &face : mesh.faces) {
            triangles += face.triangles.size();
            const Surface &surface = *loft.face(face.face).surface;
            for (std::size_t i = 0; i < face.points.size(); ++i)
                deviation = std::max(deviation, distance(face.points[i], surface.point(face.parameters[i][0], face.parameters[i][1])));
        }
        return deviation;
    };
    std::size_t cpuTriangles = 0, gpuTriangles = 0;
    const double cpuDeviation = measure(cpu, cpuTriangles), gpuDeviation = measure(accelerated, gpuTriangles);
    std::printf("loft: %zu triangoli sulla CPU, %zu con la GPU, scarto dei vertici %.3g / %.3g\n", cpuTriangles, gpuTriangles,
                cpuDeviation, gpuDeviation);
    check(gpuDeviation <= cpuDeviation + 1e-12, "vertici sulla superficie come sulla CPU");
    check(std::abs(double(cpuTriangles) - double(gpuTriangles)) <= 0.01 * double(cpuTriangles), "stessa mesh a meno dell'arrotondamento");
    if (failures == 0) std::printf("test CUDA superati\n");
    return failures == 0 ? 0 : 1;
}
