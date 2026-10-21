#include "fk_iges.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_exchange.h"
#include "fk_hermite.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_parallel.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"

namespace ForgeCad::Kernel {
namespace {

// --- Scrittura -----------------------------------------------------------------

std::string igesReal(double x) {
    if (!std::isfinite(x)) throw std::domain_error("IGES: numero non finito");
    return detail::formatReal(x);
}

// Stringa Hollerith (solo ASCII: gli altri caratteri diventano '_').
std::string hollerith(const std::string &utf8) {
    std::string ascii;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 128) ascii += char(c);
        else if ((c & 0xC0) != 0x80) ascii += '_';
    }
    return std::to_string(ascii.size()) + "H" + ascii;
}

struct IgesEntity {
    int type = 0, form = 0;
    std::vector<std::string> params;
    int subordinate = 0;  // 1: dipendente dal padre
    int use = 0;
    int color = 0;        // numero o -DE del 314
    std::string label;
};

class IgesWriter {
public:
    int add(IgesEntity e) {
        entities_.push_back(std::move(e));
        return 2 * int(entities_.size()) - 1;
    }
    IgesEntity &entity(int de) { return entities_[std::size_t((de - 1) / 2)]; }
    int point(const Vec3 &p) {
        IgesEntity e;
        e.type = 116;
        e.subordinate = 1;
        e.params = {igesReal(p.x()), igesReal(p.y()), igesReal(p.z())};
        return add(e);
    }
    int direction(const Vec3 &d) {
        const Vec3 n = normalized(d);
        IgesEntity e;
        e.type = 123;
        e.subordinate = 1;
        e.params = {igesReal(n.x()), igesReal(n.y()), igesReal(n.z())};
        return add(e);
    }
    std::string file(const IgesWriteOptions &options, double maxCoordinate) const {
        std::string stamp = options.timeStamp;
        if (stamp.empty()) {
            char buffer[32];
            const std::time_t now = std::time(nullptr);
            std::strftime(buffer, sizeof buffer, "%Y%m%d.%H%M%S", std::localtime(&now));
            stamp = buffer;
        }
        std::vector<std::string> start{"ForgeCAD IGES 5.3"};
        const std::vector<std::string> global = {
            "1H,", "1H;", hollerith(options.fileName), hollerith(options.fileName + ".igs"), hollerith("ForgeCAD"), hollerith("ForgeCAD kernel"),
            "32", "38", "6", "308", "15", hollerith(options.fileName), "1.", "2", "2HMM", "1", "1.", hollerith(stamp), "1.E-07",
            igesReal(std::max(maxCoordinate, 1.0)), hollerith(options.author), hollerith("ForgeCAD"), "11", "0", hollerith(stamp)};
        std::ostringstream out;
        const auto line = [&](const std::string &data, char section, int sequence) {
            char tail[16];
            std::snprintf(tail, sizeof tail, "%c%7d", section, sequence);
            std::string l = data;
            l.resize(72, ' ');
            out << l << tail << '\n';
        };
        int s = 0, g = 0, d = 0, p = 0;
        for (const std::string &l : start) line(l, 'S', ++s);
        // Global: parametri separati da virgole, spezzati a 72 colonne sui separatori.
        {
            std::string current;
            for (std::size_t k = 0; k < global.size(); ++k) {
                const std::string piece = global[k] + (k + 1 == global.size() ? ";" : ",");
                if (current.size() + piece.size() > 72) {
                    line(current, 'G', ++g);
                    current.clear();
                }
                current += piece;
            }
            if (!current.empty()) line(current, 'G', ++g);
        }
        // Parametri: prima si impaginano (servono i puntatori alle righe P).
        std::vector<int> firstLine(entities_.size()), lineCount(entities_.size());
        std::vector<std::string> pLines;
        std::vector<int> pOwner;
        for (std::size_t k = 0; k < entities_.size(); ++k) {
            const IgesEntity &e = entities_[k];
            std::vector<std::string> items{std::to_string(e.type)};
            items.insert(items.end(), e.params.begin(), e.params.end());
            firstLine[k] = int(pLines.size()) + 1;
            std::string current;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const std::string piece = items[i] + (i + 1 == items.size() ? ";" : ",");
                if (current.size() + piece.size() > 64 && !current.empty()) {
                    pLines.push_back(current);
                    pOwner.push_back(2 * int(k) + 1);
                    current.clear();
                }
                // Una stringa piu' lunga della riga si spezza (le Hollerith lo permettono).
                std::string rest = piece;
                while (rest.size() > 64) {
                    pLines.push_back(rest.substr(0, 64));
                    pOwner.push_back(2 * int(k) + 1);
                    rest = rest.substr(64);
                }
                current += rest;
            }
            if (!current.empty()) {
                pLines.push_back(current);
                pOwner.push_back(2 * int(k) + 1);
            }
            lineCount[k] = int(pLines.size()) + 1 - firstLine[k];
        }
        const auto field = [](const std::string &value) {
            std::string f = value;
            if (f.size() < 8) f = std::string(8 - f.size(), ' ') + f;
            return f.substr(f.size() - 8);
        };
        for (std::size_t k = 0; k < entities_.size(); ++k) {
            const IgesEntity &e = entities_[k];
            char status[16];
            std::snprintf(status, sizeof status, "000%d%02d00", e.subordinate, e.use);
            const std::string first = field(std::to_string(e.type)) + field(std::to_string(firstLine[k])) + field("0") + field("0") + field("0") + field("0") +
                                      field("0") + field("0") + field(status);
            std::string label = e.label.substr(0, 8);
            label = std::string(8 - label.size(), ' ') + label;
            const std::string second = field(std::to_string(e.type)) + field("0") + field(std::to_string(e.color)) + field(std::to_string(lineCount[k])) +
                                       field(std::to_string(e.form)) + field("") + field("") + label + field("0");
            line(first, 'D', ++d);
            line(second, 'D', ++d);
        }
        for (std::size_t k = 0; k < pLines.size(); ++k) {
            char owner[16];
            std::snprintf(owner, sizeof owner, " %7d", pOwner[k]);
            std::string data = pLines[k];
            data.resize(64, ' ');
            line(data + owner, 'P', ++p);
        }
        char terminate[80];
        std::snprintf(terminate, sizeof terminate, "S%7dG%7dD%7dP%7d", s, g, d, p);
        line(terminate, 'T', 1);
        return out.str();
    }

private:
    std::vector<IgesEntity> entities_;
};

// Curva di un edge come 110 o 126 (sul tratto dell'edge, nel verso dato).
int writeIgesCurve(IgesWriter &w, const Curve<3> &curve, const Interval &range, bool forward) {
    const Curve<3> *basis = &curve;
    while (basis->type() == CurveType::Trimmed) basis = static_cast<const TrimmedCurve<3> *>(basis)->basis().get();
    if (basis->type() == CurveType::Line) {
        const Vec3 a = curve.point(forward ? range.lo : range.hi), b = curve.point(forward ? range.hi : range.lo);
        IgesEntity e;
        e.type = 110;
        e.subordinate = 1;
        e.params = {igesReal(a.x()), igesReal(a.y()), igesReal(a.z()), igesReal(b.x()), igesReal(b.y()), igesReal(b.z())};
        return w.add(e);
    }
    BSplineCurve<3> spline(1, {0, 0, 1, 1}, {Vec3(), Vec3(1, 0, 0)});
    try {
        const std::vector<BSplineCurve<3>> pieces = rationalBezierPieces(curve, range);
        if (pieces.empty()) throw std::domain_error("vuota");
        spline = joinBezierPieces(pieces);
    } catch (const std::exception &) {
        const detail::RowSpline fit = detail::fitQuinticRows([&](double t, bool left, Vec3 *out) {
            if (left) curve.evaluateLeft(t, 2, out);
            else curve.evaluate(t, 2, out);
        }, 1, curve.breakpoints(range), 1e-9);
        spline = detail::rowCurve(fit, 0);
    }
    std::vector<Vec3> poles = spline.poles();
    std::vector<double> weights = spline.weights(), knots = spline.knots();
    if (weights.empty()) weights.assign(poles.size(), 1.0);
    if (!forward) {
        // La stessa curva percorsa al contrario: nodi riflessi, poli e pesi rovesciati.
        std::reverse(poles.begin(), poles.end());
        std::reverse(weights.begin(), weights.end());
        const double a = knots.front(), b = knots.back();
        std::vector<double> reflected;
        for (auto it = knots.rbegin(); it != knots.rend(); ++it) reflected.push_back(a + b - *it);
        knots = reflected;
    }
    bool polynomial = true;
    for (double wgt : weights) polynomial = polynomial && std::fabs(wgt - 1.0) < 1e-15;
    IgesEntity e;
    e.type = 126;
    e.subordinate = 1;
    const int K = int(poles.size()) - 1, M = spline.degree();
    e.params = {std::to_string(K), std::to_string(M), "0", "0", polynomial ? "1" : "0", "0"};
    for (double k : knots) e.params.push_back(igesReal(k));
    for (double wgt : weights) e.params.push_back(igesReal(wgt));
    for (const Vec3 &p : poles) e.params.insert(e.params.end(), {igesReal(p.x()), igesReal(p.y()), igesReal(p.z())});
    e.params.insert(e.params.end(), {igesReal(knots.front()), igesReal(knots.back()), "0.", "0.", "0."});
    return w.add(e);
}

