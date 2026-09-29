#include "fk_classify.h"
#include "fk_step.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iterator>
using namespace ForgeCad::Kernel;
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream in(argv[1]);
    auto result = readStep(std::string(std::istreambuf_iterator<char>(in), {}));
    for (const auto &part : result.bodies) {
        RayFaceIndex index(part.body);
        const auto center = (index.bounds.lo + index.bounds.hi) * 0.5;
        const double d = index.bounds.diagonal();
        int hits[2] = {};
        for (int mode = 0; mode < 2; ++mode) {
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 25; ++i) {
                Vec3 p = center + Vec3((i % 5 - 2) * d * 0.08, (i / 5 - 2) * d * 0.08, d);
                double t;
                hits[mode] += firstRayHit(part.body, p, Vec3(0,0,-1), 1e-7, t, nullptr, mode ? &index : nullptr);
            }
            auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::cout << part.name << " faces=" << index.faces.size() << " cached=" << mode << " ms/25=" << ms << " hits=" << hits[mode] << std::endl;
        }
        if(hits[0] != hits[1]) return 2;
    }
}
