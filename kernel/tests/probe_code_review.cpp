// Diagnostica manuale, senza Qt/OCCT. Non impone soglie temporali ai test.
// c++ -std=c++17 -O2 -I kernel kernel/tests/probe_code_review.cpp \
//     forgecad-release/kernel/libforgekernel.a -o /tmp/forgecad-review-probe
#include "fk_boolean.h"
#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_sweep.h"
#include "fk_tessellate.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace ForgeCad::Kernel;
using Clock = std::chrono::steady_clock;

static double intersectionArea(std::vector<Vec2> polygon, const std::vector<Vec2> &clip) {
    for (std::size_t i = 0; i < clip.size() && !polygon.empty(); ++i) {
        const Vec2 a = clip[i], d = clip[(i + 1) % clip.size()] - a;
        std::vector<Vec2> next;
        Vec2 previous = polygon.back();
        double before = cross(d, previous - a);
        for (const Vec2 &point : polygon) {
            const double after = cross(d, point - a);
            if ((before >= 0) != (after >= 0)) next.push_back(previous + (before / (before - after)) * (point - previous));
            if (after >= 0) next.push_back(point);
            previous = point; before = after;
        }
        polygon = std::move(next);
    }
    double twiceArea = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) twiceArea += cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    return 0.5 * std::fabs(twiceArea);
}

static double peakMiB() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#ifdef __APPLE__
    return double(usage.ru_maxrss) / (1024 * 1024);
#else
    return double(usage.ru_maxrss) / 1024;
#endif
}

