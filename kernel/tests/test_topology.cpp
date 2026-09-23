#include "fk_body_check.h"
#include "fk_primitives.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

bool topologyValid(const Body &body) {
    CheckOptions options;
    options.geometry = false;
    const std::vector<CheckIssue> issues = checkBody(body, options);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    return issues.empty();
}

bool hasIssue(const Body &body, CheckCode code) {
    for (const CheckIssue &issue : checkBody(body))
        if (issue.code == code) return true;
    return false;
}

template <class T>
T pick(std::mt19937 &rng, const std::vector<T> &values) {
    return values[uniformInt(rng, 0, int(values.size()) - 1)];
}

}

FK_TEST(MvfsKvfs) {
    Body body;
    const Body::MvfsResult m = body.mvfs(Vec3(1, 2, 3));
    const TopologyCounts c = body.counts();
    FK_CHECK(c.vertices == 1 && c.edges == 0 && c.faces == 1 && c.loops == 1 && c.shells == 1 && c.regions == 2);
    FK_CHECK(topologyValid(body));
    FK_CHECK(shellGenus(body, m.shell) == 0);
    FK_CHECK(body.region(m.region).solid && !body.region(body.exteriorRegion()).solid);
    body.kvfs(m.shell);
    const TopologyCounts after = body.counts();
    FK_CHECK(after.vertices == 0 && after.faces == 0 && after.shells == 0 && after.regions == 1);
}

// Sequenze casuali di operatori costruttivi controllate a ogni passo, poi
// disfatte all'indietro con le inverse fino al solido minimo. kemr viene
// provato e subito disfatto con mekr (che ricrea l'edge con un id nuovo).
FK_TEST(RandomEulerSequences) {
    std::mt19937 rng(80);
    for (int trial = 0; trial < 40; ++trial) {
        Body body;
        const Body::MvfsResult start = body.mvfs(randomVec<3>(rng, 10.0));
        enum class Inverse { Kev, Kef, Jekv };
        std::vector<std::pair<Inverse, EdgeId>> undo;
        bool ok = true;
        for (int step = 0; step < 150 && ok; ++step) {
            const int op = uniformInt(rng, 0, 3);
            if (op == 0) {  // mev
                const LoopId l = pick(rng, body.loops());
                const Body::MevResult r = body.loop(l).isolatedVertex.valid()
                    ? body.mev(l, randomVec<3>(rng, 10.0))
                    : body.mev(pick(rng, body.loopFins(l)), randomVec<3>(rng, 10.0));
                undo.push_back({Inverse::Kev, r.edge});
            } else if (op == 1) {  // mef
                std::vector<LoopId> withFins;
                for (LoopId l : body.loops())
                    if (body.loop(l).first.valid()) withFins.push_back(l);
                if (withFins.empty()) continue;
                const std::vector<FinId> fins = body.loopFins(pick(rng, withFins));
                undo.push_back({Inverse::Kef, body.mef(pick(rng, fins), pick(rng, fins)).edge});
            } else if (op == 2) {  // semv
                const std::vector<EdgeId> edges = body.edges();
                if (edges.empty()) continue;
                undo.push_back({Inverse::Jekv, body.semv(pick(rng, edges), randomVec<3>(rng, 10.0)).edge});
            } else {  // kemr + mekr
                std::vector<EdgeId> candidates;
                for (EdgeId e : body.edges())
                    if (body.fin(body.edge(e).forward).loop == body.fin(body.edge(e).backward).loop) candidates.push_back(e);
                if (candidates.empty()) continue;
                const EdgeId e = pick(rng, candidates);
                const FinId a = body.edge(e).forward, b = body.edge(e).backward;
                const LoopId l = body.fin(a).loop;
                const FinId afterB = body.fin(b).next == a ? FinId() : body.fin(b).next;
                const FinId afterA = body.fin(a).next == b ? FinId() : body.fin(a).next;
                const TopologyCounts before = body.counts();
                const LoopId ring = body.kemr(e);
                FK_CHECK(body.counts().loops == before.loops + 1 && body.counts().edges == before.edges - 1);
                ok &= topologyValid(body);
                const EdgeId restored = body.mekr(l, afterB, ring, afterA);
                for (auto &entry : undo)
                    if (entry.second == e) entry.second = restored;
                FK_CHECK(body.counts().loops == before.loops && body.counts().edges == before.edges);
            }
            ok &= topologyValid(body);
            for (ShellId s : body.shells()) FK_CHECK(shellGenus(body, s) == 0);
        }
        while (!undo.empty() && ok) {
            const auto [inverse, edge] = undo.back();
            undo.pop_back();
            if (inverse == Inverse::Kev) body.kev(edge);
            else if (inverse == Inverse::Kef) body.kef(edge);
            else body.jekv(edge);
            ok &= topologyValid(body);
        }
        const TopologyCounts c = body.counts();
        FK_CHECK(c.vertices == 1 && c.edges == 0 && c.fins == 0 && c.faces == 1 && c.loops == 1);
        FK_CHECK(body.contains(start.vertex));
    }
}

