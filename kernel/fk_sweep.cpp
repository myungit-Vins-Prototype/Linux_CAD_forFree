#include "fk_sweep.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_hermite.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_quadrature.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"

namespace ForgeCad::Kernel {
namespace {

// --- Getti del secondo ordine: valore, derivata prima e seconda in v. ---
struct J {
    double v = 0.0, d1 = 0.0, d2 = 0.0;
};
J operator+(const J &a, const J &b) { return {a.v + b.v, a.d1 + b.d1, a.d2 + b.d2}; }
J operator*(const J &a, const J &b) { return {a.v * b.v, a.d1 * b.v + a.v * b.d1, a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2}; }
J inverse(const J &a) {
    const double i = 1.0 / a.v;
    return {i, -a.d1 * i * i, (2.0 * a.d1 * a.d1 - a.v * a.d2) * i * i * i};
}
J squareRoot(const J &a) {
    const double s = std::sqrt(a.v);
    return {s, a.d1 / (2.0 * s), a.d2 / (2.0 * s) - a.d1 * a.d1 / (4.0 * s * s * s)};
}
J cosine(const J &a) { return {std::cos(a.v), -std::sin(a.v) * a.d1, -std::cos(a.v) * a.d1 * a.d1 - std::sin(a.v) * a.d2}; }
J sine(const J &a) { return {std::sin(a.v), std::cos(a.v) * a.d1, -std::sin(a.v) * a.d1 * a.d1 + std::cos(a.v) * a.d2}; }

struct JV {
    Vec3 v, d1, d2;
};
JV operator+(const JV &a, const JV &b) { return {a.v + b.v, a.d1 + b.d1, a.d2 + b.d2}; }
JV operator*(const J &s, const JV &x) { return {s.v * x.v, s.d1 * x.v + s.v * x.d1, s.d2 * x.v + 2.0 * s.d1 * x.d1 + s.v * x.d2}; }
JV operator*(double s, const JV &x) { return {s * x.v, s * x.d1, s * x.d2}; }
J dot(const JV &a, const JV &b) {
    return {dot(a.v, b.v), dot(a.d1, b.v) + dot(a.v, b.d1), dot(a.d2, b.v) + 2.0 * dot(a.d1, b.d1) + dot(a.v, b.d2)};
}
JV cross(const JV &a, const JV &b) {
    return {cross(a.v, b.v), cross(a.d1, b.v) + cross(a.v, b.d1), cross(a.d2, b.v) + 2.0 * cross(a.d1, b.d1) + cross(a.v, b.d2)};
}
JV unit(const JV &a) { return inverse(squareRoot(dot(a, a))) * a; }
JV constant(const Vec3 &v) { return {v, Vec3(), Vec3()}; }

const Curve<3> &basisOf(const Curve<3> &curve) {
    const Curve<3> *c = &curve;
    while (c->type() == CurveType::Trimmed) c = static_cast<const TrimmedCurve<3> *>(c)->basis().get();
    return *c;
}

Vec3 anyPerpendicular(const Vec3 &t) {
    const Vec3 axis = std::fabs(t.x()) < 0.6 ? Vec3(1, 0, 0) : (std::fabs(t.y()) < 0.6 ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
    return normalized(axis - dot(axis, t) * t);
}

enum class Motion { General, Translation, Rotation };

// Sistema mobile lungo il percorso.
class PathFrames {
public:
    PathFrames(const std::vector<PathSegment> &path, SweepOrientation mode, const Vec3 &hint) : path_(path), mode_(mode) {
        if (path.empty()) throw std::domain_error("sweep: percorso vuoto");
        Box box;
        for (const PathSegment &s : path) {
            if (!s.curve || !(s.range.length() > 0.0) || !s.range.isFinite()) throw std::domain_error("sweep: tratto del percorso non valido");
            for (int i = 0; i <= 16; ++i) box.add(s.curve->point(s.range.lo + s.range.length() * i / 16.0));
        }
        size_ = std::max(box.diagonal(), 1e-9);
        const double gapTolerance = 1e-6 * std::max(1.0, size_);
        const int K = int(path.size());
        for (int k = 0; k + 1 < K; ++k) {
            if (distance(endPoint(k), startPoint(k + 1)) > gapTolerance) throw std::domain_error("sweep: il percorso non e' continuo");
            if (!sameTangent(endTangent(k), startTangent(k + 1)))
                throw std::domain_error("sweep: il percorso ha un angolo vivo (i tratti devono essere tangenti: raccorda gli angoli)");
        }
        closed_ = distance(endPoint(K - 1), startPoint(0)) <= gapTolerance;
        if (closed_ && !sameTangent(endTangent(K - 1), startTangent(0)))
            throw std::domain_error("sweep: il percorso chiuso ha un angolo vivo nel punto di partenza");
        // Percorso piano? Piano per il punto iniziale, il piu' lontano e quello piu' fuori dalla loro retta.
        std::vector<Vec3> samples;
        for (const PathSegment &s : path)
            for (int i = 0; i <= 16; ++i) samples.push_back(s.curve->point(s.range.lo + s.range.length() * i / 16.0));
        const Vec3 p0 = samples.front();
        Vec3 far = p0;
        for (const Vec3 &p : samples)
            if (distance(p, p0) > distance(far, p0)) far = p;
        const Vec3 d = far - p0;
        Vec3 normal;
        double best = 0.0;
        for (const Vec3 &p : samples) {
            const Vec3 c = cross(d, p - p0);
            if (norm(c) > best) best = norm(c), normal = c;
        }
        const double planarTolerance = 1e-9 * std::max(1.0, size_);
        if (best <= planarTolerance * std::max(norm(d), 1e-300)) {
            planar_ = true;  // rettilineo
            normal_ = anyPerpendicular(startTangent(0));
            const Vec3 h = hint - dot(hint, startTangent(0)) * startTangent(0);
            if (norm(h) > 1e-6) normal_ = normalized(h);
        } else {
            normal_ = normalized(normal);
            planar_ = true;
            for (const PathSegment &s : path)
                for (int i = 0; i <= 32 && planar_; ++i)
                    planar_ = std::fabs(dot(s.curve->point(s.range.lo + s.range.length() * i / 32.0) - p0, normal_)) <= planarTolerance;
        }
        kinds_.resize(std::size_t(K));
        phi_.assign(std::size_t(K), 0.0);
        carriedN_.resize(std::size_t(K));
        carriedB_.resize(std::size_t(K));
        thetaCache_.resize(std::size_t(K));
        for (int k = 0; k < K; ++k) {
            const CurveType type = basisOf(*path[std::size_t(k)].curve).type();
            kinds_[std::size_t(k)] = type;
            if (mode == SweepOrientation::Frenet && type == CurveType::Line)
                throw std::domain_error("sweep: con Frenet il percorso non puo' avere tratti rettilinei (usa la torsione minima)");
        }
        // Sistema iniziale.
        P0_ = startPoint(0);
        T0_ = startTangent(0);
        if (mode == SweepOrientation::MinimalTwist && planar_) {
            B0_ = normal_ - dot(normal_, T0_) * T0_;
            B0_ = normalized(B0_);
            N0_ = cross(B0_, T0_);
        } else if (mode == SweepOrientation::Fixed || kinds_[0] == CurveType::Line) {
            const Vec3 h = hint - dot(hint, T0_) * T0_;
            B0_ = norm(h) > 1e-6 ? normalized(h) : anyPerpendicular(T0_);
            N0_ = cross(B0_, T0_);
        } else {
            JV P, T, N, B;
            frenet(0, path[0].range.lo, false, P, T, N, B);
            N0_ = N.v;
            B0_ = B.v;
        }
        carriedN_[0] = N0_;
        carriedB_[0] = B0_;
        // Torsione minima su un percorso non piano: ogni tratto riparte dal sistema con cui e' finito il precedente.
        if (mode == SweepOrientation::MinimalTwist && !planar_) {
            for (int k = 1; k < K; ++k) {
                JV P, T, N, B;
                evaluate(k - 1, path[std::size_t(k - 1)].range.hi, true, P, T, N, B);
                carriedN_[std::size_t(k)] = N.v;
                carriedB_[std::size_t(k)] = B.v;
                if (kinds_[std::size_t(k)] != CurveType::Line) {
                    JV P2, T2, N2, B2;
                    frenet(k, path[std::size_t(k)].range.lo, false, P2, T2, N2, B2);
                    phi_[std::size_t(k)] = std::atan2(::ForgeCad::Kernel::dot(N.v, B2.v), ::ForgeCad::Kernel::dot(N.v, N2.v));
                }
            }
        }
        if (mode == SweepOrientation::Frenet)
            for (int k = 1; k < K; ++k) checkContinuity(k - 1, k);
        // Percorso chiuso: lo scarto di rotazione alla fine si distribuisce lungo il percorso.
        if (closed_) {
            for (const PathSegment &s : path) total_ += s.range.length();
            JV P, T, N, B;
            evaluate(K - 1, path.back().range.hi, true, P, T, N, B);
            const double mismatch = std::atan2(::ForgeCad::Kernel::dot(N.v, B0_), ::ForgeCad::Kernel::dot(N.v, N0_));
            if (std::fabs(mismatch) > 1e-12) {
                if (mode == SweepOrientation::MinimalTwist) psiRate_ = -mismatch / total_;
                else if (std::fabs(mismatch) > 1e-7) throw std::domain_error("sweep: sul percorso chiuso il sistema di Frenet non torna uguale alla partenza (usa la torsione minima)");
            }
        }
        double accumulated = 0.0;
        for (const PathSegment &s : path) {
            offsets_.push_back(accumulated);
            accumulated += s.range.length();
        }
    }

    int count() const { return int(path_.size()); }
    bool closed() const { return closed_; }
    double size() const { return size_; }
    const PathSegment &segment(int k) const { return path_[std::size_t(k)]; }
    const Vec3 &startPoint() const { return P0_; }
    const Vec3 &T0() const { return T0_; }
    const Vec3 &N0() const { return N0_; }
    const Vec3 &B0() const { return B0_; }

    Motion motion(int k) const {
        if (psiRate_ != 0.0) return Motion::General;
        const CurveType type = kinds_[std::size_t(k)];
        if (type == CurveType::Line) return Motion::Translation;
        if (type == CurveType::Circle && mode_ != SweepOrientation::Fixed) return Motion::Rotation;
        return Motion::General;
    }

    // Posizione e sistema (getti) nel tratto k al parametro v.
    void evaluate(int k, double v, bool left, JV &P, JV &T, JV &N, JV &B) const {
        const PathSegment &s = path_[std::size_t(k)];
        Vec3 D[6];
        if (left) s.curve->evaluateLeft(v, 5, D);
        else s.curve->evaluate(v, 5, D);
        P = {D[0], D[1], D[2]};
        const JV D1{D[1], D[2], D[3]};
        J alpha{0.0, 0.0, 0.0};
        if (mode_ == SweepOrientation::Fixed) {
            T = constant(T0_), N = constant(N0_), B = constant(B0_);
            return;
        }
        if (mode_ == SweepOrientation::Frenet) {
            frenetFrom(D, T, N, B);
        } else if (planar_) {
            T = unit(D1);
            B = constant(B0_);
            N = cross(B, T);
        } else if (kinds_[std::size_t(k)] == CurveType::Line) {
            T = unit(D1);
            N = constant(carriedN_[std::size_t(k)]);
            B = constant(carriedB_[std::size_t(k)]);
        } else {
            frenetFrom(D, T, N, B);
            // theta' = -tau |P'|: il getto di tau |P'| dalle derivate fino alla quinta.
            const JV D2{D[2], D[3], D[4]}, D3{D[3], D[4], D[5]};
            const JV A = cross(D1, D2);
            const J rate = dot(A, D3) * inverse(dot(A, A)) * squareRoot(dot(D1, D1));
            alpha = {phi_[std::size_t(k)] + theta(k, v), -rate.v, -rate.d1};
        }
        if (psiRate_ != 0.0) alpha = alpha + J{psiRate_ * (offsets_[std::size_t(k)] + v - s.range.lo), psiRate_, 0.0};
        if (alpha.v != 0.0 || alpha.d1 != 0.0 || alpha.d2 != 0.0) {
            const J c = cosine(alpha), sn = sine(alpha);
            const JV rotatedN = c * N + sn * B;
            const JV rotatedB = J{-sn.v, -sn.d1, -sn.d2} * N + c * B;
            N = rotatedN;
            B = rotatedB;
        }
    }

    // Movimento rigido dalla partenza al giunto j (0..count): P + F F0^T (X - P0).
    Transform3 jointTransform(int joint) const {
        if (joint == 0 || (closed_ && joint == count())) return Transform3();
        JV P, T, N, B;
        evaluate(joint - 1, path_[std::size_t(joint - 1)].range.hi, true, P, T, N, B);
        return Transform3::fromFrame(Frame3(P.v, B.v, T.v)) * Transform3::fromFrame(Frame3(P0_, B0_, T0_)).inverted();
    }

private:
    Vec3 startPoint(int k) const { return path_[std::size_t(k)].curve->point(path_[std::size_t(k)].range.lo); }
    Vec3 endPoint(int k) const { return path_[std::size_t(k)].curve->point(path_[std::size_t(k)].range.hi); }
    Vec3 startTangent(int k) const {
        Vec3 d[2];
        path_[std::size_t(k)].curve->evaluate(path_[std::size_t(k)].range.lo, 1, d);
        return normalized(d[1]);
    }
    Vec3 endTangent(int k) const {
        Vec3 d[2];
        path_[std::size_t(k)].curve->evaluateLeft(path_[std::size_t(k)].range.hi, 1, d);
        return normalized(d[1]);
    }
    static bool sameTangent(const Vec3 &a, const Vec3 &b) { return norm(cross(a, b)) < 1e-8 && ::ForgeCad::Kernel::dot(a, b) > 0.0; }

    void frenet(int k, double v, bool left, JV &P, JV &T, JV &N, JV &B) const {
        Vec3 D[6];
        if (left) path_[std::size_t(k)].curve->evaluateLeft(v, 5, D);
        else path_[std::size_t(k)].curve->evaluate(v, 5, D);
        P = {D[0], D[1], D[2]};
        frenetFrom(D, T, N, B);
    }
    void frenetFrom(const Vec3 *D, JV &T, JV &N, JV &B) const {
        const JV D1{D[1], D[2], D[3]}, D2{D[2], D[3], D[4]};
        T = unit(D1);
        const JV binormal = cross(D1, D2);
        if (!(norm(binormal.v) > 1e-9 * squaredNorm(D[1]) * std::sqrt(squaredNorm(D[1])) / size_))
            throw std::domain_error("sweep: sistema di Frenet non definito (tratto rettilineo o flesso del percorso): usa la torsione minima");
        B = unit(binormal);
        N = cross(B, T);
    }
    void checkContinuity(int before, int after) const {
        JV P, T, N, B, P2, T2, N2, B2;
        evaluate(before, path_[std::size_t(before)].range.hi, true, P, T, N, B);
        evaluate(after, path_[std::size_t(after)].range.lo, false, P2, T2, N2, B2);
        if (distance(N.v, N2.v) > 1e-7)
            throw std::domain_error("sweep: con Frenet la normale del percorso salta tra due tratti (flesso): usa la torsione minima");
    }
    // theta(v) = -\int_lo^v tau |P'| dt, dalla quadratura adattiva (con una cache dei valori gia' calcolati).
    double theta(int k, double v) const {
        const PathSegment &s = path_[std::size_t(k)];
        std::map<double, double> &cache = thetaCache_[std::size_t(k)];
        if (cache.empty()) cache[s.range.lo] = 0.0;
        auto above = cache.lower_bound(v);
        if (above != cache.end() && above->first == v) return above->second;
        const auto below = above == cache.begin() ? above : std::prev(above);
        const auto from = (above != cache.end() && std::fabs(above->first - v) < std::fabs(below->first - v)) ? above : below;
        const auto rate = [&](double t) {
            Vec3 D[4];
            s.curve->evaluate(t, 3, D);
            const Vec3 A = ::ForgeCad::Kernel::cross(D[1], D[2]);
            return -::ForgeCad::Kernel::dot(A, D[3]) / squaredNorm(A) * norm(D[1]);
        };
        const double value = from->second + detail::adaptiveIntegral(rate, from->first, v, 1e-14 * std::max(1.0, std::fabs(v - from->first)), 0);
        cache[v] = value;
        return value;
    }

    std::vector<PathSegment> path_;
    SweepOrientation mode_;
    double size_ = 1.0;
    bool closed_ = false, planar_ = false;
    Vec3 normal_;
    Vec3 P0_, T0_, N0_, B0_;
    std::vector<CurveType> kinds_;
    std::vector<double> phi_;
    std::vector<Vec3> carriedN_, carriedB_;
    std::vector<double> offsets_;
    double psiRate_ = 0.0, total_ = 0.0;
    mutable std::vector<std::map<double, double>> thetaCache_;
};

// Curva del profilo come B-spline esatta (tratti di Bezier in forma standard).
BSplineCurve<3> profileNurbs(const Curve<3> &curve, const Interval &range) { return joinBezierPieces(standardBezierPieces(curve, range)); }

// Tratto del profilo nello spazio.
struct ProfilePiece {
    CurvePtr<3> curve;
    Interval range;
    int start = -1, end = -1;  // vertici del profilo
};

struct ProfileData {
    std::vector<Vec3> vertices;
    std::vector<ProfilePiece> pieces;
    std::vector<std::vector<int>> loops;  // indici dei tratti, nel verso del loop
    std::vector<int> region;              // regione di ogni loop (-1: catena aperta)
    std::vector<double> gaps;             // distanza tra la fine del tratto precedente e il vertice
};

void addLoop(ProfileData &data, const Frame3 &frame, const ProfileLoop &loop, bool closed, int region) {
    std::vector<ProfileSegment> segments = loop.segments;
    // Un loop di una sola curva chiusa si divide in due tratti.
    if (closed && segments.size() == 1) {
        const ProfileSegment s = segments.front();
        const double m = 0.5 * (s.range.lo + s.range.hi);
        segments = {{s.curve, {s.range.lo, m}}, {s.curve, {m, s.range.hi}}};
    }
    const int first = int(data.vertices.size());
    const int m = int(segments.size());
    for (int j = 0; j < m; ++j) {
        data.vertices.push_back(frame.toGlobal(Vec3(segments[std::size_t(j)].start().x(), segments[std::size_t(j)].start().y(), 0.0)));
        data.gaps.push_back(j > 0 ? distance(segments[std::size_t(j - 1)].end(), segments[std::size_t(j)].start())
                                  : (closed ? distance(segments.back().end(), segments.front().start()) : 0.0));
    }
    if (!closed) {
        data.vertices.push_back(frame.toGlobal(Vec3(segments.back().end().x(), segments.back().end().y(), 0.0)));
        data.gaps.push_back(0.0);
    }
    std::vector<int> indices;
    for (int j = 0; j < m; ++j) {
        ProfilePiece piece;
        piece.curve = embedCurve(segments[std::size_t(j)].curve, frame);
        piece.range = segments[std::size_t(j)].range;
        piece.start = first + j;
        piece.end = closed ? first + (j + 1) % m : first + j + 1;
        indices.push_back(int(data.pieces.size()));
        data.pieces.push_back(piece);
    }
    data.loops.push_back(indices);
    data.region.push_back(region);
}

Body sweep(const Frame3 &profileFrame, const ProfileData &profile, bool sheet, const std::vector<PathSegment> &path, SweepOrientation orientation) {
    const PathFrames frames(path, orientation, profileFrame.zDir());
    const int K = frames.count();
    const bool closedPath = frames.closed();
    const int joints = closedPath ? K : K + 1;
    const auto jointOf = [&](int j) { return closedPath ? j % K : j; };
    Box profileBox;
    for (const Vec3 &v : profile.vertices) profileBox.add(v);
    for (const ProfilePiece &p : profile.pieces)
        for (int i = 0; i <= 8; ++i) profileBox.add(p.curve->point(p.range.lo + p.range.length() * i / 8.0));
    const double scale = std::max({1.0, frames.size(), profileBox.diagonal()});
    const double tolerance = 1e-9 * scale;

    // Verso: il percorso esce dal piano del profilo verso +n (s = 1) o -n.
    const Vec3 n = profileFrame.zDir();
    const double along = dot(frames.T0(), n);
    if (!sheet && std::fabs(along) < 1e-6) throw std::domain_error("sweep: il profilo e' parallelo al percorso (il percorso deve uscire dal piano del profilo)");
    const double s = sheet ? 1.0 : (along > 0.0 ? 1.0 : -1.0);

    // Coordinate locali (nel sistema iniziale) e punto mobile.
    const Vec3 P0 = frames.startPoint(), T0 = frames.T0(), N0 = frames.N0(), B0 = frames.B0();
    const auto local = [&](const Vec3 &q) { return Vec3(dot(q - P0, T0), dot(q - P0, N0), dot(q - P0, B0)); };

    // Vertici e spigoli trasversali ai giunti.
    std::vector<Transform3> transforms;
    for (int j = 0; j < joints; ++j) transforms.push_back(frames.jointTransform(j));
    const int V = int(profile.vertices.size());
    std::vector<Vec3> vertices;
    for (int j = 0; j < joints; ++j)
        for (int v = 0; v < V; ++v) vertices.push_back(transforms[std::size_t(j)].applyToPoint(profile.vertices[std::size_t(v)]));
    const auto vertexIndex = [&](int joint, int v) { return jointOf(joint) * V + v; };
    std::vector<Body::BuildEdge> edges;
    const int P = int(profile.pieces.size());
    std::vector<int> sectionEdge(static_cast<std::size_t>(joints * P));
    for (int j = 0; j < joints; ++j)
        for (int p = 0; p < P; ++p) {
            const ProfilePiece &piece = profile.pieces[std::size_t(p)];
            Body::BuildEdge edge;
            edge.start = vertexIndex(j, piece.start);
            edge.end = vertexIndex(j, piece.end);
            if (j == 0) {
                edge.curve = piece.curve;
                edge.range = piece.range;
            } else {
                double k = 1.0;
                edge.curve = transformCurve(piece.curve, transforms[std::size_t(j)], &k);
                edge.range = {k * piece.range.lo, k * piece.range.hi};
            }
            const double gap = std::max(profile.gaps[std::size_t(piece.start)], profile.gaps[std::size_t(piece.end)]);
            if (gap > kLinearResolution) edge.tolerance = 1.01 * gap;
            sectionEdge[std::size_t(j * P + p)] = int(edges.size());
            edges.push_back(edge);
        }

    // Tratti del percorso: spigoli longitudinali e facce laterali.
    std::vector<Body::BuildFace> faces;
    for (int k = 0; k < K; ++k) {
        const PathSegment &segment = frames.segment(k);
        const Interval range = segment.range;
        const Motion motion = frames.motion(k);
        std::vector<int> longitudinal(static_cast<std::size_t>(V));
        std::vector<SurfacePtr> surfaces(static_cast<std::size_t>(P));
        // Punto mobile: getto di X(v) per il punto q del profilo (posizione iniziale).
        const auto moving = [&](double v, bool left, const Vec3 &w, JV *frameOut = nullptr) {
            JV Pj, T, N, B;
            frames.evaluate(k, v, left, Pj, T, N, B);
            if (frameOut) frameOut[0] = T, frameOut[1] = N, frameOut[2] = B;
            return Pj + w.x() * T + w.y() * N + w.z() * B;
        };
        if (motion == Motion::Translation) {
            const Vec3 a = segment.curve->point(range.lo), b = segment.curve->point(range.hi);
            const double length = distance(a, b);
            const Vec3 d = (b - a) / length;
            for (int v = 0; v < V; ++v) {
                Body::BuildEdge edge;
                edge.start = vertexIndex(k, v);
                edge.end = vertexIndex(k + 1, v);
                edge.curve = std::make_shared<Line<3>>(vertices[std::size_t(vertexIndex(k, v))], d);
                edge.range = {0.0, length};
                longitudinal[std::size_t(v)] = int(edges.size());
                edges.push_back(edge);
            }
            for (int p = 0; p < P; ++p) {
                const Body::BuildEdge &section = edges[std::size_t(sectionEdge[std::size_t(k * P + p)])];
                const Curve<3> &basis = basisOf(*section.curve);
                if (basis.type() == CurveType::Line) {
                    const Vec3 x = static_cast<const Line<3> &>(basis).direction();
                    surfaces[std::size_t(p)] = std::make_shared<Plane>(Frame3(section.curve->point(section.range.lo), normalized(cross(x, d)), x));
                } else if (basis.type() == CurveType::Circle
                           && norm(cross(normalized(cross(static_cast<const Circle<3> &>(basis).xAxis(), static_cast<const Circle<3> &>(basis).yAxis())), d)) < 1e-12) {
                    const auto &c = static_cast<const Circle<3> &>(basis);
                    surfaces[std::size_t(p)] = std::make_shared<CylindricalSurface>(Frame3(c.center(), d, c.xAxis()), c.radius());
                } else {
                    surfaces[std::size_t(p)] = std::make_shared<ExtrusionSurface>(section.curve, d);
                }
            }
        } else if (motion == Motion::Rotation) {
            const auto &circle = static_cast<const Circle<3> &>(basisOf(*segment.curve));
            const Vec3 axis = normalized(cross(circle.xAxis(), circle.yAxis())), center = circle.center();
            const double angle = range.length();
            const auto radial = [&](const Vec3 &q, Vec3 &foot) {
                foot = center + dot(q - center, axis) * axis;
                return distance(q, foot);
            };
            for (int v = 0; v < V; ++v) {
                const Vec3 &q = vertices[std::size_t(vertexIndex(k, v))];
                Vec3 foot;
                const double rho = radial(q, foot);
                if (!(rho > 1e-7 * scale)) throw std::domain_error("sweep: il profilo tocca il centro di curvatura di un arco del percorso");
                const Vec3 x = (q - foot) / rho;
                Body::BuildEdge edge;
                edge.start = vertexIndex(k, v);
                edge.end = vertexIndex(k + 1, v);
                edge.curve = std::make_shared<Circle<3>>(foot, x, cross(axis, x), rho);
                edge.range = {0.0, angle};
                longitudinal[std::size_t(v)] = int(edges.size());
                edges.push_back(edge);
            }
            for (int p = 0; p < P; ++p) {
                const Body::BuildEdge &section = edges[std::size_t(sectionEdge[std::size_t(k * P + p)])];
                // Il profilo resta dalla parte del percorso rispetto all'asse dell'arco.
                {
                    const Vec3 pathPoint = segment.curve->point(range.lo);
                    Vec3 foot;
                    radial(pathPoint, foot);
                    const Vec3 outward = normalized(pathPoint - foot);
                    for (int i = 0; i <= 32; ++i)
                        if (!(dot(section.curve->point(section.range.lo + section.range.length() * i / 32.0) - center, outward) > 1e-7 * scale))
                            throw std::domain_error("sweep: il raggio di un arco del percorso e' minore della distanza del profilo");
                }
                const Curve<3> &basis = basisOf(*section.curve);
                const Vec3 a = section.curve->point(section.range.lo), b = section.curve->point(section.range.hi);
                Vec3 footA;
                const double rhoA = radial(a, footA);
                const Vec3 xRef = (a - footA) / rhoA;
                const double zA = dot(a - center, axis);
                SurfacePtr surface;
                if (basis.type() == CurveType::Line) {
                    const Vec3 dir = b - a, c = cross(dir, axis);
                    const bool coplanar = norm(c) <= 1e-12 * norm(dir) || std::fabs(dot(a - center, c / norm(c))) <= 1e-9 * scale;
                    if (coplanar) {
                        Vec3 footB;
                        const double rhoB = radial(b, footB), zB = dot(b - center, axis);
                        const Vec2 d2(rhoB - rhoA, zB - zA);
                        if (std::fabs(d2.x()) <= 1e-12 * norm(d2)) surface = std::make_shared<CylindricalSurface>(Frame3(center, axis, xRef), rhoA);
                        else if (std::fabs(d2.y()) <= 1e-12 * norm(d2)) surface = std::make_shared<Plane>(Frame3(center + zA * axis, axis, xRef));
                        else {
                            const Vec2 g = d2.y() > 0.0 ? d2 : -d2;
                            surface = std::make_shared<ConicalSurface>(Frame3(center + zA * axis, axis, xRef), std::atan2(g.x(), g.y()), rhoA);
                        }
                    }
                } else if (basis.type() == CurveType::Circle) {
                    const auto &c = static_cast<const Circle<3> &>(basis);
                    const Vec3 normal = normalized(cross(c.xAxis(), c.yAxis()));
                    if (std::fabs(dot(normal, axis)) <= 1e-12 && std::fabs(dot(c.center() - center, normal)) <= 1e-9 * scale) {
                        Vec3 foot;
                        const double rhoC = radial(c.center(), foot);
                        const Vec3 x = rhoC > 1e-9 * scale ? (c.center() - foot) / rhoC : xRef;
                        const Frame3 f(center + dot(c.center() - center, axis) * axis, axis, x);
                        if (rhoC <= 1e-9 * scale) surface = std::make_shared<SphericalSurface>(f, c.radius());
                        else if (c.radius() < rhoC) surface = std::make_shared<ToroidalSurface>(f, rhoC, c.radius());
                    }
                }
                if (!surface) surface = std::make_shared<RevolutionSurface>(section.curve, center, axis);
                surfaces[std::size_t(p)] = surface;
            }
        } else {
            // Righe: i vertici, poi i poli interni di ogni curva del profilo.
            std::vector<Vec3> rows;
            for (int v = 0; v < V; ++v) rows.push_back(local(profile.vertices[std::size_t(v)]));
            std::vector<BSplineCurve<3>> nurbs;
            std::vector<std::vector<int>> rowOf(static_cast<std::size_t>(P));
            for (int p = 0; p < P; ++p) {
                const ProfilePiece &piece = profile.pieces[std::size_t(p)];
                nurbs.push_back(profileNurbs(*piece.curve, piece.range));
                const BSplineCurve<3> &c = nurbs.back();
                for (int i = 0; i < c.poleCount(); ++i) {
                    if (i == 0) rowOf[std::size_t(p)].push_back(piece.start);
                    else if (i + 1 == c.poleCount()) rowOf[std::size_t(p)].push_back(piece.end);
                    else {
                        rowOf[std::size_t(p)].push_back(int(rows.size()));
                        rows.push_back(local(c.poles()[std::size_t(i)]));
                    }
                }
            }
            const int R = int(rows.size());
            const detail::RowSampler sampler = [&](double v, bool left, Vec3 *out) {
                JV frame[3];
                for (int r = 0; r < R; ++r) {
                    const JV X = moving(v, left, rows[std::size_t(r)], r == 0 ? frame : nullptr);
                    // Le sezioni non devono tornare indietro lungo il percorso.
                    if (!(dot(X.d1, frame[0].v) > 0.0))
                        throw std::domain_error("sweep: il raggio di curvatura del percorso e' minore della distanza del profilo (le sezioni si incrociano)");
                    out[3 * r] = X.v, out[3 * r + 1] = X.d1, out[3 * r + 2] = X.d2;
                }
            };
            const detail::RowSpline fit = detail::fitQuinticRows(sampler, R, segment.curve->breakpoints(range), tolerance, 0.25 * range.length());
            for (int v = 0; v < V; ++v) {
                Body::BuildEdge edge;
                edge.start = vertexIndex(k, v);
                edge.end = vertexIndex(k + 1, v);
                edge.curve = std::make_shared<BSplineCurve<3>>(detail::rowCurve(fit, v));
                edge.range = range;
                longitudinal[std::size_t(v)] = int(edges.size());
                edges.push_back(edge);
            }
            const int vCount = int(fit.poles.front().size());
            for (int p = 0; p < P; ++p) {
                const BSplineCurve<3> &c = nurbs[std::size_t(p)];
                const int uCount = c.poleCount();
                std::vector<Vec3> poles(std::size_t(uCount * vCount));
                std::vector<double> weights;
                if (c.isRational()) weights.resize(poles.size());
                for (int i = 0; i < uCount; ++i)
                    for (int j = 0; j < vCount; ++j) {
                        poles[std::size_t(i * vCount + j)] = fit.poles[std::size_t(rowOf[std::size_t(p)][std::size_t(i)])][std::size_t(j)];
                        if (c.isRational()) weights[std::size_t(i * vCount + j)] = c.weight(i);
                    }
                surfaces[std::size_t(p)] = std::make_shared<BSplineSurface>(c.degree(), 5, c.knots(), fit.knots, uCount, vCount, poles, weights);
                // Pezze piane (un lato del profilo nel piano di un percorso piano): il piano esatto.
                if (SurfacePtr plane = planarEquivalent(*surfaces[std::size_t(p)])) surfaces[std::size_t(p)] = plane;
            }
        }
        // Facce laterali: verso dalla normale uscente s (X_u x X_v) a meta' della faccia.
        for (int p = 0; p < P; ++p) {
            const ProfilePiece &piece = profile.pieces[std::size_t(p)];
            const double tm = 0.5 * (piece.range.lo + piece.range.hi), vm = 0.5 * (range.lo + range.hi);
            Vec3 dq[2];
            piece.curve->evaluate(tm, 1, dq);
            JV frame[3];
            const JV X = moving(vm, false, local(dq[0]), frame);
            const Vec3 w = Vec3(dot(dq[1], T0), dot(dq[1], N0), dot(dq[1], B0));
            const Vec3 Xu = w.x() * frame[0].v + w.y() * frame[1].v + w.z() * frame[2].v;
            const Vec3 outward = s * cross(Xu, X.d1);
            const SurfaceProjection onSurface = projectPoint(*surfaces[std::size_t(p)], X.v);
            if (distance(onSurface.point, X.v) > 1e-6 * scale) throw std::logic_error("sweep: superficie laterale fuori dalla sezione");
            Body::BuildFace face;
            face.surface = surfaces[std::size_t(p)];
            face.sense = dot(surfaces[std::size_t(p)]->normal(onSurface.u, onSurface.v), outward) > 0.0;
            const int lower = sectionEdge[std::size_t(k * P + p)], upper = sectionEdge[std::size_t(jointOf(k + 1) * P + p)];
            const int first = longitudinal[std::size_t(piece.start)], last = longitudinal[std::size_t(piece.end)];
            if (s > 0.0) face.loops.push_back({{lower, true, nullptr, 0.0}, {last, true, nullptr, 0.0}, {upper, false, nullptr, 0.0}, {first, false, nullptr, 0.0}});
            else face.loops.push_back({{first, true, nullptr, 0.0}, {upper, true, nullptr, 0.0}, {last, false, nullptr, 0.0}, {lower, false, nullptr, 0.0}});
            faces.push_back(std::move(face));
        }
    }

    // Facce di testa (solidi su percorsi aperti): piane, con i loop del profilo.
    if (!sheet && !closedPath) {
        const Transform3 &end = transforms.back();
        const Frame3 endFrame(end.applyToPoint(profileFrame.origin()), end.applyToVector(n), end.applyToVector(profileFrame.xDir()));
        for (int cap = 0; cap < 2; ++cap) {
            const int joint = cap == 0 ? 0 : K;
            // Uscente: -s n all'inizio, +s n (spostata) alla fine; i loop del profilo girano attorno a n.
            const bool asProfile = cap == 0 ? s < 0.0 : s > 0.0;
            std::map<int, Body::BuildFace> byRegion;
            for (std::size_t l = 0; l < profile.loops.size(); ++l) {
                Body::BuildFace &face = byRegion[profile.region[l]];
                face.surface = std::make_shared<Plane>(cap == 0 ? profileFrame : endFrame);
                face.sense = asProfile;
                std::vector<Body::BuildFin> fins;
                const std::vector<int> &loop = profile.loops[l];
                if (asProfile)
                    for (int p : loop) fins.push_back({sectionEdge[std::size_t(joint * P + p)], true, nullptr, 0.0});
                else
                    for (auto it = loop.rbegin(); it != loop.rend(); ++it) fins.push_back({sectionEdge[std::size_t(joint * P + *it)], false, nullptr, 0.0});
                face.loops.push_back(std::move(fins));
            }
            for (auto &entry : byRegion) faces.push_back(std::move(entry.second));
        }
    }

    Body body = sheet ? Body::buildSheet(vertices, edges, faces) : Body::build(vertices, edges, faces);
    for (int j = 0; j < joints; ++j)
        for (int v = 0; v < V; ++v)
            if (profile.gaps[std::size_t(v)] > kLinearResolution) body.vertex(VertexId(vertexIndex(j, v))).tolerance = 1.01 * profile.gaps[std::size_t(v)];
    computePCurves(body);
    const std::vector<CheckIssue> issues = checkBody(body);
    if (!issues.empty()) throw std::domain_error("sweep: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return body;
}

}

Body sweepRegions(const Frame3 &profileFrame, const std::vector<ProfileRegion> &regions, const std::vector<PathSegment> &path, SweepOrientation orientation) {
    if (regions.empty()) throw std::domain_error("sweep: profilo senza regioni chiuse");
    ProfileData data;
    for (std::size_t r = 0; r < regions.size(); ++r) {
        const ProfileRegion &region = regions[r];
        addLoop(data, profileFrame, signedArea(region.outer) > 0.0 ? region.outer : reversed(region.outer), true, int(r));
        for (const ProfileLoop &hole : region.holes) addLoop(data, profileFrame, signedArea(hole) < 0.0 ? hole : reversed(hole), true, int(r));
    }
    return sweep(profileFrame, data, false, path, orientation);
}

Body sweepChains(const Frame3 &profileFrame, const std::vector<ProfileLoop> &chains, const std::vector<PathSegment> &path, SweepOrientation orientation) {
    if (chains.empty()) throw std::domain_error("sweep: profilo vuoto");
    ProfileData data;
    for (const ProfileLoop &chain : chains) addLoop(data, profileFrame, chain, false, -1);
    return sweep(profileFrame, data, true, path, orientation);
}

}