// Superficie della faccia: analitica (190-198) o B-spline razionale esatta (128)
// sul tratto (u, v) della faccia; `flipped`: normale IGES opposta a quella del kernel.
int writeIgesSurface(IgesWriter &w, const Surface &surface, const Interval &u, const Interval &v, bool &flipped) {
    flipped = false;
    const auto analytic = [&](int type, const Frame3 &f, std::vector<std::string> values, bool withAxis) {
        IgesEntity e;
        e.type = type;
        e.form = 1;
        e.subordinate = 1;
        const int loc = w.point(f.origin()), axis = w.direction(f.zDir()), ref = w.direction(f.xDir());
        e.params = {std::to_string(loc)};
        if (withAxis) e.params.push_back(std::to_string(axis));
        e.params.insert(e.params.end(), values.begin(), values.end());
        e.params.push_back(std::to_string(ref));
        return w.add(e);
    };
    switch (surface.type()) {
    case SurfaceType::Plane: return analytic(190, static_cast<const Plane &>(surface).frame(), {}, true);
    case SurfaceType::Cylinder: {
        const auto &c = static_cast<const CylindricalSurface &>(surface);
        return analytic(192, c.frame(), {igesReal(c.radius())}, true);
    }
    case SurfaceType::Cone: {
        const auto &c = static_cast<const ConicalSurface &>(surface);
        Frame3 f = c.frame();
        double angle = c.semiAngle();
        if (angle < 0.0) f = Frame3(f.origin(), -f.zDir(), f.xDir()), angle = -angle;
        return analytic(194, f, {igesReal(c.referenceRadius()), igesReal(angle * 180.0 / kPi)}, true);
    }
    case SurfaceType::Sphere: {
        const auto &s = static_cast<const SphericalSurface &>(surface);
        IgesEntity e;
        e.type = 196;
        e.form = 1;
        e.subordinate = 1;
        e.params = {std::to_string(w.point(s.frame().origin())), igesReal(s.radius()), std::to_string(w.direction(s.frame().zDir())),
                    std::to_string(w.direction(s.frame().xDir()))};
        return w.add(e);
    }
    case SurfaceType::Torus: {
        const auto &t = static_cast<const ToroidalSurface &>(surface);
        return analytic(198, t.frame(), {igesReal(t.majorRadius()), igesReal(t.minorRadius())}, true);
    }
    default: break;
    }
    const BSplineSurface b = toBSplineSurface(surface, u, v);
    // La forma NURBS ha lo stesso verso (S_u x S_v) della superficie? Si confronta la normale in un punto.
    {
        const double um = 0.5 * (u.lo + u.hi), vm = 0.5 * (v.lo + v.hi);
        const Vec3 p = surface.point(um, vm), n = surface.normal(um, vm);
        const SurfaceProjection q = projectPoint(b, p);
        flipped = dot(b.normal(q.u, q.v), n) < 0.0;
    }
    IgesEntity e;
    e.type = 128;
    e.subordinate = 1;
    const int K1 = b.uPoleCount() - 1, K2 = b.vPoleCount() - 1;
    bool polynomial = !b.isRational();
    e.params = {std::to_string(K1), std::to_string(K2), std::to_string(b.uDegree()), std::to_string(b.vDegree()), "0", "0", polynomial ? "1" : "0", "0", "0"};
    for (double k : b.uKnots()) e.params.push_back(igesReal(k));
    for (double k : b.vKnots()) e.params.push_back(igesReal(k));
    for (int j = 0; j <= K2; ++j)
        for (int i = 0; i <= K1; ++i) e.params.push_back(igesReal(b.weight(i, j)));
    for (int j = 0; j <= K2; ++j)
        for (int i = 0; i <= K1; ++i) {
            const Vec3 &p = b.pole(i, j);
            e.params.insert(e.params.end(), {igesReal(p.x()), igesReal(p.y()), igesReal(p.z())});
        }
    e.params.insert(e.params.end(), {igesReal(b.uKnots().front()), igesReal(b.uKnots().back()), igesReal(b.vKnots().front()), igesReal(b.vKnots().back())});
    return w.add(e);
}

// Tratto (u, v) della faccia dalle SP-curve dei suoi edge.
void faceBounds(const Body &body, FaceId f, Interval &u, Interval &v) {
    const Face &face = body.face(f);
    u = {1e300, -1e300};
    v = {1e300, -1e300};
    for (LoopId l : face.loops)
        for (FinId fin : body.loopFins(l)) {
            const Fin &data = body.fin(fin);
            if (!data.pcurve) continue;
            const Edge &edge = body.edge(data.edge);
            for (int k = 0; k <= 16; ++k) {
                const Vec2 uv = data.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
                u.lo = std::min(u.lo, uv.x()), u.hi = std::max(u.hi, uv.x());
                v.lo = std::min(v.lo, uv.y()), v.hi = std::max(v.hi, uv.y());
            }
        }
    const Interval ud = face.surface->uDomain(), vd = face.surface->vDomain();
    if (!(u.lo < u.hi)) u = ud.isFinite() ? ud : Interval{-1.0, 1.0};
    if (!(v.lo < v.hi)) v = vd.isFinite() ? vd : Interval{-1.0, 1.0};
    // Un periodo intero nelle direzioni periodiche chiuse, un margine nelle altre.
    if (face.surface->isUPeriodic() && u.hi - u.lo > 0.999 * face.surface->uPeriod()) u = {u.lo, u.lo + face.surface->uPeriod()};
    else if (!ud.isFinite() || u.lo > ud.lo || u.hi < ud.hi) u = {u.lo - 0.01 * (u.hi - u.lo), u.hi + 0.01 * (u.hi - u.lo)};
    if (face.surface->isVPeriodic() && v.hi - v.lo > 0.999 * face.surface->vPeriod()) v = {v.lo, v.lo + face.surface->vPeriod()};
    else if (!vd.isFinite() || v.lo > vd.lo || v.hi < vd.hi) v = {v.lo - 0.01 * (v.hi - v.lo), v.hi + 0.01 * (v.hi - v.lo)};
    if (ud.isFinite()) u = {std::max(u.lo, ud.lo), std::min(u.hi, ud.hi)};
    if (vd.isFinite()) v = {std::max(v.lo, vd.lo), std::min(v.hi, vd.hi)};
}

// Area con segno di un loop nello spazio (u, v) delle SP-curve (per il bordo esterno).
double loopArea(const Body &body, LoopId l) {
    double area = 0.0;
    std::vector<Vec2> polygon;
    for (FinId fin : body.loopFins(l)) {
        const Fin &data = body.fin(fin);
        if (!data.pcurve) return 0.0;
        const Edge &edge = body.edge(data.edge);
        for (int k = 0; k < 16; ++k) {
            const double f = k / 16.0;
            polygon.push_back(data.pcurve->point(data.sense ? edge.range.lo + f * edge.range.length() : edge.range.hi - f * edge.range.length()));
        }
    }
    for (std::size_t k = 0; k < polygon.size(); ++k) area += cross(polygon[k], polygon[(k + 1) % polygon.size()]);
    return 0.5 * area;
}

// --- Lettura ---------------------------------------------------------------------

struct Directory {
    int type = 0, parameter = 0, transform = 0, form = 0, color = 0;
    std::string label;
    std::vector<std::string> params;  // campi grezzi (Hollerith gia' decodificate)
};

class IgesFile {
public:
    explicit IgesFile(const std::string &content) { parse(content); }
    const Directory *find(int de) const {
        const auto it = entries_.find(de);
        return it == entries_.end() ? nullptr : &it->second;
    }
    const Directory &at(int de) const {
        const Directory *d = find(de);
        if (!d) throw std::domain_error("IGES: entita' inesistente (DE " + std::to_string(de) + ")");
        return *d;
    }
    std::vector<int> ofType(int type) const {
        std::vector<int> result;
        for (const auto &[de, d] : entries_)
            if (d.type == type) result.push_back(de);
        return result;
    }
    double unit() const { return unit_; }
    double resolution() const { return resolution_; }

private:
    void parse(const std::string &content) {
        std::vector<std::string> lines;
        std::string current;
        for (char c : content) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else if (c != '\r') {
                current += c;
            }
        }
        if (!current.empty()) lines.push_back(current);
        std::string global;
        std::vector<std::string> dLines;
        std::map<int, std::string> pData;
        std::vector<std::pair<int, std::string>> pLines;
        for (std::string l : lines) {
            if (l.size() < 73) l.resize(80, ' ');
            const char section = l[72];
            if (section == 'G') global += l.substr(0, 72);
            else if (section == 'D') dLines.push_back(l);
            else if (section == 'P') {
                const int owner = std::atoi(l.substr(64, 8).c_str());
                pData[owner] += l.substr(0, 64);
            }
        }
        if (dLines.empty()) throw std::domain_error("IGES: sezione D mancante (non e' un file IGES?)");
        // Global: separatori e unita'.
        char delimiter = ',', record = ';';
        std::size_t pos = 0;
        const auto readHollerith = [&](std::size_t &at) -> std::string {
            std::size_t digits = at;
            while (digits < global.size() && std::isdigit(static_cast<unsigned char>(global[digits]))) ++digits;
            if (digits < global.size() && (global[digits] == 'H' || global[digits] == 'h') && digits > at) {
                const int n = std::atoi(global.substr(at, digits - at).c_str());
                std::string s = global.substr(digits + 1, std::size_t(n));
                at = digits + 1 + std::size_t(n);
                return s;
            }
            return {};
        };
        {
            std::size_t at = 0;
            while (at < global.size() && global[at] == ' ') ++at;
            if (global.compare(at, 2, "1H") == 0) delimiter = global[at + 2], at += 3;
            else if (at < global.size() && global[at] == ',') at += 0;
            if (at < global.size() && global[at] == delimiter) ++at;
            while (at < global.size() && global[at] == ' ') ++at;
            if (global.compare(at, 2, "1H") == 0) record = global[at + 2], at += 3;
            while (at < global.size() && global[at] == ' ') ++at;
            if (at < global.size() && global[at] == delimiter) ++at;
            pos = at;
        }
        std::vector<std::string> fields = split(global.substr(pos), delimiter, record);
        // Dopo i due separatori: 3 prodotto, ..., 13 scala, 14 unita', 19 risoluzione.
        const auto field = [&](int index) { return index - 3 >= 0 && std::size_t(index - 3) < fields.size() ? fields[std::size_t(index - 3)] : std::string(); };
        (void)readHollerith;
        const double scale = field(13).empty() ? 1.0 : toNumber(field(13));
        const int units = field(14).empty() ? 2 : int(toNumber(field(14)));
        static const std::map<int, double> factors = {{1, 25.4}, {2, 1.0}, {4, 304.8}, {5, 1609344.0}, {6, 1000.0}, {7, 1e6}, {8, 0.0254}, {9, 1e-3}, {10, 10.0}, {11, 2.54e-5}};
        unit_ = (factors.count(units) ? factors.at(units) : 1.0) / (scale != 0.0 ? scale : 1.0);
        if (units == 3) {
            std::string name = field(15);
            for (char &c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (name.find("IN") != std::string::npos) unit_ = 25.4;
            else if (name.find("FT") != std::string::npos) unit_ = 304.8;
            else if (name.find("CM") != std::string::npos) unit_ = 10.0;
            else if (name == "M") unit_ = 1000.0;
        }
        resolution_ = field(19).empty() ? 1e-6 : std::fabs(toNumber(field(19))) * unit_;
        for (std::size_t k = 0; k + 1 < dLines.size(); k += 2) {
            const std::string &a = dLines[k], &b = dLines[k + 1];
            const auto f = [](const std::string &l, int i) { return std::atoi(l.substr(std::size_t(8 * i), 8).c_str()); };
            Directory d;
            d.type = f(a, 0);
            d.parameter = f(a, 1);
            d.transform = f(a, 6);
            d.color = f(b, 2);
            d.form = f(b, 4);
            d.label = b.substr(56, 8);
            const int de = std::atoi(a.substr(73, 7).c_str());
            const auto data = pData.find(de);
            if (data != pData.end()) {
                d.params = split(data->second, delimiter, record);
                if (!d.params.empty()) d.params.erase(d.params.begin());  // il tipo
            }
            entries_[de] = std::move(d);
        }
    }
    static double toNumber(std::string s) {
        for (char &c : s)
            if (c == 'D' || c == 'd') c = 'E';
        double value = 0.0;
        std::size_t start = 0;
        while (start < s.size() && s[start] == ' ') ++start;
        detail::parseReal(s.data() + start, s.data() + s.size(), value);
        return value;
    }
    static std::vector<std::string> split(const std::string &data, char delimiter, char record) {
        std::vector<std::string> fields;
        std::string current;
        for (std::size_t i = 0; i < data.size(); ++i) {
            const char c = data[i];
            // Stringa Hollerith: nH seguito da n caratteri.
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t j = i;
                while (j < data.size() && std::isdigit(static_cast<unsigned char>(data[j]))) ++j;
                bool onlySpaces = true;
                for (char x : current) onlySpaces = onlySpaces && x == ' ';
                if (j < data.size() && (data[j] == 'H' || data[j] == 'h') && onlySpaces) {
                    const std::size_t n = std::size_t(std::atoi(data.substr(i, j - i).c_str()));
                    current = data.substr(j + 1, n);
                    i = j + n;
                    continue;
                }
            }
            if (c == delimiter || c == record) {
                std::size_t a = 0, b = current.size();
                while (a < b && current[a] == ' ') ++a;
                while (b > a && current[b - 1] == ' ') --b;
                fields.push_back(current.substr(a, b - a));
                current.clear();
                if (c == record) break;
                continue;
            }
            current += c;
        }
        return fields;
    }