FK_TEST(FaceRingOperators) {
    // Prisma con foro: kfmrh ha trasformato il tappo in un anello della base.
    Body body = makePrism(Frame3(), {Vec2(0, 0), Vec2(10, 0), Vec2(10, 10), Vec2(0, 10)},
                          {{Vec2(4, 4), Vec2(6, 4), Vec2(6, 6), Vec2(4, 6)}}, 3.0);
    const ShellId shell = body.shells().front();
    FK_CHECK(shellGenus(body, shell) == 1);
    // mfkrh su un anello e kfmrh di nuovo: stessi conteggi, stesso genere.
    LoopId ring;
    FaceId owner;
    for (FaceId f : body.faces())
        if (body.face(f).loops.size() == 2) {
            owner = f;
            ring = body.face(f).loops.back();
        }
    FK_CHECK(ring.valid());
    const TopologyCounts before = body.counts();
    const FaceId separated = body.mfkrh(ring);
    FK_CHECK(body.counts().faces == before.faces + 1);
    body.kfmrh(separated, owner);
    FK_CHECK(body.counts().faces == before.faces && body.counts().loops == before.loops);
    FK_CHECK(topologyValid(body));
    FK_CHECK(shellGenus(body, shell) == 1);
}

FK_TEST(SemvSplitsGeometry) {
    Body body = makeBox(Frame3(), 4.0, 2.0, 1.0);
    const EdgeId e = body.edges().front();
    const Interval range = body.edge(e).range;
    const double middle = 0.5 * (range.lo + range.hi);
    const Vec3 point = body.edge(e).curve->point(middle);
    const Body::SemvResult split = body.semv(e, point, middle);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(body.edge(e).range.hi == middle && body.edge(split.edge).range.lo == middle);
    FK_CHECK(body.edgeEnd(e) == split.vertex && body.edgeStart(split.edge) == split.vertex);
    body.jekv(split.edge);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(body.edge(e).range.hi == range.hi && body.counts().edges == 12);
}

// Il controllo deve accorgersi dei body rovinati.
FK_TEST(CheckerDetectsDamage) {
    const Body box = makeBox(Frame3(Vec3(1, 2, 3), Vec3(0, 0, 1), Vec3(1, 1, 0)), 3.0, 2.0, 1.0);
    FK_CHECK(checkBody(box).empty());

    Body moved = box;
    moved.vertex(moved.vertices().front()).point += Vec3(0.0, 0.0, 1e-3);
    FK_CHECK(hasIssue(moved, CheckCode::VertexOffEdge));

    Body tolerant = moved;  // con una tolleranza locale sufficiente il vertice e' accettato
    tolerant.vertex(tolerant.vertices().front()).tolerance = 2e-3;
    FK_CHECK(!hasIssue(tolerant, CheckCode::VertexOffEdge));

    Body flipped = box;
    flipped.face(flipped.faces().front()).sense = false;
    FK_CHECK(hasIssue(flipped, CheckCode::LoopOrientation));

    Body shifted = box;
    const FaceId face = shifted.faces().back();
    const Frame3 frame = static_cast<const Plane &>(*shifted.face(face).surface).frame();
    shifted.face(face).surface = std::make_shared<Plane>(Frame3(frame.origin() + 0.01 * frame.zDir(), frame.zDir(), frame.xDir()));
    FK_CHECK(hasIssue(shifted, CheckCode::EdgeOffFace));

    Body broken = box;
    const FinId fin = broken.fins().front();
    broken.fin(fin).next = broken.fin(broken.fin(fin).next).next;
    FK_CHECK(hasIssue(broken, CheckCode::BrokenLoop));

    Body bare = box;
    bare.edge(bare.edges().front()).curve.reset();
    FK_CHECK(hasIssue(bare, CheckCode::MissingGeometry));

    FK_CHECK_THROWS(Body().vertex(VertexId(0)));
    FK_CHECK_THROWS(Body(box).kev(box.edges().front()));  // non e' uno sperone
    FK_CHECK_THROWS(Body(box).kemr(box.edges().front()));  // fin in facce diverse
}
