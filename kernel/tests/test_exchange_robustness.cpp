#include "fk_test.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_iges.h"
#include "fk_bspline_surface.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_step.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <future>
#include <new>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <string>

// Limite esclusivo di questo runner: una regressione non deve realmente
// allocare gigabyte per provare il rifiuto di un piccolo input malformato.
namespace {
thread_local std::size_t allocationLimit = 0;
thread_local std::size_t blockedAllocation = 0;
struct AllocationGuard {
    AllocationGuard() { blockedAllocation = 0; allocationLimit = 1024 * 1024; }
    ~AllocationGuard() { allocationLimit = 0; }
};
}
void *operator new(std::size_t size) {
    if (allocationLimit && size > allocationLimit) {
        blockedAllocation = size;
        throw std::bad_alloc();
    }
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
// Mantiene visibile al compilatore il confine della sostituzione new/delete:
// l'inlining di free produce falsi avvisi mismatched-new-delete con GCC.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void operator delete(void *p) noexcept { std::free(p); }
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

using namespace ForgeCad::Kernel;
namespace {
std::string cubeStep() {
    ExchangeBody b; b.name = "cube"; b.body = makeBox(Frame3(), 1, 2, 3);
    return writeStep({b});
}
int firstId(const std::string &s, const std::string &type) {
    const auto at = s.find("=" + type + "(");
    if (at == std::string::npos) throw std::runtime_error("tipo assente nella fixture");
    return std::atoi(s.c_str() + s.rfind('#', at) + 1);
}
std::size_t openOf(const std::string &s, int id) { return s.find('(', s.find("#" + std::to_string(id) + "=")); }
std::pair<std::size_t, std::size_t> argumentSpan(const std::string &s, std::size_t open, int wanted) {
    std::size_t begin = open + 1;
    int depth = 0, index = 0;
    bool quoted = false;
    for (std::size_t i = begin; i < s.size(); ++i) {
        if (s[i] == '\'') {
            if (quoted && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
            quoted = !quoted;
        }
        if (quoted) continue;
        if (s[i] == '(') ++depth;
        else if (s[i] == ')' && depth) --depth;
        else if ((s[i] == ',' || s[i] == ')') && !depth) {
            if (index == wanted) return {begin, i};
            begin = i + 1;
            ++index;
        }
    }
    throw std::runtime_error("argomento assente nella fixture");
}
std::string argument(const std::string &s, int id, int n) {
    const auto span = argumentSpan(s, openOf(s, id), n);
    return s.substr(span.first, span.second - span.first);
}
std::string replaceArgument(std::string s, std::size_t open, int n, const std::string &value) {
    const auto span = argumentSpan(s, open, n);
    s.replace(span.first, span.second - span.first, value);
    return s;
}
int referenceId(const std::string &s) { return std::atoi(s.c_str() + s.find('#') + 1); }
int nextId(const std::string &s) {
    int largest = 0;
    for (auto at = s.find('#'); at != std::string::npos; at = s.find('#', at + 1))
        largest = std::max(largest, std::atoi(s.c_str() + at + 1));
    return largest + 1;
}
std::string append(std::string s, const std::string &entities) { s.insert(s.rfind("ENDSEC;"), entities + "\n"); return s; }
std::string ref(int id) { return "#" + std::to_string(id); }
bool mentions(const StepReadResult &result, const std::string &text) {
    for (const auto &note : result.notes) if (note.find(text) != std::string::npos) return true;
    return false;
}
std::string splineEdge(const std::string &degree, const std::string &multiplicities) {
    std::string s = cubeStep();
    const int edge = firstId(s, "EDGE_CURVE"), id = nextId(s);
    const auto p0 = argument(s, referenceId(argument(s, edge, 1)), 1);
    const auto p1 = argument(s, referenceId(argument(s, edge, 2)), 1);
    s = replaceArgument(s, openOf(s, edge), 3, ref(id));
    return append(s, ref(id) + "=B_SPLINE_CURVE_WITH_KNOTS(''," + degree + ",(" + p0 + "," + p1
                        + "),.UNSPECIFIED.,.F.,.F.," + multiplicities + ",(0.,1.),.UNSPECIFIED.);");
}
std::string surfaceStep(bool rational = false) {
    const std::vector<Vec3> v{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    std::vector<Body::BuildEdge> edges;
    for (int i = 0; i < 4; ++i) {
        Body::BuildEdge e;
        e.start = i; e.end = (i + 1) % 4;
        e.curve = std::make_shared<Line<3>>(v[i], v[e.end] - v[i]); e.range = {0,1};
        edges.push_back(e);
    }
    Body::BuildFace f;
    f.surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0,0,1,1}, std::vector<double>{0,0,1,1},
                                               2, 2, std::vector<Vec3>{{0,0,0},{0,1,0},{1,0,0},{1,1,0}},
                                               rational ? std::vector<double>{1,2,1,2} : std::vector<double>{});
    f.sense = true; f.loops = {{{0,true,{},0},{1,true,{},0},{2,true,{},0},{3,true,{},0}}};
    ExchangeBody b; b.name = "sheet"; b.body = Body::buildSheet(v, edges, {f});
    return writeStep({b});
}
std::string assembly(const std::string &translation, bool nested) {
    std::string s = cubeStep();
    const int productContext = firstId(s, "PRODUCT_CONTEXT"), definitionContext = firstId(s, "PRODUCT_DEFINITION_CONTEXT");
    const int child = firstId(s, "PRODUCT_DEFINITION"), rep = firstId(s, "ADVANCED_BREP_SHAPE_REPRESENTATION");
    const auto context = argument(s, rep, 2);
    int id = nextId(s);
    const auto add = [&](const std::string &entity) { const int n = id++; s = append(s, ref(n) + "=" + entity + ";"); return n; };
    const int point = add("CARTESIAN_POINT('',(0.,0.,0.))");
    const int z = add("DIRECTION('',(0.,0.,1.))"), x = add("DIRECTION('',(1.,0.,0.))");
    const int origin = add("AXIS2_PLACEMENT_3D(''," + ref(point) + "," + ref(z) + "," + ref(x) + ")");
    const auto parent = [&](const std::string &name) {
        const int p = add("PRODUCT('" + name + "','" + name + "','',(" + ref(productContext) + "))");
        const int form = add("PRODUCT_DEFINITION_FORMATION('',''," + ref(p) + ")");
        const int def = add("PRODUCT_DEFINITION('design',''," + ref(form) + "," + ref(definitionContext) + ")");
        const int shape = add("PRODUCT_DEFINITION_SHAPE('',''," + ref(def) + ")");
        const int representation = add("SHAPE_REPRESENTATION('',(" + ref(origin) + ")," + context + ")");
        add("SHAPE_DEFINITION_REPRESENTATION(" + ref(shape) + "," + ref(representation) + ")");
        return std::make_pair(def, representation);
    };
    const auto group = parent("group");
    const auto occurrence = [&](int owner, int ownerRep, int component, int componentRep, const std::string &shift) {
        const int usage = add("NEXT_ASSEMBLY_USAGE_OCCURRENCE('occ','occ',''," + ref(owner) + "," + ref(component) + ",$)");
        const int shape = add("PRODUCT_DEFINITION_SHAPE('',''," + ref(usage) + ")");
        const int p = add("CARTESIAN_POINT('',(" + shift + ",0.,0.))");
        const int target = add("AXIS2_PLACEMENT_3D(''," + ref(p) + "," + ref(z) + "," + ref(x) + ")");
        const int transform = add("ITEM_DEFINED_TRANSFORMATION('',''," + ref(origin) + "," + ref(target) + ")");
        const int relation = add("(REPRESENTATION_RELATIONSHIP('',''," + ref(componentRep) + "," + ref(ownerRep)
                                  + ") REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(" + ref(transform) + ") SHAPE_REPRESENTATION_RELATIONSHIP())");
        add("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(" + ref(relation) + "," + ref(shape) + ")");
    };
    if (nested) {
        occurrence(group.first, group.second, child, rep, "0.");
        const auto root = parent("root");
        occurrence(root.first, root.second, group.first, group.second, "0.");
        occurrence(root.first, root.second, group.first, group.second, translation);
    } else {
        occurrence(group.first, group.second, child, rep, "0.");
        occurrence(group.first, group.second, child, rep, translation);
    }
    return s;
}
template<class T> void raw(std::string &s, T value) { s.append(reinterpret_cast<const char *>(&value), sizeof value); }
std::string truncatedSurface(bool validKnots) {
    std::string s = "FKBD"; raw(s, std::uint32_t(1)); raw(s, std::uint32_t(0x01020304));
    raw(s, std::uint8_t(3)); raw(s, std::uint8_t(7)); raw(s, std::int32_t(1)); raw(s, std::int32_t(1));
    for (int axis = 0; axis < 2; ++axis) {
        raw(s, std::uint32_t(validKnots ? 10002 : 0));
        if (validKnots) {
            raw(s, 0.);
            for (int i = 0; i < 10000; ++i) raw(s, double(i));
            raw(s, 9999.);
        }
    }
    raw(s, std::int32_t(10000)); raw(s, std::int32_t(10000));
    return s;
}
}

FK_TEST(ExchangeAutomaticTimestampsUseLocalTimeInConcurrentExports) {
    const auto containsStepStamp = [](const std::string &output, const std::string &stamp) {
        return output.find("FILE_NAME('model','" + stamp + "',") != std::string::npos;
    };
    const auto containsIgesStamps = [](const std::string &output, const std::string &stamp) {
        const std::string token = std::to_string(stamp.size()) + "H" + stamp;
        return output.find(token + ",") != std::string::npos
            && output.find(token + ";") != std::string::npos;
    };
    StepWriteOptions stepOptions;
    stepOptions.timeStamp = "1998-07-06T05:04:03";
    IgesWriteOptions igesOptions;
    igesOptions.timeStamp = "19980706.050403";
    FK_CHECK(containsStepStamp(writeStep({}, stepOptions), stepOptions.timeStamp));
    FK_CHECK(containsIgesStamps(writeIges({}, igesOptions), igesOptions.timeStamp));

    // Un chiamante può avere ancora un calendario UTC restituito dalla libreria C.
    // L'export deve lasciarlo intatto anche su libc che condividono il buffer con localtime.
    const std::time_t epoch = 0;
    for (bool step : {true, false}) {
        const std::tm *utc = std::gmtime(&epoch);
        FK_CHECK(utc && utc->tm_year == 70);
        if (step) writeStep({});
        else writeIges({});
        FK_CHECK(utc->tm_year == 70 && utc->tm_mon == 0 && utc->tm_mday == 1);
    }

    struct Export {
        std::time_t before;
        std::string output;
        std::time_t after;
    };
    std::vector<std::future<std::vector<Export>>> workers;
    std::promise<void> begin;
    const std::shared_future<void> start = begin.get_future();
    for (int worker = 0; worker < 6; ++worker) {
        const bool step = worker % 2 == 0;
        workers.push_back(std::async(std::launch::async, [start, step] {
            start.wait();
            std::vector<Export> exports;
            for (int i = 0; i < 128; ++i) {
                const auto before = std::time(nullptr);
                std::string output = step ? writeStep({}) : writeIges({});
                exports.push_back({before, std::move(output), std::time(nullptr)});
            }
            return exports;
        }));
    }
    begin.set_value();
    std::vector<std::vector<Export>> exports;
    for (auto &worker : workers) exports.push_back(worker.get());
    // Oracolo sequenziale, a worker terminati. Accetta anche il cambio di secondo.
    for (std::size_t worker = 0; worker < exports.size(); ++worker) {
        const bool step = worker % 2 == 0;
        const char *format = step ? "%Y-%m-%dT%H:%M:%S" : "%Y%m%d.%H%M%S";
        for (const Export &item : exports[worker]) {
            bool matched = false;
            for (auto time = item.before; time <= item.after && !matched; ++time) {
                char expected[32];
                const std::tm *local = std::localtime(&time);
                if (local && std::strftime(expected, sizeof expected, format, local)) {
                    matched = step ? containsStepStamp(item.output, expected)
                                   : containsIgesStamps(item.output, expected);
                }
            }
            FK_CHECK(matched);
        }
    }
}

FK_TEST(ExchangeRejectsDeepStepSyntax) {
    for (const std::string &open : {std::string("("), std::string("T(")}) {
        for (int depth : {100, 100000}) {
            std::string s = "ISO-10303-21;\nDATA;\n#1=IGNORED(";
            for (int n = 0; n < depth; ++n) s += open;
            s += "0" + std::string(std::size_t(depth), ')') + ");\nENDSEC;\nEND-ISO-10303-21;\n";
            bool rejected = false;
            try { readStep(s, {1}); }
            catch (const std::domain_error &e) { rejected = std::string(e.what()).find("annidati") != std::string::npos; }
            FK_CHECK(rejected == (depth == 100000));
        }
    }
}
FK_TEST(ExchangeRejectsTruncatedSurfaceBeforeAllocation) {
    for (bool validKnots : {false, true}) {
        const auto data = truncatedSurface(validKnots);
        AllocationGuard guard;
        bool rejected = false;
        try { readBodyBinary(data); }
        catch (const std::invalid_argument &) { rejected = true; }
        catch (const std::bad_alloc &) {}
        FK_CHECK(rejected && blockedAllocation == 0);
    }
}
FK_TEST(ExchangeValidatesStepSplineCountsBeforeExpansion) {
    FK_CHECK(readStep(splineEdge("1", "(2,2)"), {1}).bodies.size() == 1);
    for (const auto &entry : std::vector<std::pair<std::string,std::string>>{
             {"1", "(100000000,2)"}, {"1", "(2.5,2)"}, {"1", "(-2,2)"}, {"1", "(1,1)"},
             {"1", "(1e99,2)"}, {"1e99", "(2,2)"}, {"2147483647", "(2,2)"}, {"1.5", "(2,2)"}}) {
        const auto s = splineEdge(entry.first, entry.second);
        AllocationGuard guard;
        const auto result = readStep(s, {1});
        FK_CHECK(result.bodies.empty() && !result.notes.empty() && !mentions(result, "bad_alloc") && blockedAllocation == 0);
    }
    for (bool rational : {false, true}) {
        const auto surface = surfaceStep(rational);
        FK_CHECK(readStep(surface, {1}).bodies.size() == 1);
        for (int axis : {0, 1}) {
            const auto marker = surface.find("B_SPLINE_SURFACE_WITH_KNOTS(");
            FK_CHECK(marker != std::string::npos);
            if (marker == std::string::npos) continue;
            const auto open = surface.find('(', marker);
            for (const std::string mult : {"(100000000,2)", "(2.5,2)", "(1e99,2)"}) {
                const auto s = replaceArgument(surface, open, axis + (rational ? 0 : 8), mult);
                AllocationGuard guard;
                const auto result = readStep(s, {1});
                FK_CHECK(result.bodies.empty() && !result.notes.empty() && !mentions(result, "bad_alloc") && blockedAllocation == 0);
            }
        }
    }
}
FK_TEST(ExchangePreservesDistinctStepOccurrences) {
    for (bool nested : {false, true}) for (const std::string shift : {"0.", "0.0000004", "0.000002"}) {
        const auto s = assembly(shift, nested);
        for (int threads : {1, 2}) {
            const auto result = readStep(s, {threads});
            FK_CHECK(result.notes.empty() && result.bodies.size() == 2);
            for (const auto &b : result.bodies) FK_CHECK_NEAR(massProperties(b.body).volume, 6., 1e-10);
            if (result.bodies.size() == 2) {
                const double dx = massProperties(result.bodies[1].body).centroid.x() - massProperties(result.bodies[0].body).centroid.x();
                FK_CHECK_NEAR(std::abs(dx), std::stod(shift), 1e-12);
            }
        }
    }
}

FK_TEST(ExchangePreservesDistinctMappedItemsAndDeduplicatesVisits) {
    const std::string original = cubeStep();
    const int shapeDefinition = firstId(original, "SHAPE_DEFINITION_REPRESENTATION");
    const int representation = firstId(original, "ADVANCED_BREP_SHAPE_REPRESENTATION");
    int id = nextId(original);
    // Due percorsi allo stesso oggetto, senza una nuova occorrenza.
    const auto duplicate = append(original, ref(id++) + "=SHAPE_DEFINITION_REPRESENTATION("
        + argument(original, shapeDefinition, 0) + "," + argument(original, shapeDefinition, 1) + ");");
    FK_CHECK(readStep(duplicate, {1}).bodies.size() == 1);

    std::string mapped = original;
    const auto add = [&](const std::string &entity) { const int n = id++; mapped = append(mapped, ref(n) + "=" + entity + ";"); return n; };
    const int point = add("CARTESIAN_POINT('',(0.,0.,0.))");
    const int z = add("DIRECTION('',(0.,0.,1.))"), x = add("DIRECTION('',(1.,0.,0.))");
    const int origin = add("AXIS2_PLACEMENT_3D(''," + ref(point) + "," + ref(z) + "," + ref(x) + ")");
    const int map = add("REPRESENTATION_MAP(" + ref(origin) + "," + ref(representation) + ")");
    const int first = add("MAPPED_ITEM(''," + ref(map) + "," + ref(origin) + ")");
    const int second = add("MAPPED_ITEM(''," + ref(map) + "," + ref(origin) + ")");
    const int rep = add("SHAPE_REPRESENTATION('',(" + ref(first) + "," + ref(second) + ")," + argument(original, representation, 2) + ")");
    mapped = replaceArgument(mapped, openOf(mapped, shapeDefinition), 1, ref(rep));
    const auto result = readStep(mapped, {1});
    FK_CHECK(result.notes.empty() && result.bodies.size() == 2);
    for (const auto &b : result.bodies) FK_CHECK_NEAR(massProperties(b.body).volume, 6., 1e-10);
}

// Le pose di controllo isolano la cancellazione nell'orientamento delle shell.
FK_TEST(ExchangePreservesTranslatedTiltedCubeOrientation) {
    for (bool tilted : {false, true}) for (double offset : {0., 3e7}) {
        const Frame3 frame(Vec3(1.2 * offset, -.9 * offset, 1.73 * offset),
                           tilted ? Vec3(1,2,3) : Vec3(0,0,1),
                           tilted ? Vec3(2,-1,0) : Vec3(1,0,0));
        ExchangeBody input;
        input.name = "posed cube";
        input.body = makeBox(frame, 1, 1, 1);
        FK_CHECK(checkBody(input.body, {true,true}).empty());
        const auto expected = massProperties(input.body);
        FK_CHECK_NEAR(expected.volume, 1., 1e-7);
        const auto check = [&](const auto &result) {
            FK_CHECK(result.notes.empty());
            FK_CHECK(result.bodies.size() == 1);
            if (result.bodies.size() != 1) return;
            const Body &body = result.bodies.front().body;
            FK_CHECK(!body.isSheet());
            FK_CHECK(body.faces().size() == 6);
            FK_CHECK(checkBody(body, {true,true}).empty());
            const auto actual = massProperties(body);
            // Budget del round trip a circa 5e7 mm, con volume firmato.
            FK_CHECK_NEAR(actual.volume, expected.volume, 1e-7);
            for (int axis = 0; axis < 3; ++axis)
                FK_CHECK_NEAR(actual.centroid[axis], expected.centroid[axis], 1e-7);
        };
        const auto step = writeStep({input});
        FK_CHECK(step.find("MANIFOLD_SOLID_BREP(") != std::string::npos);
        check(readStep(step, {1}));
        IgesWriteOptions options;
        options.mode = IgesMode::Surfaces;
        check(readIges(writeIges({input}, options), {1}));
    }
}

FK_TEST(ExchangeStepPreservesCavityShellAndDisjointSolids) {
    // La cavita' deve restare un vuoto della stessa regione, non un solido.
    const Body outer = makeBox(Frame3(), 3, 3, 3);
    const Body inner = makeBox(Frame3(Vec3(1,1,1), Vec3(0,0,1), Vec3(1,0,0)), 1, 1, 1);
    const Body cavity = booleanOperation(outer, inner, BooleanOperation::Subtract);
    FK_CHECK(cavity.shells().size() == 2);
    FK_CHECK(checkBody(cavity, {true,true}).empty());
    FK_CHECK_NEAR(massProperties(cavity).volume, 26., 1e-10);
    const Body separate = makeBox(Frame3(Vec3(10,0,0), Vec3(0,0,1), Vec3(1,0,0)), 1, 1, 1);
    const Body collection = Body::collect({&cavity, &separate});
    for (bool disjoint : {false, true}) {
        ExchangeBody input;
        input.body = disjoint ? collection : cavity;
        const auto text = writeStep({input});
        FK_CHECK(text.find("BREP_WITH_VOIDS(") != std::string::npos);
        const auto result = readStep(text, {1});
        FK_CHECK(result.notes.empty());
        FK_CHECK(result.bodies.size() == (disjoint ? 2u : 1u));
        double volume = 0.;
        int cavityCount = 0, cubeCount = 0;
        for (const auto &entry : result.bodies) {
            FK_CHECK(!entry.body.isSheet());
            FK_CHECK(checkBody(entry.body, {true,true}).empty());
            const double v = massProperties(entry.body).volume;
            volume += v;
            if (entry.body.shells().size() == 2) {
                ++cavityCount;
                FK_CHECK_NEAR(v, 26., 1e-10);
                FK_CHECK(entry.body.faces().size() == 12);
            } else {
                ++cubeCount;
                FK_CHECK_NEAR(v, 1., 1e-10);
                FK_CHECK(entry.body.faces().size() == 6);
            }
        }
        FK_CHECK(cavityCount == 1 && cubeCount == (disjoint ? 1 : 0));
        FK_CHECK_NEAR(volume, disjoint ? 27. : 26., 1e-10);
    }
}

FK_TEST(ExchangePreservesThinTiltedSolid) {
    for (double offset : {0., 3e7}) {
        const Frame3 frame(Vec3(1.2 * offset, -.9 * offset, 1.73 * offset), Vec3(1,2,3), Vec3(2,-1,0));
        ExchangeBody input;
        input.body = makeBox(frame, 1, 1, .001);
        FK_CHECK(checkBody(input.body, {true,true}).empty());
        const double budget = offset == 0. ? 1e-12 : 1e-8;
        FK_CHECK_NEAR(massProperties(input.body).volume, .001, budget);
        const auto check = [&](const auto &result) {
            FK_CHECK(result.notes.empty());
            FK_CHECK(result.bodies.size() == 1);
            if (result.bodies.size() != 1) return;
            const Body &body = result.bodies.front().body;
            FK_CHECK(!body.isSheet() && body.faces().size() == 6);
            FK_CHECK(checkBody(body, {true,true}).empty());
            FK_CHECK_NEAR(massProperties(body).volume, .001, budget);
        };
        check(readStep(writeStep({input}), {1}));
        IgesWriteOptions options;
        options.mode = IgesMode::Surfaces;
        check(readIges(writeIges({input}, options), {1}));
    }
}

namespace {
// Ricostruisce le shell in una sola regione, eventualmente invertendo
// insieme normale e contorni. Nessuna classificazione corregge la fixture.
Body singleRegion(const std::vector<std::pair<const Body *, bool>> &inputs) {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    for (const auto &input : inputs) {
        const Body &body = *input.first;
        const bool reverse = input.second;
        std::map<int, int> vertexIndex, edgeIndex;
        for (VertexId vertex : body.vertices()) {
            vertexIndex[vertex.index] = int(vertices.size());
            vertices.push_back(body.vertex(vertex).point);
        }
        for (EdgeId edge : body.edges()) {
            edgeIndex[edge.index] = int(edges.size());
            const auto &source = body.edge(edge);
            edges.push_back({vertexIndex.at(body.edgeStart(edge).index),
                             vertexIndex.at(body.edgeEnd(edge).index),
                             source.curve, source.range, source.tolerance});
        }
        for (FaceId face : body.faces()) {
            const auto &source = body.face(face);
            Body::BuildFace target;
            target.surface = source.surface;
            target.sense = reverse ? !source.sense : source.sense;
            for (LoopId loop : source.loops) {
                auto fins = body.loopFins(loop);
                if (reverse) std::reverse(fins.begin(), fins.end());
                std::vector<Body::BuildFin> cycle;
                for (FinId fin : fins) {
                    const auto &f = body.fin(fin);
                    cycle.push_back({edgeIndex.at(f.edge.index), reverse ? !f.sense : f.sense,
                                     f.pcurve, f.pcurveTolerance});
                }
                target.loops.push_back(std::move(cycle));
            }
            faces.push_back(std::move(target));
        }
    }
    return Body::build(vertices, edges, faces);
}
}

FK_TEST(ExchangeStepDiagnosesUncontainedInwardShell) {
    ExchangeBody input;
    const Body outward = makeBox(Frame3(), 1, 1, 1);
    input.body = singleRegion({{&outward, true}});
    FK_CHECK(checkBody(input.body, {true,true}).empty());
    FK_CHECK_NEAR(massProperties(input.body).volume, -1., 1e-12);
    bool diagnosed = false;
    try {
        (void)writeStep({input});
    } catch (const std::domain_error &error) {
        const std::string message = error.what();
        diagnosed = message.find("STEP:") != std::string::npos
            && message.find("shell") != std::string::npos
            && message.find("esterna") != std::string::npos;
    }
    // Nessun catch generico: errori di geometria, allocazione o altro tipo
    // restano fallimenti del runner, non diagnosi valide del caso richiesto.
    FK_CHECK(diagnosed);
}


FK_TEST(ExchangeStepRejectsCavityInConcaveExteriorNotch) {
    const Body box = makeBox(Frame3(), 4, 4, 4);
    const Body cutter = makeBox(Frame3(Vec3(1,1,-1), Vec3(0,0,1), Vec3(1,0,0)), 2, 4, 6);
    const Body concave = booleanOperation(box, cutter, BooleanOperation::Subtract);
    FK_CHECK(concave.shells().size() == 1);
    FK_CHECK(checkBody(concave, {true,true}).empty());
    FK_CHECK_NEAR(massProperties(concave).volume, 40., 1e-10);
    // Il cubo e' interamente nella tacca aperta, quindi fuori dal materiale,
    // ma il suo box [1.5,2.5]x[2,3]x[1,2] e' interno al box [0,4]^3.
    const Body falseVoid = makeBox(Frame3(Vec3(1.5,2,1), Vec3(0,0,1), Vec3(1,0,0)), 1, 1, 1);
    FK_CHECK_NEAR(massProperties(booleanOperation(concave, falseVoid, BooleanOperation::Intersect)).volume, 0., 1e-12);
    ExchangeBody input;
    input.body = singleRegion({{&concave, false}, {&falseVoid, true}});
    FK_CHECK(input.body.shells().size() == 2);
    const auto shells = input.body.shells();
    FK_CHECK(input.body.shell(shells[0]).region == input.body.shell(shells[1]).region);
    FK_CHECK(checkBody(input.body, {true,true}).empty());
    FK_CHECK_NEAR(massProperties(input.body).volume, 39., 1e-10);
    bool diagnosed = false;
    try {
        (void)writeStep({input});
    } catch (const std::domain_error &error) {
        const std::string message = error.what();
        diagnosed = message.find("STEP:") != std::string::npos
            && message.find("shell") != std::string::npos
            && message.find("esterna") != std::string::npos;
    }
    FK_CHECK(diagnosed);
}

FK_TEST(ExchangeStepDiagnosesZeroVolumeClosedShell) {
    const std::vector<Vec3> vertices{{0,0,0}, {1,0,0}, {0,1,0}};
    std::vector<Body::BuildEdge> edges;
    for (int i = 0; i < 3; ++i) {
        const int end = (i + 1) % 3;
        const Vec3 direction = vertices[end] - vertices[i];
        edges.push_back({i, end, std::make_shared<Line<3>>(vertices[i], direction),
                         {0., norm(direction)}, 0.});
    }
    // Due facce esattamente coincidenti, con normali e co-edge opposti:
    // shell topologicamente chiusa, volume nullo per identita' geometrica.
    Body::BuildFace front, back;
    front.surface = back.surface = std::make_shared<Plane>(Frame3());
    front.sense = true;
    front.loops = {{{0,true,{},0}, {1,true,{},0}, {2,true,{},0}}};
    back.sense = false;
    back.loops = {{{2,false,{},0}, {1,false,{},0}, {0,false,{},0}}};
    ExchangeBody input;
    input.body = Body::build(vertices, edges, {front, back});
    FK_CHECK(!input.body.isSheet());
    FK_CHECK(input.body.shells().size() == 1 && input.body.faces().size() == 2);
    for (EdgeId edge : input.body.edges()) FK_CHECK(!input.body.isLaminar(edge));
    FK_CHECK_NEAR(massProperties(input.body).volume, 0., 0.);
    bool diagnosed = false;
    try {
        (void)writeStep({input});
    } catch (const std::domain_error &error) {
        const std::string message = error.what();
        diagnosed = message.find("orientamento") != std::string::npos
            && message.find("shell") != std::string::npos;
    }
    // Altri tipi d'eccezione non soddisfano il contratto sul segno incerto.
    FK_CHECK(diagnosed);
}