public:
    static double number(const Directory &d, std::size_t index, double fallback = 0.0) {
        if (index >= d.params.size() || d.params[index].empty()) return fallback;
        return toNumber(d.params[index]);
    }
    static int integer(const Directory &d, std::size_t index, int fallback = 0) {
        if (index >= d.params.size() || d.params[index].empty()) return fallback;
        return int(std::lround(toNumber(d.params[index])));
    }

private:
    std::map<int, Directory> entries_;
    double unit_ = 1.0, resolution_ = 1e-6;
};

// Conversione delle entita' in curve e superfici del kernel (in mm, trasformate).
class IgesGeometry {
public:
    explicit IgesGeometry(const IgesFile &file) : f_(file), s_(file.unit()) {}

    Transform3 transform(int de, int depth = 0) const {
        if (de <= 0 || depth > 16) return Transform3();
        const Directory &d = f_.at(de);
        if (d.type != 124) return Transform3();
        double m[12];
        for (int k = 0; k < 12; ++k) m[k] = IgesFile::number(d, std::size_t(k));
        // R riga per riga con T in quarta colonna: p' = R p + T (T in unita' del file).
        const Vec3 origin(m[3] * s_, m[7] * s_, m[11] * s_);
        const Vec3 x(m[0], m[4], m[8]), y(m[1], m[5], m[9]), z(m[2], m[6], m[10]);
        Transform3 t;
        if (dot(cross(x, y), z) > 0.0) {
            t = Transform3::fromFrame(Frame3(origin, z, x));
        } else {
            // Simmetria: R = -R' con R' una rotazione; T(p) = t - R' p.
            t = Transform3::translation(origin) * Transform3::scaling(Vec3(0, 0, 0), -1.0) * Transform3::fromFrame(Frame3(Vec3(0, 0, 0), -z, -x));
        }
        return transform(d.transform, depth + 1) * t;
    }
    Vec3 point(const Directory &d, std::size_t at) const {
        return Vec3(IgesFile::number(d, at) * s_, IgesFile::number(d, at + 1) * s_, IgesFile::number(d, at + 2) * s_);
    }
    Vec3 pointEntity(int de) const {
        const Directory &d = f_.at(de);
        return transform(d.transform).applyToPoint(point(d, 0));
    }
    Vec3 directionEntity(int de) const {
        const Directory &d = f_.at(de);
        const Vec3 v(IgesFile::number(d, 0), IgesFile::number(d, 1), IgesFile::number(d, 2));
        return normalized(transform(d.transform).applyToVector(v));
    }

    // Curva con il tratto; `from`/`to`: i punti degli estremi (nel verso della curva).
    struct Piece {
        CurvePtr<3> curve;
        Interval range;
    };
    // Una curva (anche composta: piu' tratti) nello spazio del modello.
    std::vector<Piece> curve(int de) const {
        const Directory &d = f_.at(de);
        const Transform3 t = transform(d.transform);
        const auto transformed = [&](CurvePtr<3> c) -> CurvePtr<3> {
            if (d.transform <= 0) return c;
            return std::make_shared<TransformedCurve>(c, t);
        };
        switch (d.type) {
        case 100: {
            const double z = IgesFile::number(d, 0) * s_;
            const Vec3 c(IgesFile::number(d, 1) * s_, IgesFile::number(d, 2) * s_, z), a(IgesFile::number(d, 3) * s_, IgesFile::number(d, 4) * s_, z),
                b(IgesFile::number(d, 5) * s_, IgesFile::number(d, 6) * s_, z);
            const double r = distance(c, a);
            const Vec3 x = normalized(a - c), y = cross(Vec3(0, 0, 1), x);
            double sweep = std::atan2(dot(b - c, y), dot(b - c, x));
            if (sweep <= 1e-12) sweep += kTwoPi;
            if (distance(a, b) < 1e-12 * std::max(1.0, r)) sweep = kTwoPi;
            return {{transformed(std::make_shared<Circle<3>>(c, x, y, r)), {0.0, sweep}}};
        }
        case 102: {
            std::vector<Piece> all;
            const int n = IgesFile::integer(d, 0);
            for (int k = 0; k < n; ++k) {
                const std::vector<Piece> p = curve(IgesFile::integer(d, std::size_t(1 + k)));
                all.insert(all.end(), p.begin(), p.end());
            }
            // La composta dentro una trasformazione propria.
            if (d.transform > 0)
                for (Piece &p : all) p.curve = std::make_shared<TransformedCurve>(p.curve, t);
            return all;
        }
        case 104: {
            if (d.form != 1) throw std::domain_error("IGES: conica non ellittica (104) non gestita");
            const double A = IgesFile::number(d, 0), B = IgesFile::number(d, 1), C = IgesFile::number(d, 2), D = IgesFile::number(d, 3),
                         E = IgesFile::number(d, 4), F = IgesFile::number(d, 5), z = IgesFile::number(d, 6) * s_;
            // Centro e assi dell'ellisse A x^2 + B xy + C y^2 + D x + E y + F = 0.
            const double det = 4.0 * A * C - B * B;
            if (!(std::fabs(det) > 0.0)) throw std::domain_error("IGES: ellisse degenere");
            const double cx = (B * E - 2.0 * C * D) / det, cy = (B * D - 2.0 * A * E) / det;
            const double Fc = A * cx * cx + B * cx * cy + C * cy * cy + D * cx + E * cy + F;
            const double theta = 0.5 * std::atan2(B, A - C);
            const double ct = std::cos(theta), st = std::sin(theta);
            const double a1 = A * ct * ct + B * ct * st + C * st * st, a2 = A * st * st - B * ct * st + C * ct * ct;
            const double rx = std::sqrt(-Fc / a1) * s_, ry = std::sqrt(-Fc / a2) * s_;
            const Vec3 center(cx * s_, cy * s_, z), x(ct, st, 0), y(-st, ct, 0);
            const auto angleOf = [&](double px, double py) {
                const Vec3 p(px * s_ - center.x(), py * s_ - center.y(), 0.0);
                return std::atan2(dot(p, y) / ry, dot(p, x) / rx);
            };
            double t0 = angleOf(IgesFile::number(d, 7), IgesFile::number(d, 8)), t1 = angleOf(IgesFile::number(d, 9), IgesFile::number(d, 10));
            if (t1 <= t0 + 1e-12) t1 += kTwoPi;
            return {{transformed(std::make_shared<Ellipse<3>>(center, x, y, rx, ry)), {t0, t1}}};
        }
        case 106: {
            const int ip = IgesFile::integer(d, 0), n = IgesFile::integer(d, 1);
            std::vector<Vec3> points;
            if (d.form == 1 || d.form == 11 || d.form == 63) {
                const double z = IgesFile::number(d, 2) * s_;
                for (int k = 0; k < n; ++k) points.push_back(Vec3(IgesFile::number(d, std::size_t(3 + 2 * k)) * s_, IgesFile::number(d, std::size_t(4 + 2 * k)) * s_, z));
            } else {
                const int stride = ip == 3 ? 6 : 3;
                for (int k = 0; k < n; ++k) points.push_back(point(d, std::size_t(2 + stride * k)));
            }
            if (d.form == 63 && !points.empty()) points.push_back(points.front());
            std::vector<Piece> all;
            for (std::size_t k = 0; k + 1 < points.size(); ++k) {
                if (distance(points[k], points[k + 1]) <= 0.0) continue;
                all.push_back({transformed(std::make_shared<Line<3>>(points[k], normalized(points[k + 1] - points[k]))), {0.0, distance(points[k], points[k + 1])}});
            }
            return all;
        }
        case 110: {
            const Vec3 a = point(d, 0), b = point(d, 3);
            if (!(distance(a, b) > 0.0)) throw std::domain_error("IGES: segmento nullo");
            return {{transformed(std::make_shared<Line<3>>(a, normalized(b - a))), {0.0, distance(a, b)}}};
        }
        case 112: {
            // Spline parametrica: N tratti cubici in potenze di s = t - T(i), poi Bezier esatte.
            const int n = IgesFile::integer(d, 3);
            std::vector<double> T;
            for (int k = 0; k <= n; ++k) T.push_back(IgesFile::number(d, std::size_t(4 + k)));
            std::vector<Vec3> poles;
            std::vector<double> knots;
            const std::size_t base = std::size_t(5 + n);
            for (int k = 0; k < n; ++k) {
                const std::size_t at = base + std::size_t(12 * k);
                const double h = T[std::size_t(k + 1)] - T[std::size_t(k)];
                Vec3 a[4];
                for (int c = 0; c < 3; ++c)
                    for (int m = 0; m < 4; ++m) a[m][c] = IgesFile::number(d, at + std::size_t(4 * c + m)) * s_;
                // P(s) = a0 + a1 s + a2 s^2 + a3 s^3, s in [0, h] -> poli di Bezier in u = s / h.
                const Vec3 b0 = a[0], b1 = a[0] + a[1] * (h / 3.0), b2 = a[0] + a[1] * (2.0 * h / 3.0) + a[2] * (h * h / 3.0),
                           b3 = a[0] + a[1] * h + a[2] * (h * h) + a[3] * (h * h * h);
                if (k == 0) poles.push_back(b0);
                poles.insert(poles.end(), {b1, b2, b3});
            }
            knots.insert(knots.end(), 4, T.front());
            for (int k = 1; k < n; ++k) knots.insert(knots.end(), 3, T[std::size_t(k)]);
            knots.insert(knots.end(), 4, T.back());
            return {{transformed(std::make_shared<BSplineCurve<3>>(3, knots, poles)), {T.front(), T.back()}}};
        }
        case 126: {
            const int K = IgesFile::integer(d, 0), M = IgesFile::integer(d, 1);
            const int nk = K + M + 2;
            std::vector<double> knots, weights;
            std::vector<Vec3> poles;
            for (int k = 0; k < nk; ++k) knots.push_back(IgesFile::number(d, std::size_t(6 + k)));
            for (int k = 0; k <= K; ++k) weights.push_back(IgesFile::number(d, std::size_t(6 + nk + k), 1.0));
            for (int k = 0; k <= K; ++k) poles.push_back(point(d, std::size_t(6 + nk + K + 1 + 3 * k)));
            const std::size_t after = std::size_t(6 + nk + K + 1 + 3 * (K + 1));
            double v0 = IgesFile::number(d, after, knots[std::size_t(M)]), v1 = IgesFile::number(d, after + 1, knots[std::size_t(K + 1)]);
            bool polynomial = true;
            for (double w : weights) polynomial = polynomial && std::fabs(w - 1.0) < 1e-15;
            const auto c = std::make_shared<BSplineCurve<3>>(M, knots, poles, polynomial ? std::vector<double>{} : weights);
            v0 = std::max(v0, c->domain().lo);
            v1 = std::min(v1, c->domain().hi);
            return {{transformed(c), {v0, v1}}};
        }
        default: throw std::domain_error("IGES: curva non gestita (tipo " + std::to_string(d.type) + ")");
        }
    }

