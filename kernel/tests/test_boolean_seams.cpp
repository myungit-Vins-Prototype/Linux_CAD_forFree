#include "fk_loft.h"
#include "fk_boolean.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_primitives.h"
#include "fk_mass.h"
#include "fk_history.h"
#include "fk_step.h"
#include "fk_tessellate.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void checked(const Body &body, double volume) {
    require(checkBody(body, {true, true}).empty(), "B-rep o loop non validi");
    require(std::fabs(massProperties(body, 1e-10).volume - volume) < 1e-6, "volume analitico errato");
    TessellationOptions mesh;
    mesh.deflection = 0.1;
    require(tessellate(body, mesh).failedFaces == 0, "tassellazione incompleta");
}
void checkSeam(const Body &body) {
    bool found = false;
    for (EdgeId id : body.edges()) {
        const Edge &edge = body.edge(id);
        if (!edge.forward.valid() || !edge.backward.valid()
            || body.finFace(edge.forward) != body.finFace(edge.backward)) continue;
        found = true;
        const auto &a = body.fin(edge.forward), &b = body.fin(edge.backward);
        require(a.pcurve && b.pcurve, "parametrizzazione della cucitura persa");
        const double t = (edge.range.lo + edge.range.hi) * 0.5;
        require(distance(a.pcurve->point(t), b.pcurve->point(t)) > 0.5, "i due lati UV della cucitura sono confusi");
    }
    require(found, "la prova deve conservare una cucitura esplicita");
}
}
int main() {
    try {
        LoftSection bottom, top;
        bottom.loop.segments = {{std::make_shared<Circle<2>>(Vec2(), Vec2(1,0), Vec2(0,1), 5), {0, kTwoPi}}};
        top = bottom;
        top.frame = Frame3(Vec3(0,0,10), Vec3(0,0,1), Vec3(1,0,0));
        LoftOptions loft;
        loft.mergeTangentFaces = true;
        const Body base = loftSolid({bottom, top}, loft);
        checkSeam(base);
        checked(base, 250 * kPi);
        const Body pin = makeCylinder(Frame3(Vec3(0,0,8), Vec3(0,0,1), Vec3(1,0,0)), 2, 6);
        for (bool reverse : {false, true}) {
            const Body &a = reverse ? pin : base, &b = reverse ? base : pin;
            for (BooleanOperation op : {BooleanOperation::Unite, BooleanOperation::Intersect, BooleanOperation::Subtract}) {
                BooleanOptions options;
                options.threads = 1;
                BodyHistory history;
                const Body result = booleanOperation(a, b, op, options, &history);
                const double volume = op == BooleanOperation::Unite ? 266 : op == BooleanOperation::Intersect ? 8 : reverse ? 16 : 242;
                checked(result, volume * kPi);
                for (FaceId face : result.faces())
                    require(history.isDeclared(EntityKind::Face, face.index), "origine della faccia persa");
                if (op == BooleanOperation::Unite || (op == BooleanOperation::Subtract && !reverse)) checkSeam(result);
                options.threads = 4;
                require(writeBodyBinary(result) == writeBodyBinary(booleanOperation(a, b, op, options)), "esito dipendente dal parallelismo");
                if (op == BooleanOperation::Unite && !reverse) {
                    ExchangeBody exported; exported.name = "closed-loft-and-pin"; exported.body = result;
                    const auto imported = readStep(writeStep({exported}));
                    require(imported.bodies.size() == 1, "STEP con numero di corpi errato");
                    checked(imported.bodies.front().body, volume * kPi);
                }
            }
        }
        std::cout << "PASS cucitura loft: unione/intersezione/differenza, ordini, UV, volumi, genealogia, mesh, STEP e parallelismo\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
