#include "fk_surface_poles_influence.h"
#include "fk_cancel.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace ForgeCad::Kernel {
std::vector<SurfacePoleInfluence> surfacePoleInfluence(const BSplineSurface &s,
    const std::vector<std::array<int,2>> &drivers, int ru, int rv) {
    if (ru < 0 || rv < 0 || ru > 32 || rv > 32 || ((ru == 0) != (rv == 0)) || drivers.size() > 4096)
        throw std::domain_error("Influenza poli: scegliere raggi U/V da1 a32.");
    const int nu = s.uPoleCount(), nv = s.vPoleCount();
    std::map<std::pair<int,int>,double> weights;
    const auto add = [&](int u, int v, double w) {
        auto &value = weights[{u,v}]; value = std::max(value,w);
        if (weights.size() > 4096) throw std::domain_error("Influenza poli: oltre4096poli; ridurre raggi o selezione.");
    };
    const auto indices = [](int i, int count, bool periodic) {
        if (!periodic) return i < 0 || i >= count ? std::vector<int>{} : std::vector<int>{i};
        const int period = count - 1;
        i = (i % period + period) % period;
        return i == 0 ? std::vector<int>{0,count-1} : std::vector<int>{i};
    };
    for (const auto &p : drivers) {
        cancellationPoint();
        if (p[0] < 0 || p[0] >= nu || p[1] < 0 || p[1] >= nv)
            throw std::domain_error("Influenza poli: comando fuori griglia.");
        for (int du = -ru; du <= ru; ++du)
            for (int dv = -rv; dv <= rv; ++dv) {
                const double d = std::hypot(double(du)/(ru+1),double(dv)/(rv+1));
                if (d >= 1) continue;
                // Wendland C2 compatta: nessun gradino al limite del campo.
                const double t = 1-d, w = t*t*t*t*(1+4*d);
                for (int u : indices(p[0]+du,nu,ru>0&&s.isUPeriodic()))
                    for (int v : indices(p[1]+dv,nv,rv>0&&s.isVPeriodic())) add(u,v,w);
            }
    }
    std::vector<SurfacePoleInfluence> result;
    for (const auto &p : weights) result.push_back({p.first.first,p.first.second,p.second});
    return result;
}
} // namespace ForgeCad::Kernel
