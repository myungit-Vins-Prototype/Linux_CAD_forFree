#pragma once
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_kernel.h"
#include "cad_sketch_refs.h"
#include "fk_cancel.h"
#include "fk_helix.h"
#include "fk_helix_projection.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace K10SketchProjectionTests {
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
static void need(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
static QString hx(double x) {
    std::ostringstream s;
    s << std::hexfloat << x;
    return QString::fromStdString(s.str());
}
static QJsonArray vec(const Vec3 &p) { return {hx(p[0]), hx(p[1]), hx(p[2])}; }
static QJsonObject frame(const Frame3 &f) {
    return {{"origin", vec(f.origin())}, {"x", vec(f.xDir())}, {"y", vec(f.yDir())}, {"z", vec(f.zDir())}};
}
static QJsonArray interval(Interval r) { return {hx(r.lo), hx(r.hi)}; }
static QJsonObject sourceJson(const CurvePtr<3> &p) {
    if (auto t = dynamic_cast<const TrimmedCurve<3> *>(p.get()))
        return {{"kind", "trim"}, {"range", interval(t->domain())}, {"basis", sourceJson(t->basis())}};
    if (auto t = dynamic_cast<const TransformedCurve *>(p.get())) {
        QJsonArray m;
        for (int i = 0; i < 3; ++i) {
            QJsonArray row;
            for (int j = 0; j < 3; ++j)
                row.append(hx(t->transform().matrix(i, j)));
            m.append(row);
        }
        return {{"kind", "transform"},
                {"matrix", m},
                {"translation", vec(t->transform().translationPart())},
                {"basis", sourceJson(t->basis())}};
    }
    auto h = dynamic_cast<const HelixCurve *>(p.get());
    need(h, "export requires original Helix");
    const auto &s = h->spec();
    double rise = h->risePerRadian(), slope = h->radialSlope();
    QString attribution = "original stored IEEE getters";
    return {{"kind", "helix"},
            {"frame", frame(s.frame)},
            {"radius", hx(s.radius)},
            {"pitch", hx(s.pitch)},
            {"turns", hx(s.turns)},
            {"taper", hx(s.taper)},
            {"startAngle", hx(s.startAngle)},
            {"spiral", s.spiral},
            {"leftHanded", s.leftHanded},
            {"sense", hx(s.leftHanded ? -1. : 1.)},
            {"stored_rise", hx(rise)},
            {"stored_slope", hx(slope)},
            {"coefficient_attribution", attribution}};
}
static QJsonObject splineJson(const CurveObject &o) {
    auto pieces = curveGeometry(o);
    need(pieces.size() == 1, "actual cache has one curve");
    auto b = dynamic_cast<const BSplineCurve<2> *>(pieces[0].curve.get());
    need(b, "actual cache is BSpline");
    QJsonArray p, k, w, qt;
    for (const auto &v : b->poles())
        p.append(QJsonArray{hx(v[0]), hx(v[1])});
    for (double v : b->knots())
        k.append(hx(v));
    for (double v : b->weights())
        w.append(hx(v));
    for (auto v : o.controlPoints)
        qt.append(QJsonArray{hx(v.x()), hx(v.y())});
    return {{"kind", "spline"},
            {"degree", b->degree()},
            {"poles", p},
            {"knots", k},
            {"weights", w},
            {"range", interval(pieces[0].range)},
            {"qt_control_points", qt},
            {"valid", o.numericallyValid},
            {"construction", o.construction}};
}
struct Case {
    QString label;
    CurvePtr<3> source;
    Interval range;
    SketchObject sketch;
    double budget = 1e-7;
    bool migrated = true, negative = false;
};
static SketchObject sketch(int plane, bool custom = false) {
    SketchObject s;
    s.plane = plane;
    s.customFrame = custom;
    if (custom) {
        double o[3] = {.3, -.5, 1.2}, n[3] = {1, 2, 3}, x[3] = {2, -1, .5};
        for (int i = 0; i < 3; ++i) {
            s.frame.origin[i] = o[i];
            s.frame.normal[i] = n[i];
            s.frame.xAxis[i] = x[i];
        }
    }
    return s;
}
static CurvePtr<3> wrapped(const HelixSpec &s, const Transform3 &t = Transform3()) {
    return std::make_shared<TransformedCurve>(std::make_shared<HelixCurve>(s), t);
}
static std::vector<Case> cases() {
    std::vector<Case> r;
    HelixSpec s;
    s.radius = 1.25;
    s.pitch = .9;
    s.turns = .35;
    auto add = [&](QString name, CurvePtr<3> p, SketchObject k, double budget = 1e-7, bool migrated = true) {
        r.push_back({name, p, p->domain(), k, budget, migrated, false});
    };
    add("wrapped-cylinder", wrapped(s), sketch(2));
    s.radius = .8;
    s.pitch = 1.2;
    s.turns = 1.25;
    auto pose = Transform3::translation({3, -4, 2}) * Transform3::rotation({}, Vec3(1, 2, 3), .7);
    add("wrapped-rigid-pose", wrapped(s, pose), sketch(1));
    s.radius = 1.3;
    s.pitch = .6;
    s.turns = 2.3;
    s.taper = -.12;
    s.leftHanded = true;
    s.startAngle = .37;
    s.frame = Frame3({.2, .4, -.3}, {1, 2, 4}, {2, -1, .2});
    CurvePtr<3> p = std::make_shared<TrimmedCurve<3>>(std::make_shared<HelixCurve>(s), .25, 4.6);
    p = std::make_shared<TransformedCurve>(p, pose);
    p = std::make_shared<TrimmedCurve<3>>(p, .6, 3.7);
    add("nested-trim-conical-left", p, sketch(2));
    s = HelixSpec{};
    s.spiral = true;
    s.radius = .6;
    s.pitch = .7;
    s.turns = .9;
    s.startAngle = -.8;
    add("spiral-custom-frame", wrapped(s, pose), sketch(0, true));
    double matrix[3][3] = {{1.4, .2, 0}, {-.3, .75, .1}, {.1, 0, .8}};
    s = HelixSpec{};
    s.radius = 1.1;
    s.pitch = .5;
    s.turns = .6;
    s.taper = .15;
    add("affine-shear", wrapped(s, Transform3::fromParts(matrix, {3, -4, 2})), sketch(0, true));
    s = HelixSpec{};
    s.radius = .9;
    s.pitch = .3;
    s.turns = 4;
    s.leftHanded = true;
    s.startAngle = .2;
    p = std::make_shared<TrimmedCurve<3>>(std::make_shared<HelixCurve>(s), 7, 21);
    p = std::make_shared<TransformedCurve>(p, pose);
    p = std::make_shared<TrimmedCurve<3>>(p, 11.2, 14.3);
    add("absolute-parameter-trim", p, sketch(2));
    s = HelixSpec{};
    s.radius = .7;
    s.pitch = .5;
    s.turns = 1.1;
    add("small-affine-scale", wrapped(s, Transform3::scaling({}, .001)), sketch(2));
    s.radius = 2.1;
    s.pitch = .45;
    s.turns = .55;
    add("tight-accuracy", wrapped(s, Transform3::translation({15, -3, 2})), sketch(0, true), 1e-8);
    s = HelixSpec{};
    s.radius = 1.25;
    s.pitch = .9;
    s.turns = .35;
    add("naked-legacy-compatibility", std::make_shared<HelixCurve>(s), sketch(2), 1e-7, false);
    s = HelixSpec{};
    s.radius = 1e-4;
    s.pitch = 1e-6;
    s.turns = 1e6;
    p = wrapped(s);
    add("alias-million-turns-YZ", p, sketch(2));
    r.back().negative = true;
    p = std::make_shared<TrimmedCurve<3>>(p, p->domain().lo, p->domain().hi);
    add("alias-million-turns-XZ-trim", p, sketch(1));
    r.back().negative = true;
    return r;
}
static QByteArray snapshot(const DocumentState &d) {
    QByteArray b;
    need(encodeDocument(d, b, false).isEmpty(), "document encoding failed");
    return b;
}
static bool same(const QVector<SketchEntity> &a, const QVector<SketchEntity> &b) {
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].index != b[i].index)
            return false;
    return true;
}
static double witness(const Case &c, const SketchObject &s) {
    double error = 0;
    auto f = sketchAxes(s);
    for (int i = 0; i <= 512; ++i) {
        double t = c.negative ? i * kTwoPi / 512 : c.range.lo + (c.range.hi - c.range.lo) * i / 512;
        auto v = f.toLocal(c.source->point(t));
        Vec2 q(v.x(), v.y());
        if (!s.curves.empty()) {
            auto a = curveGeometry(s.curves.back());
            error = std::max(error, norm(a[0].curve->point(t) - q));
        } else if (!s.segments.empty()) {
            auto seg = s.segments.back();
            Vec2 a(seg.first.x(), seg.first.y()), b(seg.second.x(), seg.second.y());
            auto d = b - a;
            double u = std::clamp(dot(q - a, d) / dot(d, d), 0., 1.);
            error = std::max(error, norm(q - a - u * d));
        }
    }
    return error;
}
static void runCase(Case c, const QString &directory) {
    PrecisionScope precision(c.budget);
    DocumentState doc;
    doc.sketches.append(c.sketch);
    auto before = snapshot(doc);
    QVector<SketchEntity> created;
    const auto sourceBefore = sourceJson(c.source);
    auto error = appendProjectedCurve(doc.sketches[0], c.source, c.range, true, true, &created);
    auto &s = doc.sketches[0];
    double observed = error.isEmpty() ? witness(c, s) : 0;
    QJsonArray actual, segments;
    for (const auto &o : s.curves)
        actual.append(splineJson(o));
    for (const auto &v : s.segments)
        segments.append(
            QJsonArray{QJsonArray{hx(v.first.x()), hx(v.first.y())}, QJsonArray{hx(v.second.x()), hx(v.second.y())}});
    QJsonObject json{{"label", c.label},
                     {"source", sourceBefore},
                     {"range", interval(c.range)},
                     {"projection", frame(sketchAxes(s))},
                     {"budget", hx(c.budget)},
                     {"migrated", c.migrated},
                     {"expected_rejection", c.negative},
                     {"rejected", !error.isEmpty()},
                     {"error", error},
                     {"actual", actual},
                     {"actual_segments", segments},
                     {"created_count", created.size()},
                     {"state_unchanged", snapshot(doc) == before},
                     {"sample_witness", hx(observed)},
                     {"sample_witness_is_certificate", false}};
    if (!directory.isEmpty()) {
        QFile f(directory + "/" + c.label + ".json");
        need(f.open(QIODevice::WriteOnly), "open export");
        f.write(QJsonDocument(json).toJson());
    }
    need(sourceBefore == sourceJson(c.source), "original source changed");
    if (c.negative) {
        need(!error.isEmpty(), "aliased curve accepted and published beyond budget");
        need(snapshot(doc) == before && created.empty(), "rejected operation mutated sketch/created");
        return;
    }
    need(error.isEmpty(), qPrintable(error));
    need(s.curves.size() == 1 && s.segments.empty() && created.size() == 1 && created[0].kind == 1 &&
             created[0].index == 0,
         "actual publication/provenance");
    need(s.curves[0].construction && !s.geometricConstraints.empty(), "construction/fixed flags lost");
    auto a = curveGeometry(s.curves[0]);
    need(a.size() == 1 && a[0].range.lo == c.range.lo && a[0].range.hi == c.range.hi,
         "original parameter range changed");
    need(observed <= c.budget, "actual Qt geometry exceeds original physical budget");
    if (c.migrated)
        need(detail::requireHelixProjection(c.source, c.range, sketchAxes(s), a[0].curve, c.budget) <= c.budget,
             "actual final certificate");
    std::cout << " witness=" << observed << " budget=" << c.budget << " poles=" << s.curves[0].controlPoints.size();
}
class CallbackCurve final : public Curve<3> {
  public:
    mutable int calls = 0;
    bool cancel;
    CallbackCurve(bool c) : cancel(c) {}
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return {0, 1}; }
    void evaluate(double t, int order, Vec3 *out) const override {
        if (++calls == 5) {
            if (cancel)
                currentCancellation->store(true);
            else
                throw std::domain_error("intentional callback failure");
        }
        out[0] = {t, t * t, .2 * t};
        if (order > 0)
            out[1] = {1, 2 * t, .2};
        for (int i = 2; i <= order; ++i)
            out[i] = i == 2 ? Vec3(0, 2, 0) : Vec3();
    }
};
static void atomic() {
    for (int mode = 0; mode < 5; ++mode) {
        DocumentState d;
        SketchObject s;
        s.name = "seed";
        s.segments.append({QPointF(2, 3), QPointF(5, 7)});
        s.constructionSegments.append(0);
        SketchConstraint fixed;
        fixed.type = ConstraintType::Fix;
        fixed.first.kind = 0;
        fixed.first.element = 0;
        fixed.positions = {QPointF(2, 3), QPointF(5, 7)};
        s.geometricConstraints.append(fixed);
        d.sketches.append(s);
        auto before = snapshot(d);
        QVector<SketchEntity> created{{0, 0}}, previous = created;
        auto token = std::make_shared<std::atomic<bool>>(mode == 0);
        CancellationScope scope(token);
        CurvePtr<3> p = mode == 0   ? cases()[0].source
                        : mode == 3 ? cases()[9].source
                                    : std::make_shared<CallbackCurve>(mode == 1);
        if (mode == 4) {
            p = cases()[0].source;
            for (int j = 1; j < 257; ++j)
                p = std::make_shared<TransformedCurve>(p, Transform3());
        }
        bool cancelled = false;
        QString error;
        try {
            error = appendProjectedCurve(d.sketches[0], p, p->domain(), false, true, &created);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
        need(mode < 2 ? cancelled : !error.isEmpty(), "failure/cancellation not propagated");
        need(snapshot(d) == before && same(created, previous), "failure/cancellation published partial sketch");
    }
}
static void contracts() {
    auto c = cases()[4];
    auto f = sketchAxes(c.sketch);
    auto data = detail::helixProjectionErrorBound(c.source, c.range, f);
    auto expected = data.value(.4);
    auto source = c.source;
    c.source.reset();
    source.reset();
    need(norm(data.value(.4) - expected) == 0, "snapshot lifetime");
    c = cases()[4];
    double deviation = -7;
    auto p = detail::projectHelixCertified(c.source, c.range, f, c.budget, &deviation);
    need(p && deviation >= 0 && deviation <= c.budget, "project/deviation contract");
    auto poles = p->poles();
    poles[poles.size() / 2][1] += .001;
    auto wrong = std::make_shared<BSplineCurve<2>>(p->degree(), p->knots(), poles, p->weights());
    bool rejected = false;
    try {
        detail::requireHelixProjection(c.source, c.range, f, wrong, c.budget);
    } catch (const std::exception &) {
        rejected = true;
    }
    need(rejected, "altered actual poles accepted");
    for (double budget : {0., -1., std::numeric_limits<double>::quiet_NaN()}) {
        deviation = -7;
        rejected = false;
        try {
            detail::projectHelixCertified(c.source, c.range, f, budget, &deviation);
        } catch (const std::exception &) {
            rejected = true;
        }
        need(rejected && deviation == -7, "invalid budget publishes output");
    }
    for (auto range : {Interval{c.range.lo, c.range.lo}, Interval{-1, c.range.hi},
                       Interval{c.range.lo, std::numeric_limits<double>::infinity()}}) {
        rejected = false;
        deviation = -7;
        try {
            detail::projectHelixCertified(c.source, range, f, c.budget, &deviation);
        } catch (const std::exception &) {
            rejected = true;
        }
        need(rejected && deviation == -7, "invalid range publishes output");
    }
    auto token = std::make_shared<std::atomic<bool>>(true);
    {
        CancellationScope scope(token);
        bool cancelled = false;
        deviation = -7;
        try {
            detail::projectHelixCertified(c.source, c.range, f, c.budget, &deviation);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
        need(cancelled && deviation == -7, "provider cancellation publishes deviation");
    }
    auto deep = cases()[0].source;
    for (int i = 1; i < 257; ++i)
        deep = std::make_shared<TransformedCurve>(deep, Transform3());
    rejected = false;
    try {
        detail::helixProjectionErrorBound(deep, {0, 1}, f);
    } catch (const std::exception &) {
        rejected = true;
    }
    need(rejected, "wrapper depth cap not enforced");
    auto unsupported = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 0));
    need(!detail::projectHelixCertified(unsupported, {0, 1}, f, 1e-7), "unsupported must be empty");
}
inline int run(const QString &directory = {}) {
    if (!directory.isEmpty())
        QDir().mkpath(directory);
    int pass = 0, fail = 0;
    auto run = [&](const QString &name, auto fn) {
        std::cout << name.toStdString();
        try {
            fn();
            ++pass;
            std::cout << " PASS\n";
        } catch (const std::exception &e) {
            ++fail;
            std::cout << " FAIL: " << e.what() << '\n';
        } catch (const OperationCancelled &) {
            ++fail;
            std::cout << " FAIL: unexpected cancellation\n";
        }
    };
    for (auto c : cases())
        run(c.label, [&] { runCase(c, directory); });
    run("public-failure-cancellation-atomicity", atomic);
    run("original-provider-contracts", contracts);
    std::cout << "groups=" << pass + fail << " pass=" << pass << " fail=" << fail << '\n';
    return fail ? 1 : 0;
}

} // namespace K10SketchProjectionTests
