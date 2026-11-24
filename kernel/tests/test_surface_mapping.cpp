#include "fk_surface_mapping.h"
#include "fk_surface_mapping_bounds.h"
#include "fk_mapping_trim_witness.h"
#include "fk_quilt_mapping.h"
#include "fk_bspline_surface.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_body_io.h"
#include "fk_step.h"
#include "fk_tessellate.h"
#include "fk_pcurve.h"
#include "fk_planar.h"
#include <algorithm>
#include <array>
#include "fk_curve_algo.h"
#include "fk_classify_uv.h"
#include "fk_curve_surface.h"
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
namespace {
constexpr Vec2 holeCenter(.312345,.172345);
constexpr double holeHalfX=.00004,holeHalfY=.00003;
Body planarRectangleWithHole(double halfX,double halfY,double z) {
    Body::BuildFace face;
    face.surface=std::make_shared<Plane>(Frame3(Vec3(0,0,z),Vec3(0,0,1),Vec3(1,0,0)));
    const std::vector<Vec3> points{{-halfX,-halfY,z},{halfX,-halfY,z},{halfX,halfY,z},{-halfX,halfY,z},
        {holeCenter.x()-holeHalfX,holeCenter.y()-holeHalfY,z},{holeCenter.x()-holeHalfX,holeCenter.y()+holeHalfY,z},
        {holeCenter.x()+holeHalfX,holeCenter.y()+holeHalfY,z},{holeCenter.x()+holeHalfX,holeCenter.y()-holeHalfY,z}};
    std::vector<Body::BuildEdge> edges;
    for(int start:{0,4}) {
        std::vector<Body::BuildFin> loop;
        for(int i=0;i<4;++i) {
            const int from=start+i,to=start+(i+1)%4;
            const auto direction=points[to]-points[from];
            edges.push_back({from,to,std::make_shared<Line<3>>(points[from],direction),{0,norm(direction)},0});
            loop.push_back({from,true,nullptr,0});
        }
        face.loops.push_back(std::move(loop));
    }
    auto sheet=Body::buildSheet(points,edges,{face});
    require(computePCurves(sheet)==0&&checkBody(sheet,{true,true}).empty(),"fixture piana con foro non valida");
    return sheet;
}

void checkInteriorHoleInFaceWrap() {
    const auto target=planarRectangleWithHole(2.,2.,0.);
    const auto distant=makePlaneSheet(Frame3(Vec3(5.,0.,0.),Vec3(0,0,1),Vec3(1,0,0)),1.);
    const auto plug=makePlaneSheet(Frame3(Vec3(holeCenter.x(),holeCenter.y(),0.),Vec3(0,0,1),Vec3(1,0,0)),2.*holeHalfX);
    const auto source=makePlaneSheet(Frame3(Vec3(0.,0.,.02),Vec3(0,0,1),Vec3(1,0,0)),1.);
    const auto perforated=planarRectangleWithHole(1.,1.,.02);
    require(FaceParameterClassifier(target,target.faces().front(),1e-8).classify(holeCenter)==PointLocation::Outside
            &&FaceParameterClassifier(source,source.faces().front(),1e-8).classify(holeCenter)==PointLocation::Inside
            &&FaceParameterClassifier(perforated,perforated.faces().front(),1e-8).classify(holeCenter)==PointLocation::Outside,
            "foro fuori griglia e materiale sorgente attesi");
    const SurfaceTargetFace primary{&target,target.faces().front()};
    const std::vector<SurfaceTargetFace> open{primary,{&distant,distant.faces().front()}};
    const std::vector<SurfaceTargetFace> covered{primary,{&plug,plug.faces().front()}};
    const SurfaceWrapOptions options;
    const double holeArea=4.*holeHalfX*holeHalfY;
    for(const auto *input:{&perforated,&source}) {
        const bool hasHole=input==&perforated;
        double deviation=-1.;
        const auto result=wrapBodyOnFaces(*input,hasHole?open:covered,options,nullptr,&deviation);
        require(result.isSheet()&&result.faces().size()==1&&checkBody(result,{true,true}).empty()
                &&std::isfinite(deviation)&&deviation>=0.&&deviation<=options.tolerance
                &&result.face(result.faces().front()).loops.size()==(hasHole?2u:1u)
                &&std::fabs(faceArea(result,result.faces().front())-(4.-(hasHole?holeArea:0.)))<1e-10,
                "foro sorgente allineato o seconda faccia di copertura devono consentire la deformazione");
    }
    const auto before=writeBodyBinary(source);
    for(bool provideHistory:{false,true}) {
        auto history=sameIndexHistory(perforated,perforated);
        const auto face=perforated.faces().front().index;
        const auto evolution=history.evolution(EntityKind::Face,0,face);
        const auto edges=history.resultSize(EntityKind::Edge);
        double deviation=-1.;
        bool rejected=false;
        try{(void)wrapBodyOnFaces(source,open,options,provideHistory?&history:nullptr,&deviation);}
        catch(const std::domain_error &e){rejected=std::string(e.what()).find("un punto non incontra")!=std::string::npos;}
        require(rejected&&deviation==-1.&&history.resultSize(EntityKind::Edge)==edges
                &&history.evolution(EntityKind::Face,0,face)==evolution&&writeBodyBinary(source)==before,
                "foro interno multifaccia rilevato prima di pubblicare deviazione e genealogia");
    }
}
void checkHoleInCylindricalFaceWrap() {
    constexpr double radius = 200.;
    const auto cylinder = std::make_shared<CylindricalSurface>(Frame3(), radius);
    const auto toU = [](double x) { return kHalfPi + std::asin(x / radius); };
    const auto patch = [&](const std::vector<std::array<double, 4>> &rectangles) {
        std::vector<Vec3> points;
        std::vector<Body::BuildEdge> edges;
        Body::BuildFace face;
        face.surface = cylinder;
        for (const auto &rectangle : rectangles) {
            const double u0 = toU(rectangle[0]), u1 = toU(rectangle[1]);
            const double v0 = rectangle[2], v1 = rectangle[3];
            const int start = static_cast<int>(points.size());
            points.insert(points.end(), {cylinder->point(u0, v0), cylinder->point(u1, v0),
                                         cylinder->point(u1, v1), cylinder->point(u0, v1)});
            edges.push_back({start, start + 1, cylinder->vIso(v0), {u0, u1}, 0});
            edges.push_back({start + 1, start + 2, cylinder->uIso(u1), {v0, v1}, 0});
            edges.push_back({start + 3, start + 2, cylinder->vIso(v1), {u0, u1}, 0});
            edges.push_back({start, start + 3, cylinder->uIso(u0), {v0, v1}, 0});
            if (face.loops.empty())
                face.loops.push_back({{start, true, nullptr, 0}, {start + 1, true, nullptr, 0},
                                      {start + 2, false, nullptr, 0}, {start + 3, false, nullptr, 0}});
            else
                face.loops.push_back({{start + 3, true, nullptr, 0}, {start + 2, true, nullptr, 0},
                                      {start + 1, false, nullptr, 0}, {start, false, nullptr, 0}});
        }
        auto body = Body::buildSheet(points, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "archi e generatrici dei trim cilindrici devono formare una pezza valida");
        return body;
    };

    const double x = holeCenter.x(), y = holeCenter.y();
    const double y0 = y - holeHalfY, y1 = y + holeHalfY;
    const auto target = patch({{-2., 2., -2., 2.}, {x - holeHalfX, x + holeHalfX, y0, y1}});
    const auto partial = patch({{x - .25 * holeHalfX, x + 1.25 * holeHalfX, y0 - holeHalfY, y1 + holeHalfY}});
    const auto complete = patch({{x - 1.25 * holeHalfX, x + 1.25 * holeHalfX, y0 - holeHalfY, y1 + holeHalfY}});
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto sourceBefore = writeBodyBinary(source);
    const auto targetBefore = writeBodyBinary(target);
    const Vec2 witnessUV(toU(x - .75 * holeHalfX), y), centerUV(toU(x), y);
    const auto classify = [](const Body &body, const Vec2 &uv) {
        return FaceParameterClassifier(body, body.faces().front(), 1e-8).classify(uv);
    };
    require(classify(target, witnessUV) == PointLocation::Outside
            && classify(partial, centerUV) == PointLocation::Inside
            && classify(partial, witnessUV) == PointLocation::Outside
            && classify(complete, witnessUV) == PointLocation::Inside,
            "il tappo cilindrico parziale copre il centro del foro e lascia scoperta una striscia");

    const SurfaceTargetFace support{&target, target.faces().front()};
    SurfaceWrapOptions options;
    options.anchor = {kHalfPi, 0.};
    double deviation = -1.;
    const auto mapped = wrapBodyOnFaces(source, {support, {&complete, complete.faces().front()}},
                                        options, nullptr, &deviation);
    require(mapped.isSheet() && mapped.faces().size() == 1 && checkBody(mapped, {true, true}).empty()
            && std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance,
            "il tappo cilindrico completo deve sostenere il wrap");
    for (auto vertex : mapped.vertices()) {
        const auto &p = mapped.vertex(vertex).point;
        require(std::fabs(std::hypot(p.x(), p.y()) - radius) <= options.tolerance,
                "i vertici deformati devono giacere sul cilindro");
    }

    for (const auto &supports : {std::vector<SurfaceTargetFace>{support},
                                 std::vector<SurfaceTargetFace>{support, {&partial, partial.faces().front()}}}) {
        deviation = -1.;
        auto history = sameIndexHistory(source, source);
        const auto evolution = history.evolution(EntityKind::Face, 0, source.faces().front().index);
        bool rejected = false;
        try {
            (void)wrapBodyOnFaces(source, supports, options, &history, &deviation);
        } catch (const std::domain_error &e) {
            rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
        }
        require(rejected && deviation == -1. && history.resultSize(EntityKind::Face) == 1
                && history.evolution(EntityKind::Face, 0, source.faces().front().index) == evolution,
                "il foro cilindrico scoperto deve fallire prima di pubblicare deviazione e genealogia");
        require(writeBodyBinary(source) == sourceBefore && writeBodyBinary(target) == targetBefore,
                "il rifiuto lascia invariati la sorgente e il supporto cilindrico");
    }

    // Every cylindrical patch has a single outer loop. The four projected
    // trims enclose the same off-grid hole as the single perforated patch.
    const auto left = patch({{-2., x - holeHalfX, -2., 2.}});
    const auto right = patch({{x + holeHalfX, 2., -2., 2.}});
    const auto below = patch({{x - holeHalfX, x + holeHalfX, -2., y0}});
    const auto above = patch({{x - holeHalfX, x + holeHalfX, y1, 2.}});
    const auto selected = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
    for (const auto *part : {&left, &right, &below, &above})
        require(part->face(part->faces().front()).loops.size() == 1
                && classify(*part, witnessUV) == PointLocation::Outside,
                "la striscia deve essere fuori dai quattro contorni esterni cilindrici");

    std::vector<SurfaceTargetFace> quilt{selected(left), selected(right), selected(below), selected(above)};
    for (const auto *plug : {&complete, &partial}) {
        auto supports = quilt;
        supports.push_back(selected(*plug));
        if (plug == &partial)
            std::reverse(supports.begin(), supports.end());
        deviation = -1.;
        auto history = sameIndexHistory(source, source);
        const auto face = source.faces().front().index;
        const auto evolution = history.evolution(EntityKind::Face, 0, face);
        if (plug == &complete) {
            const auto filled = wrapBodyOnFaces(source, supports, options, &history, &deviation);
            require(filled.isSheet() && filled.faces().size() == 1 && checkBody(filled, {true, true}).empty()
                    && std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance
                    && history.descendants(EntityKind::Face, 0, face).size() == 1,
                    "il tappo completo fra i contorni esterni cilindrici consente il wrap");
        } else {
            bool rejected = false;
            try {
                (void)wrapBodyOnFaces(source, supports, options, &history, &deviation);
            } catch (const std::domain_error &e) {
                rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
            }
            require(rejected && deviation == -1. && history.resultSize(EntityKind::Face) == 1
                    && history.evolution(EntityKind::Face, 0, face) == evolution,
                    "la striscia fra contorni esterni cilindrici va rilevata prima di pubblicare il risultato");
            require(writeBodyBinary(source) == sourceBefore, "il rifiuto non modifica la sorgente");
        }
    }
}

void checkPartlyCoveredCircularHoleInFaceWrap() {
    constexpr double radius = .00005;
    const Vec2 center(.312345, .172345);
    const auto circle = [](const Vec2 &at, double r) {
        return ProfileLoop{{{std::make_shared<Circle<2>>(makeCircle(at, r)), {0., kTwoPi}}}};
    };
    ProfileRegion plate;
    const std::array<Vec2, 4> corners{{{-2., -2.}, {2., -2.}, {2., 2.}, {-2., 2.}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec2 edge = corners[(i + 1) % corners.size()] - corners[i];
        plate.outer.segments.push_back({std::make_shared<Line<2>>(corners[i], edge), {0., norm(edge)}});
    }
    plate.holes = {circle(center, radius)};
    const auto target = planarSheet(Frame3(), {plate});
    const auto partial = planarSheet(Frame3(), {{circle(center + Vec2(.5 * radius, 0.), radius), {}}});
    const auto complete = planarSheet(Frame3(), {{circle(center, 1.25 * radius), {}}});
    const auto source = makePlaneSheet(Frame3(Vec3(0., 0., -.02), Vec3(0., 0., 1.), Vec3(1., 0., 0.)), 1.);
    SurfaceWrapOptions options;
    options.angle = .2;
    options.shift = {.05, -.08};
    options.normalOffset = .005;
    const Vec2 uncovered = center - Vec2(.8 * radius, 0.);
    const auto classify = [](const Body &body, const Vec2 &p) {
        return FaceParameterClassifier(body, body.faces().front(), 1e-8).classify(p);
    };
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const Vec2 fromShift = uncovered - options.shift;
    const Vec2 sourceWitness(c * fromShift.x() + s * fromShift.y(), -s * fromShift.x() + c * fromShift.y());
    require(classify(source, sourceWitness) == PointLocation::Inside
            && classify(target, uncovered) == PointLocation::Outside
            && classify(partial, uncovered) == PointLocation::Outside
            && classify(partial, center) == PointLocation::Inside
            && classify(complete, uncovered) == PointLocation::Inside,
            "il tappo eccentrico sostiene il centro ma lascia una falce di materiale scoperta");

    const SurfaceTargetFace support{&target, target.faces().front()};
    double deviation = -1.;
    const auto mapped = wrapBodyOnFaces(source, {support, {&complete, complete.faces().front()}}, options, nullptr, &deviation);
    require(mapped.isSheet() && mapped.faces().size() == 1 && checkBody(mapped, {true, true}).empty()
            && std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance
            && std::fabs(faceArea(mapped, mapped.faces().front()) - 4.) < 1e-9,
            "tappo circolare completo sostiene lo schizzo ruotato e traslato");
    for (auto vertex : mapped.vertices())
        require(std::fabs(mapped.vertex(vertex).point.z() + .015) < 1e-9,
                "quota negativa conservata sul supporto piano");

    deviation = -1.;
    bool rejected = false;
    try {
        (void)wrapBodyOnFaces(source, {support, {&partial, partial.faces().front()}}, options, nullptr, &deviation);
    } catch (const std::domain_error &e) {
        rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
    }
    require(rejected && deviation == -1., "la falce scoperta deve rifiutare la deformazione prima di pubblicare il residuo");
}

void checkSplineHoleInFaceWrap() {
    constexpr Vec2 center(.312345, .172345);
    constexpr double width = .0002, height = .0002;
    const auto line = [](ProfileLoop &loop, const Vec2 &a, const Vec2 &b) {
        const Vec2 edge = b - a;
        loop.segments.push_back({std::make_shared<Line<2>>(a, edge), {0., norm(edge)}});
    };
    ProfileRegion profile;
    const std::array<Vec2, 4> corners{{{-2., -2.}, {2., -2.}, {2., 2.}, {-2., 2.}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
        line(profile.outer, corners[i], corners[(i + 1) % corners.size()]);
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto complete = planarSheet(Frame3(), {profile});
    const auto secondary = makePlaneSheet(Frame3(Vec3(-.5, 0., 0.), Vec3(0., 0., 1.), Vec3(1., 0., 0.)), .2);
    const SurfaceTargetFace second{&secondary, secondary.faces().front()};
    const Vec2 uncovered = center + Vec2(0., .25 * height);
    const Vec2 boxMidpoint = center + Vec2(0., .5 * height);
    double deviation = -1.;
    const auto mapped = wrapBodyOnFaces(source, {{&complete, complete.faces().front()}, second},
                                        SurfaceWrapOptions(), nullptr, &deviation);
    require(checkBody(mapped, {true, true}).empty() && std::fabs(faceArea(mapped, mapped.faces().front()) - 4.) < 1e-9
            && std::isfinite(deviation) && deviation >= 0. && deviation <= SurfaceWrapOptions().tolerance,
            "piastre complanari senza foro conservano l'area");

    const auto before = writeBodyBinary(source);
    for (const auto &weights : {std::vector<double>{}, std::vector<double>{1., 1., .8, 1., 1.}}) {
        std::vector<Vec2> poles;
        const std::array<double, 5> ordinates{{0., .1, 1., .1, 0.}};
        for (std::size_t i = 0; i < ordinates.size(); ++i)
            poles.push_back(center + Vec2((.5 * i - 1.) * width, ordinates[i] * height));
        auto bow = std::make_shared<BSplineCurve<2>>(4, std::vector<double>{0., 0., 0., 0., 0., 1., 1., 1., 1., 1.},
                                                    poles, weights);
        ProfileLoop hole;
        hole.segments.push_back({bow, {0., 1.}});
        line(hole, poles.back(), poles.front());
        profile.holes = {hole};
        const auto open = planarSheet(Frame3(), {profile});
        const auto openFace = open.faces().front();
        const auto classifier = FaceParameterClassifier(open, openFace, 1e-8);
        require(checkBody(open, {true, true}).empty() && open.face(openFace).loops.size() == 2
                && classifier.classify(uncovered) == PointLocation::Outside
                && classifier.classify(boxMidpoint) == PointLocation::Inside
                && FaceParameterClassifier(source, source.faces().front(), 1e-8).classify(uncovered) == PointLocation::Inside,
                "foro B-spline sotto il centro del box dei poli, con materiale sorgente al suo interno");

        auto history = sameIndexHistory(source, source);
        const int sourceFace = source.faces().front().index;
        const auto evolution = history.evolution(EntityKind::Face, 0, sourceFace);
        deviation = -1.;
        bool rejected = false;
        try {
            (void)wrapBodyOnFaces(source, {{&open, openFace}, second}, SurfaceWrapOptions(), &history, &deviation);
        } catch (const std::domain_error &e) {
            rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
        }
        require(rejected && deviation == -1. && history.resultSize(EntityKind::Face) == 1
                && history.evolution(EntityKind::Face, 0, sourceFace) == evolution && writeBodyBinary(source) == before,
                "estremo interno B-spline rivela il foro prima di pubblicare deviazione e genealogia");
    }
}

void checkEnclosedCurvedGapBetweenFaceTrims() {
    constexpr double radius = .00005;
    const Vec2 center(.312345, .172345);
    const auto circle = [](const Vec2 &at, double r) {
        return ProfileLoop{{{std::make_shared<Circle<2>>(makeCircle(at, r)), {0., kTwoPi}}}};
    };
    const auto line = [](ProfileLoop &loop, const Vec2 &a, const Vec2 &b) {
        const Vec2 edge = b - a;
        loop.segments.push_back({std::make_shared<Line<2>>(a, edge), {0., norm(edge)}});
    };
    const auto panel = [&](bool left) {
        const double x = center.x(), y = center.y();
        const Vec2 bottom(x, -2.), top(x, 2.);
        const double side = left ? -2. : 2.;
        const Vec2 outerBottom(side, -2.), outerTop(side, 2.);
        const double from = left ? kPi / 2. : 3. * kPi / 2.;
        const auto arc = std::make_shared<Circle<2>>(center, Vec2(1., 0.), Vec2(0., -1.), radius);
        ProfileRegion profile;
        line(profile.outer, left ? outerBottom : outerTop, left ? bottom : top);
        line(profile.outer, left ? bottom : top, arc->point(from));
        profile.outer.segments.push_back({arc, {from, from + kPi}});
        line(profile.outer, arc->point(from + kPi), left ? top : bottom);
        line(profile.outer, left ? top : bottom, left ? outerTop : outerBottom);
        line(profile.outer, left ? outerTop : outerBottom, left ? outerBottom : outerTop);
        return planarSheet(Frame3(), {profile});
    };
    const auto left = panel(true), right = panel(false);
    const auto partial = planarSheet(Frame3(), {{circle(center + Vec2(.5 * radius, 0.), radius), {}}});
    const auto complete = planarSheet(Frame3(), {{circle(center, 1.25 * radius), {}}});
    const Frame3 frame(Vec3(0., 0., -.02), Vec3(0., 0., 1.), Vec3(1., 0., 0.));
    const auto source = makePlaneSheet(frame, 1.);
    SurfaceWrapOptions options;
    options.angle = .2;
    options.shift = {.05, -.08};
    options.normalOffset = .005;
    const auto toSource = [&](const Vec2 &p) {
        const Vec2 shifted = p - options.shift;
        const double c = std::cos(options.angle), s = std::sin(options.angle);
        return Vec2(c * shifted.x() + s * shifted.y(), -s * shifted.x() + c * shifted.y());
    };
    const Vec2 uncovered = center - Vec2(.8 * radius, 0.);
    const Vec2 sourceWitness = toSource(uncovered);
    ProfileRegion sourceProfile;
    const std::array<Vec2, 4> corners{{{-1., -1.}, {1., -1.}, {1., 1.}, {-1., 1.}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
        line(sourceProfile.outer, corners[i], corners[(i + 1) % corners.size()]);
    sourceProfile.holes = {circle(toSource(center), 1.25 * radius)};
    const auto perforated = planarSheet(frame, {sourceProfile});
    const auto classify = [](const Body &body, const Vec2 &p) {
        return FaceParameterClassifier(body, body.faces().front(), 1e-8).classify(p);
    };
    for (const auto *piece : {&left, &right, &partial, &complete}) {
        require(piece->faces().size() == 1 && piece->face(piece->faces().front()).loops.size() == 1
                && checkBody(*piece, {true, true}).empty(), "pezza incurvata senza loop interno");
    }
    require(classify(source, sourceWitness) == PointLocation::Inside
            && classify(perforated, sourceWitness) == PointLocation::Outside
            && classify(left, uncovered) == PointLocation::Outside
            && classify(right, uncovered) == PointLocation::Outside
            && classify(partial, uncovered) == PointLocation::Outside
            && classify(partial, center) == PointLocation::Inside
            && classify(complete, uncovered) == PointLocation::Inside,
            "gli intagli e il disco decentrato lasciano una falce sopra il materiale sorgente");

    const std::vector<SurfaceTargetFace> partlyCovered{
        {&left, left.faces().front()}, {&right, right.faces().front()}, {&partial, partial.faces().front()}};
    auto fullyCovered = partlyCovered;
    fullyCovered.back() = {&complete, complete.faces().front()};
    for (const auto *input : {&source, &perforated}) {
        const bool hasHole = input == &perforated;
        double deviation = -1.;
        const auto result = wrapBodyOnFaces(*input, hasHole ? partlyCovered : fullyCovered, options, nullptr, &deviation);
        require(result.isSheet() && result.faces().size() == 1
                && result.face(result.faces().front()).loops.size() == (hasHole ? 2u : 1u)
                && checkBody(result, {true, true}).empty()
                && std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance
                && std::fabs(faceArea(result, result.faces().front()) - faceArea(*input, input->faces().front())) < 1e-9,
                "il tappo completo o il foro sorgente più ampio conservano la lamina");
    }

    const auto before = writeBodyBinary(source);
    auto history = sameIndexHistory(perforated, perforated);
    const auto face = perforated.faces().front().index;
    const auto evolution = history.evolution(EntityKind::Face, 0, face);
    const auto edges = history.resultSize(EntityKind::Edge);
    double deviation = -1.;
    bool rejected = false;
    try {
        (void)wrapBodyOnFaces(source, partlyCovered, options, &history, &deviation);
    } catch (const std::domain_error &e) {
        rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
    }
    require(rejected && deviation == -1. && writeBodyBinary(source) == before
            && history.resultSize(EntityKind::Edge) == edges
            && history.evolution(EntityKind::Face, 0, face) == evolution,
            "la falce fra contorni curvi rifiuta la lamina piena prima di pubblicare il risultato");
}

void checkContinuousOrthogonalTrimCoverage() {
    using Coverage = detail::MappingTrimCoverage;
    using Rectangle = std::array<double, 4>;
    const auto polygon = [](const std::vector<std::vector<Vec2>> &loops) {
        std::vector<Vec3> points;
        std::vector<Body::BuildEdge> edges;
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        for (const auto &loop : loops) {
            const int first = static_cast<int>(points.size());
            for (const auto &point : loop)
                points.emplace_back(point.x(), point.y(), 0.);
            face.loops.emplace_back();
            for (int i = 0; i < static_cast<int>(loop.size()); ++i) {
                const int from = first + i, to = first + (i + 1) % static_cast<int>(loop.size());
                const auto direction = points[to] - points[from];
                const double length = std::max(std::abs(direction.x()), std::abs(direction.y()));
                edges.push_back({from, to, std::make_shared<Line<3>>(points[from], direction / length), {0., length}, 0.});
                face.loops.back().push_back({from, true, nullptr, 0.});
            }
        }
        auto body = Body::buildSheet(points, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(), "poligono ortogonale esatto valido");
        return body;
    };
    const auto rectangle = [&](double x0, double x1, double y0, double y1, const Rectangle *hole = nullptr) {
        std::vector<std::vector<Vec2>> loops{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
        if (hole) {
            const auto &[left, right, bottom, top] = *hole;
            loops.push_back({{left, bottom}, {left, top}, {right, top}, {right, bottom}});
        }
        return polygon(loops);
    };
    const auto selected = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
    const auto source = rectangle(.28125, .375, .28125, .375);
    constexpr double seam = .3125;
    const double next = std::nextafter(seam, 1.);
    const auto left = rectangle(.25, seam, .25, .5);
    const auto right = rectangle(seam, .5, .25, .5);
    const auto rightWithGap = rectangle(next, .5, .25, .5);
    const auto rightWithWideGap = rectangle(seam + 0x1p-22, .5, .25, .5);
    const std::vector<SurfaceTargetFace> covered{selected(left), selected(right)};
    const std::vector<SurfaceTargetFace> gap{selected(left), selected(rightWithGap)};
    const std::vector<SurfaceTargetFace> wideGap{selected(left), selected(rightWithWideGap)};
    const auto certify = [&](const std::vector<SurfaceTargetFace> &targets, const Frame3 &chart = Frame3()) {
        return detail::certifyMappingTrimCoverage(source, targets, Frame3(), chart);
    };
    require(certify(covered) == Coverage::Covered, "certifica rettangoli contigui lungo l'intera giunzione");
    require(certify(gap) == Coverage::Uncovered, "rileva la lacuna aperta fra due coordinate double consecutive");
    require(detail::certifyMappingTrimCoverage(source, gap, Frame3(), Frame3(), linearResolution()) ==
            Coverage::Inconclusive, "una lacuna sotto la precisione di contatto lascia la prova inconclusiva");
    constexpr Rectangle hole{.3125, .34375, .3125, .34375};
    const auto perforatedTarget = rectangle(.25, .5, .25, .5, &hole);
    const auto perforatedSource = rectangle(.28125, .375, .28125, .375, &hole);
    require(certify({selected(perforatedTarget)}) == Coverage::Uncovered,
            "il foro interno lascia scoperto il materiale sorgente");
    require(detail::certifyMappingTrimCoverage(perforatedSource, {selected(perforatedTarget)}, Frame3(), Frame3()) ==
            Coverage::Covered, "i bordi dei due fori coincidono e la sorgente esclude il vuoto");
    const auto concaveTarget = polygon({{{.25, .25}, {.5, .25}, {.5, .34375},
                                           {.34375, .34375}, {.34375, .5}, {.25, .5}}});
    const auto concaveSource = polygon({{{.28125, .28125}, {.375, .28125}, {.375, .34375},
                                           {.34375, .34375}, {.34375, .375}, {.28125, .375}}});
    require(certify({selected(concaveTarget)}) == Coverage::Uncovered,
            "il rientro concavo del supporto lascia scoperta la sorgente piena");
    require(detail::certifyMappingTrimCoverage(concaveSource, {selected(concaveTarget)}, Frame3(), Frame3()) ==
            Coverage::Covered, "il rientro concavo coincidente lascia la sorgente interamente coperta");
    auto repeated = std::vector<SurfaceTargetFace>(128, selected(left));
    require(certify(repeated) == Coverage::Inconclusive, "oltre il budget la copertura resta inconclusiva");
    require(certify(covered, Frame3(Vec3(), Vec3(0., 0., 1.), Vec3(1., 1., 0.))) == Coverage::Inconclusive,
            "riferimento non cartesiano fuori dalla famiglia certificata");

    const auto result = wrapBodyOnFaces(source, covered, SurfaceWrapOptions());
    require(result.isSheet() && checkBody(result, {true, true}).empty(), "wrap valido attraverso la giunzione");
    const auto tolerant = wrapBodyOnFaces(source, gap, SurfaceWrapOptions());
    require(tolerant.isSheet() && checkBody(tolerant, {true, true}).empty(), "wrap valido entro la precisione di contatto");
    auto history = sameIndexHistory(source, source);
    const auto sourceId = source.faces().front().index;
    const auto evolution = history.evolution(EntityKind::Face, 0, sourceId);
    double deviation = -1.;
    bool rejected = false;
    try {
        (void)wrapBodyOnFaces(source, wideGap, SurfaceWrapOptions(), &history, &deviation);
    } catch (const std::domain_error &e) {
        const std::string message(e.what());
        rejected = message.find("copertura continua dei trim") != std::string::npos
            && message.find("Punto nel piano sorgente: X=") != std::string::npos;
    }
    require(rejected && deviation == -1. && history.resultSize(EntityKind::Face) == 1 &&
            history.evolution(EntityKind::Face, 0, sourceId) == evolution,
            "il certificato rifiuta la lacuna prima di pubblicare deviazione e genealogia");
}

void checkContinuousConvexTrimCoverage() {
    using Coverage = detail::MappingTrimCoverage;
    const auto circle = [](const Vec2 &center, double radius, bool counterclockwise = true) {
        const auto curve = std::make_shared<Circle<2>>(center, Vec2(1., 0.),
                                                       Vec2(0., counterclockwise ? 1. : -1.), radius);
        return ProfileLoop{{{curve, {0., kTwoPi}}}};
    };
    const auto disk = [&](double radius, std::vector<ProfileLoop> holes = {}) {
        return planarSheet(Frame3(), {{circle(Vec2(), radius), std::move(holes)}});
    };
    const auto triangle = [](bool above) {
        const auto diagonal = std::make_shared<Line<3>>(Vec3(), Vec3(3., 4., 0.));
        const auto end = diagonal->point(1.);
        const Vec3 corner = above ? Vec3(0., end.y(), 0.) : Vec3(end.x(), 0., 0.);
        const std::vector<Vec3> points{Vec3(), end, corner};
        const std::vector<Body::BuildEdge> edges{
            {0, 1, diagonal, {0., 1.}, 0.},
            {1, 2, std::make_shared<Line<3>>(end, corner - end), {0., distance(end, corner)}, 0.},
            {2, 0, std::make_shared<Line<3>>(corner, -corner), {0., norm(corner)}, 0.}
        };
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops = above
            ? std::vector<std::vector<Body::BuildFin>>{{{0, true, nullptr, 0.}, {1, true, nullptr, 0.}, {2, true, nullptr, 0.}}}
            : std::vector<std::vector<Body::BuildFin>>{{{2, false, nullptr, 0.}, {1, false, nullptr, 0.}, {0, false, nullptr, 0.}}};
        auto body = Body::buildSheet(points, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(), "triangolo obliquo valido");
        return body;
    };
    Vec2 gap;
    const auto certify = [&](const Body &source, const Body &target, double tolerance = linearResolution()) {
        return detail::certifyMappingTrimCoverage(source, {{&target, target.faces().front()}}, Frame3(), Frame3(),
                                                  tolerance, &gap);
    };
    const auto above = triangle(true), below = triangle(false);
    require(certify(above, above) == Coverage::Covered, "coincidenza continua del bordo obliquo");
    require(certify(below, above) == Coverage::Uncovered && gap.x() > 0. && gap.x() < .6 &&
            gap.y() > 0. && gap.y() < .8 && .6 * gap.y() < .8 * gap.x(),
            "lacuna oltre il bordo obliquo entro le stesse estensioni XY");

    const auto crossing = planarSheet(Frame3(), {{circle(Vec2(.3, .4), .125), {}}});
    const std::vector<SurfaceTargetFace> quilt{{&above, above.faces().front()}, {&below, below.faces().front()}};
    require(certify(crossing, above) == Coverage::Uncovered && certify(crossing, below) == Coverage::Uncovered,
            "il disco attraversa ciascun lato della diagonale");
    require(detail::certifyMappingTrimCoverage(crossing, quilt, Frame3(), Frame3()) == Coverage::Covered,
            "la giunzione obliqua delle due pezze copre l'intero disco");

    ProfileLoop wedge;
    wedge.segments = {
        {std::make_shared<Line<2>>(Vec2(), Vec2(0., 1.)), {0., .8}},
        {std::make_shared<Line<2>>(Vec2(0., .8), Vec2(-3., -4.)), {0., .5}},
        {std::make_shared<Line<2>>(Vec2(-.3, .4), Vec2(3., -4.)), {0., .5}}};
    const auto third = planarSheet(Frame3(), {{wedge, {}}});
    // The oblique left patch reaches only part of the right patch's
    // vertical side. Their union has a reentrant corner at (.0,.8).
    const auto vertical = makePlaneSheet(Frame3({.5, .5, 0.}, {0., 0., 1.}, {1., 0., 0.}), .5);
    const auto acrossPartialSeam = planarSheet(Frame3(), {{circle(Vec2(.0, .4), .125), {}}});
    const std::vector<SurfaceTargetFace> partial{{&vertical, vertical.faces().front()},
                                                  {&third, third.faces().front()}};
    require(detail::certifyMappingTrimCoverage(acrossPartialSeam, partial, Frame3(), Frame3()) == Coverage::Covered,
            "fasce continue su unione concava con giunzione parziale");
    SurfaceWrapOptions partialOptions;
    partialOptions.anchor = {-.5, -.5};
    const auto wrapped = wrapBodyOnFaces(acrossPartialSeam, partial, partialOptions);
    require(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty(),
            "wrap multifaccia valido attraverso la giunzione parziale");
    const auto missingSliver = planarSheet(Frame3(), {{circle(Vec2(.0, .4), .25), {}}});
    require(detail::certifyMappingTrimCoverage(missingSliver, partial, Frame3(), Frame3()) != Coverage::Covered,
            "le fasce non possono certificare una copertura oltre il bordo obliquo");

    const auto acrossThree = planarSheet(Frame3(), {{circle(Vec2(.24, .4), .3), {}}});
    auto three = quilt;
    three.push_back({&third, third.faces().front()});
    require(detail::certifyMappingTrimCoverage(acrossThree, quilt, Frame3(), Frame3()) != Coverage::Covered &&
            detail::certifyMappingTrimCoverage(acrossThree, three, Frame3(), Frame3()) == Coverage::Covered,
            "la prova assembla tre appoggi lungo le due giunzioni");

    const std::vector<Vec2> poles{{-.5, 0.}, {0., 1.}, {.5, 0.}};
    const auto bow = std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0.,0.,0.,1.,1.,1.},
                                                      poles, std::vector<double>{1.,.75,1.});
    const auto splineSource = planarSheet(Frame3(), {{{{{bow, {0., 1.}},
        {std::make_shared<Line<2>>(poles.back(), Vec2(-1., 0.)), {0., 1.}}}}, {}}});
    const auto wide = makePlaneSheet(Frame3(), 2.);
    const auto small = makePlaneSheet(Frame3(), .25);
    require(certify(splineSource, wide) == Coverage::Covered &&
            certify(splineSource, small) == Coverage::Inconclusive,
            "la copertura del trim spline usa i poli come inviluppo, non come testimoni");

    const auto full = disk(2.);
    const auto narrow = disk(1.75);
    const auto perforated = disk(2., {circle(Vec2(.625, 0.), .125, false)});
    require(checkBody(perforated, {true, true}).empty(), "disco con foro circolare valido");
    require(certify(narrow, full) == Coverage::Covered, "inclusione continua del disco");
    require(certify(full, narrow) == Coverage::Uncovered && norm(gap) > 1.75 && norm(gap) < 2.,
            "corona circolare scoperta benché il centro sia coperto");
    require(certify(full, narrow, .125) == Coverage::Inconclusive, "corona entro le bande di tolleranza");
    require(certify(perforated, perforated) == Coverage::Covered, "foro circolare coincidente");
    require(certify(full, perforated) == Coverage::Uncovered && gap.x() == .625 && gap.y() == 0.,
            "foro interno circolare espone materiale sorgente");

    bool rejected = false;
    try {
        (void)wrapBodyOnFaces(full, {{&perforated, perforated.faces().front()}}, SurfaceWrapOptions());
    } catch (const std::domain_error &error) {
        rejected = std::string(error.what()).find("copertura continua dei trim") != std::string::npos;
    }
    require(rejected, "wrap rifiutato dal certificato del foro circolare");
}

void checkContinuousSplineSupportTrims() {
    using Coverage = detail::MappingTrimCoverage;
    const auto panel = [](bool left, double bow) {
        const Vec3 bottom(0., -.8, 0.), top(0., .8, 0.), point(left ? -.6 : .6, 0., 0.);
        const std::vector<double> knots{0.,0.,0.,1.,1.,1.}, weights{1., .75, 1.};
        const auto curve = std::make_shared<BSplineCurve<3>>(2, knots, std::vector<Vec3>{bottom, {bow, 0., 0.}, top}, weights);
        const Vec3 from = left ? top : bottom, to = left ? bottom : top;
        const std::vector<Body::BuildEdge> edges{
            {0, 1, curve, {0., 1.}, 0.},
            {left ? 1 : 0, 2, std::make_shared<Line<3>>(from, point - from), {0., 1.}, 0.},
            {2, left ? 0 : 1, std::make_shared<Line<3>>(point, to - point), {0., 1.}, 0.}
        };
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops = {{{0, left, nullptr, 0.}, {1, true, nullptr, 0.}, {2, true, nullptr, 0.}}};
        auto body = Body::buildSheet({bottom, top, point}, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(), "pezza con giunzione spline valida");
        return body;
    };
    const auto left = panel(true, .25), right = panel(false, .25), separated = panel(false, .5);
    const auto inLeft = makePlaneSheet(Frame3({-.2, 0., 0.}, {0., 0., 1.}, {1., 0., 0.}), .0625);
    const auto source = makePlaneSheet(Frame3(), .25);
    const auto certify = [](const Body &body, const std::vector<SurfaceTargetFace> &targets) {
        return detail::certifyMappingTrimCoverage(body, targets, Frame3(), Frame3());
    };
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    require(certify(inLeft, {targets.front()}) == Coverage::Covered, "area inscritta nel trim spline razionale");
    require(certify(source, {targets.front()}) != Coverage::Covered &&
        certify(source, {targets.back()}) != Coverage::Covered &&
        certify(source, targets) == Coverage::Covered, "unione continua lungo la giunzione spline razionale");
    require(certify(source, {targets.front(), {&separated, separated.faces().front()}}) == Coverage::Inconclusive,
            "spline diverse non cancellano la giunzione aperta");
    const auto wrapped = wrapBodyOnFaces(source, targets, SurfaceWrapOptions());
    require(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty(),
            "wrap su una giunzione spline razionale");
}

enum class InteriorTrimCut { Rational, SimpleKnot, RoundedPoles };

void checkContinuousInteriorBezierCut(InteriorTrimCut variant) {
    using Coverage = detail::MappingTrimCoverage;
    const bool simpleKnot = variant == InteriorTrimCut::SimpleKnot;
    const bool roundedPoles = variant == InteriorTrimCut::RoundedPoles;
    const double displaced = roundedPoles ? .0625 : 0.;
    const Vec3 bottom(-.75 + displaced, -.75, 0.), top(0., .75, 0.);
    const Vec3 cut = simpleKnot ? Vec3(.046875, -.65625, 0.) : Vec3(.25, -.25, 0.);
    const Vec3 leftCorner(-.75, .75, 0.);
    const Vec3 rightCorner = simpleKnot ? Vec3(1.25, -.4375, 0.) : Vec3(.625, .125, 0.);
    const auto carrier = [&](double perturbation) {
        if (simpleKnot) {
            // One insertion at 1/2 in a quadratic, with the exact dyadic
            // controls written directly. The trim starts at 1/4.
            const Vec3 first = bottom - Vec3(2. * perturbation, 0., 0.);
            const Vec3 middle(1.25 + 3. * perturbation, -.75, 0.);
            return std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., .5, 1., 1., 1.},
                std::vector<Vec3>{first, .5 * (first + middle), .5 * (middle + top), top});
        }
        // With the displaced native controls, the cut endpoint is unchanged
        // but the two restricted interior abscissas are 1/48 and 23/48.
        return std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., 1., 1., 1.},
            std::vector<Vec3>{bottom - Vec3(perturbation, 0., 0.), {1.5 - displaced + perturbation, -.75, 0.}, top},
            std::vector<double>{1., .5, 1.});
    };
    const auto segment = [](const Vec3 &from, const Vec3 &to) {
        return std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0., 0., 1., 1.},
                                                std::vector<Vec3>{from, to});
    };
    const auto panel = [&](bool left, double perturbation, bool clipLeft = false) {
        const bool clipped = !left || clipLeft;
        const Vec3 start = clipped ? cut : bottom, corner = left ? leftCorner : rightCorner;
        const bool forward = left;
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops = {{{0, forward, nullptr, 0.}, {1, true, nullptr, 0.}, {2, true, nullptr, 0.}}};
        const Interval range{clipped ? (simpleKnot ? .25 : .5) : 0., 1.};
        const int from = left ? 1 : 0, to = left ? 0 : 1;
        auto body = Body::buildSheet({start, top, corner}, {{0, 1, carrier(perturbation), range, 0.},
            {from, 2, segment(left ? top : start, corner), {0., 1.}, 0.},
            {2, to, segment(corner, left ? start : top), {0., 1.}, 0.}}, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "pezza spline valida con estremo del trim interno alla campata");
        return body;
    };
    const double separation = roundedPoles ? 0x1p-40 : (simpleKnot ? .0625 : .1875);
    const auto left = panel(true, 0.), right = panel(false, 0.), separated = panel(false, separation);
    const Vec3 center = simpleKnot ? Vec3(.4375, -.375, 0.) : Vec3(.25, .1875, 0.);
    const auto source = makePlaneSheet(Frame3(center, {0., 0., 1.}, {1., 0., 0.}), simpleKnot ? .0625 : .125);
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto certify = [&](const std::vector<SurfaceTargetFace> &supports) {
        return detail::certifyMappingTrimCoverage(source, supports, Frame3(), Frame3());
    };
    const char *success = "la parte comune di una campata razionale copre la sorgente con entrambi gli ordini";
    if (simpleKnot) success = "la giunzione copre la sorgente attraversando il nodo semplice dopo il taglio interno";
    if (roundedPoles) success = "i poli interni non rappresentabili conservano la prova in coordinate omogenee";
    require(certify({targets.front()}) != Coverage::Covered && certify({targets.back()}) != Coverage::Covered &&
            certify(targets) == Coverage::Covered && certify({targets.back(), targets.front()}) == Coverage::Covered,
            success);
    if (roundedPoles) {
        // The perturbation is below the classification tolerance. At t=3/4
        // it moves only x, by 2*separation/13, with the same trim endpoints.
        require(distance(carrier(separation)->point(.5), cut) == 0. &&
                carrier(separation)->point(.75).x() > carrier(0.)->point(.75).x(),
                "la fessura quasi coincidente conserva l’estremo esatto del trim");
        const auto clipped = panel(true, 0., true);
        const SurfaceTargetFace clippedLeft{&clipped, clipped.faces().front()};
        require(certify({clippedLeft, targets.back()}) == Coverage::Covered &&
                certify({clippedLeft, {&separated, separated.faces().front()}}) != Coverage::Covered,
                "il confronto omogeneo diretto distingue due restrizioni quasi coincidenti sullo stesso intervallo");
    } else {
        const Vec3 gap = simpleKnot ? Vec3(.46875, -.3671875, 0.) : Vec3(.30078125, .2890625, 0.);
        const auto classify = [&](const Body &body) {
            return classifyPointOnFace(body, body.faces().front(), gap, linearResolution());
        };
        require(classify(source) == PointLocation::Inside && classify(left) == PointLocation::Outside &&
                classify(separated) == PointLocation::Outside, "il punto di prova cade nella fessura fra i carrier");
    }
    require(certify({targets.front(), {&separated, separated.faces().front()}}) != Coverage::Covered,
            "gli stessi estremi della campata rifilata non cancellano una fessura tra carrier diversi");
    const auto wrapped = wrapBodyOnFaces(source, targets, SurfaceWrapOptions());
    require(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty(),
            "wrap valido su una giunzione parziale interna a una campata Bézier");
}