int main(int argc, char **argv) {
    try {
        if (argc < 2) throw std::invalid_argument("Uso: iso [poli=512] | boxes [thread=0] [ripetizioni=5000] | disjoint/sparse [lati=1024] [thread=1] | spring [thread=0] [ripetizioni=10] | fillet [ripetizioni=100]");
        const std::string mode = argv[1];
        std::cout << std::setprecision(12);
        if (mode == "iso") {
            const int n = argc > 2 ? std::stoi(argv[2]) : 512, degree = 3;
            if (n < 4) throw std::invalid_argument("Servono almeno 4 poli per direzione");
            std::vector<double> knots(4, 0.0);
            for (int i = 1; i < n - degree; ++i) knots.push_back(double(i) / (n - degree));
            knots.insert(knots.end(), 4, 1.0);
            std::vector<Vec3> poles;
            poles.reserve(std::size_t(n) * n);
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j) poles.emplace_back(i, j, 0.001 * i * j);
            const BSplineSurface surface(degree, degree, knots, knots, n, n, std::move(poles));
            const double before = peakMiB();
            const auto start = Clock::now();
            const auto iso = surface.uIso(0.123456789);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            const double after = peakMiB();
            bool valid = true;
            for (int j = 0; j <= 20; ++j)
                valid = valid && distance(iso->point(j / 20.0), surface.point(0.123456789, j / 20.0)) < 1e-9;
            std::cout << "poles=" << n << " before_peak_MiB=" << before << " after_peak_MiB=" << after
                      << " first_ms=" << ms << " valid=" << valid << '\n';
            return valid ? 0 : 1;
        }
        if (mode == "fillet" || mode == "spring") {
            const int repetitions = mode == "fillet" ? (argc > 2 ? std::stoi(argv[2]) : 100)
                : (argc > 3 ? std::stoi(argv[3]) : 10);
            if (repetitions < 1) throw std::invalid_argument("Ripetizioni non positive");
            BooleanOptions options;
            options.threads = mode == "spring" && argc > 2 ? std::stoi(argv[2]) : 0;
            Body a, b;
            if (mode == "fillet") a = makeBox(Frame3(), 10, 10, 10);
            else {
                HelixSpec spec;
                spec.radius = 4; spec.pitch = 1.5; spec.turns = 0.5;
                const auto helix = std::make_shared<HelixCurve>(spec);
                ProfileRegion circle;
                circle.outer.segments = {{std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 0.5), {0, kTwoPi}}};
                const Frame3 frame(helix->point(0), normalized(helix->derivative(0)), Vec3(0.3, 0.7, 0.2));
                b = sweepRegions(frame, {circle}, {{helix, helix->domain()}});
                a = makeCylinder(Frame3(Vec3(0, 0, -1), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4, 5);
            }
            const double before = peakMiB();
            const auto start = Clock::now();
            Body result;
            for (int i = 0; i < repetitions; ++i)
                result = mode == "fillet" ? blendEdges(a, {a.edges().front()}, 1.0, false)
                    : booleanOperation(a, b, BooleanOperation::Subtract, options);
            const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
            const double after = peakMiB();
            double expected = 1000 - 10 * (1 - kPi / 4);
            bool valid = checkBody(result).empty();
            if (mode == "spring") {
                BooleanOptions serial; serial.threads = 1;
                const Body reference = booleanOperation(a, b, BooleanOperation::Subtract, serial);
                expected = massProperties(reference).volume;
                valid = valid && reference.faces().size() == result.faces().size() && reference.edges().size() == result.edges().size();
            }
            const double volume = massProperties(result).volume;
            const auto mesh = tessellate(result, {});
            valid = valid && std::fabs(volume - expected) < 1e-7 && mesh.failedFaces == 0;
            std::cout << "mode=" << mode << " threads=" << options.threads << " repetitions=" << repetitions
                      << " before_peak_MiB=" << before << " after_peak_MiB=" << after << " seconds=" << seconds
                      << " volume=" << volume << " faces=" << result.faces().size() << " edges=" << result.edges().size()
                      << " failed_mesh=" << mesh.failedFaces << " valid=" << valid << '\n';
            return valid ? 0 : 1;
        }
        if (mode != "boxes" && mode != "disjoint" && mode != "sparse") throw std::invalid_argument("Modo sconosciuto");
        BooleanOptions options;
        int repetitions = 1;
        double commonVolume = 0;
        Body a, b;
        if (mode == "boxes") {
            options.threads = argc > 2 ? std::stoi(argv[2]) : 0;
            repetitions = argc > 3 ? std::stoi(argv[3]) : 5000;
            if (repetitions < 1) throw std::invalid_argument("Ripetizioni non positive");
            a = makeBox(Frame3(), 10, 10, 10);
            b = makeBox(Frame3(Vec3(5, 3, 2), Vec3(0, 0, 1), Vec3(1, 0, 0)), 10, 10, 10);
        } else {
            const int n = argc > 2 ? std::stoi(argv[2]) : 1024;
            if (n < 3) throw std::invalid_argument("Servono almeno 3 lati");
            options.threads = argc > 3 ? std::stoi(argv[3]) : 1;
            std::vector<Vec2> polygon;
            for (int i = 0; i < n; ++i) {
                const double angle = kTwoPi * i / n;
                polygon.emplace_back(10 * std::cos(angle), 10 * std::sin(angle));
            }
            a = makePrism(Frame3(), polygon, {}, 10);
            const double shift = mode == "sparse" ? 19 : 100;
            b = makePrism(Frame3(Vec3(shift, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), polygon, {}, 10);
            if (mode == "sparse") {
                std::vector<Vec2> shifted = polygon;
                for (Vec2 &point : shifted) point += Vec2(shift, 0);
                commonVolume = 10 * intersectionArea(polygon, shifted);
            }
        }
        const double before = peakMiB();
        const auto start = Clock::now();
        Body result;
        for (int i = 0; i < repetitions; ++i) result = booleanOperation(a, b, BooleanOperation::Subtract, options);
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        const double after = peakMiB();
        const double expected = mode == "boxes" ? 720.0 : massProperties(a).volume - commonVolume;
        const bool valid = checkBody(result).empty() && std::fabs(massProperties(result).volume - expected) < 1e-7;
        std::cout << "faces=" << a.faces().size() << " threads=" << options.threads << " repetitions=" << repetitions
                  << " before_peak_MiB=" << before << " after_peak_MiB=" << after << " seconds=" << seconds
                  << " valid=" << valid << '\n';
        return valid ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
