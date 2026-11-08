#include "cad_circular_pattern_fit.h"

#include "fk_bernstein.h"
#include "fk_nurbs.h"
#include "fk_precision.h"
#include "fk_topology.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

namespace ForgeCad {
namespace {
using namespace Kernel;

// Per ogni tratto si limita |Ha Wb - Hb Wa| / (Wa Wb) con l'inviluppo
// convesso dei coefficienti di Bernstein. Vale sull'intero intervallo,
// anche fra i punti usati soltanto per proporre il numero di istanze.
double borderGap(const std::vector<BSplineCurve<3>> &a, const std::vector<BSplineCurve<3>> &b,
                 const Transform3 &rotation, bool reverse) {
    if (a.empty() || a.size() != b.size()) return std::numeric_limits<double>::infinity();
    double gap = 0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        const auto &first = a[k], &second = b[reverse ? b.size() - 1 - k : k];
        std::vector<double> wa, wb;
        std::vector<Vec3> pa, pb;
        for (int i = 0; i < first.poleCount(); ++i) {
            wa.push_back(first.weight(i));
            pa.push_back(rotation.applyToPoint(first.poles()[std::size_t(i)]));
        }
        for (int i = 0; i < second.poleCount(); ++i) {
            const int j = reverse ? second.poleCount() - 1 - i : i;
            wb.push_back(second.weight(j));
            pb.push_back(second.poles()[std::size_t(j)]);
        }
        const double minimumA = *std::min_element(wa.begin(), wa.end()), minimumB = *std::min_element(wb.begin(), wb.end());
        if (!(minimumA > 0) || !(minimumB > 0)) return std::numeric_limits<double>::infinity();
        const double denominator = minimumA * minimumB;
        if (!(denominator > 0) || !std::isfinite(denominator)) return std::numeric_limits<double>::infinity();
        const BernsteinPolynomial weightA(wa), weightB(wb);
        double squared = 0;
        for (int coordinate = 0; coordinate < 3; ++coordinate) {
            std::vector<double> ha, hb;
            for (std::size_t i = 0; i < pa.size(); ++i) ha.push_back(pa[i][coordinate] * wa[i]);
            for (std::size_t i = 0; i < pb.size(); ++i) hb.push_back(pb[i][coordinate] * wb[i]);
            const auto numerator = BernsteinPolynomial(ha) * weightB - BernsteinPolynomial(hb) * weightA;
            double bound = 0;
            for (double coefficient : numerator.coefficients()) {
                if (!std::isfinite(coefficient)) return std::numeric_limits<double>::infinity();
                bound = std::max(bound, std::fabs(coefficient));
            }
            squared += bound * bound;
        }
        gap = std::max(gap, std::sqrt(squared) / denominator);
    }
    return gap;
}
}

bool fitCircularPatternBorders(const ForgeBody &source, const Kernel::Vec3 &origin, const Kernel::Vec3 &axis,
                               int &count, double &maximumGap, QString *error) {
    using namespace Kernel;
    count = 0;
    maximumGap = 0;
    if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!source || !source->isSheet() || source->shells().size() != 1)
        return fail(QStringLiteral("Scegli un singolo corpo di superficie da ripetere."));
    if (!std::isfinite(norm(axis)) || !(norm(axis) > angularResolution())
        || !std::isfinite(norm(origin))) return fail(QStringLiteral("L'asse di rotazione non e' valido."));
    const Vec3 direction = normalized(axis);
    struct Border { EdgeId id; std::vector<BSplineCurve<3>> pieces; };
    std::vector<Border> borders;
    try {
        for (EdgeId id : source->edges()) {
            if (!source->isLaminar(id)) continue;
            const auto &edge = source->edge(id);
            try { borders.push_back({id, standardBezierPieces(*edge.curve, edge.range)}); }
            catch (const std::domain_error &) { /* Curva non convertibile: non certificabile. */ }
        }
        int found = 0;
        double foundGap = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < borders.size(); ++i) for (std::size_t j = i + 1; j < borders.size(); ++j) {
            const auto &a = source->edge(borders[i].id), &b = source->edge(borders[j].id);
            // I contorni devono percorrere il giunto in versi opposti.
            const bool reverse = a.forward.valid() == b.forward.valid();
            std::set<int> candidates;
            for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                Vec3 first = a.curve->point(a.range.lo + fraction * a.range.length()) - origin;
                Vec3 second = b.curve->point(b.range.lo + (reverse ? 1 - fraction : fraction) * b.range.length()) - origin;
                first -= dot(first, direction) * direction;
                second -= dot(second, direction) * direction;
                if (!(norm(first) > linearResolution()) || !(norm(second) > linearResolution())) continue;
                const double angle = std::fabs(std::atan2(dot(direction, cross(first, second)), dot(first, second)));
                if (!(angle > angularResolution())) continue;
                const double proposed = kTwoPi / angle;
                if (proposed >= 1.5 && proposed < 500.5) candidates.insert(int(std::lround(proposed)));
            }
            for (int candidate : candidates) for (double sign : {-1.0, 1.0}) {
                const auto rotation = Transform3::rotation(origin, direction, sign * kTwoPi / candidate);
                const double gap = borderGap(borders[i].pieces, borders[j].pieces, rotation, reverse);
                // Budget scelto a priori: risoluzione geometrica del kernel.
                // Non si allarga usando lo scarto misurato o tolleranze importate.
                if (gap <= linearResolution() && candidate >= found) {
                    if (candidate > found || gap < foundGap) foundGap = gap;
                    found = candidate;
                }
            }
        }
        if (!found) return fail(QStringLiteral("Nessuna chiusura esatta trovata fra 2 e 500 istanze. Verifica l'asse e che i bordi siano sovrapponibili con un passo di 360°/N; il numero non viene arrotondato forzando la chiusura."));
        count = found;
        maximumGap = foundGap;
        return true;
    } catch (const std::exception &failure) {
        return fail(QStringLiteral("Calcolo dai bordi non riuscito: %1").arg(QString::fromUtf8(failure.what())));
    }
}
}
