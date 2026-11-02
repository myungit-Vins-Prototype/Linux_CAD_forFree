#include "fk_test.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_step.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
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