enum class IntervalTrimEndpoint { Saturated, SimpleKnot, ScaledWeights };

void checkIntervalBezierTrimCut(IntervalTrimEndpoint variant) {
    using Coverage = detail::MappingTrimCoverage;
    const bool simpleKnot = variant != IntervalTrimEndpoint::Saturated;
    const bool scaledWeights = variant == IntervalTrimEndpoint::ScaledWeights;
    const Vec3 bottom = scaledWeights ? Vec3(-.65625, -.65625, 0.) : Vec3(-.5, -.5625, 0.);
    const Vec3 top(0., -bottom.y(), 0.);
    const Vec3 cut = scaledWeights ? Vec3(0., -.46875, 0.) : Vec3(.25, -.4375, 0.);
    const auto straight = [](const Vec3 &a, const Vec3 &b) {
        return std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0., 0., 1., 1.}, std::vector<Vec3>{a, b});
    };
    enum class Weights { Scaled, UnitScale, RoundedInterior, ChangedEnd };
    const auto panel = [&](bool left, bool clipped, double perturbation = 0., double trimOffset = 0.,
                           Weights treatment = Weights::Scaled) {
        const Vec3 first = bottom - Vec3(perturbation, 0., 0.);
        const Vec3 middle(scaledWeights ? 1.3125 + 2. * perturbation : 1.0625 + perturbation, bottom.y(), 0.);
        std::vector<double> knots{0., 0., 0., 3., 3., 3.};
        std::vector<Vec3> controls{first, middle, top};
        std::vector<double> weights;
        if (scaledWeights) {
            // Exact knot insertion in a rational quadratic with weights
            // (1,1/2,1), followed by a common homogeneous scale. The new
            // native weights are exact, but some products XW and YW round.
            const double scale = treatment == Weights::Scaled || treatment == Weights::RoundedInterior ?
                0x1.ffffffffffff8p-1 : 1.;
            knots.insert(knots.begin() + 3, 1.5);
            controls = {first, (2. * first + middle) / 3., (middle + 2. * top) / 3., top};
            weights = {scale, .75 * scale, .75 * scale, scale};
            if (treatment == Weights::RoundedInterior) weights[1] = std::nextafter(weights[1], 1.);
            if (treatment == Weights::ChangedEnd) weights.back() = .5;
        } else if (simpleKnot) {
            // An exact insertion preserves the curve, but now the two
            // first-level de Boor factors at t=1 use different denominators.
            knots.insert(knots.begin() + 3, 1.5);
            controls = {first, .5 * (first + middle), .5 * (middle + top), top};
        }
        const auto curve = std::make_shared<BSplineCurve<3>>(2, std::move(knots), std::move(controls), std::move(weights));
        const Vec3 start = clipped ? cut + Vec3(trimOffset, 0., 0.) : bottom;
        const Vec3 corner = left ? Vec3(scaledWeights ? bottom.x() : -.5625, top.y(), 0.) :
                                   Vec3(.875, scaledWeights ? -.125 : -.25, 0.);
        const int from = left ? 1 : 0, to = left ? 0 : 1;
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops = {{{0, left, nullptr, 0.}, {1, true, nullptr, 0.}, {2, true, nullptr, 0.}}};
        auto body = Body::buildSheet({start, top, corner}, {{0, 1, curve, {clipped ? 1. : 0., 3.}, 0.},
            {from, 2, straight(left ? top : start, corner), {0., 1.}, 0.},
            {2, to, straight(corner, left ? start : top), {0., 1.}, 0.}}, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "trim valido con parametro normalizzato non rappresentabile");
        return body;
    };
    // At t=1 the normalized parameter is 1/3, but the Cartesian endpoints
    // are exact in both the polynomial and the scaled rational cases.
    const auto left = panel(true, false), cutLeft = panel(true, true), right = panel(false, true);
    const auto shifted = panel(false, true, scaledWeights ? .046875 : .0625);
    const auto roundedEnd = panel(false, true, 0., 0x1p-50);
    const Vec3 sourceCenter = scaledWeights ? Vec3(.21875, -.21875, 0.) :
        (simpleKnot ? Vec3(.40625, -.28125, 0.) : Vec3(.4375, -.0625, 0.));
    const auto source = makePlaneSheet(Frame3(sourceCenter, {0., 0., 1.}, {1., 0., 0.}), .0625);
    const auto target = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
    const auto certify = [&](const std::vector<SurfaceTargetFace> &targets) {
        return detail::certifyMappingTrimCoverage(source, targets, Frame3(), Frame3());
    };
    const char *expected = "la campata con parametro 1/3 conserva la giunzione tramite identità nativa esatta";
    if (simpleKnot) expected = "la cella non satura conserva l’estremo esatto con denominatori di de Boor diversi";
    if (scaledWeights) expected = "il fattore comune esatto dei pesi recupera l’estremo razionale della giunzione";
    require(certify({target(left)}) != Coverage::Covered && certify({target(right)}) != Coverage::Covered &&
            certify({target(left), target(right)}) == Coverage::Covered &&
            certify({target(right), target(left)}) == Coverage::Covered &&
            certify({target(cutLeft), target(right)}) == Coverage::Covered,
            expected);
    const Vec3 gap = scaledWeights ? Vec3(.2265625, -.216796875, 0.) :
        (simpleKnot ? Vec3(.4140625, -.27734375, 0.) : Vec3(.42578125, -.0625, 0.));
    require(classifyPointOnFace(source, source.faces().front(), gap, linearResolution()) == PointLocation::Inside &&
            classifyPointOnFace(left, left.faces().front(), gap, linearResolution()) == PointLocation::Outside &&
            classifyPointOnFace(shifted, shifted.faces().front(), gap, linearResolution()) == PointLocation::Outside &&
            certify({target(cutLeft), target(shifted)}) != Coverage::Covered &&
            certify({target(left), target(roundedEnd)}) != Coverage::Covered,
            "estremi comuni o tollerati non dimostrano l’identità della giunzione a 1/3");
    if (scaledWeights) {
        const auto perturbedWeight = panel(false, true, 0., 0., Weights::RoundedInterior);
        require(certify({target(left), target(perturbedWeight)}) != Coverage::Covered,
                "un peso spostato di un ulp non dimostra un fattore comune esatto");
        const auto rescaledRight = panel(false, true, 0., 0., Weights::UnitScale);
        require(certify({target(left), target(rescaledRight)}) == Coverage::Covered &&
                certify({target(rescaledRight), target(left)}) == Coverage::Covered &&
                certify({target(cutLeft), target(rescaledRight)}) == Coverage::Covered,
                "pesi nativi distinti ma esattamente proporzionali conservano la giunzione intervallare");
        const auto changedWeight = panel(false, true, 0., 0., Weights::ChangedEnd);
        const auto &native = *rescaledRight.edge(rescaledRight.edges().front()).curve;
        const auto &changed = *changedWeight.edge(changedWeight.edges().front()).curve;
        // Varying the last weight does not move the trim endpoints or the
        // simple knot. Only the second cell of the shared chain changes.
        for (double parameter : {1., 1.5, 3.})
            require(distance(native.point(parameter), changed.point(parameter)) == 0.,
                    "i due trim mantengono estremi e giunzione interna");
        const Vec3 weightGap(.240234375, -.171875, 0.);
        require(classifyPointOnFace(source, source.faces().front(), weightGap, linearResolution()) ==
                    PointLocation::Inside &&
                classifyPointOnFace(left, left.faces().front(), weightGap, linearResolution()) ==
                    PointLocation::Outside &&
                classifyPointOnFace(changedWeight, changedWeight.faces().front(), weightGap, linearResolution()) ==
                    PointLocation::Outside &&
                certify({target(cutLeft), target(changedWeight)}) != Coverage::Covered,
                "gli stessi poli e nodi con pesi non proporzionali lasciano una fessura");
        const auto scaledWrap = wrapBodyOnFaces(source, {target(left), target(rescaledRight)}, SurfaceWrapOptions());
        require(scaledWrap.isSheet() && scaledWrap.faces().size() == 1 && checkBody(scaledWrap, {true, true}).empty(),
                "Wrap valido con pesi nativi su scale distinte");
    }
    const auto wrap = wrapBodyOnFaces(source, {target(left), target(right)}, SurfaceWrapOptions());
    require(wrap.isSheet() && wrap.faces().size() == 1 && checkBody(wrap, {true, true}).empty(),
            "Wrap valido con coefficienti omogenei racchiusi");
}

