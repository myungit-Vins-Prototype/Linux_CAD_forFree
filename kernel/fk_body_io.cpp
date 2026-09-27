#include "fk_body_io.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_helix.h"

namespace ForgeCad::Kernel {
namespace {

constexpr char kMagic[4] = {'F', 'K', 'B', 'D'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kByteOrder = 0x01020304;  // letto come e' scritto: stesso ordine dei byte

enum Record : std::uint8_t { Curve2 = 1, Curve3 = 2, SurfaceRecord = 3, EndOfGeometry = 0 };
enum CurveKind : std::uint8_t { LineKind, CircleKind, EllipseKind, BSplineKind, TrimmedKind, TransformedKind, HelixKind };

class Writer {
public:
    void raw(const void *data, std::size_t size) { out_.append(static_cast<const char *>(data), size); }
    void u8(std::uint8_t v) { raw(&v, 1); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void f64(double v) { raw(&v, 8); }
    void flag(bool v) { u8(v ? 1 : 0); }
    template <int N>
    void vec(const Vec<N> &v) {
        for (int i = 0; i < N; ++i) f64(v[i]);
    }
    void doubles(const std::vector<double> &values) {
        u32(std::uint32_t(values.size()));
        for (double v : values) f64(v);
    }
    void frame(const Frame3 &f) {
        vec(f.origin());
        vec(f.xDir());
        vec(f.yDir());
        vec(f.zDir());
    }
    std::string take() { return std::move(out_); }

private:
    std::string out_;
};

class Reader {
public:
    explicit Reader(const std::string &data) : data_(data) {}
    void raw(void *target, std::size_t size) {
        if (size > data_.size() - pos_) throw std::invalid_argument("readBodyBinary: dati troncati");
        std::memcpy(target, data_.data() + pos_, size);
        pos_ += size;
    }
    std::uint8_t u8() {
        std::uint8_t v;
        raw(&v, 1);
        return v;
    }
    std::int32_t i32() {
        std::int32_t v;
        raw(&v, 4);
        return v;
    }
    std::uint32_t u32() {
        std::uint32_t v;
        raw(&v, 4);
        return v;
    }
    double f64() {
        double v;
        raw(&v, 8);
        return v;
    }
    bool flag() { return u8() != 0; }
    template <int N>
    Vec<N> vec() {
        Vec<N> v;
        for (int i = 0; i < N; ++i) v[i] = f64();
        return v;
    }
    // Numero di elementi di `size` byte ciascuno: non oltre i dati rimasti.
    std::size_t count(std::size_t size) {
        const std::uint32_t n = u32();
        if (std::size_t(n) * size > data_.size() - pos_) throw std::invalid_argument("readBodyBinary: lunghezza non valida");
        return n;
    }
    std::vector<double> doubles() {
        std::vector<double> values(count(8));
        for (double &v : values) v = f64();
        return values;
    }
    Frame3 frame() {
        const Vec3 origin = vec<3>(), x = vec<3>(), y = vec<3>(), z = vec<3>();
        return Frame3::fromAxes(origin, x, y, z);
    }
    bool atEnd() const { return pos_ == data_.size(); }

private:
    const std::string &data_;
    std::size_t pos_ = 0;
};

// Geometria in ordine di dipendenza: una curva o superficie compare dopo le
// curve su cui e' costruita; i riferimenti sono indici nelle tre tabelle.
class GeometryWriter {
public:
    explicit GeometryWriter(Writer &out) : out_(out) {}

    template <int N>
    int curve(const CurvePtr<N> &curve) {
        if (!curve) return -1;
        auto &table = curves<N>();
        const auto found = table.find(curve.get());
        if (found != table.end()) return found->second;
        Writer record;
        writeCurve<N>(*curve, record);
        out_.u8(N == 2 ? Curve2 : Curve3);
        const std::string bytes = record.take();
        out_.raw(bytes.data(), bytes.size());
        const int index = int(table.size());
        table[curve.get()] = index;
        return index;
    }

    int surface(const SurfacePtr &surface) {
        if (!surface) return -1;
        const auto found = surfaces_.find(surface.get());
        if (found != surfaces_.end()) return found->second;
        Writer record;
        const Surface &s = *surface;
        record.u8(std::uint8_t(s.type()));
        switch (s.type()) {
        case SurfaceType::Plane: record.frame(static_cast<const Plane &>(s).frame()); break;
        case SurfaceType::Cylinder: {
            const auto &c = static_cast<const CylindricalSurface &>(s);
            record.frame(c.frame());
            record.f64(c.radius());
            break;
        }
        case SurfaceType::Cone: {
            const auto &c = static_cast<const ConicalSurface &>(s);
            record.frame(c.frame());
            record.f64(c.semiAngle());
            record.f64(c.referenceRadius());
            break;
        }
        case SurfaceType::Sphere: {
            const auto &c = static_cast<const SphericalSurface &>(s);
            record.frame(c.frame());
            record.f64(c.radius());
            break;
        }
        case SurfaceType::Torus: {
            const auto &c = static_cast<const ToroidalSurface &>(s);
            record.frame(c.frame());
            record.f64(c.majorRadius());
            record.f64(c.minorRadius());
            break;
        }
        case SurfaceType::Extrusion: {
            const auto &e = static_cast<const ExtrusionSurface &>(s);
            record.i32(curve<3>(e.curve()));
            record.vec(e.direction());
            break;
        }
        case SurfaceType::Revolution: {
            const auto &r = static_cast<const RevolutionSurface &>(s);
            record.i32(curve<3>(r.meridian()));
            record.vec(r.axisPoint());
            record.vec(r.axisDirection());
            break;
        }
        case SurfaceType::BSpline: {
            const auto &b = static_cast<const BSplineSurface &>(s);
            record.i32(b.uDegree());
            record.i32(b.vDegree());
            record.doubles(b.uKnots());
            record.doubles(b.vKnots());
            record.i32(b.uPoleCount());
            record.i32(b.vPoleCount());
            for (int i = 0; i < b.uPoleCount(); ++i)
                for (int j = 0; j < b.vPoleCount(); ++j) record.vec(b.pole(i, j));
            record.flag(b.isRational());
            if (b.isRational())
                for (int i = 0; i < b.uPoleCount(); ++i)
                    for (int j = 0; j < b.vPoleCount(); ++j) record.f64(b.weight(i, j));
            break;
        }
        default: throw std::domain_error("writeBodyBinary: tipo di superficie non salvabile");
        }
        out_.u8(SurfaceRecord);
        const std::string bytes = record.take();
        out_.raw(bytes.data(), bytes.size());
        const int index = int(surfaces_.size());
        surfaces_[surface.get()] = index;
        return index;
    }

private:
    template <int N>
    std::map<const Curve<N> *, int> &curves() {
        if constexpr (N == 2) return curves2_;
        else return curves3_;
    }

    // Le curve base si scrivono prima (in out_), il record in `record`.
    template <int N>
    void writeCurve(const Curve<N> &c, Writer &record) {
        switch (c.type()) {
        case CurveType::Line: {
            const auto &l = static_cast<const Line<N> &>(c);
            record.u8(LineKind);
            record.vec(l.origin());
            record.vec(l.direction());
            return;
        }
        case CurveType::Circle: {
            const auto &k = static_cast<const Circle<N> &>(c);
            record.u8(CircleKind);
            record.vec(k.center());
            record.vec(k.xAxis());
            record.vec(k.yAxis());
            record.f64(k.radius());
            return;
        }
        case CurveType::Ellipse: {
            const auto &e = static_cast<const Ellipse<N> &>(c);
            record.u8(EllipseKind);
            record.vec(e.center());
            record.vec(e.xAxis());
            record.vec(e.yAxis());
            record.f64(e.xRadius());
            record.f64(e.yRadius());
            return;
        }
        case CurveType::BSpline: {
            const auto &b = static_cast<const BSplineCurve<N> &>(c);
            record.u8(BSplineKind);
            record.i32(b.degree());
            record.doubles(b.knots());
            record.u32(std::uint32_t(b.poles().size()));
            for (const Vec<N> &p : b.poles()) record.vec(p);
            record.doubles(b.weights());
            return;
        }
        case CurveType::Trimmed: {
            const auto &t = static_cast<const TrimmedCurve<N> &>(c);
            const int basis = curve<N>(t.basis());
            record.u8(TrimmedKind);
            record.i32(basis);
            record.f64(t.domain().lo);
            record.f64(t.domain().hi);
            return;
        }
        default: break;
        }
        if constexpr (N == 3) {
            if (c.type() == CurveType::Transformed) {
                const auto &t = static_cast<const TransformedCurve &>(c);
                const int basis = curve<3>(t.basis());
                record.u8(TransformedKind);
                record.i32(basis);
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) record.f64(t.transform().matrix(i, j));
                record.vec(t.transform().translationPart());
                return;
            }
            if (const auto *helix = dynamic_cast<const HelixCurve *>(&c)) {
                const HelixSpec &spec = helix->spec();
                record.u8(HelixKind);
                record.frame(spec.frame);
                for (double v : {spec.radius, spec.pitch, spec.turns, spec.taper, spec.startAngle}) record.f64(v);
                record.flag(spec.spiral);
                record.flag(spec.leftHanded);
                return;
            }
        }
        throw std::domain_error("writeBodyBinary: tipo di curva non salvabile");
    }

    Writer &out_;
    std::map<const Curve<2> *, int> curves2_;
    std::map<const Curve<3> *, int> curves3_;
    std::map<const Surface *, int> surfaces_;
};

class GeometryReader {
public:
    explicit GeometryReader(Reader &in) : in_(in) {}

    void readAll() {
        for (;;) {
            const std::uint8_t record = in_.u8();
            if (record == EndOfGeometry) return;
            if (record == Curve2) curves2_.push_back(readCurve<2>());
            else if (record == Curve3) curves3_.push_back(readCurve<3>());
            else if (record == SurfaceRecord) surfaces_.push_back(readSurface());
            else throw std::invalid_argument("readBodyBinary: record di geometria sconosciuto");
        }
    }

    template <int N>
    CurvePtr<N> curve(std::int32_t index) const {
        const auto &table = curves<N>();
        if (index == -1) return nullptr;
        if (index < 0 || std::size_t(index) >= table.size()) throw std::invalid_argument("readBodyBinary: riferimento a una curva non valido");
        return table[std::size_t(index)];
    }
    SurfacePtr surface(std::int32_t index) const {
        if (index == -1) return nullptr;
        if (index < 0 || std::size_t(index) >= surfaces_.size()) throw std::invalid_argument("readBodyBinary: riferimento a una superficie non valido");
        return surfaces_[std::size_t(index)];
    }

private:
    template <int N>
    const std::vector<CurvePtr<N>> &curves() const {
        if constexpr (N == 2) return curves2_;
        else return curves3_;
    }

    template <int N>
    CurvePtr<N> readCurve() {
        const std::uint8_t kind = in_.u8();
        switch (kind) {
        case LineKind: {
            const Vec<N> origin = in_.vec<N>(), direction = in_.vec<N>();
            return std::make_shared<Line<N>>(origin, direction);
        }
        case CircleKind: {
            const Vec<N> center = in_.vec<N>(), x = in_.vec<N>(), y = in_.vec<N>();
            return std::make_shared<Circle<N>>(center, x, y, in_.f64());
        }
        case EllipseKind: {
            const Vec<N> center = in_.vec<N>(), x = in_.vec<N>(), y = in_.vec<N>();
            const double a = in_.f64(), b = in_.f64();
            return std::make_shared<Ellipse<N>>(center, x, y, a, b);
        }
        case BSplineKind: {
            const int degree = in_.i32();
            std::vector<double> knots = in_.doubles();
            std::vector<Vec<N>> poles(in_.count(8 * N));
            for (Vec<N> &p : poles) p = in_.vec<N>();
            std::vector<double> weights = in_.doubles();
            return std::make_shared<BSplineCurve<N>>(degree, std::move(knots), std::move(poles), std::move(weights));
        }
        case TrimmedKind: {
            const CurvePtr<N> basis = curve<N>(in_.i32());
            const double first = in_.f64(), last = in_.f64();
            return std::make_shared<TrimmedCurve<N>>(basis, first, last);
        }
        default: break;
        }
        if constexpr (N == 3) {
            if (kind == TransformedKind) {
                const CurvePtr<3> basis = curve<3>(in_.i32());
                double m[3][3];
                for (auto &row : m)
                    for (double &v : row) v = in_.f64();
                const Vec3 t = in_.vec<3>();
                return std::make_shared<TransformedCurve>(basis, Transform3::fromParts(m, t));
            }
            if (kind == HelixKind) {
                HelixSpec spec;
                spec.frame = in_.frame();
                for (double *v : {&spec.radius, &spec.pitch, &spec.turns, &spec.taper, &spec.startAngle}) *v = in_.f64();
                spec.spiral = in_.flag();
                spec.leftHanded = in_.flag();
                return std::make_shared<HelixCurve>(spec);
            }
        }
        throw std::invalid_argument("readBodyBinary: tipo di curva sconosciuto");
    }

    SurfacePtr readSurface() {
        const std::uint8_t type = in_.u8();
        switch (SurfaceType(type)) {
        case SurfaceType::Plane: return std::make_shared<Plane>(in_.frame());
        case SurfaceType::Cylinder: {
            const Frame3 frame = in_.frame();
            return std::make_shared<CylindricalSurface>(frame, in_.f64());
        }
        case SurfaceType::Cone: {
            const Frame3 frame = in_.frame();
            const double semiAngle = in_.f64(), radius = in_.f64();
            return std::make_shared<ConicalSurface>(frame, semiAngle, radius);
        }
        case SurfaceType::Sphere: {
            const Frame3 frame = in_.frame();
            return std::make_shared<SphericalSurface>(frame, in_.f64());
        }
        case SurfaceType::Torus: {
            const Frame3 frame = in_.frame();
            const double major = in_.f64(), minor = in_.f64();
            return std::make_shared<ToroidalSurface>(frame, major, minor);
        }
        case SurfaceType::Extrusion: {
            const CurvePtr<3> base = curve<3>(in_.i32());
            return std::make_shared<ExtrusionSurface>(base, in_.vec<3>());
        }
        case SurfaceType::Revolution: {
            const CurvePtr<3> meridian = curve<3>(in_.i32());
            const Vec3 point = in_.vec<3>(), direction = in_.vec<3>();
            return std::make_shared<RevolutionSurface>(meridian, point, direction);
        }
        case SurfaceType::BSpline: {
            const int uDegree = in_.i32(), vDegree = in_.i32();
            std::vector<double> uKnots = in_.doubles(), vKnots = in_.doubles();
            const std::int32_t uCount = in_.i32(), vCount = in_.i32();
            if (uCount <= 0 || vCount <= 0 || std::int64_t(uCount) * vCount > 100000000)
                throw std::invalid_argument("readBodyBinary: numero di poli non valido");
            std::vector<Vec3> poles(std::size_t(uCount) * std::size_t(vCount));
            for (Vec3 &p : poles) p = in_.vec<3>();
            std::vector<double> weights;
            if (in_.flag()) {
                weights.resize(poles.size());
                for (double &w : weights) w = in_.f64();
            }
            return std::make_shared<BSplineSurface>(uDegree, vDegree, std::move(uKnots), std::move(vKnots), uCount, vCount, std::move(poles),
                                                    std::move(weights));
        }
        }
        throw std::invalid_argument("readBodyBinary: tipo di superficie sconosciuto");
    }

    Reader &in_;
    std::vector<CurvePtr<2>> curves2_;
    std::vector<CurvePtr<3>> curves3_;
    std::vector<SurfacePtr> surfaces_;
};

}

namespace detail {

struct BodyIO {
    static std::string write(const Body &body) {
        Writer out;
        out.raw(kMagic, 4);
        out.u32(kVersion);
        out.u32(kByteOrder);
        // Prima tutta la geometria, poi la topologia con gli indici.
        GeometryWriter geometry(out);
        std::vector<int> edgeCurves, finCurves, faceSurfaces;
        for (const Edge &e : body.edges_) edgeCurves.push_back(geometry.curve<3>(e.curve));
        for (const Fin &f : body.fins_) finCurves.push_back(geometry.curve<2>(f.pcurve));
        for (const Face &f : body.faces_) faceSurfaces.push_back(geometry.surface(f.surface));
        out.u8(EndOfGeometry);

        out.u32(std::uint32_t(body.vertices_.size()));
        for (const Vertex &v : body.vertices_) {
            out.vec(v.point);
            out.f64(v.tolerance);
            out.flag(v.alive);
        }
        out.u32(std::uint32_t(body.edges_.size()));
        for (std::size_t i = 0; i < body.edges_.size(); ++i) {
            const Edge &e = body.edges_[i];
            out.i32(e.forward.index);
            out.i32(e.backward.index);
            out.i32(edgeCurves[i]);
            out.f64(e.range.lo);
            out.f64(e.range.hi);
            out.f64(e.tolerance);
            out.flag(e.alive);
        }
        out.u32(std::uint32_t(body.fins_.size()));
        for (std::size_t i = 0; i < body.fins_.size(); ++i) {
            const Fin &f = body.fins_[i];
            for (int id : {f.loop.index, f.edge.index, f.next.index, f.previous.index, f.vertex.index}) out.i32(id);
            out.flag(f.sense);
            out.i32(finCurves[i]);
            out.f64(f.pcurveTolerance);
            out.flag(f.alive);
        }
        out.u32(std::uint32_t(body.loops_.size()));
        for (const Loop &l : body.loops_) {
            out.i32(l.face.index);
            out.i32(l.first.index);
            out.i32(l.isolatedVertex.index);
            out.flag(l.alive);
        }
        out.u32(std::uint32_t(body.faces_.size()));
        for (std::size_t i = 0; i < body.faces_.size(); ++i) {
            const Face &f = body.faces_[i];
            out.i32(f.shell.index);
            out.u32(std::uint32_t(f.loops.size()));
            for (LoopId l : f.loops) out.i32(l.index);
            out.i32(faceSurfaces[i]);
            out.flag(f.sense);
            out.flag(f.alive);
        }
        out.u32(std::uint32_t(body.shells_.size()));
        for (const Shell &s : body.shells_) {
            out.i32(s.region.index);
            out.u32(std::uint32_t(s.faces.size()));
            for (FaceId f : s.faces) out.i32(f.index);
            out.flag(s.alive);
        }
        out.u32(std::uint32_t(body.regions_.size()));
        for (const Region &r : body.regions_) {
            out.flag(r.solid);
            out.u32(std::uint32_t(r.shells.size()));
            for (ShellId s : r.shells) out.i32(s.index);
            out.flag(r.alive);
        }
        return out.take();
    }