    SurfacePtr surface(int de, bool &flipped) const {
        flipped = false;
        const Directory &d = f_.at(de);
        const Transform3 t = transform(d.transform);
        const auto framed = [&](int loc, int axis, int ref) {
            const Vec3 o = pointEntity(loc);
            const Vec3 z = axis > 0 ? directionEntity(axis) : Vec3(0, 0, 1);
            Vec3 x = ref > 0 ? directionEntity(ref) : (std::fabs(z.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0));
            if (norm(cross(z, x)) < 1e-12) x = std::fabs(z.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            const Frame3 local(o, z, x);
            if (d.transform <= 0) return local;
            return Frame3(t.applyToPoint(local.origin()), normalized(t.applyToVector(local.zDir())), normalized(t.applyToVector(local.xDir())));
        };
        switch (d.type) {
        case 108: {
            const Vec3 n(IgesFile::number(d, 0), IgesFile::number(d, 1), IgesFile::number(d, 2));
            const double D = IgesFile::number(d, 3) * s_;
            const Vec3 o = n * (D / dot(n, n));
            const Frame3 local(o, normalized(n), std::fabs(n.x()) < 0.9 * norm(n) ? Vec3(1, 0, 0) : Vec3(0, 1, 0));
            if (d.transform <= 0) return std::make_shared<Plane>(local);
            return std::make_shared<Plane>(Frame3(t.applyToPoint(o), normalized(t.applyToVector(local.zDir())), normalized(t.applyToVector(local.xDir()))));
        }
        case 190: return std::make_shared<Plane>(framed(IgesFile::integer(d, 0), IgesFile::integer(d, 1), d.form == 1 ? IgesFile::integer(d, 2) : 0));
        case 192:
            return std::make_shared<CylindricalSurface>(framed(IgesFile::integer(d, 0), IgesFile::integer(d, 1), d.form == 1 ? IgesFile::integer(d, 3) : 0),
                                                        IgesFile::number(d, 2) * s_);
        case 194:
            return std::make_shared<ConicalSurface>(framed(IgesFile::integer(d, 0), IgesFile::integer(d, 1), d.form == 1 ? IgesFile::integer(d, 4) : 0),
                                                    IgesFile::number(d, 3) * kPi / 180.0, IgesFile::number(d, 2) * s_);
        case 196:
            return std::make_shared<SphericalSurface>(framed(IgesFile::integer(d, 0), d.form == 1 ? IgesFile::integer(d, 2) : 0, d.form == 1 ? IgesFile::integer(d, 3) : 0),
                                                      IgesFile::number(d, 1) * s_);
        case 198:
            return std::make_shared<ToroidalSurface>(framed(IgesFile::integer(d, 0), IgesFile::integer(d, 1), d.form == 1 ? IgesFile::integer(d, 4) : 0),
                                                     IgesFile::number(d, 2) * s_, IgesFile::number(d, 3) * s_);
        case 120: {
            const std::vector<Piece> axis = curve(IgesFile::integer(d, 0));
            const std::vector<Piece> generatrix = curve(IgesFile::integer(d, 1));
            if (axis.size() != 1 || generatrix.size() != 1) throw std::domain_error("IGES: rivoluzione con curve composte non gestita");
            const Vec3 a = axis[0].curve->point(axis[0].range.lo), b = axis[0].curve->point(axis[0].range.hi);
            CurvePtr<3> meridian = generatrix[0].curve;
            if (d.transform > 0) meridian = std::make_shared<TransformedCurve>(meridian, t);
            const Vec3 pa = d.transform > 0 ? t.applyToPoint(a) : a, pb = d.transform > 0 ? t.applyToPoint(b) : b;
            return std::make_shared<RevolutionSurface>(std::make_shared<TrimmedCurve<3>>(meridian, generatrix[0].range.lo, generatrix[0].range.hi), pa, normalized(pb - pa));
        }
        case 122: {
            const std::vector<Piece> directrix = curve(IgesFile::integer(d, 0));
            if (directrix.size() != 1) throw std::domain_error("IGES: cilindro tabulato con curva composta non gestito");
            const Vec3 end = point(d, 1);
            const Vec3 start = directrix[0].curve->point(directrix[0].range.lo);
            CurvePtr<3> c = std::make_shared<TrimmedCurve<3>>(directrix[0].curve, directrix[0].range.lo, directrix[0].range.hi);
            Vec3 dir = end - start;
            if (d.transform > 0) c = std::make_shared<TransformedCurve>(c, t), dir = t.applyToVector(dir);
            return std::make_shared<ExtrusionSurface>(c, normalized(dir));
        }
        case 128: {
            const int K1 = IgesFile::integer(d, 0), K2 = IgesFile::integer(d, 1), M1 = IgesFile::integer(d, 2), M2 = IgesFile::integer(d, 3);
            const int n1 = K1 + M1 + 2, n2 = K2 + M2 + 2;
            std::size_t at = 9;
            std::vector<double> uk, vk, weights;
            for (int k = 0; k < n1; ++k) uk.push_back(IgesFile::number(d, at++));
            for (int k = 0; k < n2; ++k) vk.push_back(IgesFile::number(d, at++));
            std::vector<double> w((K1 + 1) * (K2 + 1));
            for (int j = 0; j <= K2; ++j)
                for (int i = 0; i <= K1; ++i) w[std::size_t(i * (K2 + 1) + j)] = IgesFile::number(d, at++, 1.0);
            std::vector<Vec3> poles((K1 + 1) * (K2 + 1));
            for (int j = 0; j <= K2; ++j)
                for (int i = 0; i <= K1; ++i) {
                    Vec3 p = point(d, at);
                    at += 3;
                    if (d.transform > 0) p = t.applyToPoint(p);
                    poles[std::size_t(i * (K2 + 1) + j)] = p;
                }
            bool polynomial = true;
            for (double x : w) polynomial = polynomial && std::fabs(x - 1.0) < 1e-15;
            return std::make_shared<BSplineSurface>(M1, M2, uk, vk, K1 + 1, K2 + 1, poles, polynomial ? std::vector<double>{} : w);
        }
        case 114: {
            // Superficie spline parametrica: pezze bicubiche in potenze -> Bezier esatte.
            const int M = IgesFile::integer(d, 2), N = IgesFile::integer(d, 3);
            std::vector<double> TU, TV;
            for (int k = 0; k <= M; ++k) TU.push_back(IgesFile::number(d, std::size_t(4 + k)));
            for (int k = 0; k <= N; ++k) TV.push_back(IgesFile::number(d, std::size_t(5 + M + k)));
            const std::size_t base = std::size_t(6 + M + N);
            const int nu = 3 * M + 1, nv = 3 * N + 1;
            std::vector<Vec3> poles(std::size_t(nu * nv));
            for (int i = 0; i < M; ++i)
                for (int j = 0; j < N; ++j) {
                    const std::size_t at = base + std::size_t(48 * (i * N + j));
                    const double hu = TU[std::size_t(i + 1)] - TU[std::size_t(i)], hv = TV[std::size_t(j + 1)] - TV[std::size_t(j)];
                    // Coefficienti a[p][q] di s^p t^q per ogni coordinata, poi in u = s / hu, v = t / hv.
                    for (int c = 0; c < 3; ++c) {
                        double a[4][4];
                        for (int q = 0; q < 4; ++q)
                            for (int p = 0; p < 4; ++p) a[p][q] = IgesFile::number(d, at + std::size_t(16 * c + 4 * q + p)) * s_ * std::pow(hu, p) * std::pow(hv, q);
                        // Potenze -> Bernstein (matrice di cambio base per il grado 3).
                        static const double P2B[4][4] = {{1, 0, 0, 0}, {1, 1.0 / 3, 0, 0}, {1, 2.0 / 3, 1.0 / 3, 0}, {1, 1, 1, 1}};
                        for (int r = 0; r < 4; ++r)
                            for (int s2 = 0; s2 < 4; ++s2) {
                                double value = 0.0;
                                for (int p = 0; p < 4; ++p)
                                    for (int q = 0; q < 4; ++q) value += P2B[r][p] * P2B[s2][q] * a[p][q];
                                poles[std::size_t((3 * i + r) * nv + 3 * j + s2)][c] = value;
                            }
                    }
                }
            std::vector<double> uk, vk;
            uk.insert(uk.end(), 4, TU.front());
            for (int k = 1; k < M; ++k) uk.insert(uk.end(), 3, TU[std::size_t(k)]);
            uk.insert(uk.end(), 4, TU.back());
            vk.insert(vk.end(), 4, TV.front());
            for (int k = 1; k < N; ++k) vk.insert(vk.end(), 3, TV[std::size_t(k)]);
            vk.insert(vk.end(), 4, TV.back());
            if (d.transform > 0)
                for (Vec3 &p : poles) p = t.applyToPoint(p);
            return std::make_shared<BSplineSurface>(3, 3, uk, vk, nu, nv, poles);
        }
        default: throw std::domain_error("IGES: superficie non gestita (tipo " + std::to_string(d.type) + ")");
        }
    }

private:
    const IgesFile &f_;
    double s_;
};

// Nome dalla proprieta' 406 forma 15 tra i puntatori finali dell'entita' (a partire da `after`).
std::string propertyName(const IgesFile &file, const Directory &d, std::size_t after) {
    const int nv = IgesFile::integer(d, after);
    const std::size_t npAt = after + 1 + std::size_t(std::max(0, nv));
    const int np = IgesFile::integer(d, npAt);
    for (int k = 0; k < np; ++k) {
        const Directory *p = file.find(IgesFile::integer(d, npAt + 1 + std::size_t(k)));
        if (p && p->type == 406 && p->form == 15 && p->params.size() >= 2) return p->params[1];
    }
    std::string label = d.label;
    while (!label.empty() && label.back() == ' ') label.pop_back();
    while (!label.empty() && label.front() == ' ') label.erase(label.begin());
    return label;
}

bool colourOf(const IgesFile &file, const Directory &d, double rgb[3]) {
    if (d.color >= 0) {
        static const double table[9][3] = {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
        if (d.color < 1 || d.color > 8) return false;
        for (int k = 0; k < 3; ++k) rgb[k] = table[d.color][k];
        return true;
    }
    const Directory *c = file.find(-d.color);
    if (!c || c->type != 314) return false;
    for (int k = 0; k < 3; ++k) rgb[k] = IgesFile::number(*c, std::size_t(k)) / 100.0;
    return true;
}

// Volume con segno dalla tassellazione (per girare i solidi cuciti al contrario).
double roughVolume(const Body &body) {
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    TessellationOptions options;
    options.deflection = 1e-3 * std::max(box.diagonal(), 1e-3);
    double volume = 0.0;
    for (const FaceMesh &mesh : tessellate(body, options).faces)
        for (const std::array<int, 3> &tri : mesh.triangles)
            volume += dot(mesh.points[std::size_t(tri[0])], cross(mesh.points[std::size_t(tri[1])], mesh.points[std::size_t(tri[2])])) / 6.0;
    return volume;
}

// Verso di una faccia dai suoi loop nello spazio (u, v) (le superfici limitate
// 144 non lo dicono; i loop seguono il verso della faccia nel solido): il loop
// esterno gira in senso antiorario attorno alla normale della faccia. Sui loop
// avvolti in u decide il piu' basso (percorso verso u crescenti: la faccia e'
// sopra, tra lui e il successivo). 0 se non si capisce.
int loopSense(const Body &body, FaceId f) {
    const Face &face = body.face(f);
    const Surface &surface = *face.surface;
    const double pu = surface.isUPeriodic() ? surface.uPeriod() : 0.0, pv = surface.isVPeriodic() ? surface.vPeriod() : 0.0;
    double bestArea = 0.0, lowestV = std::numeric_limits<double>::infinity();
    int wrapSense = 0, wrapping = 0;
    for (LoopId l : face.loops) {
        std::vector<Vec2> polygon;
        for (FinId fin : body.loopFins(l)) {
            const Fin &data = body.fin(fin);
            if (!data.pcurve) return 0;
            const Edge &edge = body.edge(data.edge);
            for (int k = 0; k < 16; ++k) {
                const double t = k / 16.0;
                Vec2 q = data.pcurve->point(data.sense ? edge.range.lo + t * edge.range.length() : edge.range.hi - t * edge.range.length());
                if (!polygon.empty()) {
                    const Vec2 &last = polygon.back();
                    if (pu > 0.0) q[0] -= pu * std::round((q[0] - last[0]) / pu);
                    if (pv > 0.0) q[1] -= pv * std::round((q[1] - last[1]) / pv);
                }
                polygon.push_back(q);
            }
        }
        if (polygon.size() < 3) continue;
        Vec2 closing = polygon.front();
        if (pu > 0.0) closing[0] -= pu * std::round((closing[0] - polygon.back()[0]) / pu);
        if (pv > 0.0) closing[1] -= pv * std::round((closing[1] - polygon.back()[1]) / pv);
        const Vec2 drift = closing - polygon.front();
        if (std::fabs(drift[1]) > 1e-9) return 0;  // avvolto in v: lasciato com'e'
        if (std::fabs(drift[0]) > 1e-9) {
            double meanV = 0.0;
            for (const Vec2 &q : polygon) meanV += q[1];
            meanV /= double(polygon.size());
            ++wrapping;
            if (meanV < lowestV) lowestV = meanV, wrapSense = drift[0] > 0.0 ? 1 : -1;
            continue;
        }
        double area = 0.0;
        for (std::size_t k = 0; k < polygon.size(); ++k) area += cross(polygon[k], polygon[(k + 1) % polygon.size()]);
        if (std::fabs(area) > std::fabs(bestArea)) bestArea = area;
    }
    // Un solo loop avvolto (calotta attorno a un polo): la faccia puo' stare dall'una o dall'altra parte.
    if (wrapping == 1) return 0;
    if (bestArea != 0.0 && wrapping == 0) return bestArea > 0.0 ? 1 : -1;
    if (bestArea == 0.0) return wrapSense;
    return 0;
}

}  // namespace

std::string writeIges(const std::vector<ExchangeBody> &bodies, const IgesWriteOptions &options) {
    IgesWriter w;
    double maxCoordinate = 1.0;
    for (const ExchangeBody &exchange : bodies) {
        const Body &body = exchange.body;
        for (VertexId v : body.vertices())
            for (int k = 0; k < 3; ++k) maxCoordinate = std::max(maxCoordinate, std::fabs(body.vertex(v).point[k]));
        int colour = 0;
        if (exchange.hasColor) {
            IgesEntity c;
            c.type = 314;
            c.params = {igesReal(100.0 * exchange.color[0]), igesReal(100.0 * exchange.color[1]), igesReal(100.0 * exchange.color[2]), hollerith("colore")};
            colour = -w.add(c);
        }
        IgesEntity nameProperty;
        nameProperty.type = 406;
        nameProperty.form = 15;
        nameProperty.params = {"1", hollerith(exchange.name)};
        const int name = w.add(nameProperty);
        if (exchange.curve) {
            // Curva (elica): un'entita' indipendente con il nome e il colore.
            const Interval range = exchange.curveRange;
            if (!range.isFinite() || !(range.hi > range.lo)) throw std::domain_error("IGES: curva senza tratto");
            for (int k = 0; k < 2; ++k) {
                const Vec3 p = exchange.curve->point(k == 0 ? range.lo : range.hi);
                for (int i = 0; i < 3; ++i) maxCoordinate = std::max(maxCoordinate, std::fabs(p[i]));
            }
            IgesEntity &curve = w.entity(writeIgesCurve(w, *exchange.curve, range, true));
            curve.subordinate = 0;
            curve.color = colour;
            curve.label = exchange.name;
            curve.params.insert(curve.params.end(), {"0", "1", std::to_string(name)});
            continue;
        }
        const bool solids = options.mode == IgesMode::Solids && !body.isSheet();
        // Superfici delle facce (con il verso).
        std::map<int, int> surfaceOf;
        std::map<int, bool> flippedOf;
        for (FaceId f : body.faces()) {
            Interval u, v;
            faceBounds(body, f, u, v);
            bool flipped = false;
            surfaceOf[f.index] = writeIgesSurface(w, *body.face(f).surface, u, v, flipped);
            flippedOf[f.index] = flipped;
        }
        if (solids) {
            // Facce senza bordo (sfera, toro interi): IGES vuole almeno un loop per faccia. La sfera
            // si divide nell'equatore, il toro in due meta' lungo due meridiani (cerchi esatti).
            struct Synthetic {
                CurvePtr<3> curve;
                Vec3 point;
            };
            std::vector<Synthetic> synthetic;
            struct SplitFace {
                int face;
                std::vector<std::vector<std::pair<int, bool>>> pieces;  // per meta': loop di (edge sintetico, verso)
            };
            std::vector<SplitFace> splits;
            for (FaceId f : body.faces()) {
                const Face &face = body.face(f);
                bool bounded = false;
                for (LoopId l : face.loops) bounded = bounded || !body.loop(l).isolatedVertex.valid();
                if (bounded) continue;
                SplitFace split;
                split.face = f.index;
                const bool s0 = face.sense;
                if (face.surface->type() == SurfaceType::Sphere) {
                    const auto &sphere = static_cast<const SphericalSurface &>(*face.surface);
                    const Frame3 &fr = sphere.frame();
                    const auto circle = std::make_shared<Circle<3>>(fr.origin(), fr.xDir(), fr.yDir(), sphere.radius());
                    synthetic.push_back({circle, circle->point(0.0)});
                    const int e = int(synthetic.size()) - 1;
                    split.pieces = {{{e, s0}}, {{e, !s0}}};
                } else if (face.surface->type() == SurfaceType::Torus) {
                    const auto &torus = static_cast<const ToroidalSurface &>(*face.surface);
                    const Frame3 &fr = torus.frame();
                    const double R = torus.majorRadius(), r = torus.minorRadius();
                    const auto m0 = std::make_shared<Circle<3>>(fr.origin() + R * fr.xDir(), fr.xDir(), fr.zDir(), r);
                    const auto m1 = std::make_shared<Circle<3>>(fr.origin() - R * fr.xDir(), -fr.xDir(), fr.zDir(), r);
                    synthetic.push_back({m0, m0->point(0.0)});
                    synthetic.push_back({m1, m1->point(0.0)});
                    const int e0 = int(synthetic.size()) - 2, e1 = int(synthetic.size()) - 1;
                    // Due facce: meta' u in [0, pi] (bordi e0 al contrario, e1 dritto) e [pi, 2 pi].
                    split.pieces = {{{e0, !s0}, {e1, s0}}, {{e0, s0}, {e1, !s0}}};
                } else {
                    throw std::domain_error("IGES: faccia senza bordo su una superficie non gestita");
                }
                splits.push_back(split);
            }
            // Vertici ed edge in una lista ciascuno; loop, facce, shell, solido.
            IgesEntity vertexList;
            vertexList.type = 502;
            vertexList.form = 1;
            vertexList.subordinate = 1;
            std::map<int, int> vertexIndex;
            const std::vector<VertexId> vertices = body.vertices();
            vertexList.params.push_back(std::to_string(vertices.size()));
            vertexList.params.back() = std::to_string(vertices.size() + synthetic.size());
            for (VertexId v : vertices) {
                vertexIndex[v.index] = int(vertexIndex.size()) + 1;
                const Vec3 &p = body.vertex(v).point;
                vertexList.params.insert(vertexList.params.end(), {igesReal(p.x()), igesReal(p.y()), igesReal(p.z())});
            }
            for (const Synthetic &e : synthetic) vertexList.params.insert(vertexList.params.end(), {igesReal(e.point.x()), igesReal(e.point.y()), igesReal(e.point.z())});
            const int vertexDe = w.add(vertexList);
            IgesEntity edgeList;
            edgeList.type = 504;
            edgeList.form = 1;
            edgeList.subordinate = 1;
            std::map<int, int> edgeIndex;
            const std::vector<EdgeId> edges = body.edges();
            edgeList.params.push_back(std::to_string(edges.size() + synthetic.size()));
            for (EdgeId e : edges) {
                edgeIndex[e.index] = int(edgeIndex.size()) + 1;
                const Edge &edge = body.edge(e);
                const int curve = writeIgesCurve(w, *edge.curve, edge.range, true);
                edgeList.params.insert(edgeList.params.end(), {std::to_string(curve), std::to_string(vertexDe), std::to_string(vertexIndex.at(body.edgeStart(e).index)),
                                                               std::to_string(vertexDe), std::to_string(vertexIndex.at(body.edgeEnd(e).index))});
            }
            for (std::size_t k = 0; k < synthetic.size(); ++k) {
                const int curve = writeIgesCurve(w, *synthetic[k].curve, {0.0, kTwoPi}, true);
                const std::string vertex = std::to_string(vertices.size() + k + 1);
                edgeList.params.insert(edgeList.params.end(), {std::to_string(curve), std::to_string(vertexDe), vertex, std::to_string(vertexDe), vertex});
            }
            const int edgeDe = w.add(edgeList);
            const int firstSynthetic = int(edges.size()) + 1;
            std::map<int, std::vector<std::pair<int, bool>>> shellFaces;
            for (const SplitFace &split : splits) {
                const Face &face = body.face(FaceId(split.face));
                for (const auto &piece : split.pieces) {
                    std::vector<int> loops;
                    const bool againstSurface = face.sense == flippedOf.at(split.face);
                    for (const auto &[edgeIndexValue, faceSense] : piece) {
                        const bool sense = faceSense != againstSurface;
                        IgesEntity loop;
                        loop.type = 508;
                        loop.form = 1;
                        loop.subordinate = 1;
                        loop.params = {"1", "0", std::to_string(edgeDe), std::to_string(firstSynthetic + edgeIndexValue), sense ? "1" : "0", "0"};
                        loops.push_back(w.add(loop));
                    }
                    IgesEntity faceEntity;
                    faceEntity.type = 510;
                    faceEntity.form = 1;
                    faceEntity.subordinate = 1;
                    faceEntity.params = {std::to_string(surfaceOf.at(split.face)), std::to_string(loops.size()), "0"};
                    for (int l : loops) faceEntity.params.push_back(std::to_string(l));
                    shellFaces[face.shell.index].push_back({w.add(faceEntity), face.sense != flippedOf.at(split.face)});
                }
            }
            for (FaceId f : body.faces()) {
                const Face &face = body.face(f);
                bool bounded = false;
                for (LoopId l : face.loops) bounded = bounded || !body.loop(l).isolatedVertex.valid();
                if (!bounded) continue;
                std::vector<int> loops;
                std::vector<LoopId> ordered(face.loops.begin(), face.loops.end());
                bool outerFirst = false;
                if (!face.surface->isUPeriodic() && !face.surface->isVPeriodic() && ordered.size() > 0) {
                    // Il bordo esterno (area positiva nel verso della faccia) per primo.
                    for (std::size_t k = 0; k < ordered.size(); ++k)
                        if ((loopArea(body, ordered[k]) > 0.0) == face.sense) {
                            std::swap(ordered[0], ordered[k]);
                            outerFirst = true;
                            break;
                        }
                }
                // I loop IGES girano attorno alla normale della superficie: con la faccia rovesciata al contrario.
                const bool againstSurface = face.sense == flippedOf.at(f.index);
                for (LoopId l : ordered) {
                    if (body.loop(l).isolatedVertex.valid()) continue;
                    IgesEntity loop;
                    loop.type = 508;
                    loop.form = 1;
                    loop.subordinate = 1;
                    std::vector<FinId> fins = body.loopFins(l);
                    if (againstSurface) std::reverse(fins.begin(), fins.end());
                    loop.params.push_back(std::to_string(fins.size()));
                    for (FinId fin : fins)
                        loop.params.insert(loop.params.end(), {"0", std::to_string(edgeDe), std::to_string(edgeIndex.at(body.fin(fin).edge.index)),
                                                               body.fin(fin).sense != againstSurface ? "1" : "0", "0"});
                    loops.push_back(w.add(loop));
                }
                IgesEntity faceEntity;
                faceEntity.type = 510;
                faceEntity.form = 1;
                faceEntity.subordinate = 1;
                faceEntity.params = {std::to_string(surfaceOf.at(f.index)), std::to_string(loops.size()), outerFirst ? "1" : "0"};
                for (int l : loops) faceEntity.params.push_back(std::to_string(l));
                shellFaces[face.shell.index].push_back({w.add(faceEntity), face.sense != flippedOf.at(f.index)});
            }
            // Una shell per shell del body; la prima e' l'esterna, le altre vuoti (orientate al contrario).
            std::vector<int> shells;
            for (const auto &[shell, faces] : shellFaces) {
                IgesEntity s;
                s.type = 514;
                s.form = 1;
                s.subordinate = 1;
                s.params.push_back(std::to_string(faces.size()));
                for (const auto &[de, sense] : faces) s.params.insert(s.params.end(), {std::to_string(de), sense ? "1" : "0"});
                shells.push_back(w.add(s));
            }
            IgesEntity msbo;
            msbo.type = 186;
            msbo.color = colour;
            msbo.label = exchange.name;
            msbo.params = {std::to_string(shells.front()), "1", std::to_string(shells.size() - 1)};
            for (std::size_t k = 1; k < shells.size(); ++k) msbo.params.insert(msbo.params.end(), {std::to_string(shells[k]), "1"});
            msbo.params.insert(msbo.params.end(), {"0", "1", std::to_string(name)});
            w.add(msbo);
        } else {
            // Superfici limitate: per ogni faccia i bordi come curve composte nello spazio del modello.
            const auto trimmedFace = [&](int surface, const std::vector<int> &boundaries) {
                IgesEntity trimmed;
                trimmed.type = 144;
                trimmed.color = colour;
                trimmed.label = exchange.name;
                trimmed.params = {std::to_string(surface), "1", std::to_string(boundaries.size() - 1), std::to_string(boundaries.front())};
                for (std::size_t k = 1; k < boundaries.size(); ++k) trimmed.params.push_back(std::to_string(boundaries[k]));
                trimmed.params.insert(trimmed.params.end(), {"0", "1", std::to_string(name)});
                w.add(trimmed);
            };
            const auto boundaryOf = [&](int surface, const std::vector<std::pair<CurvePtr<3>, bool>> &curves) {
                IgesEntity composite;
                composite.type = 102;
                composite.subordinate = 1;
                composite.params.push_back(std::to_string(curves.size()));
                for (const auto &[curve, forward] : curves) composite.params.push_back(std::to_string(writeIgesCurve(w, *curve, {0.0, kTwoPi}, forward)));
                const int curve = w.add(composite);
                IgesEntity onSurface;
                onSurface.type = 142;
                onSurface.subordinate = 1;
                onSurface.params = {"0", std::to_string(surface), "0", std::to_string(curve), "2"};
                return w.add(onSurface);
            };
            for (FaceId f : body.faces()) {
                const Face &face = body.face(f);
                bool bounded = false;
                for (LoopId l : face.loops) bounded = bounded || !body.loop(l).isolatedVertex.valid();
                if (!bounded) {
                    // Sfera e toro interi divisi come nei solidi (i bordi antiorari attorno alla normale della superficie).
                    const int surface = surfaceOf.at(f.index);
                    if (face.surface->type() == SurfaceType::Sphere) {
                        const auto &sphere = static_cast<const SphericalSurface &>(*face.surface);
                        const Frame3 &fr = sphere.frame();
                        const auto circle = std::make_shared<Circle<3>>(fr.origin(), fr.xDir(), fr.yDir(), sphere.radius());
                        trimmedFace(surface, {boundaryOf(surface, {{circle, true}})});
                        trimmedFace(surface, {boundaryOf(surface, {{circle, false}})});
                    } else if (face.surface->type() == SurfaceType::Torus) {
                        const auto &torus = static_cast<const ToroidalSurface &>(*face.surface);
                        const Frame3 &fr = torus.frame();
                        const double R = torus.majorRadius(), r = torus.minorRadius();
                        const auto m0 = std::make_shared<Circle<3>>(fr.origin() + R * fr.xDir(), fr.xDir(), fr.zDir(), r);
                        const auto m1 = std::make_shared<Circle<3>>(fr.origin() - R * fr.xDir(), -fr.xDir(), fr.zDir(), r);
                        trimmedFace(surface, {boundaryOf(surface, {{m0, false}}), boundaryOf(surface, {{m1, true}})});
                        trimmedFace(surface, {boundaryOf(surface, {{m0, true}}), boundaryOf(surface, {{m1, false}})});
                    } else {
                        throw std::domain_error("IGES: faccia senza bordo su una superficie non gestita");
                    }
                    continue;
                }
                const bool reverse = face.sense == flippedOf.at(f.index);  // i loop antiorari attorno alla normale IGES
                std::vector<int> boundaries;
                std::vector<LoopId> ordered(face.loops.begin(), face.loops.end());
                for (std::size_t k = 0; k < ordered.size(); ++k)
                    if ((loopArea(body, ordered[k]) > 0.0) == face.sense) {
                        std::swap(ordered[0], ordered[k]);
                        break;
                    }
                for (LoopId l : ordered) {
                    if (body.loop(l).isolatedVertex.valid()) continue;
                    std::vector<FinId> fins = body.loopFins(l);
                    if (reverse) std::reverse(fins.begin(), fins.end());
                    IgesEntity composite;
                    composite.type = 102;
                    composite.subordinate = 1;
                    composite.params.push_back(std::to_string(fins.size()));
                    for (FinId fin : fins) {
                        const Edge &edge = body.edge(body.fin(fin).edge);
                        composite.params.push_back(std::to_string(writeIgesCurve(w, *edge.curve, edge.range, body.fin(fin).sense != reverse)));
                    }
                    const int curve = w.add(composite);
                    IgesEntity onSurface;
                    onSurface.type = 142;
                    onSurface.subordinate = 1;
                    onSurface.params = {"0", std::to_string(surfaceOf.at(f.index)), "0", std::to_string(curve), "2"};
                    boundaries.push_back(w.add(onSurface));
                }
                IgesEntity trimmed;
                trimmed.type = 144;
                trimmed.color = colour;
                trimmed.label = exchange.name;
                if (boundaries.empty()) {
                    trimmed.params = {std::to_string(surfaceOf.at(f.index)), "0", "0", "0"};
                } else {
                    trimmed.params = {std::to_string(surfaceOf.at(f.index)), "1", std::to_string(boundaries.size() - 1), std::to_string(boundaries.front())};
                    for (std::size_t k = 1; k < boundaries.size(); ++k) trimmed.params.push_back(std::to_string(boundaries[k]));
                }
                trimmed.params.insert(trimmed.params.end(), {"0", "1", std::to_string(name)});
                w.add(trimmed);
            }
        }
    }
    return w.file(options, maxCoordinate);
}

IgesReadResult readIges(const std::string &content, const IgesReadOptions &options) {
    using namespace detail;
    const IgesFile file(content);
    const IgesGeometry geometry(file);
    IgesReadResult result;
    const double tolerance = std::max(10.0 * file.resolution(), 1e-6);

    const unsigned workers = options.threads > 0 ? threadCount(options.threads) : std::min(8u, threadCount(0));
    struct Imported {
        ExchangeBody body;
        std::vector<std::string> notes;
        std::string failure;
        bool valid = false;
        std::exception_ptr unexpected;
    };
    const auto append = [&](Imported &item, const std::string &name) {
        if (item.unexpected) std::rethrow_exception(item.unexpected);
        result.notes.insert(result.notes.end(), item.notes.begin(), item.notes.end());
        if (item.valid) {
            item.body.name = name;
            result.bodies.push_back(std::move(item.body));
        } else result.notes.push_back(name + ": non ricostruito (" + item.failure + ")");
    };

    // Risultati isolati per corpo, raccolti nell'ordine del file.
    const std::vector<int> solids = file.ofType(186);
    std::vector<Imported> imported(solids.size());
    parallelFor(solids.size(), solids.size() >= 4 ? workers : 1u, [&](std::size_t i) {
        Imported &item = imported[i];
        try {
            const int de = solids[i];
            const Directory &msbo = file.at(de);
            std::string name = propertyName(file, msbo, std::size_t(3 + 2 * IgesFile::integer(msbo, 2)));
            item.body.name = name;
            try {
                RawModel model;
                std::map<std::pair<int, int>, int> vertexOf, edgeOf;
                const auto vertex = [&](int list, int index) {
                    const auto key = std::make_pair(list, index);
                    const auto it = vertexOf.find(key);
                    if (it != vertexOf.end()) return it->second;
                    const Directory &d = file.at(list);
                    Vec3 p = geometry.point(d, std::size_t(1 + 3 * (index - 1)));
                    if (d.transform > 0) p = geometry.transform(d.transform).applyToPoint(p);
                    model.points.push_back(p);
                    return vertexOf[key] = int(model.points.size()) - 1;
                };
                const auto edge = [&](int list, int index) {
                    const auto key = std::make_pair(list, index);
                    const auto it = edgeOf.find(key);
                    if (it != edgeOf.end()) return it->second;
                    const Directory &d = file.at(list);
                    const std::size_t at = std::size_t(1 + 5 * (index - 1));
                    const std::vector<IgesGeometry::Piece> pieces = geometry.curve(IgesFile::integer(d, at));
                    if (pieces.size() != 1) throw std::domain_error("IGES: edge con una curva composta");
                    RawEdge e;
                    e.curve = pieces[0].curve;
                    e.start = vertex(IgesFile::integer(d, at + 1), IgesFile::integer(d, at + 2));
                    e.end = vertex(IgesFile::integer(d, at + 3), IgesFile::integer(d, at + 4));
                    model.edges.push_back(e);
                    return edgeOf[key] = int(model.edges.size()) - 1;
                };
                const auto shell = [&](int shellDe, bool sense) {
                    const Directory &s = file.at(shellDe);
                    const int n = IgesFile::integer(s, 0);
                    for (int k = 0; k < n; ++k) {
                        const Directory &faceEntity = file.at(IgesFile::integer(s, std::size_t(1 + 2 * k)));
                        const bool faceOrientation = IgesFile::integer(s, std::size_t(2 + 2 * k), 1) != 0;
                        RawFace face;
                        bool flipped = false;
                        face.surface = geometry.surface(IgesFile::integer(faceEntity, 0), flipped);
                        face.sense = (faceOrientation != flipped) == sense;
                        const int loops = IgesFile::integer(faceEntity, 1);
                        for (int l = 0; l < loops; ++l) {
                            const Directory &loop = file.at(IgesFile::integer(faceEntity, std::size_t(3 + l)));
                            std::vector<RawFin> fins;
                            const int count = IgesFile::integer(loop, 0);
                            std::size_t at = 1;
                            for (int k2 = 0; k2 < count; ++k2) {
                                const int type = IgesFile::integer(loop, at), list = IgesFile::integer(loop, at + 1), index = IgesFile::integer(loop, at + 2);
                                const bool orientation = IgesFile::integer(loop, at + 3, 1) != 0;
                                const int curves = IgesFile::integer(loop, at + 4);
                                at += 5 + std::size_t(2 * std::max(0, curves));
                                if (type == 1) continue;  // vertice (polo)
                                fins.push_back({edge(list, index), orientation});
                            }
                            // I loop girano attorno alla normale della superficie: con la faccia rovesciata (e la shell) si girano.
                            if (faceOrientation != sense) {
                                std::reverse(fins.begin(), fins.end());
                                for (RawFin &fin : fins) fin.sense = !fin.sense;
                            }
                            if (!fins.empty()) face.loops.push_back(std::move(fins));
                        }
                        model.faces.push_back(std::move(face));
                    }
                };
                shell(IgesFile::integer(msbo, 0), IgesFile::integer(msbo, 1, 1) != 0);
                const int voids = IgesFile::integer(msbo, 2);
                for (int k = 0; k < voids; ++k) shell(IgesFile::integer(msbo, std::size_t(3 + 2 * k)), IgesFile::integer(msbo, std::size_t(4 + 2 * k), 1) != 0);
                ExchangeBody body;
                body.name = name;
                body.body = assembleBody(model, true, &item.notes);
                body.hasColor = colourOf(file, msbo, body.color);
                item.body = std::move(body);
                item.valid = true;
            } catch (const std::exception &failure) {
                item.failure = failure.what();
            }
        } catch (...) { item.unexpected = std::current_exception(); }
    });
    for (Imported &item : imported) {
        const std::string name = item.body.name.empty() ? "solido " + std::to_string(result.bodies.size() + 1) : item.body.name;
        append(item, name);
    }

    // Superfici limitate (144, 143): facce con i loro bordi, poi cucite (per nome:
    // le facce dello stesso corpo, cosi' corpi che si toccano restano separati).
    struct LooseGroup {
        RawModel model;
        bool hasColour = false;
        std::array<double, 3> colour{0, 0, 0};
    };
    std::map<std::string, LooseGroup> groups;
    const auto addBoundary = [&](RawModel &model, RawFace &face, const std::vector<IgesGeometry::Piece> &pieces) {
        std::vector<RawFin> fins;
        for (const IgesGeometry::Piece &p : pieces) {
            RawEdge e;
            e.curve = p.curve;
            e.hasRange = true;
            e.range = p.range;
            model.points.push_back(p.curve->point(p.range.lo));
            e.start = int(model.points.size()) - 1;
            model.points.push_back(p.curve->point(p.range.hi));
            e.end = int(model.points.size()) - 1;
            model.edges.push_back(e);
            fins.push_back({int(model.edges.size()) - 1, true});
        }
        if (!fins.empty()) face.loops.push_back(std::move(fins));
    };
    const auto boundaryCurve = [&](int de) -> std::vector<IgesGeometry::Piece> {
        const Directory &d = file.at(de);
        if (d.type == 142) {
            const int model = IgesFile::integer(d, 3);
            if (model > 0) return geometry.curve(model);
            throw std::domain_error("IGES: bordo solo nello spazio dei parametri (non gestito)");
        }
        return geometry.curve(de);
    };
    for (int type : {144, 143}) {
        for (int de : file.ofType(type)) {
            const Directory &d = file.at(de);
            try {
                std::string name = type == 144 ? propertyName(file, d, std::size_t(4 + IgesFile::integer(d, 2))) : std::string();
                LooseGroup &group = groups[name];
                RawFace face;
                bool flipped = false;
                if (type == 144) {
                    face.surface = geometry.surface(IgesFile::integer(d, 0), flipped);
                    const int n1 = IgesFile::integer(d, 1), n2 = IgesFile::integer(d, 2);
                    if (n1 != 0) {
                        addBoundary(group.model, face, boundaryCurve(IgesFile::integer(d, 3)));
                    } else if (face.surface->type() == SurfaceType::Sphere || face.surface->type() == SurfaceType::Torus) {
                        // Sfera o toro interi: la faccia senza bordo.
                    } else if (face.surface->type() == SurfaceType::BSpline) {
                        // Il bordo naturale della B-spline: le quattro isoparametriche.
                        const Interval u = face.surface->uDomain(), v = face.surface->vDomain();
                        std::vector<IgesGeometry::Piece> border{{face.surface->vIso(v.lo), u}, {face.surface->uIso(u.hi), v}};
                        border.push_back({reversedCurve(face.surface->vIso(v.hi)), {-u.hi, -u.lo}});
                        border.push_back({reversedCurve(face.surface->uIso(u.lo)), {-v.hi, -v.lo}});
                        addBoundary(group.model, face, border);
                    } else {
                        throw std::domain_error("IGES: superficie limitata senza bordo su una superficie illimitata");
                    }
                    for (int k = 0; k < n2; ++k) addBoundary(group.model, face, boundaryCurve(IgesFile::integer(d, std::size_t(4 + k))));
                } else {
                    face.surface = geometry.surface(IgesFile::integer(d, 1), flipped);
                    const int n = IgesFile::integer(d, 2);
                    for (int k = 0; k < n; ++k) {
                        const Directory &b = file.at(IgesFile::integer(d, std::size_t(3 + k)));
                        // 141: tipo, preferenza, superficie, N curve, (curva, verso, K, parametri...)
                        const int curves = IgesFile::integer(b, 3);
                        std::vector<IgesGeometry::Piece> pieces;
                        std::size_t at = 4;
                        for (int c = 0; c < curves; ++c) {
                            const std::vector<IgesGeometry::Piece> p = geometry.curve(IgesFile::integer(b, at));
                            const bool sense = IgesFile::integer(b, at + 1, 1) != 2;
                            const int k2 = IgesFile::integer(b, at + 2);
                            at += 3 + std::size_t(std::max(0, k2));
                            for (const IgesGeometry::Piece &piece : p) {
                                if (sense) pieces.push_back(piece);
                                else pieces.push_back({reversedCurve(piece.curve), {-piece.range.hi, -piece.range.lo}});
                            }
                        }
                        addBoundary(group.model, face, pieces);
                    }
                }
                face.sense = !flipped;
                double rgb[3];
                if (!group.hasColour && colourOf(file, d, rgb)) {
                    group.hasColour = true;
                    group.colour = {rgb[0], rgb[1], rgb[2]};
                }
                group.model.faces.push_back(std::move(face));
            } catch (const std::exception &failure) {
                result.notes.push_back("superficie " + std::to_string(de) + " saltata (" + failure.what() + ")");
            }
        }
    }
    struct Component {
        RawModel model;
        bool closed;
        std::string name;
        bool hasColour;
        std::array<double, 3> colour;
    };
    std::vector<Component> pending;
    int index = 0;
    for (auto &[groupName, group] : groups) {
        RawModel &loose = group.model;
        if (loose.faces.empty()) continue;
        sewModel(loose, tolerance);
        // Componenti connesse attraverso gli edge.
        std::vector<int> parent(loose.faces.size());
        for (std::size_t k = 0; k < parent.size(); ++k) parent[k] = int(k);
        std::function<int(int)> root = [&](int k) { return parent[std::size_t(k)] == k ? k : parent[std::size_t(k)] = root(parent[std::size_t(k)]); };
        std::map<int, int> firstFace;
        for (std::size_t f = 0; f < loose.faces.size(); ++f)
            for (const auto &loop : loose.faces[f].loops)
                for (const RawFin &fin : loop) {
                    const auto it = firstFace.find(fin.edge);
                    if (it == firstFace.end()) firstFace[fin.edge] = int(f);
                    else parent[std::size_t(root(int(f)))] = root(it->second);
                }
        std::map<int, std::vector<std::size_t>> components;
        for (std::size_t f = 0; f < loose.faces.size(); ++f) components[root(int(f))].push_back(f);
        int part = 0;
        for (const auto &[r, faces] : components) {
            RawModel piece;
            // Copia soltanto la topologia usata da questa componente.
            std::map<int, int> pointOf, edgeOf;
            const auto point = [&](int old) {
                const auto found = pointOf.find(old);
                if (found != pointOf.end()) return found->second;
                const int next = int(piece.points.size());
                piece.points.push_back(loose.points[std::size_t(old)]);
                pointOf[old] = next;
                return next;
            };
            std::map<int, int> uses;
            for (std::size_t f : faces) {
                piece.faces.push_back(loose.faces[f]);
                for (auto &loop : piece.faces.back().loops)
                    for (RawFin &fin : loop) {
                        const int old = fin.edge;
                        const auto found = edgeOf.find(old);
                        if (found == edgeOf.end()) {
                            RawEdge edge = loose.edges[std::size_t(old)];
                            edge.start = point(edge.start);
                            edge.end = point(edge.end);
                            fin.edge = int(piece.edges.size());
                            edgeOf[old] = fin.edge;
                            piece.edges.push_back(std::move(edge));
                        } else fin.edge = found->second;
                        ++uses[fin.edge];
                    }
            }
            bool closed = true;
            for (const auto &[edge, count] : uses) closed = closed && count == 2;
            // Una faccia senza bordo (sfera, toro interi) e' chiusa da sola.
            if (uses.empty()) closed = true;
            std::string name = groupName;
            if (name.empty()) name = (closed ? "solido " : "superficie ") + std::to_string(++index);
            else if (components.size() > 1) name += " (" + std::to_string(++part) + ")";
            pending.push_back({std::move(piece), closed, std::move(name), group.hasColour, group.colour});
        }
    }
    // I gruppi non servono piu': libera i vettori prima dell'assemblaggio.
    groups.clear();
    imported.clear();
    imported.resize(pending.size());
    parallelFor(pending.size(), pending.size() >= 4 ? workers : 1u, [&](std::size_t i) {
        Component &component = pending[i];
        RawModel &piece = component.model;
        const bool closed = component.closed;
        Imported &item = imported[i];
        try {
            try {
                Body body = assembleBody(piece, closed, &item.notes);
                bool reoriented = false;
                std::size_t k = 0;
                for (FaceId f : body.faces()) {
                    if (k >= piece.faces.size()) break;
                    const int sense = loopSense(body, f);
                    if (sense != 0 && (sense > 0) != body.face(f).sense) piece.faces[k].sense = !piece.faces[k].sense, reoriented = true;
                    ++k;
                }
                if (reoriented) body = assembleBody(piece, closed, nullptr);
                if (closed && roughVolume(body) < 0.0) {
                    for (RawFace &face : piece.faces) {
                        face.sense = !face.sense;
                        for (auto &loop : face.loops) {
                            std::reverse(loop.begin(), loop.end());
                            for (RawFin &fin : loop) fin.sense = !fin.sense;
                        }
                    }
                    body = assembleBody(piece, closed, nullptr);
                }
                ExchangeBody exchange;
                exchange.name = component.name;
                exchange.body = std::move(body);
                exchange.hasColor = component.hasColour;
                for (int k = 0; k < 3; ++k) exchange.color[k] = component.colour[std::size_t(k)];
                item.body = std::move(exchange);
                item.valid = true;
            } catch (const std::exception &failure) {
                item.failure = failure.what();
            }
        } catch (...) { item.unexpected = std::current_exception(); }
    });
    for (std::size_t i = 0; i < imported.size(); ++i) append(imported[i], pending[i].name);
    return result;
}

}