void checkRoundedTrimEndpoint() {
    using Coverage = detail::MappingTrimCoverage;
    const Vec3 cut(.5625, 0., 0.), bottom(.5625, -.75, 0.), top(.5625, .75, 0.);
    constexpr double weight = 0x1.ccccccccccd31p-1;
    require(std::fma(.8125, weight, -(.8125 * weight)) != 0.,
            "il prodotto omogeneo di un polo nativo richiede arrotondamento");
    const auto segment = [](const Vec3 &a, const Vec3 &b) {
        return std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0., 0., 1., 1.}, std::vector<Vec3>{a, b});
    };
    const auto panel = [&](bool left, bool clipped, double bulge, double endOffset = 0.) {
        const auto curve = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0., 0., 0., 0., 1., 1., 1., 1.},
            std::vector<Vec3>{bottom, cut + Vec3(bulge, -.25, 0.), cut - Vec3(bulge, -.25, 0.), top},
            std::vector<double>{1., weight, weight, 1.});
        const Vec3 start = clipped ? cut + Vec3(endOffset, 0., 0.) : bottom;
        const Vec3 corner(left ? -.1875 : 1.3125, .75, 0.);
        const int from = left ? 1 : 0, to = left ? 0 : 1;
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops = {{{0, left, nullptr, 0.}, {1, true, nullptr, 0.}, {2, true, nullptr, 0.}}};
        auto body = Body::buildSheet({start, top, corner}, {{0, 1, curve, {clipped ? .5 : 0., 1.}, 0.},
            {from, 2, segment(left ? top : start, corner), {0., 1.}, 0.},
            {2, to, segment(corner, left ? start : top), {0., 1.}, 0.}}, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "trim cubico valido con due coppie di poli simmetrici");
        return body;
    };
    const auto left = panel(true, false, .25), clippedLeft = panel(true, true, .25);
    const auto right = panel(false, true, .25), open = panel(false, true, 0.);
    const auto displaced = panel(false, true, .25, 0x1p-53);
    const auto source = makePlaneSheet(Frame3({.53125, .4375, 0.}, {0., 0., 1.}, {1., 0., 0.}), .0625);
    const auto target = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
    const auto certify = [&](const std::vector<SurfaceTargetFace> &targets) {
        return detail::certifyMappingTrimCoverage(source, targets, Frame3(), Frame3());
    };
    require(certify({target(left)}) != Coverage::Covered && certify({target(right)}) != Coverage::Covered &&
            certify({target(left), target(right)}) == Coverage::Covered &&
            certify({target(right), target(left)}) == Coverage::Covered &&
            certify({target(clippedLeft), target(right)}) == Coverage::Covered,
            "l'estremo esatto della cubica razionale sopravvive ai prodotti intermedi arrotondati");
    const Vec3 gap(.53125, .4375, 0.);
    require(classifyPointOnFace(source, source.faces().front(), gap, linearResolution()) == PointLocation::Inside &&
            classifyPointOnFace(left, left.faces().front(), gap, linearResolution()) == PointLocation::Outside &&
            classifyPointOnFace(open, open.faces().front(), gap, linearResolution()) == PointLocation::Outside &&
            certify({target(left), target(open)}) != Coverage::Covered &&
            certify({target(left), target(displaced)}) != Coverage::Covered,
            "estremi coincidenti o spostati di un ULP non eliminano una fessura");
    const auto result = wrapBodyOnFaces(source, {target(left), target(right)}, SurfaceWrapOptions());
    require(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty(),
            "Wrap valido attraverso una giunzione cubica rifilata");
}