    static Body read(const std::string &data) {
        Reader in(data);
        char magic[4];
        in.raw(magic, 4);
        if (std::memcmp(magic, kMagic, 4) != 0) throw std::invalid_argument("readBodyBinary: non e' un body di ForgeCAD");
        if (in.u32() != kVersion) throw std::invalid_argument("readBodyBinary: versione del formato non gestita");
        if (in.u32() != kByteOrder) throw std::invalid_argument("readBodyBinary: ordine dei byte diverso");
        GeometryReader geometry(in);
        geometry.readAll();

        Body body;
        body.vertices_.resize(in.count(33));
        for (Vertex &v : body.vertices_) {
            v.point = in.vec<3>();
            v.tolerance = in.f64();
            v.alive = in.flag();
        }
        body.edges_.resize(in.count(37));
        for (Edge &e : body.edges_) {
            e.forward = FinId(in.i32());
            e.backward = FinId(in.i32());
            e.curve = geometry.curve<3>(in.i32());
            e.range.lo = in.f64();
            e.range.hi = in.f64();
            e.tolerance = in.f64();
            e.alive = in.flag();
        }
        body.fins_.resize(in.count(34));
        for (Fin &f : body.fins_) {
            f.loop = LoopId(in.i32());
            f.edge = EdgeId(in.i32());
            f.next = FinId(in.i32());
            f.previous = FinId(in.i32());
            f.vertex = VertexId(in.i32());
            f.sense = in.flag();
            f.pcurve = geometry.curve<2>(in.i32());
            f.pcurveTolerance = in.f64();
            f.alive = in.flag();
        }
        body.loops_.resize(in.count(13));
        for (Loop &l : body.loops_) {
            l.face = FaceId(in.i32());
            l.first = FinId(in.i32());
            l.isolatedVertex = VertexId(in.i32());
            l.alive = in.flag();
        }
        body.faces_.resize(in.count(14));
        for (Face &f : body.faces_) {
            f.shell = ShellId(in.i32());
            f.loops.resize(in.count(4));
            for (LoopId &l : f.loops) l = LoopId(in.i32());
            f.surface = geometry.surface(in.i32());
            f.sense = in.flag();
            f.alive = in.flag();
        }
        body.shells_.resize(in.count(9));
        for (Shell &s : body.shells_) {
            s.region = RegionId(in.i32());
            s.faces.resize(in.count(4));
            for (FaceId &f : s.faces) f = FaceId(in.i32());
            s.alive = in.flag();
        }
        body.regions_.resize(in.count(6));
        for (Region &r : body.regions_) {
            r.solid = in.flag();
            r.shells.resize(in.count(4));
            for (ShellId &s : r.shells) s = ShellId(in.i32());
            r.alive = in.flag();
        }
        if (!in.atEnd()) throw std::invalid_argument("readBodyBinary: dati in piu' alla fine");
        validate(body);
        return body;
    }

