#include "cad_cuda_tessellation.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cuda_support.h"
#include "fk_bspline_surface.h"

namespace ForgeCad {

namespace {

std::atomic<bool> enabled{true};
std::atomic<std::uint64_t> gpuPoints{0};

static_assert(sizeof(Kernel::Vec2) == 2 * sizeof(double) && sizeof(Kernel::Vec3) == 3 * sizeof(double),
              "Vec2/Vec3 devono essere double consecutivi");

class CudaSurfaceEvaluator final : public Kernel::SurfaceBatchEvaluator {
public:
    ~CudaSurfaceEvaluator() override {
        for (const auto &entry : handles_) forgecad_cuda_release_surface(entry.second);
    }

    bool evaluate(const Kernel::Surface &surface, const Kernel::Vec2 *uv, std::size_t count, Kernel::Vec3 *points,
                  Kernel::Vec3 *normals) override {
        const auto *spline = dynamic_cast<const Kernel::BSplineSurface *>(&surface);
        if (!spline) return false;
        void *handle = nullptr;
        {
            // La superficie vive quanto il body tassellato, e quindi quanto l'acceleratore.
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = handles_.find(spline);
            if (found == handles_.end()) found = handles_.emplace(spline, upload(*spline)).first;
            handle = found->second;
        }
        if (!handle || !forgecad_cuda_evaluate_surface(handle, uv->c, count, points->c, normals->c)) return false;
        gpuPoints += count;
        return true;
    }

private:
    static void *upload(const Kernel::BSplineSurface &spline) {
        std::vector<double> poles, weights;
        poles.reserve(std::size_t(spline.uPoleCount()) * std::size_t(spline.vPoleCount()) * 3);
        for (int i = 0; i < spline.uPoleCount(); ++i)
            for (int j = 0; j < spline.vPoleCount(); ++j) {
                const Kernel::Vec3 &p = spline.pole(i, j);
                poles.insert(poles.end(), {p[0], p[1], p[2]});
                if (spline.isRational()) weights.push_back(spline.weight(i, j));
            }
        ForgeCudaBSplineSurface data{spline.uDegree(), spline.vDegree(), spline.uPoleCount(), spline.vPoleCount(),
                                     spline.uKnots().data(), spline.vKnots().data(), poles.data(),
                                     spline.isRational() ? weights.data() : nullptr};
        return forgecad_cuda_upload_surface(data);
    }

    std::mutex mutex_;
    std::unordered_map<const Kernel::BSplineSurface *, void *> handles_;  // nullptr = non gestita
};

}

bool cudaTessellationAvailable() {
    static const bool available = forgecad_cuda_available();
    return available;
}

void setCudaTessellationEnabled(bool value) {
    enabled = value;
}

bool cudaTessellationEnabled() {
    return enabled.load();
}

std::unique_ptr<Kernel::SurfaceBatchEvaluator> makeTessellationAccelerator() {
    if (!enabled.load() || !cudaTessellationAvailable()) return nullptr;
    return std::make_unique<CudaSurfaceEvaluator>();
}

std::uint64_t cudaTessellationPoints() {
    return gpuPoints.load();
}

}