void checkContinuousPartialSplineSeam() {
    using Coverage = detail::MappingTrimCoverage;
    const Vec3 diagonal(1., 1., 0.);
    const double q = Line<3>(Vec3(), diagonal).point(1.).x();
    const Vec3 bottom(-q, -q, 0.), junction(0., -q, 0.), midpoint(.125, 0., 0.), top(0., q, 0.);
    const Vec3 leftCorner(-q, q, 0.), rightCorner(q, 0., 0.);
    const Vec3 lowerPole(.1875, -.5 * q, 0.), upperPole(.1875, .5 * q, 0.);
    const auto leftCurve = std::make_shared<BSplineCurve<3>>(2,
        std::vector<double>{0., 0., 0., .25, .25, .5, .5, 1., 1., 1.},
        std::vector<Vec3>{bottom, {-.5 * q, -q - .125, 0.}, junction, lowerPole, midpoint, upperPole, top},
        std::vector<double>{1., .75, 1., .875, 1., .75, 1.});
    const auto line = [](const Vec3 &from, const Vec3 &direction) {
        return std::make_shared<Line<3>>(from, direction);
    };
    const auto build = [](const std::vector<Vec3> &points, const std::vector<Body::BuildEdge> &edges) {
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops.resize(1);
        for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
            CurvePtr<2> pcurve;
            if (const auto *straight = dynamic_cast<const Line<3> *>(edges[i].curve.get())) {
                const auto &from = straight->origin(), &direction = straight->direction();
                const auto sign = [](double value) { return double((value > 0.) - (value < 0.)); };
                // Normalize the same integral direction in both dimensions.
                pcurve = std::make_shared<Line<2>>(Vec2(from.x(), from.y()),
                    Vec2(sign(direction.x()), sign(direction.y())));
            }
            face.loops.front().push_back({i, true, pcurve, 0.});
        }
        auto body = Body::buildSheet(points, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "pezza valida con giunzione spline parziale");
        return body;
    };
    const auto left = build({bottom, top, leftCorner}, {
        {0, 1, leftCurve, {0., 1.}, 0.},
        {1, 2, line(top, {-1., 0., 0.}), {0., q}, 0.},
        {2, 0, line(leftCorner, {0., -1., 0.}), {0., 2. * q}, 0.}});
    const auto rightPanel = [&](double gap) {
        const std::vector<double> knots{2., 2., 2., 4., 4., 4.};
        const auto upper = std::make_shared<BSplineCurve<3>>(2, knots,
            std::vector<Vec3>{top, upperPole + Vec3(gap, 0., 0.), midpoint}, std::vector<double>{1., .75, 1.});
        const auto lower = std::make_shared<BSplineCurve<3>>(2, knots,
            std::vector<Vec3>{midpoint, lowerPole, junction}, std::vector<double>{1., .875, 1.});
        return build({top, midpoint, junction, rightCorner}, {
            {0, 1, upper, {2., 4.}, 0.}, {1, 2, lower, {2., 4.}, 0.},
            {2, 3, line(junction, diagonal), {0., 1.}, 0.},
            {3, 0, line(rightCorner, {-1., 1., 0.}), {0., 1.}, 0.}});
    };
    const auto right = rightPanel(0.), separated = rightPanel(.25);
    const auto source = makePlaneSheet(Frame3({.09375, .0625, 0.}, {0., 0., 1.}, {1., 0., 0.}), .125);
    const std::vector<SurfaceTargetFace> targets{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto certify = [&](const std::vector<SurfaceTargetFace> &supports) {
        return detail::certifyMappingTrimCoverage(source, supports, Frame3(), Frame3());
    };
    require(certify({targets.front()}) != Coverage::Covered && certify({targets.back()}) != Coverage::Covered &&
            certify(targets) == Coverage::Covered && certify({targets.back(), targets.front()}) == Coverage::Covered,
            "due campate razionali opposte coprono la sorgente sulla sola parte comune della spline");
    require(certify({targets.front(), {&separated, separated.faces().front()}}) != Coverage::Covered,
            "gli stessi estremi non chiudono la fessura fra campate con poli diversi");
    const auto wrapped = wrapBodyOnFaces(source, targets, SurfaceWrapOptions());
    require(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty(),
            "wrap valido sopra la giunzione razionale parziale");

    const Vec3 contact(.125, -.0625, 0.);
    const auto leftFace = left.faces().front();
    require(FaceParameterClassifier(left, leftFace, linearResolution()).classify(Vec2(contact.x(), contact.y())) ==
                PointLocation::Inside &&
            classifyPointOnFace(left, leftFace, contact, linearResolution()) == PointLocation::Inside,
            "un nodo interno C0 tangente al raggio UV non e' un attraversamento");
    const auto aligned = makePlaneSheet(Frame3({contact.x(), .0625, 0.}, {0., 0., 1.}, {1., 0., 0.}), .125);
    const auto alignedWrap = wrapBodyOnFaces(aligned, targets, SurfaceWrapOptions());
    require(alignedWrap.isSheet() && alignedWrap.faces().size() == 1 &&
            checkBody(alignedWrap, {true, true}).empty(), "wrap allineato al nodo interno C0");
}

void checkContinuousReentrantSplineSeam() {
    using Coverage = detail::MappingTrimCoverage;
    const Vec3 bottom(0., -.5, 0.), junction(0., .5, 0.), top(0., 1., 0.);
    const Vec3 seamPole(.25, 0., 0.);
    const auto panel = [](const std::vector<Vec3> &points, CurvePtr<3> spline,
                          Interval range = {0., 1.}, bool forward = true) {
        Body::BuildFace face;
        face.surface = std::make_shared<Plane>(Frame3());
        face.loops.resize(1);
        std::vector<Body::BuildEdge> edges{{forward ? 0 : 1, forward ? 1 : 0, spline, range, 0.}};
        for (int i = 1; i < static_cast<int>(points.size()); ++i) {
            const int next = (i + 1) % static_cast<int>(points.size());
            const Vec3 delta = points[next] - points[i];
            edges.push_back({i, next, std::make_shared<Line<3>>(points[i], delta), {0., norm(delta)}, 0.});
        }
        for (int i = 0; i < static_cast<int>(edges.size()); ++i)
            face.loops.front().push_back({i, i != 0 || forward, nullptr, 0.});
        auto body = Body::buildSheet(points, edges, {face});
        require(computePCurves(body) == 0 && checkBody(body, {true, true}).empty(),
                "pezza spline valida per il perimetro rientrante");
        return body;
    };
    const auto leftPanel = [&](double residualBow) {
        const auto curve = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., .5, .5, 1., 1., 1.},
            std::vector<Vec3>{bottom, seamPole, junction, {residualBow, .75, 0.}, top},
            std::vector<double>{1., .75, 1., .75, 1.});
        return panel({bottom, top, {-1., 1., 0.}, {-1., -.5, 0.}}, curve);
    };
    const auto left = leftPanel(.125), inward = leftPanel(-.375);
    const auto right = panel({junction, bottom, {1., -.5, 0.}, {1., .5, 0.}},
        std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., 1., 1., 1.},
            std::vector<Vec3>{junction, seamPole, bottom}, std::vector<double>{1., .75, 1.}));
    const auto selected = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
    const std::vector<SurfaceTargetFace> targets{selected(left), selected(right)};
    const auto sheet = [](const Vec3 &center, double halfSize) {
        return makePlaneSheet(Frame3(center, {0., 0., 1.}, {1., 0., 0.}), halfSize);
    };
    const auto certify = [&](const Body &source, const std::vector<SurfaceTargetFace> &supports) {
        return detail::certifyMappingTrimCoverage(source, supports, Frame3(), Frame3());
    };
    const auto spanning = sheet({.0625, .125, 0.}, .1875);
    require(certify(spanning, {targets.front()}) != Coverage::Covered &&
            certify(spanning, {targets.back()}) != Coverage::Covered &&
            certify(spanning, targets) == Coverage::Covered &&
            certify(spanning, {targets.back(), targets.front()}) == Coverage::Covered,
            "la lamina attraversa la giunzione spline con perimetro residuo rientrante");
    const auto wrapped = wrapBodyOnFaces(spanning, targets, SurfaceWrapOptions());
    require(wrapped.isSheet() && wrapped.faces().size() == 1 && checkBody(wrapped, {true, true}).empty(),
            "wrap valido sulla giunzione con perimetro rientrante");
    require(certify(sheet({0., .5, 0.}, .125), targets) != Coverage::Covered,
            "il certificato conserva il vuoto del rientro");
    const auto underResidual = sheet({-.0625, .75, 0.}, .03125);
    require(certify(underResidual, targets) == Coverage::Covered &&
            certify(underResidual, {selected(inward), targets.back()}) != Coverage::Covered,
            "la corda del residuo non copre il vuoto lasciato dall'arco rivolto all'interno");

    const auto unbroken = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., 1., 1., 1.},
        std::vector<Vec3>{bottom, {.25, .25, 0.}, top});
    const Vec3 cut(.125, .25, 0.);
    const auto fullPanel = panel({bottom, top, {-1., 1., 0.}, {-1., -.5, 0.}}, unbroken);
    const auto cutPanel = panel({cut, bottom, {1., -.5, 0.}, {1., .25, 0.}}, unbroken, {0., .5}, false);
    const std::vector<SurfaceTargetFace> cutTargets{selected(fullPanel), selected(cutPanel)};
    const auto acrossCut = sheet({.0625, -.125, 0.}, .125);
    require(certify(acrossCut, cutTargets) == Coverage::Covered &&
            certify(acrossCut, {cutTargets.back(), cutTargets.front()}) == Coverage::Covered,
            "il perimetro rientrante resta certificabile con taglio interno a una singola Bézier");
    require(certify(sheet({.125, .25, 0.}, .0625), cutTargets) != Coverage::Covered,
            "il taglio interno conserva il vuoto del rientro");

    // The lower control point of the residual arc is below the notch.
    // The two half-intervals of the rational arc stay above it.
    const auto cubicLeft = [&](double residualBow) {
        return panel({bottom, top, {-1., 1., 0.}, {-1., -.5, 0.}},
            std::make_shared<BSplineCurve<3>>(3,
                std::vector<double>{0., 0., 0., 0., .5, .5, .5, 1., 1., 1., 1.},
                std::vector<Vec3>{bottom, {.25, -.25, 0.}, {.25, .25, 0.}, junction,
                                  {residualBow, .875, 0.}, {residualBow, .25, 0.}, top},
                std::vector<double>{1., .625, .625, 1., .625, .625, 1.}));
    };
    const auto shaped = cubicLeft(.25), inwardCubic = cubicLeft(-.25);
    const auto other = panel({junction, bottom, {1., -.5, 0.}, {1., .5, 0.}},
        std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0., 0., 0., 0., 1., 1., 1., 1.},
            std::vector<Vec3>{junction, {.25, .25, 0.}, {.25, -.25, 0.}, bottom},
            std::vector<double>{1., .625, .625, 1.}));
    const std::vector<SurfaceTargetFace> refinedTargets{selected(shaped), selected(other)};
    require(certify(spanning, {refinedTargets.front()}) != Coverage::Covered &&
            certify(spanning, {refinedTargets.back()}) != Coverage::Covered &&
            certify(spanning, refinedTargets) == Coverage::Covered &&
            certify(spanning, {refinedTargets.back(), refinedTargets.front()}) == Coverage::Covered,
            "la suddivisione dell'inviluppo separa il residuo razionale dal triangolo");
    const Vec3 gap(-.03125, .65625, 0.);
    const auto onResidual = sheet(gap, .015625);
    require(classifyPointOnFace(inwardCubic, inwardCubic.faces().front(), gap, linearResolution()) ==
                PointLocation::Outside &&
            certify(onResidual, refinedTargets) == Coverage::Covered &&
            certify(onResidual, {selected(inwardCubic), refinedTargets.back()}) != Coverage::Covered,
            "la suddivisione non copre materiale oltre il residuo cubico rivolto all'interno");

    const auto leftwardPanel = [&](double residualBow, double seamBow = -.25) {
        return panel({bottom, top, {-1., 1., 0.}, {-1., -.5, 0.}},
            std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., .5, .5, 1., 1., 1.},
                std::vector<Vec3>{bottom, {seamBow, 0., 0.}, junction, {residualBow, .75, 0.}, top},
                std::vector<double>{1., .75, 1., .75, 1.}));
    };
    const auto oppositePanel = [&](double seamBow) {
        return panel({junction, bottom, {1., -.5, 0.}, {1., .5, 0.}},
            std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., 1., 1., 1.},
                std::vector<Vec3>{junction, {seamBow, 0., 0.}, bottom}, std::vector<double>{1., .75, 1.}));
    };
    const auto clearedLeft = leftwardPanel(-.125), gougedLeft = leftwardPanel(-.5);
    const auto leftwardRight = oppositePanel(-.25);
    const auto bridge = sheet({-.171875, .4375, 0.}, .125);
    const std::vector<SurfaceTargetFace> bridgeTargets{selected(clearedLeft), selected(leftwardRight)};
    const auto locate = [](const Body &body, const Vec3 &point) {
        return classifyPointOnFace(body, body.faces().front(), point, linearResolution());
    };
    const Vec3 acrossSeam(-.0546875, .34375, 0.), inNotch(-.0625, .546875, 0.);
    require(locate(bridge, acrossSeam) == PointLocation::Inside &&
            locate(clearedLeft, acrossSeam) == PointLocation::Outside &&
            locate(leftwardRight, acrossSeam) == PointLocation::Inside,
            "la lamina passa realmente dall'altro lato della giunzione");
    require(certify(bridge, {bridgeTargets.front()}) != Coverage::Covered &&
            certify(bridge, {bridgeTargets.back()}) != Coverage::Covered &&
            certify(bridge, bridgeTargets) == Coverage::Covered &&
            certify(bridge, {bridgeTargets.back(), bridgeTargets.front()}) == Coverage::Covered,
            "il winding residuo certifica la sorgente a lato dell'arco che attraversa il triangolo");
    require(locate(bridge, inNotch) == PointLocation::Inside &&
            locate(clearedLeft, inNotch) == PointLocation::Inside &&
            locate(gougedLeft, inNotch) == PointLocation::Outside &&
            locate(leftwardRight, inNotch) == PointLocation::Outside &&
            certify(bridge, {selected(gougedLeft), bridgeTargets.back()}) != Coverage::Covered,
            "un residuo piu' profondo lascia un vuoto nella stessa lamina");

    const auto diskLeft = leftwardPanel(-.125, -.375), diskGouged = leftwardPanel(-.5, -.375);
    const auto diskRight = oppositePanel(-.375);
    const auto circle = std::make_shared<Circle<2>>(Vec2(-.15625, .46875), Vec2(1., 0.), Vec2(0., 1.), .125);
    const auto disc = planarSheet(Frame3(), {{ProfileLoop{{{circle, {0., kTwoPi}}}}, {}}});
    const std::vector<SurfaceTargetFace> discTargets{selected(diskLeft), selected(diskRight)};
    const Vec3 boxCorner(-.03125, .59375, 0.), acrossDisc(-.0537109375, .40625, 0.);
    require(locate(disc, boxCorner) == PointLocation::Outside &&
            locate(diskLeft, boxCorner) == PointLocation::Outside &&
            locate(diskRight, boxCorner) == PointLocation::Outside &&
            locate(disc, acrossDisc) == PointLocation::Inside &&
            locate(diskLeft, acrossDisc) == PointLocation::Outside &&
            locate(diskRight, acrossDisc) == PointLocation::Inside,
            "il disco attraversa la giunzione e lascia fuori l'angolo del rettangolo del rientro");
    require(certify(disc, {discTargets.front()}) != Coverage::Covered &&
            certify(disc, {discTargets.back()}) != Coverage::Covered &&
            certify(disc, discTargets) == Coverage::Covered &&
            certify(disc, {discTargets.back(), discTargets.front()}) == Coverage::Covered,
            "i riquadri senza materiale consentono il certificato del disco in entrambi gli ordini");
    const auto closeDisk = [&](double radius) {
        const auto curve = std::make_shared<Circle<2>>(Vec2(-.15625, .46875), Vec2(1., 0.), Vec2(0., 1.), radius);
        return planarSheet(Frame3(), {{ProfileLoop{{{curve, {0., kTwoPi}}}}, {}}});
    };
    const auto close = closeDisk(40705. * 0x1p-18), crossing = closeDisk(40708. * 0x1p-18);
    const auto t = 9. / 128., u = 1. - t, w = u * u + 1.5 * t * u + t * t;
    const Vec3 narrowGap(-.1875 * t * u / w + 0x1p-20, (.5 * u * u + 1.125 * t * u + t * t) / w, 0.);
    require(locate(close, acrossDisc) == PointLocation::Inside &&
            certify(close, {discTargets.front()}) != Coverage::Covered &&
            certify(close, {discTargets.back()}) != Coverage::Covered &&
            certify(close, discTargets) == Coverage::Covered &&
            certify(close, {discTargets.back(), discTargets.front()}) == Coverage::Covered,
            "separazione continua del disco con margine inferiore a 10^-5 mm dal rientro");
    require(locate(crossing, narrowGap) == PointLocation::Inside &&
            locate(diskLeft, narrowGap) == PointLocation::Outside &&
            locate(diskRight, narrowGap) == PointLocation::Outside &&
            certify(crossing, discTargets) != Coverage::Covered,
            "un disco appena piu' grande attraversa realmente il contorno del rientro");

    const Vec3 overGap(-.072265625, .5546875, 0.);
    require(locate(disc, overGap) == PointLocation::Inside &&
            locate(diskLeft, overGap) == PointLocation::Inside &&
            locate(diskGouged, overGap) == PointLocation::Outside &&
            locate(diskRight, overGap) == PointLocation::Outside &&
            certify(disc, {selected(diskGouged), discTargets.back()}) != Coverage::Covered,
            "la suddivisione lascia inconclusiva la copertura quando il vuoto entra nel disco");

    const std::vector<Vec3> sourceVertices{{-.28125, .3125, 0.}, {-.03125, .375, 0.},
                                           {-.046875, .5, 0.}, {-.171875, .625, 0.}};
    const auto splineSheet = [](const std::vector<Vec3> &points) {
        Body::BuildFace sourceFace;
        sourceFace.surface = std::make_shared<Plane>(Frame3());
        sourceFace.loops.resize(1);
        std::vector<Body::BuildEdge> sourceEdges;
        for (int i = 0; i < int(points.size()); ++i) {
            const int next = (i + 1) % int(points.size());
            const Vec3 a = points[i], b = points[next];
            const auto curve = i == 0
                ? std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0., 0., 0., 1., 1., 1.},
                    std::vector<Vec3>{a, .5 * (a + b) + Vec3(0., -.015625, 0.), b}, std::vector<double>{1., .75, 1.})
                : std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0., 0., 1., 1.}, std::vector<Vec3>{a, b});
            sourceEdges.push_back({i, next, curve, {0., 1.}, 0.});
            sourceFace.loops.front().push_back({i, true, nullptr, 0.});
        }
        auto result = Body::buildSheet(points, sourceEdges, {sourceFace});
        require(computePCurves(result) == 0 && checkBody(result, {true, true}).empty(),
                "sorgente spline valida per la prova sul rientro");
        return result;
    };
    const auto splineSource = splineSheet(sourceVertices);
    const Vec3 acrossSpline(-.0615234375, .390625, 0.);
    require(locate(splineSource, boxCorner) == PointLocation::Outside &&
            locate(splineSource, acrossSpline) == PointLocation::Inside &&
            locate(diskLeft, acrossSpline) == PointLocation::Outside &&
            locate(diskRight, acrossSpline) == PointLocation::Inside &&
            certify(splineSource, discTargets) == Coverage::Covered &&
            certify(splineSource, {discTargets.back(), discTargets.front()}) == Coverage::Covered,
            "il winding copre l'inviluppo convesso di una sorgente spline");
    const auto splineGouged = leftwardPanel(-.75, -.375);
    const Vec3 splineGap(-.11328125, .5625, 0.);
    require(locate(splineSource, splineGap) == PointLocation::Inside &&
            locate(diskLeft, splineGap) == PointLocation::Inside &&
            locate(splineGouged, splineGap) == PointLocation::Outside &&
            locate(diskRight, splineGap) == PointLocation::Outside &&
            certify(splineSource, {selected(splineGouged), discTargets.back()}) != Coverage::Covered,
            "il vuoto nel materiale spline rende la copertura inconclusiva");

    const std::vector<Vec3> concaveVertices{{-.28125, .3125, 0.}, {.3125, .3125, 0.}, {.3125, .4375, 0.},
                                            {-.09375, .4375, 0.}, {-.09375, .625, 0.}, {-.28125, .625, 0.}};
    const auto concaveSource = splineSheet(concaveVertices);
    const Vec3 inHull(.0625, .53125, 0.), concaveGap(-.109375, .59375, 0.);
    const auto midpoint = .5 * (concaveVertices[2] + concaveVertices[4]);
    // Above the seam, the shallow residual arc has x >= -.1875/(4*.875).
    require(inHull.y() == midpoint.y() && concaveVertices.front().x() < inHull.x() && inHull.x() < midpoint.x() &&
            concaveVertices[4].x() * .875 < -.1875 * .25 &&
            locate(concaveSource, inHull) == PointLocation::Outside &&
            locate(diskLeft, inHull) == PointLocation::Outside &&
            locate(diskRight, inHull) == PointLocation::Outside &&
            certify(concaveSource, {discTargets.front()}) != Coverage::Covered &&
            certify(concaveSource, {discTargets.back()}) != Coverage::Covered &&
            certify(concaveSource, discTargets) == Coverage::Covered &&
            certify(concaveSource, {discTargets.back(), discTargets.front()}) == Coverage::Covered,
            "il winding della sorgente esclude il rientro interno all'inviluppo dei poli");
    const auto ordinateNumerator = [&](double t) {
        return (.5 - concaveGap.y()) * (1. - t) * (1. - t) +
               (.75 - concaveGap.y()) * 1.5 * t * (1. - t) + (1. - concaveGap.y()) * t * t;
    };
    // At the witness ordinate the deep arc has 3/16 < t < 1/4.
    // Its weight polynomial is at most one, so x <= -.75*(3/16)*(13/16).
    require(ordinateNumerator(.1875) < 0. && ordinateNumerator(.25) > 0. &&
            -.75 * .1875 * .8125 < concaveGap.x() &&
            locate(concaveSource, concaveGap) == PointLocation::Inside &&
            locate(diskGouged, concaveGap) == PointLocation::Outside &&
            locate(diskRight, concaveGap) == PointLocation::Outside &&
            certify(concaveSource, {selected(diskGouged), discTargets.back()}) != Coverage::Covered,
            "il winding non esclude un riquadro con materiale sorgente scoperto");
}