    // Ogni indice delle entita' vive nel suo vettore (o -1 dove l'entita' puo'
    // mancare): i dati non validi non devono far leggere fuori dai vettori.
    static void validate(const Body &body) {
        const auto check = [](int index, std::size_t size, bool optional) {
            if ((optional && index == -1) || (index >= 0 && std::size_t(index) < size)) return;
            throw std::invalid_argument("readBodyBinary: indice di un'entita' non valido");
        };
        for (const Edge &e : body.edges_) {
            if (!e.alive) continue;
            check(e.forward.index, body.fins_.size(), true);
            check(e.backward.index, body.fins_.size(), true);
        }
        for (const Fin &f : body.fins_) {
            if (!f.alive) continue;
            check(f.loop.index, body.loops_.size(), false);
            check(f.edge.index, body.edges_.size(), false);
            check(f.next.index, body.fins_.size(), false);
            check(f.previous.index, body.fins_.size(), false);
            check(f.vertex.index, body.vertices_.size(), false);
        }
        for (const Loop &l : body.loops_) {
            if (!l.alive) continue;
            check(l.face.index, body.faces_.size(), false);
            check(l.first.index, body.fins_.size(), true);
            check(l.isolatedVertex.index, body.vertices_.size(), true);
        }
        for (const Face &f : body.faces_) {
            if (!f.alive) continue;
            check(f.shell.index, body.shells_.size(), false);
            for (LoopId l : f.loops) check(l.index, body.loops_.size(), false);
        }
        for (const Shell &s : body.shells_) {
            if (!s.alive) continue;
            check(s.region.index, body.regions_.size(), false);
            for (FaceId f : s.faces) check(f.index, body.faces_.size(), false);
        }
        for (const Region &r : body.regions_)
            if (r.alive)
                for (ShellId s : r.shells) check(s.index, body.shells_.size(), false);
        if (body.regions_.empty()) throw std::invalid_argument("readBodyBinary: manca la region esterna");
    }
};

}

std::string writeBodyBinary(const Body &body) { return detail::BodyIO::write(body); }

Body readBodyBinary(const std::string &data) { return detail::BodyIO::read(data); }

}