void checkContinuousSplineOpeningCoverage() {
    using Coverage = detail::MappingTrimCoverage;
    const Vec2 center(.25, .25);
    const std::array<Vec2, 4> corners{{{-1., -1.}, {1., -1.}, {1., 1.}, {-1., 1.}}};
    const auto rectangle = [&](double halfSize) {
        ProfileLoop loop;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto from = halfSize * corners[i], to = halfSize * corners[(i + 1) % corners.size()];
            loop.segments.push_back({std::make_shared<Line<2>>(from, to - from), {0., 2. * halfSize}});
        }
        return loop;
    };
    ProfileLoop hole;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        // The hole runs clockwise. Its arc bows away from the square.
        const auto from = center + .125 * corners[(4 - i) % 4];
        const auto to = center + .125 * corners[3 - i], side = to - from;
        const Vec2 middle = .5 * (from + to) + .125 * Vec2(-side.y(), side.x());
        const auto edge = std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0.,0.,0.,1.,1.,1.},
            std::vector<Vec2>{from, middle, to}, std::vector<double>{1.,.75,1.});
        hole.segments.push_back({edge, {0., 1.}});
    }
    const auto support = planarSheet(Frame3(), {{rectangle(2.), {hole}}});
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto circular = std::make_shared<Circle<2>>(center, Vec2(1., 0.), Vec2(0., -1.), .25);
    const ProfileLoop circularHole{{{circular, {0., kTwoPi}}}};
    const auto drilled = planarSheet(Frame3(), {{rectangle(1.), {circularHole}}});
    const Frame3 plugFrame({center.x(), center.y(), 0.}, {0., 0., 1.}, {1., 0., 0.});
    const auto plug = makePlaneSheet(plugFrame, .25);
    const auto partial = makePlaneSheet(plugFrame, .03125);
    const std::vector<SurfaceTargetFace> target{{&support, support.faces().front()}};
    require(checkBody(support, {true, true}).empty() && checkBody(drilled, {true, true}).empty(),
            "trim razionale e foro sorgente validi");
    Vec2 gap;
    const auto certify = [&](const Body &body, const std::vector<SurfaceTargetFace> &targets, double tolerance) {
        return detail::certifyMappingTrimCoverage(body, targets, Frame3(), Frame3(), tolerance, &gap);
    };
    require(certify(source, target, linearResolution()) == Coverage::Uncovered &&
            gap.x() == center.x() && gap.y() == center.y(), "witness certificato nella cavità spline");
    require(certify(source, target, .125) == Coverage::Inconclusive,
            "witness nella banda di tolleranza inconcludente");
    require(certify(drilled, target, linearResolution()) == Coverage::Covered,
            "il foro sorgente racchiude l'intera cavità spline");
    const std::vector<SurfaceTargetFace> withPlug{target.front(), {&plug, plug.faces().front()}};
    require(certify(source, withPlug, linearResolution()) == Coverage::Covered,
            "il secondo appoggio copre tutta la cavità spline");
    const std::vector<SurfaceTargetFace> withPartial{target.front(), {&partial, partial.faces().front()}};
    require(certify(source, withPartial, linearResolution()) == Coverage::Uncovered &&
            (std::abs(gap.x() - center.x()) > .03125 || std::abs(gap.y() - center.y()) > .03125),
            "la regione attorno al tappo parziale contiene materiale scoperto");
    const auto plugged = wrapBodyOnFaces(source, withPlug, SurfaceWrapOptions());
    require(plugged.isSheet() && checkBody(plugged, {true, true}).empty() &&
            plugged.face(plugged.faces().front()).loops.size() == 1, "wrap sul foro tappato");
    const auto wrapped = wrapBodyOnFaces(drilled, target, SurfaceWrapOptions());
    require(wrapped.isSheet() && checkBody(wrapped, {true, true}).empty() &&
            wrapped.face(wrapped.faces().front()).loops.size() == 2, "wrap con foro sorgente conservato");
    bool rejected = false;
    try {
        (void)wrapBodyOnFaces(source, target, SurfaceWrapOptions());
    } catch (const std::domain_error &e) {
        rejected = std::string(e.what()).find("copertura continua dei trim") != std::string::npos;
    }
    require(rejected, "wrap pubblico rifiuta il foro spline scoperto");
}

void checkContinuousSplineOpeningWithoutChordRegion() {
    using Coverage = detail::MappingTrimCoverage;
    const auto source = makePlaneSheet(Frame3(), 1.);
    const Frame3 plugFrame({.25, .3125, 0.}, {0., 0., 1.}, {1., 0., 0.});
    const auto complete = makePlaneSheet(plugFrame, .25);
    const auto partial = makePlaneSheet(plugFrame, .015625);
    ProfileRegion profile;
    const auto line = [](ProfileLoop &loop, const Vec2 &a, const Vec2 &b) {
        loop.segments.push_back({std::make_shared<Line<2>>(a, b - a), {0., norm(b - a)}});
    };
    const std::array<Vec2, 4> corners{{{-2., -2.}, {2., -2.}, {2., 2.}, {-2., 2.}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
        line(profile.outer, corners[i], corners[(i + 1) % corners.size()]);
    const std::vector<Vec2> poles{{.125, .25}, {.1875, .375}, {.25, .3125}, {.3125, .375}, {.375, .25}};
    for (const auto &weights : {std::vector<double>{}, std::vector<double>{1., .75, 1.25, 1.5, 1.}}) {
        // The arc and its closing line share the same chord. Exercise both
        // a multiple knot and extraction across simple interior knots.
        const double secondKnot = weights.empty() ? .25 : .75;
        const auto arc = std::make_shared<BSplineCurve<2>>(2,
            std::vector<double>{0., 0., 0., .25, secondKnot, 1., 1., 1.}, poles, weights);
        ProfileLoop hole{{{arc, {0., 1.}}}};
        line(hole, poles.back(), poles.front());
        profile.holes = {hole};
        const auto support = planarSheet(Frame3(), {profile});
        const auto face = support.faces().front();
        require(checkBody(support, {true, true}).empty(), "foro spline con due fin e nodi interni");
        const SurfaceTargetFace target{&support, face};
        const auto asTarget = [](const Body &body) { return SurfaceTargetFace{&body, body.faces().front()}; };
        Vec2 gap;
        const auto certify = [&](const std::vector<SurfaceTargetFace> &targets, double tolerance) {
            return detail::certifyMappingTrimCoverage(source, targets, Frame3(), Frame3(), tolerance, &gap);
        };
        const auto classifier = FaceParameterClassifier(support, face, linearResolution());
        require(certify({target}, linearResolution()) == Coverage::Uncovered &&
                classifier.classify(gap) == PointLocation::Outside,
                "la frontiera continua certifica un testimone nel foro senza poligono delle corde");
        require(certify({target, asTarget(partial)}, linearResolution()) == Coverage::Uncovered &&
                classifier.classify(gap) == PointLocation::Outside &&
                FaceParameterClassifier(partial, partial.faces().front(), linearResolution()).classify(gap) ==
                    PointLocation::Outside,
                "il testimone rimane nella parte del foro scoperta dal tappo");
        require(certify({target}, .0625) == Coverage::Inconclusive,
                "il foro senza poligono non dà un testimone nella fascia di tolleranza");
        require(certify({target, asTarget(complete)}, linearResolution()) == Coverage::Covered,
                "il tappo completo contiene tutto il foro senza poligono");
        double deviation = -1.;
        bool rejected = false;
        try {
            (void)wrapBodyOnFaces(source, {target}, SurfaceWrapOptions(), nullptr, &deviation);
        } catch (const std::domain_error &e) {
            rejected = std::string(e.what()).find("copertura continua dei trim") != std::string::npos;
        }
        require(rejected && deviation == -1., "wrap rifiuta il foro senza poligono prima di pubblicare il residuo");
    }

    // A clipped arc and its closing line force the winding proof to use
    // the restricted controls rather than the native carrier's full range.
    const auto arc = std::make_shared<BSplineCurve<2>>(2,
        std::vector<double>{0., 0., 0., 1., 1., 1.},
        std::vector<Vec2>{{-.125, .0625}, {.125, .4375}, {.375, .25}},
        std::vector<double>{1., .5, 1.});
    ProfileLoop clipped{{{arc, {.5, 1.}}}};
    line(clipped, {.375, .25}, {.125, .25});
    profile.holes = {clipped};
    const auto support = planarSheet(Frame3(), {profile});
    const auto face = support.faces().front();
    Vec2 gap;
    require(checkBody(support, {true, true}).empty() &&
            detail::certifyMappingTrimCoverage(source, {{&support, face}}, Frame3(), Frame3(),
                                                linearResolution(), &gap) == Coverage::Uncovered &&
            FaceParameterClassifier(support, face, linearResolution()).classify(gap) == PointLocation::Outside,
            "il winding Bernstein usa la campata razionale rifilata del foro");
}

void checkEnclosedGapBetweenFaceTrims() {
    const auto source = makePlaneSheet(Frame3(Vec3(0., 0., .02), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1.);
    const auto perforated = planarRectangleWithHole(1., 1., .02);
    const auto sourceBefore = writeBodyBinary(source);
    const auto classify = [](const Body &body, const Vec2 &p) {
        return FaceParameterClassifier(body, body.faces().front(), 1e-8).classify(p);
    };
    require(classify(source, holeCenter) == PointLocation::Inside
            && classify(perforated, holeCenter) == PointLocation::Outside,
            "la sorgente piena copre il centro della lacuna, quella forata lo esclude");

    for (double angle : {0., .23}) {
        const double c = std::cos(angle), s = std::sin(angle);
        const auto rotate = [c, s](const Vec2 &p) {
            return Vec2(c * p.x() - s * p.y(), s * p.x() + c * p.y());
        };
        const auto panel = [&](double x0, double x1, double y0, double y1) {
            const std::array<Vec2, 4> corners{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
            std::vector<Vec3> vertices;
            for (const auto &corner : corners) {
                const auto point = holeCenter + rotate(corner);
                vertices.emplace_back(point.x(), point.y(), 0.);
            }
            std::vector<Body::BuildEdge> edges;
            std::vector<Body::BuildFin> loop;
            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                const int j = (i + 1) % vertices.size();
                const auto direction = vertices[j] - vertices[i];
                const double length = norm(direction);
                CurvePtr<3> curve = std::make_shared<Line<3>>(vertices[i], direction);
                if (angle != 0.) {
                    curve = std::make_shared<TrimmedCurve<3>>(curve, 0., length);
                    if (i % 2 == 0)
                        curve = std::make_shared<TransformedCurve>(curve, Transform3());
                }
                edges.push_back({i, j, curve, {0., length}, 0.});
                loop.push_back({i, true, nullptr, 0.});
            }
            Body::BuildFace face;
            face.surface = std::make_shared<Plane>(Frame3());
            face.loops = {std::move(loop)};
            auto sheet = Body::buildSheet(vertices, edges, {face});
            require(computePCurves(sheet) == 0 && sheet.faces().size() == 1
                    && sheet.face(sheet.faces().front()).loops.size() == 1
                    && checkBody(sheet, {true, true}).empty(), "pezza della cornice valida e senza loop interno");
            const auto handles = sheet.edges();
            for (std::size_t i = 0; i < handles.size(); ++i)
                require(sheet.edge(handles[i]).curve->type() == edges[i].curve->type(),
                        "la fixture conserva i wrapper delle rette nella cornice ruotata");
            return sheet;
        };
        const std::array<Body, 4> frame{{panel(-3., -holeHalfX, -3., 3.),
                                         panel(holeHalfX, 3., -3., 3.),
                                         panel(-holeHalfX, holeHalfX, -3., -holeHalfY),
                                         panel(-holeHalfX, holeHalfX, holeHalfY, 3.)}};
        const auto plug = panel(-holeHalfX, holeHalfX, -holeHalfY, holeHalfY);
        std::vector<SurfaceTargetFace> open;
        for (const auto &piece : frame) {
            require(classify(piece, holeCenter) == PointLocation::Outside, "centro fuori dal trim di ogni pezza");
            open.push_back({&piece, piece.faces().front()});
        }
        if (angle != 0.)
            std::reverse(open.begin(), open.end());
        auto covered = open;
        covered.push_back({&plug, plug.faces().front()});
        require(classify(plug, holeCenter) == PointLocation::Inside, "tappo dentro la lacuna della cornice");

        SurfaceWrapOptions options;
        options.angle = angle;
        options.shift = holeCenter - rotate(holeCenter);
        const double holeArea = 4. * holeHalfX * holeHalfY;
        for (const auto *input : {&source, &perforated}) {
            const bool hasHole = input == &perforated;
            double deviation = -1.;
            const auto result = wrapBodyOnFaces(*input, hasHole ? open : covered, options, nullptr, &deviation);
            require(result.isSheet() && result.faces().size() == 1 && checkBody(result, {true, true}).empty()
                    && std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance
                    && result.face(result.faces().front()).loops.size() == (hasHole ? 2u : 1u)
                    && std::fabs(faceArea(result, result.faces().front()) - (4. - (hasHole ? holeArea : 0.))) < 1e-9,
                    "cornice chiusa o allineata al foro sorgente ammette una deformazione valida");
        }

        auto history = sameIndexHistory(perforated, perforated);
        const auto face = perforated.faces().front().index;
        const auto evolution = history.evolution(EntityKind::Face, 0, face);
        const auto edges = history.resultSize(EntityKind::Edge);
        double deviation = -1.;
        bool rejected = false;
        try {
            (void)wrapBodyOnFaces(source, open, options, &history, &deviation);
        } catch (const std::domain_error &e) {
            rejected = std::string(e.what()).find("un punto non incontra") != std::string::npos;
        }
        require(rejected && deviation == -1. && history.resultSize(EntityKind::Edge) == edges
                && history.evolution(EntityKind::Face, 0, face) == evolution && writeBodyBinary(source) == sourceBefore,
                "lacuna tra i soli trim esterni rilevata prima di pubblicare deviazione e genealogia");
    }
}

Body splineGraphQuilt(double bump, bool dyadic, bool uneven) {
    const auto graph = [dyadic, uneven](double lo, double hi, double relief) {
        std::vector<double> u{lo, lo, lo}, v{-2., -2., -2.};
        if (lo == 0. && uneven) {
            u.insert(u.end(), {1200. / 4096., 1240. / 4096., 1256. / 4096., 1257. / 4096.});
            v.insert(v.end(), {640. / 4096., 680. / 4096., 696. / 4096., 697. / 4096.});
        } else if (lo == 0. && dyadic) {
            u.insert(u.end(), {1241. / 4096., 1242. / 4096., 1243. / 4096., 1244. / 4096.});
            v.insert(v.end(), {685. / 4096., 686. / 4096., 687. / 4096., 688. / 4096.});
        } else if (lo == 0.) {
            u.insert(u.end(), {.3031, .3033, .3035, .3037});
            v.insert(v.end(), {.1671, .1673, .1675, .1677});
        }
        u.insert(u.end(), 3, hi);
        v.insert(v.end(), 3, 2.);
        std::vector<Vec3> poles;
        for (std::size_t i = 0; i + 3 < u.size(); ++i) {
            for (std::size_t j = 0; j + 3 < v.size(); ++j) {
                // I coefficienti quadratici riproducono x=u, y=v e z=u*u/512.
                // Il polo (3,3) modifica la quota soltanto fra i quattro nodi interni.
                poles.emplace_back(.5 * (u[i + 1] + u[i + 2]), .5 * (v[j + 1] + v[j + 2]),
                    u[i + 1] * u[i + 2] / 512. + (i == 3 && j == 3 ? relief : 0.));
            }
        }
        return std::make_shared<BSplineSurface>(2, 2, u, v, u.size() - 3, v.size() - 3, poles);
    };
    const auto l = graph(-2., 0., 0.);
    const auto r = graph(0., 2., bump);
    const std::vector<Vec3> points{l->point(-2., -2.), l->point(0., -2.), r->point(2., -2.),
                                   l->point(-2., 2.), l->point(0., 2.), r->point(2., 2.)};
    const std::vector<Body::BuildEdge> edges{
        {0, 1, l->vIso(-2.), {-2., 0.}, 0}, {1, 2, r->vIso(-2.), {0., 2.}, 0},
        {2, 5, r->uIso(2.), {-2., 2.}, 0}, {4, 5, r->vIso(2.), {0., 2.}, 0},
        {3, 4, l->vIso(2.), {-2., 0.}, 0}, {0, 3, l->uIso(-2.), {-2., 2.}, 0},
        {1, 4, l->uIso(0.), {-2., 2.}, 0}};
    Body::BuildFace left, right;
    left.surface = l;
    right.surface = r;
    left.loops = {{{0, true, nullptr, 0}, {6, true, nullptr, 0}, {4, false, nullptr, 0}, {5, false, nullptr, 0}}};
    right.loops = {{{1, true, nullptr, 0}, {2, true, nullptr, 0}, {3, false, nullptr, 0}, {6, false, nullptr, 0}}};
    auto quilt = Body::buildSheet(points, edges, {left, right});
    require(computePCurves(quilt) == 0 && checkBody(quilt, {true, true}).empty(),
            "appoggio curvo con giunzione condivisa valido");
    return quilt;
}

void checkCurvedFaceWrapResidual(bool dyadic, bool oblique = false) {
    const auto source = makePlaneSheet(Frame3(), 1.);
    const auto before = writeBodyBinary(source);
    const auto sourceFace = source.faces().front();
    // Il massimo della base quadratica cade a 17/73 del tratto centrale non uniforme.
    constexpr double t = 17. / 73.;
    constexpr double basisPeak = (40. + 16. * t) / 56. * (1. - t) + (17. - 16. * t) / 17. * t;
    const Vec2 witness = oblique ? Vec2((1240. + 16. * t) / 4096., (680. + 16. * t) / 4096.)
        : dyadic ? Vec2(1242.5 / 4096., 686.5 / 4096.) : Vec2(.3034, .1674);
    const double peakWeight = oblique ? basisPeak * basisPeak : .5625;
    SurfaceWrapOptions options;
    if (oblique) {
        options.angle = .4;
        options.shift = {.05, -.08};
    }
    const double baseHeight = witness.x() * witness.x() / 512.;
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const auto sourceAt = [&](const Vec2 &physical) {
        const auto p = physical - options.shift;
        return Vec2(c * p.x() + s * p.y(), -s * p.x() + c * p.y());
    };
    const auto witnessUV = sourceAt(witness);
    const double fitBudget = .2 * std::min(options.tolerance, linearResolution());
    SurfacePtr referenceImage;
    const double largeRelief = oblique ? 3.3e-8 : 1e-5;
    for (double relief : {0., 1e-8, -1e-8, largeRelief, -largeRelief}) {
        const auto quilt = splineGraphQuilt(relief, dyadic, oblique);
        const auto faces = quilt.faces();
        const auto expected = quilt.face(faces[1]).surface->point(witness.x(), witness.y());
        require(distance(expected, Vec3(witness.x(), witness.y(), baseHeight + peakWeight * relief)) < 1e-12,
                "quota dell'appoggio al centro del rilievo");
        require(FaceParameterClassifier(source, sourceFace, 1e-8).classify(witnessUV) == PointLocation::Inside
                && FaceParameterClassifier(quilt, faces[1], 1e-8).classify(witness) == PointLocation::Inside,
                "residuo misurato all'interno del materiale e dell'appoggio");
        const std::vector<SurfaceTargetFace> targets{{&quilt, faces[0]}, {&quilt, faces[1]}};
        const auto checkSamples = [&](const Surface &image, double bound) {
            const double peakResidual = distance(expected, image.point(witnessUV.x(), witnessUV.y()));
            require(peakResidual <= bound + 1e-12,
                    "limite del residuo sottostimato al picco fisico del rilievo");
            // x=0 campiona la giunzione originale della quilt anche con rotazione e traslazione.
            const double outer = oblique ? .625 : .875, inner = oblique ? .25 : .5;
            for (double x : {-outer, -inner, 0., inner, outer}) {
                for (double y : {-outer, -inner, 0., inner, outer}) {
                    const auto uv = sourceAt(Vec2(x, y));
                    const double residual = distance(Vec3(x, y, x * x / 512.), image.point(uv.x(), uv.y()));
                    require(residual <= bound + 1e-12,
                            "limite del residuo sottostimato sulla giunzione o fuori dal rilievo");
                }
            }
        };
        const auto checkCertificate = [&](const Surface &image) {
            const auto proof = detail::projectedMappingResidualBound(
                source, sourceFace, targets, options, Frame3(), image, {-1., 1.}, {-1., 1.}, fitBudget);
            if (!dyadic) {
                require(proof.status == detail::SurfaceMappingBoundStatus::Unsupported,
                        "nodi decimali: la prova richiede coordinate di Greville esatte");
            } else if (std::fabs(relief) == largeRelief) {
                const double measured = distance(expected, image.point(witnessUV.x(), witnessUV.y()));
                require(measured > fitBudget && proof.status == detail::SurfaceMappingBoundStatus::Failed,
                        "rilievo oltre il budget sul carrier di riferimento");
            } else {
                require(proof.status == detail::SurfaceMappingBoundStatus::Certified
                        && std::isfinite(proof.upperBound) && proof.upperBound >= 0.
                        && proof.upperBound <= fitBudget,
                        "certificato continuo attivo sul carrier B-spline interpolato");
                checkSamples(image, proof.upperBound);
            }
        };
        if (referenceImage)
            checkCertificate(*referenceImage);
        const auto seed = sameIndexHistory(quilt, quilt);
        auto history = seed;
        double deviation = -1.;
        try {
            const auto mapped = wrapBodyOnFaces(source, targets, options, &history, &deviation);
            require(mapped.isSheet() && mapped.faces().size() == 1 && checkBody(mapped, {true, true}).empty(),
                    "deformazione curva attraverso la giunzione valida");
            if (relief == 0.) {
                referenceImage = mapped.face(mapped.faces().front()).surface;
                require(referenceImage->type() == SurfaceType::BSpline, "il certificato usa il carrier interpolato");
                checkCertificate(*referenceImage);
            }
            const auto &carrier = *mapped.face(mapped.faces().front()).surface;
            require(std::isfinite(deviation) && deviation >= 0. && deviation <= options.tolerance,
                    "deviazione multifaccia fuori tolleranza");
            checkSamples(carrier, deviation + (oblique ? 0. : 1e-9));
            if (oblique)
                checkSamples(carrier, fitBudget);
        } catch (const std::domain_error &e) {
            require(std::fabs(relief) == largeRelief && std::string(e.what()).find("residuo") != std::string::npos
                    && deviation == -1. && history.resultSize(EntityKind::Face) == seed.resultSize(EntityKind::Face)
                    && history.evolution(EntityKind::Face, 0, faces[1].index)
                        == seed.evolution(EntityKind::Face, 0, faces[1].index)
                    && writeBodyBinary(source) == before,
                    "rilievo fuori dal budget: rifiuto prima di pubblicare deviazione e genealogia");
        }
    }
}


void checkObliqueRankOneGraph() {
    constexpr double side = .03125, budget = 2e-8;
    const Interval window{-side, side};
    const std::vector<double> knots{-side, -side, -side, -side, side, side, side, side};
    const auto quilt = splineGraphQuilt(0., true, true);
    const auto faces = quilt.faces();
    std::vector<SurfaceTargetFace> targets{{&quilt, faces[0]}, {&quilt, faces[1]}};
    for (double tilt : {0., 1e-18}) {
        // Due colonne UV oblique quasi proporzionali a entrambi i ranghi.
        const Frame3 frame(Vec3(), Vec3(0., -1., tilt), Vec3(1., tilt, 1.));
        auto source = makePlaneSheet(frame, side);
        const auto sourceFace = source.faces().front();
        const std::array<Vec2, 4> corners{{{-side, -side}, {side, -side}, {side, side}, {-side, side}}};
        const auto fins = source.loopFins(source.face(sourceFace).loops.front());
        for (std::size_t i = 0; i < corners.size(); ++i) {
            auto &fin = source.fin(fins[i]);
            source.edge(fin.edge).range = {0., 2. * side};
            fin.pcurve = std::make_shared<Line<2>>(corners[i], corners[(i + 1) % 4] - corners[i]);
        }
        require(checkBody(source, {true, true}).empty(), "sorgente verticale con dominio rettangolare");
        SurfaceWrapOptions options;
        options.angle = .4;
        options.shift = {0., .1};
        const double c = std::cos(options.angle), s = std::sin(options.angle);
        for (double height : {-.125, .125}) {
            options.normalOffset = height;
            const auto expected = [&](double u, double v, bool normalize) {
                const auto p = frame.toGlobal(Vec3(u, v, 0.));
                const double x = c * p.x() - s * p.y(), y = s * p.x() + c * p.y() + .1;
                const Vec3 normal(-x / 256., 0., 1.);
                return Vec3(x, y, x * x / 512.) + (p.z() + height) * (normalize ? normalized(normal) : normal);
            };
            // Si esprime in base Bernstein cubica la mappa con normale non normalizzata.
            const auto bernstein = [](const std::array<Vec3, 4> &samples) {
                const auto r = 27. * samples[1] - 8. * samples[0] - samples[3];
                const auto s = 27. * samples[2] - samples[0] - 8. * samples[3];
                return std::array<Vec3, 4>{samples[0], (2. * r - s) / 18., (2. * s - r) / 18., samples[3]};
            };
            std::array<std::array<Vec3, 4>, 4> rows;
            for (int i = 0; i < 4; ++i) {
                std::array<Vec3, 4> samples;
                for (int j = 0; j < 4; ++j)
                    samples[j] = expected(side * (2. * i / 3. - 1.), side * (2. * j / 3. - 1.), false);
                rows[i] = bernstein(samples);
            }
            std::vector<Vec3> poles;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    poles.push_back(bernstein({rows[0][j], rows[1][j], rows[2][j], rows[3][j]})[i]);
            const auto bound = [&](const BSplineSurface &image) {
                return detail::projectedMappingResidualBound(
                    source, sourceFace, targets, options, Frame3(), image, window, window, budget);
            };
            const BSplineSurface image(3, 3, knots, knots, 4, 4, poles);
            const auto proof = bound(image);
            require(proof.status == detail::SurfaceMappingBoundStatus::Certified
                    && proof.upperBound >= 0. && proof.upperBound <= budget,
                    "proiezione obliqua di rango uno o quasi: certificato sul carrier a quota firmata");
            for (double u : {-side, -.5 * side, 0., .5 * side, side})
                for (double v : {-side, -.5 * side, 0., .5 * side, side})
                    require(distance(expected(u, v, true), image.point(u, v)) <= proof.upperBound + 1e-12,
                            "limite del residuo sottostimato per la proiezione obliqua di rango uno o quasi");

            for (auto &pole : poles)
                pole += Vec3(0., 0., 1e-5);
            const BSplineSurface displaced(3, 3, knots, knots, 4, 4, poles);
            require(distance(expected(.5 * side, 0., true), displaced.point(.5 * side, 0.)) > budget,
                    "errore del carrier traslato in un punto interno");
            require(bound(displaced).status == detail::SurfaceMappingBoundStatus::Failed,
                    "carrier traslato oltre il budget con le pezze in ordine diretto");
            std::swap(targets[0], targets[1]);
            require(bound(displaced).status == detail::SurfaceMappingBoundStatus::Failed,
                    "carrier traslato oltre il budget con le pezze in ordine inverso");
            std::swap(targets[0], targets[1]);
        }
    }
}

void checkObliqueReliefOutsideSource() {
    constexpr double side = .65;
    const auto source = makePlaneSheet(Frame3(), side);
    const auto quilt = splineGraphQuilt(1e-5, true, true);
    const auto faces = quilt.faces();
    const std::vector<SurfaceTargetFace> targets{{&quilt, faces[0]}, {&quilt, faces[1]}};
    // The relief is inside the rotated sheet's axis-aligned box but outside
    // the actual sheet. Try one side in each direction of rotation.
    for (double angle : {-.4, .4}) {
        SurfaceWrapOptions options;
        options.angle = angle;
        const Vec2 center(1240. / 4096., 680. / 4096.);
        const Vec2 beyond(side + .035, .2);
        const double c = std::cos(angle), s = std::sin(angle);
        options.shift = center - Vec2(c * beyond.x() - s * beyond.y(), s * beyond.x() + c * beyond.y());
        double deviation = -1.;
        const auto mapped = wrapBodyOnFaces(source, targets, options, nullptr, &deviation);
        const auto &image = *mapped.face(mapped.faces().front()).surface;
        const auto bound = detail::projectedMappingResidualBound(source, source.faces().front(), targets,
            options, Frame3(), image, {-side, side}, {-side, side}, .2 * linearResolution());
        require(bound.status == detail::SurfaceMappingBoundStatus::Certified && bound.upperBound <= deviation
                && deviation <= options.tolerance && checkBody(mapped, {true, true}).empty(),
                "rilievo fuori dal materiale sul lato obliquo: risultato valido e coperto dal limite dichiarato");
    }
}

void checkProjectedFaceOnCarrierKnot() {
    const auto panel = [](double lo, double hi, bool crease) {
        std::vector<double> knots{lo, lo};
        if (crease)
            knots.push_back(1.);
        knots.insert(knots.end(), 2, hi);
        std::vector<Vec3> poles;
        for (std::size_t i = 1; i + 1 < knots.size(); ++i) {
            for (double v : {-2., 2.})
                poles.emplace_back(knots[i], v, crease && knots[i] == hi ? .25 : 0.);
        }
        const auto graph = std::make_shared<BSplineSurface>(1, 1, knots,
            std::vector<double>{-2., -2., 2., 2.}, knots.size() - 2, 2, poles);
        const std::vector<Vec3> points{graph->point(lo, -2.), graph->point(hi, -2.),
                                      graph->point(hi, 2.), graph->point(lo, 2.)};
        const std::vector<Body::BuildEdge> edges{
            {0, 1, graph->vIso(-2.), {lo, hi}, 0}, {1, 2, graph->uIso(hi), {-2., 2.}, 0},
            {3, 2, graph->vIso(2.), {lo, hi}, 0}, {0, 3, graph->uIso(lo), {-2., 2.}, 0}};
        Body::BuildFace face;
        face.surface = graph;
        face.loops = {{{0, true, std::make_shared<Line<2>>(Vec2(0., -2.), Vec2(1., 0.)), 0},
                       {1, true, std::make_shared<Line<2>>(Vec2(hi, 0.), Vec2(0., 1.)), 0},
                       {2, false, std::make_shared<Line<2>>(Vec2(0., 2.), Vec2(1., 0.)), 0},
                       {3, false, std::make_shared<Line<2>>(Vec2(lo, 0.), Vec2(0., 1.)), 0}}};
        auto body = Body::buildSheet(points, edges, {face});
        require(checkBody(body, {true, true}).empty(), "pezza bilineare valida");
        return body;
    };
    const auto left = panel(-2., 0., false), right = panel(0., 2., true);
    const auto source = makePlaneSheet(Frame3(Vec3(1., 0., 0.), Vec3(1., 0., 0.), Vec3(0., 1., 0.)), .5);
    const auto &target = *right.face(right.faces().front()).surface;
    const auto normal = target.normal(1., 0.);
    require(distance(normal, target.normal(1.125, 0.)) < 1e-12
            && distance(normal, target.normal(.875, 0.)) > .1,
            "la normale sul nodo segue il tratto a destra della piega");
    const std::vector<double> knots{-.5, -.5, -.5, -.5, .5, .5, .5, .5};
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const double u = (knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.;
            const double v = (knots[j + 1] + knots[j + 2] + knots[j + 3]) / 3.;
            poles.push_back(target.point(1., u) + (1. + v) * normal);
        }
    }
    const BSplineSurface image(3, 3, knots, knots, 4, 4, poles);
    require(distance(image.point(.1, .2), target.point(1., .1) + 1.2 * normal) < 1e-12,
            "carrier alzato correttamente sul nodo");
    SurfaceWrapOptions options;
    options.normalOffset = 1.;
    const std::vector<SurfaceTargetFace> faces{{&left, left.faces().front()}, {&right, right.faces().front()}};
    const auto proof = detail::projectedMappingResidualBound(source, source.faces().front(), faces, options,
        Frame3(), image, {-.5, .5}, {-.5, .5}, .2 * linearResolution());
    require(proof.status != detail::SurfaceMappingBoundStatus::Failed,
            "la normale del tratto adiacente al nodo non deve rifiutare il carrier");
}

}

int main(){try{
    const auto cylinder=makeCylinder(Frame3(),10,20);FaceId side;for(auto f:cylinder.faces())if(cylinder.face(f).surface->type()==SurfaceType::Cylinder)side=f;
    BodyHistory history;const auto flat=unrollFace(cylinder,side,Frame3(),&history);
    require(checkBody(flat,{true,true}).empty(),"sviluppo cilindro non valido");
    require(std::fabs(faceArea(flat,flat.faces().front())-400*kPi)<1e-8,"area cilindro non conservata");
    double length=0;for(auto e:flat.edges())length+=arcLength(*flat.edge(e).curve,flat.edge(e).range);
    require(std::fabs(length-2*(20+20*kPi))<1e-8,"perimetro sviluppo errato");
    require(history.isDeclared(EntityKind::Face,flat.faces().front().index),"genealogia sviluppo assente");
    const auto plane=makePlaneSheet(Frame3(),3);const auto planar=unrollFace(plane,plane.faces().front(),Frame3(Vec3(2,4,6),Vec3(0,1,0),Vec3(1,0,0)));
    require(std::fabs(faceArea(planar,planar.faces().front())-36)<1e-10,"area sviluppo piano errata");
    Body::BuildFace conical;conical.surface=std::make_shared<ConicalSurface>(Frame3(),.4,10);
    std::vector<Vec3> points;std::vector<Body::BuildEdge> edges;const double angle=1.5,lo=0,hi=5;
    const auto &cone=*conical.surface;
    points={cone.point(0,lo),cone.point(angle,lo),cone.point(angle,hi),cone.point(0,hi)};
    edges={{0,1,cone.vIso(lo),{0,angle},0},{1,2,cone.uIso(angle),{lo,hi},0},{3,2,cone.vIso(hi),{0,angle},0},{0,3,cone.uIso(0),{lo,hi},0}};
    conical.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
    auto coneBody=Body::buildSheet(points,edges,{conical});require(computePCurves(coneBody)==0,"pc cono");
    auto coneFlat=unrollFace(coneBody,coneBody.faces().front(),Frame3());
    require(std::fabs(faceArea(coneFlat,coneFlat.faces().front())-angle*(10*hi+.5*std::sin(.4)*hi*hi))<1e-8,"area cono non conservata");
    bool rejected=false;auto invalid=plane;invalid.face(invalid.faces().front()).surface=std::make_shared<SphericalSurface>(Frame3(),10);
    try{unrollFace(invalid,invalid.faces().front(),Frame3());}catch(const std::domain_error&){rejected=true;}require(rejected,"superficie non sviluppabile accettata");
    auto trimmedCylinder=cylinder;
    for(auto fin:trimmedCylinder.fins())trimmedCylinder.fin(fin).pcurveTolerance=1e-8;
    const auto tolerantFlat=unrollFace(trimmedCylinder,side,Frame3());
    require(checkBody(tolerantFlat,{true,true}).empty(),"sviluppo bordo con precisione sorgente");
    const auto source=makeBox(Frame3(Vec3(-2,-1,0),Vec3(0,0,1),Vec3(1,0,0)),4,2,1);
    const CylindricalSurface target(Frame3(),20);SurfaceWrapOptions options;options.anchor=Vec2(kHalfPi,10);options.tolerance=1e-6;double error;
    auto wrapped=wrapBodyOnSurface(source,target,options,&history,&error);
    require(checkBody(wrapped,{true,true}).empty()&&!wrapped.isSheet(),"corpo deformato non valido");
    require(error<=options.tolerance,"fit deformazione oltre budget");
    require(std::fabs(massProperties(wrapped,1e-8).volume-8.2)<1e-4,"volume deformazione cilindrica errato");
    for(auto face:wrapped.faces())require(history.isDeclared(EntityKind::Face,face.index),"genealogia deformazione assente");
    SurfaceQuiltMap quilt({{&cylinder,side}},Vec2(kHalfPi,10));
    for(double x:{-2.,0.,2.})for(double y:{-1.,0.,1.}) {
        const Vec2 uv(kHalfPi+x/10.,10+y);const auto flat=quilt.flatten(0,uv);
        require(distance(flat,Vec2(x,y))<1e-9,"sviluppo metrico cilindrico");
        require(distance(quilt.fold(Vec3(flat.x(),flat.y(),0)),cylinder.face(side).surface->point(uv.x(),uv.y()))<1e-9,"mappa quilt andata/ritorno");
    }
    auto quiltBody=quilt.bend(source,options,&history,&error);
    require(checkBody(quiltBody,{true,true}).empty()&&error<=options.tolerance,"piegatura da quilt appiattito B-rep");
    bool exactContact=false;
    for(auto f:quiltBody.faces())exactContact=exactContact||quiltBody.face(f).surface.get()==cylinder.face(side).surface.get();
    require(exactContact,"faccia a quota zero conserva il carrier esatto e la parametrizzazione");
    for(auto e:quiltBody.edges())for(int k=0;k<=12;++k) {
        const auto &edge=quiltBody.edge(e);const auto p=edge.curve->point(edge.range.lo+(edge.range.hi-edge.range.lo)*k/12.);
        require(std::fabs(std::hypot(p.x(),p.y())-10)<1e-7||std::fabs(std::hypot(p.x(),p.y())-11)<1e-7||k!=0,"appoggio e quota della cella cilindrica");
    }
    ExchangeBody quiltExchange;quiltExchange.name="quilt";quiltExchange.body=quiltBody;
    const auto quiltStep=readStep(writeStep({quiltExchange}));
    require(quiltStep.bodies.size()==1&&checkBody(quiltStep.bodies[0].body,{true,true}).empty(),"STEP del corpo piegato con superfici quintiche");
    auto roundtrip=readBodyBinary(writeBodyBinary(wrapped));require(checkBody(roundtrip,{true,true}).empty(),"roundtrip B-rep");
    TessellationOptions mesh;mesh.deflection=.05;require(tessellate(roundtrip,mesh).failedFaces==0,"mesh deformazione incompleta");
    ExchangeBody item;item.name="wrapped";item.body=wrapped;auto step=readStep(writeStep({item}));require(step.bodies.size()==1&&checkBody(step.bodies[0].body).empty(),"scambio STEP deformazione");
    const std::vector<double> knots{0,0,0,1,1,1};std::vector<Vec3> poles;
    const double xy[3]={-5,0,5},square[3]={25,-25,25};for(int i=0;i<3;++i)for(int j=0;j<3;++j)poles.emplace_back(xy[i],xy[j],.02*(square[i]+square[j]));
    const BSplineSurface curved(2,2,knots,knots,3,3,poles);
    const auto small=makeBox(Frame3(Vec3(-.1,-.1,0),Vec3(0,0,1),Vec3(1,0,0)),.2,.2,.02);
    SurfaceWrapOptions onNurbs;onNurbs.anchor=Vec2(.5,.5);auto freeform=wrapBodyOnSurface(small,curved,onNurbs,&history,&error);
    require(checkBody(freeform,{true,true}).empty()&&error<=onNurbs.tolerance,"deformazione su NURBS a doppia curvatura");
    // Ingombro e orientamento di un riempimento su un carrier con asse
    // lungo V e dettagli locali: il fit deve rispettare edge/faccia (1e-7),
    // anche se il budget dell'operazione e' 1e-6.
    const std::vector<double> detailU{0,0,0,0,.1,.2,.3,.4,.5,.6,.7,.8,.9,1,1,1,1};
    const std::vector<double> detailV{0,0,1,1};std::vector<Vec3> detailPoles;
    for(int i=0;i<13;++i)for(double longitudinal:{-50.,50.})
        detailPoles.emplace_back(longitudinal,-15.+2.5*i,2.*std::sin(1.5*i));
    const BSplineSurface detailed(3,1,detailU,detailV,13,2,detailPoles);
    SurfaceWrapOptions repeated;repeated.sourcePlane=Frame3(Vec3(0,0,-18),Vec3(0,0,1),Vec3(1,0,0));repeated.anchor=Vec2(.5,.5);repeated.angle=kHalfPi;
    for(double pos:{-12.,3.,18.}) {
        const auto instance=makeBox(Frame3(Vec3(pos,-2,-18),Vec3(0,0,1),Vec3(1,0,0)),3.6,3.8,.1);
        auto result=wrapBodyOnSurface(instance,detailed,repeated,&history,&error);
        require(result.faces().size()==6&&checkBody(result,{true,true}).empty(),"fit con dettagli NURBS e asse lungo V");
        for(auto e:result.edges())require(result.edge(e).tolerance<=kLinearResolution,"nessuna inflazione delle tolleranze edge");
    }
    auto sheet=makePlaneSheet(Frame3(Vec3(0,0,10),Vec3(0,0,1),Vec3(1,0,0)),1);
    auto collected=Body::collect({&source,&sheet},&history);
    require(checkBody(collected,{true,true}).empty()&&!collected.isSheet(),"collezione mista topologicamente valida");
    require(std::fabs(massProperties(collected).volume-8)<1e-7,"le lamine non alterano il volume della collezione");
    require(history.operandCount()==2&&history.origins(EntityKind::Face,6)[0].operand==1,"genealogia della seconda sorgente nella collezione");
    auto mixedRoundtrip=readBodyBinary(writeBodyBinary(collected));
    require(checkBody(mixedRoundtrip,{true,true}).empty()&&mixedRoundtrip.regions().size()==collected.regions().size(),"regioni miste conservate dal codec");
    ExchangeBody mixedItem;mixedItem.name="mixed";mixedItem.body=collected;const auto mixedStep=readStep(writeStep({mixedItem}));
    int sheets=0,solids=0;for(const auto &part:mixedStep.bodies){require(checkBody(part.body,{true,true}).empty(),"STEP misto valido");if(part.body.isSheet())++sheets;else ++solids;}
    require(sheets==1&&solids==1,"STEP mantiene solido e lamina distinti");
    const auto support=[](double lo,double hi,bool curved) {
        SurfacePtr surface;
        if(curved) {
            const std::vector<double> ku{0,0,0,1,1,1},kv{0,0,1,1};std::vector<Vec3> poles;
            const double x[3]={lo,.5*(lo+hi),hi},z[3]={.025*lo*lo,.025*lo*hi,.025*hi*hi};
            for(int i=0;i<3;++i)for(double y:{-3.,3.})poles.emplace_back(x[i],y,z[i]);
            surface=std::make_shared<BSplineSurface>(2,1,ku,kv,3,2,poles);
        } else surface=std::make_shared<Plane>(Frame3());
        const double a=curved?0:lo,b=curved?1:hi,c=curved?0:-3,d=curved?1:3;
        std::vector<Vec3> vertices{surface->point(a,c),surface->point(b,c),surface->point(b,d),surface->point(a,d)};
        std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(c),{a,b},0},{1,2,surface->uIso(b),{c,d},0},
            {3,2,surface->vIso(d),{a,b},0},{0,3,surface->uIso(a),{c,d},0}};
        Body::BuildFace face;face.surface=surface;face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
        auto result=Body::buildSheet(vertices,edges,{face});require(computePCurves(result)==0,"SP-curve appoggio");return result;
    };
    // Due facce del medesimo carrier, una sola giunzione topologica.
    const std::vector<Vec3> joinedPoints{curved.point(0,0),curved.point(.5,0),curved.point(1,0),curved.point(0,1),curved.point(.5,1),curved.point(1,1)};
    std::vector<Body::BuildEdge> joinedEdges{{0,1,curved.vIso(0),{0,.5},0},{1,2,curved.vIso(0),{.5,1},0},
        {2,5,curved.uIso(1),{0,1},0},{4,5,curved.vIso(1),{.5,1},0},{3,4,curved.vIso(1),{0,.5},0},
        {0,3,curved.uIso(0),{0,1},0},{1,4,curved.uIso(.5),{0,1},0}};
    Body::BuildFace joinedLeft,joinedRight;joinedLeft.surface=joinedRight.surface=std::make_shared<BSplineSurface>(curved);
    joinedLeft.loops={{{0,true,nullptr,0},{6,true,nullptr,0},{4,false,nullptr,0},{5,false,nullptr,0}}};
    joinedRight.loops={{{1,true,nullptr,0},{2,true,nullptr,0},{3,false,nullptr,0},{6,false,nullptr,0}}};
    auto quiltSupport=Body::buildSheet(joinedPoints,joinedEdges,{joinedLeft,joinedRight});
    require(computePCurves(quiltSupport)==0&&checkBody(quiltSupport,{true,true}).empty(),"quilt topologico di due NURBS");
    const std::vector<SurfaceTargetFace> flattenTargets{{&quiltSupport,quiltSupport.faces()[0]},{&quiltSupport,quiltSupport.faces()[1]}};
    const auto autoAnchor=surfaceQuiltAnchor(flattenTargets);
    require(distance(autoAnchor,Vec2(.5,.5))<1e-12,"origine sul bordo topologico comune");
    SurfaceQuiltMap flattenMap(flattenTargets,autoAnchor);
    SurfaceFlattenStats flattenStats;BodyHistory flattenHistory;
    const Frame3 flattenPlane(Vec3(2,4,6),normalized(Vec3(1,2,3)),Vec3(1,0,0));
    const auto flatJoined=flattenMap.flattenSheet(flattenPlane,1e-6,&flattenStats,&flattenHistory);
    require(flatJoined.isSheet()&&flatJoined.faces().size()==2&&flatJoined.edges().size()==7&&flatJoined.vertices().size()==6&&
        checkBody(flatJoined,{true,true}).empty(),"appiattimento conserva il bordo comune e la lamina");
    for(auto v:flatJoined.vertices())require(std::fabs(flattenPlane.toLocal(flatJoined.vertex(v).point).z())<1e-12,"tutti i vertici sul piano destinazione");
    require(flattenStats.samples>400&&flattenStats.minimumStretch<1&&flattenStats.maximumStretch>1&&
        flattenStats.boundaryDeviation<=2e-8,"deformazione della doppia curvatura misurata, contorni nel budget");
    for(auto f:flatJoined.faces())require(flattenHistory.isDeclared(EntityKind::Face,f.index),"genealogia delle facce appiattite");
    require(flattenHistory.descendants(EntityKind::Edge,0,6).size()==1,"un solo discendente del bordo comune");
    SurfaceQuiltMap reverseMap({flattenTargets[1],flattenTargets[0]},autoAnchor);
    require(checkBody(reverseMap.flattenSheet(flattenPlane,1e-6),{true,true}).empty(),"ordine inverso delle facce appiattite valido");
    const std::vector<SurfaceTargetFace> planeTarget{{&plane,plane.faces()[0]}};
    SurfaceQuiltMap planeMap(planeTarget,surfaceQuiltAnchor(planeTarget));
    const auto flatPlane=planeMap.flattenSheet(flattenPlane,1e-6,&flattenStats);
    require(std::fabs(faceArea(flatPlane,flatPlane.faces()[0])-36)<1e-9&&
        std::fabs(flattenStats.minimumStretch-1)<1e-10&&std::fabs(flattenStats.maximumStretch-1)<1e-10,
        "appiattimento di un piano senza deformazione artificiale");
    rejected=false;try{quilt.flattenSheet(flattenPlane,1e-6);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"superficie periodica non tagliata rifiutata esplicitamente");
    ExchangeBody flattenExchange;flattenExchange.name="superfici appiattite";flattenExchange.body=flatJoined;
    const auto flattenStep=readStep(writeStep({flattenExchange}));
    require(flattenStep.bodies.size()==1&&flattenStep.bodies[0].body.faces().size()==2&&
        checkBody(flattenStep.bodies[0].body,{true,true}).empty(),"STEP dell'insieme appiattito");
    SurfaceQuiltMap multiQuilt({{&quiltSupport,quiltSupport.faces()[0]},{&quiltSupport,quiltSupport.faces()[1]}},Vec2(.5,.5));
    for(int side=0;side<2;++side)for(double u:{.1,.4})for(double v:{.2,.7}) {
        const Vec2 uv(side?.5+u:u,v);const auto xy=multiQuilt.flatten(side,uv);
        require(distance(multiQuilt.fold(Vec3(xy.x(),xy.y(),0)),curved.point(uv.x(),uv.y()))<1e-9,"corrispondenza multifaccia reversibile");
    }
    SurfaceQuiltMap offGridQuilt({{&quiltSupport,quiltSupport.faces()[0]}},Vec2(.37,.43));
    for(double v:{.13,.41,.77}) {
        const auto xy=offGridQuilt.flatten(0,Vec2(.37,v));
        require(std::fabs(xy.x())<1e-12,"origine della mappa esatta anche fuori dai nodi del fit");
        require(distance(offGridQuilt.fold(Vec3(0,xy.y(),.1)),curved.point(.37,v)+.1*curved.normal(.37,v))<1e-9,"ancoraggio e normale coerenti lungo la meridiana");
    }
    const auto seamLeft=multiQuilt.flatten(0,Vec2(.5,.5)),seamRight=multiQuilt.flatten(1,Vec2(.5,.5));
    require(distance(seamLeft,seamRight)<1e-10,"nessuna cucitura artificiale nella mappa piana");
    const auto bentAcross=multiQuilt.bend(small,onNurbs,&history,&error);
    require(checkBody(bentAcross,{true,true}).empty()&&error<=onNurbs.tolerance,"cella solida piegata attraverso la giunzione");
    rejected=false;try{multiQuilt.fold(Vec3(100,0,0));}catch(const std::domain_error&){rejected=true;}
    require(rejected,"materiale fuori dal quilt non viene esteso silenziosamente");
    const auto spanning=makeBox(Frame3(Vec3(-1,-.5,0),Vec3(0,0,1),Vec3(1,0,0)),2,1,.2);
    auto left=support(-3,0,false),right=support(0,3,false);SurfaceWrapOptions joined;
    auto across=wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(across.faces().size()==6&&checkBody(across,{true,true}).empty(),"un unico solido attraversa due facce destinazione");
    require(std::fabs(massProperties(across).volume-.4)<1e-7,"volume fra due facce coplanari");
    // Multifaccia uses physical distances in the tangent plane: changing
    // the UV chart of a flat target must not shear or shrink the source.
    const auto obliqueSupport=[](double lo,double hi,double shear,bool reversed) {
        const auto surface=std::make_shared<BSplineSurface>(1,1,
            std::vector<double>{lo,lo,hi,hi},std::vector<double>{-3.,-3.,3.,3.},2,2,
            std::vector<Vec3>{{lo-3.*shear,-3.,0.},{lo+3.*shear,3.,0.},
                              {hi-3.*shear,-3.,0.},{hi+3.*shear,3.,0.}});
        const std::vector<Vec3> points{surface->point(lo,-3.),surface->point(hi,-3.),
                                     surface->point(hi,3.),surface->point(lo,3.)};
        const std::vector<Body::BuildEdge> edges{
            {0,1,surface->vIso(-3.),{lo,hi},0},{1,2,surface->uIso(hi),{-3.,3.},0},
            {3,2,surface->vIso(3.),{lo,hi},0},{0,3,surface->uIso(lo),{-3.,3.},0}};
        Body::BuildFace face;face.surface=surface;face.sense=!reversed;
        if(reversed)face.loops={{{3,true,nullptr,0},{2,true,nullptr,0},{1,false,nullptr,0},{0,false,nullptr,0}}};
        else face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
        auto result=Body::buildSheet(points,edges,{face});
        require(computePCurves(result)==0&&checkBody(result,{true,true}).empty(),
                "SP-curve e orientamento appoggio con UV oblique");
        return result;
    };
    for(double shear:{-1.,1.})for(bool reversed:{false,true}) {
        const auto obliqueLeft=obliqueSupport(-3.,0.,shear,reversed);
        const auto obliqueRight=obliqueSupport(0.,3.,shear,reversed);
        const std::vector<SurfaceTargetFace> targets{{&obliqueLeft,obliqueLeft.faces()[0]},
                                                    {&obliqueRight,obliqueRight.faces()[0]}};
        SurfaceWrapOptions physical;physical.angle=.3;physical.normalOffset=.1;
        const auto result=wrapBodyOnFaces(spanning,targets,physical,&history,&error);
        require(checkBody(result,{true,true}).empty()&&error<=physical.tolerance,
                "appoggio multifaccia con UV oblique valido");
        require(std::fabs(massProperties(result).volume-.4)<1e-7,
                "UV oblique non devono ridurre il volume su appoggio piano");
        const auto input=spanning.vertices(),output=result.vertices();
        const double orientation=reversed?-1.:1.,c=std::cos(physical.angle),s=std::sin(physical.angle);
        for(std::size_t i=0;i<input.size();++i) {
            const auto p=spanning.vertex(input[i]).point;
            const Vec3 expected(c*p.x()-s*p.y(),orientation*(s*p.x()+c*p.y()),
                                orientation*(p.z()+physical.normalOffset));
            require(distance(result.vertex(output[i]).point,expected)<1e-8,
                    "posizione fisica indipendente da shear UV e verso della faccia");
        }
        // Y=4 lies outside both trimmed faces. The old normalized Sv axis
        // silently compressed it to |Y|=sqrt(8), incorrectly accepting it.
        SurfaceWrapOptions outside;
        require(!fitSurfaceWrapPlacement({Vec2(0.,4.)},targets,outside,.001),
                "UV oblique non devono nascondere un centro fuori dall'appoggio");
        require(distance(outside.shift,Vec2())==0.,"centraggio rifiutato conserva la traslazione");
        physical.rigid=true;
        const auto rigidResult=wrapBodyOnFaces(spanning,targets,physical);
        const auto rigidOutput=rigidResult.vertices();
        for(std::size_t i=0;i<output.size();++i)
            require(distance(result.vertex(output[i]).point,rigidResult.vertex(rigidOutput[i]).point)<1e-8,
                    "posa rigida e deformabile coincidono sull'appoggio piano");
    }
    left=support(-3,0,true);right=support(0,3,true);joined.anchor=Vec2(1,.5);
    across=wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(across.faces().size()==6&&checkBody(across,{true,true}).empty()&&error<=joined.tolerance,"attraversamento di due facce NURBS tangenti");
    const auto vertices=spanning.vertices(),mapped=across.vertices();
    for(std::size_t i=0;i<vertices.size();++i) {
        const auto p=spanning.vertex(vertices[i]).point;const auto normal=normalized(Vec3(-.05*p.x(),0,1));
        const auto expected=Vec3(p.x(),p.y(),.025*p.x()*p.x())+p.z()*normal;
        require(distance(across.vertex(mapped[i]).point,expected)<1e-7,"mappa sulle due superfici uguale alla parabola analitica");
    }
    left=support(-3,-.25,false);right=support(.25,3,false);joined.anchor=Vec2();rejected=false;
    try{wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"vuoto fra superfici destinazione rifiutato");
    // A narrow gap falls between the original edge/face fitting samples.
    // One continuous source edge still crosses it, so require a real contact
    // at an interior parameter rather than accepting the fitted solid.
    constexpr double gapLo=.312345,gapHi=.312355;
    const auto narrowLeft=support(-3.,gapLo,false),narrowRight=support(gapHi,3.,false);
    const std::vector<SurfaceTargetFace> narrowTargets{{&narrowLeft,narrowLeft.faces()[0]},
                                                     {&narrowRight,narrowRight.faces()[0]}};
    auto gapHistory=sameIndexHistory(plane,plane);
    const auto gapFace=plane.faces().front().index;
    const auto originalEvolution=gapHistory.evolution(EntityKind::Face,0,gapFace);
    double gapDeviation=-1.;
    rejected=false;
    try{wrapBodyOnFaces(spanning,narrowTargets,SurfaceWrapOptions(),&gapHistory,&gapDeviation);}
    catch(const std::domain_error &e){rejected=std::string(e.what()).find("un punto non incontra")!=std::string::npos;}
    require(rejected&&gapDeviation==-1.&&gapHistory.resultSize(EntityKind::Face)==1
            &&gapHistory.evolution(EntityKind::Face,0,gapFace)==originalEvolution,
            "gap multifaccia rilevato sul bordo prima di pubblicare deviazione e genealogia");
    // A quadratic edge enters the gap near its off-grid X maximum
    // and leaves on the same side; the chord is entirely on the left.
    constexpr double peakParameter=.3712345,peakX=.312352;
    const double x0=peakX-peakParameter*peakParameter;
    const double x1=x0+peakParameter,x2=x0+2.*peakParameter-1.;
    const auto bow=std::make_shared<BSplineCurve<3>>(2,std::vector<double>{0,0,0,1,1,1},
        std::vector<Vec3>{{x0,-.5,0.},{x1,0.,0.},{x2,.5,0.}});
    const Vec3 bowStart=bow->point(0.),bowEnd=bow->point(1.);
    const auto chordDirection=bowStart-bowEnd;
    const auto chord=std::make_shared<Line<3>>(bowEnd,chordDirection);
    Body::BuildFace bowedFace;
    bowedFace.surface=std::make_shared<Plane>(Frame3());
    bowedFace.loops={{{0,true,nullptr,0},{1,true,nullptr,0}}};
    auto bowed=Body::buildSheet({bowStart,bowEnd},
        {{0,1,bow,{0.,1.},0.},{1,0,chord,{0.,norm(chordDirection)},0.}},{bowedFace});
    require(computePCurves(bowed)==0&&checkBody(bowed,{true,true}).empty()
            &&bowStart.x()<gapLo&&bowEnd.x()<gapLo&&gapLo<bow->point(peakParameter).x()
            &&bow->point(peakParameter).x()<gapHi,"fixture bordo curvo nella fessura");
    SurfaceWrapOptions shiftedBow;
    shiftedBow.shift=Vec2(-2e-5,0.);
    const auto clearBow=wrapBodyOnFaces(bowed,narrowTargets,shiftedBow,nullptr,&error);
    require(clearBow.isSheet()&&checkBody(clearBow,{true,true}).empty()&&error<=shiftedBow.tolerance
            &&std::fabs(faceArea(clearBow,clearBow.faces().front())-faceArea(bowed,bowed.faces().front()))<1e-9,
            "bordo curvo traslato fuori dalla fessura resta valido");
    auto bowHistory=sameIndexHistory(spanning,spanning);
    const auto oldBowFaceCount=bowHistory.resultSize(EntityKind::Face);
    const auto bowBefore=writeBodyBinary(bowed);
    double bowDeviation=-1.;
    rejected=false;
    try{(void)wrapBodyOnFaces(bowed,narrowTargets,SurfaceWrapOptions(),&bowHistory,&bowDeviation);}
    catch(const std::domain_error &e){rejected=std::string(e.what()).find("un punto non incontra")!=std::string::npos;}
    require(rejected&&bowDeviation==-1.&&bowHistory.resultSize(EntityKind::Face)==oldBowFaceCount
            &&writeBodyBinary(bowed)==bowBefore,"bordo curvo che rientra dalla fessura rifiutato atomicamente");

    const auto leftPart=makeBox(Frame3(Vec3(-1.,-.5,0.),Vec3(0.,0.,1.),Vec3(1.,0.,0.)),1.1,1.,.2);
    const auto rightPart=makeBox(Frame3(Vec3(.6,-.5,0.),Vec3(0.,0.,1.),Vec3(1.,0.,0.)),.4,1.,.2);
    const auto disconnected=Body::collect({&leftPart,&rightPart});
    const auto splitResult=wrapBodyOnFaces(disconnected,narrowTargets,SurfaceWrapOptions(),nullptr,&error);
    require(splitResult.faces().size()==12&&checkBody(splitResult,{true,true}).empty()
            &&std::fabs(massProperties(splitResult).volume-.3)<1e-8,
            "materiale disconnesso ai lati del gap non richiede copertura del box globale");

    checkInteriorHoleInFaceWrap();
    checkHoleInCylindricalFaceWrap();
    checkPartlyCoveredCircularHoleInFaceWrap();
    checkSplineHoleInFaceWrap();
    checkEnclosedCurvedGapBetweenFaceTrims();
    checkContinuousOrthogonalTrimCoverage();
    checkContinuousConvexTrimCoverage();
    checkContinuousSplineSupportTrims();
    checkContinuousInteriorBezierCut(InteriorTrimCut::Rational);
    checkContinuousInteriorBezierCut(InteriorTrimCut::SimpleKnot);
    checkContinuousInteriorBezierCut(InteriorTrimCut::RoundedPoles);
    checkIntervalBezierTrimCut(IntervalTrimEndpoint::Saturated);
    checkIntervalBezierTrimCut(IntervalTrimEndpoint::SimpleKnot);
    checkIntervalBezierTrimCut(IntervalTrimEndpoint::ScaledWeights);
    checkRoundedTrimEndpoint();
    checkContinuousPartialSplineSeam();
    checkContinuousReentrantSplineSeam();
    checkContinuousSplineOpeningCoverage();
    checkContinuousSplineOpeningWithoutChordRegion();
    checkEnclosedGapBetweenFaceTrims();
    checkCurvedFaceWrapResidual(true);
    checkCurvedFaceWrapResidual(false);
    checkCurvedFaceWrapResidual(true, true);
    checkObliqueReliefOutsideSource();
    checkObliqueRankOneGraph();
    checkProjectedFaceOnCarrierKnot();

    left=support(-3,0,false);right=support(0,3,false);SurfaceWrapOptions centered;
    require(fitSurfaceWrapPlacement({Vec2(3.1,0),Vec2(-2,0)},{{&left,left.faces()[0]},{&right,right.faces()[0]}},centered,.5),"centraggio trova piccola traslazione con tutti i centri nelle facce");
    require(centered.shift.x()<0&&norm(centered.shift)<=.5,"traslazione limitata senza scala");
    const auto previous=centered.shift;
    require(!fitSurfaceWrapPlacement({Vec2(10,0)},{{&left,left.faces()[0]},{&right,right.faces()[0]}},centered,.1)&&distance(centered.shift,previous)==0,"ricerca fallita conserva la posizione");
    Body::BuildFace triangleFace;triangleFace.surface=std::make_shared<Plane>(Frame3());
    const std::vector<Vec3> trianglePoints{{0,0,0},{1,0,0},{0,1,0}};
    std::vector<Body::BuildEdge> triangleEdges;
    for(int i=0;i<3;++i){const auto d=trianglePoints[(i+1)%3]-trianglePoints[i];triangleEdges.push_back({i,(i+1)%3,std::make_shared<Line<3>>(trianglePoints[i],normalized(d)),{0,norm(d)},0});}
    triangleFace.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,true,nullptr,0}}};
    auto triangle=Body::buildSheet(trianglePoints,triangleEdges,{triangleFace});require(computePCurves(triangle)==0,"SP-curve triangolo sorgente");
    const auto distant=makePlaneSheet(Frame3(Vec3(10,0,0),Vec3(0,0,1),Vec3(1,0,0)),1);
    SurfaceWrapOptions polygonMapping;
    const auto mappedTriangle=wrapBodyOnFaces(triangle,{{&triangle,triangle.faces()[0]},{&distant,distant.faces()[0]}},polygonMapping,&history,&error);
    require(checkBody(mappedTriangle,{true,true}).empty()&&std::fabs(faceArea(mappedTriangle,mappedTriangle.faces()[0])-.5)<1e-9,"fit rettangolare esteso soltanto fuori dal triangolo materiale");
    // Ogni sorgente e' una rototraslazione: distanze, volume e carrier
    // analitici restano invariati, Z segue la normale al centro del profilo.
    options.rigid=true;options.normalOffset=.3;
    auto rigid=wrapBodyOnSurface(source,target,options,&history,&error);
    require(error==0&&checkBody(rigid,{true,true}).empty(),"posizionamento rigido valido senza fit");
    require(std::fabs(massProperties(rigid).volume-8)<1e-8,"volume rigido invariato");
    const auto originalVertices=source.vertices(),rigidVertices=rigid.vertices();
    for(std::size_t i=0;i<originalVertices.size();++i)for(std::size_t j=0;j<i;++j)
        require(std::fabs(distance(source.vertex(originalVertices[i]).point,source.vertex(originalVertices[j]).point)-
                          distance(rigid.vertex(rigidVertices[i]).point,rigid.vertex(rigidVertices[j]).point))<1e-10,"distanze rigide conservate");
    for(auto f:rigid.faces())require(rigid.face(f).surface->type()==SurfaceType::Plane,"piani sorgenti conservati");
    const auto near=makeBox(Frame3(Vec3(1,-.5,0),Vec3(0,0,1),Vec3(1,0,0)),1,1,.2);
    left=support(-3,0,true);right=support(0,3,true);joined.anchor=Vec2(1,.5);joined.rigid=true;
    rigid=wrapBodyOnFaces(near,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(std::fabs(massProperties(rigid).volume-.2)<1e-9,"volume rigido su due facce");
    const auto ids=rigid.vertices();Vec3 baseCenter,topCenter;int countBase=0,countTop=0;
    const auto from=near.vertices();for(std::size_t i=0;i<from.size();++i) {
        if(near.vertex(from[i]).point.z()<.1){baseCenter+=rigid.vertex(ids[i]).point;++countBase;}
        else {topCenter+=rigid.vertex(ids[i]).point;++countTop;}
    }
    baseCenter=baseCenter/double(countBase);topCenter=topCenter/double(countTop);
    require(distance(baseCenter,Vec3(1.5,0,.025*1.5*1.5))<1e-9,"centro sorgente sul supporto corretto");
    require(distance(normalized(topCenter-baseCenter),normalized(Vec3(-.075,0,1)))<1e-10,"ogni copia segue la normale locale");
    FaceParameterClassifier classifier(left,left.faces()[0],1e-7);
    require(classifier.classify(Vec2(.5,.5))==PointLocation::Inside&&classifier.classify(Vec2(-.1,.5))==PointLocation::Outside&&
            classifier.classify(Vec2(1,.5))==PointLocation::Boundary,"classificazione UV interno/esterno/bordo");
    // La scorciatoia retta/Bezier non deve perdere radici su una falda
    // ripiegata, ne' nascondere due destinazioni sovrapposte.
    std::vector<Vec3> foldPoles;
    for(int i=0;i<3;++i)for(double y:{-1.,1.})foldPoles.emplace_back(i==1?-1.:1.,y,i-1.);
    const BSplineSurface fold(2,1,{0,0,0,1,1,1},{0,0,1,1},3,2,foldPoles);
    auto hits=numericCurveSurface(Line<3>(Vec3(.25,0,-2),Vec3(0,0,1)),{0,4},fold,1e-8);
    require(hits.parameters.size()==2&&std::fabs(hits.parameters[0]-1.5)<1e-7&&std::fabs(hits.parameters[1]-2.5)<1e-7,"ricerca completa delle due radici su superficie ripiegata");
    // Un secondo carrier regolare alla stessa impronta resta ambiguo.
    const auto over=makePlaneSheet(Frame3(Vec3(0,0,1),Vec3(0,0,1),Vec3(1,0,0)),3);
    left=support(-3,0,false);right=support(0,3,false);joined.anchor=Vec2();rejected=false;
    try{wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]},{&over,over.faces()[0]}},joined);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"sovrapposizioni rigide rifiutate senza scelta arbitraria");
    options.rigid=false;
    options.normalOffset=-21;rejected=false;try{wrapBodyOnSurface(source,target,options);}catch(const std::domain_error&){rejected=true;}require(rejected,"inversione deformazione accettata");
    std::cout<<"PASS sviluppo piano/cilindro/cono, aree/lunghezze, rifiuto doppia curvatura, deformazione cilindrica/volume, genealogia, B-rep, STEP e mesh, appoggio multifaccia continuo/NURBS/vuoti\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
