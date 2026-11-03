#include "fk_loft_sections.h"
#include "fk_sketch_projection.h"
#include "fk_curve_ops.h"
#include "fk_hermite.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
void verifyConstruction(const Curve<3> &source, const Curve<3> &curve, const Interval &range, double tolerance) {
    const auto nodes = curve.breakpoints(range);
    for (std::size_t k = 1; k < nodes.size(); ++k) {
        cancellationPoint();
        const double lo = nodes[k - 1], span = nodes[k] - lo;
        for (double f : {0.0, .071, .211, .419, .577, .733, .907, 1.0}) {
            const double t = lo + f * span;
            const double error = distance(source.point(t), curve.point(t));
            if (!std::isfinite(error) || error > tolerance)
                throw std::domain_error("loft: sezione implicita fuori dal budget di costruzione 1e-9 mm");
        }
    }
}
CurvePtr<3> constructionCurve(const CurvePtr<3> &source, const Interval &range, double tolerance = kLoftSectionFitTolerance) {
    if (!source || !isImplicitSketchProjection(*source)) return source;
    const auto domain = source->domain();
    if (!range.isFinite() || !(range.length() > 0) || range.lo < domain.lo || range.hi > domain.hi)
        throw std::domain_error("loft: intervallo della sezione implicita non valido");
    // Adatta prima nel sistema della sorgente: traslazioni e rotazioni non
    // devono cambiare la suddivisione adattiva o creare nodi quasi coincidenti.
    // L'eventuale amplificazione del fit viene contabilizzata prima del calcolo.
    if (source->type() == CurveType::Transformed) {
        const auto &wrapper = static_cast<const TransformedCurve &>(*source);
        double scale = 1.0;
        if (!wrapper.transform().isSimilarity(&scale)) {
            double norm2 = 0.0;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    const double value = wrapper.transform().matrix(i, j);
                    norm2 += value * value;
                }
            scale = std::sqrt(norm2);
        }
        if (!std::isfinite(scale) || !(scale > 0))
            throw std::domain_error("loft: trasformazione della sezione non valida");
        auto basis = constructionCurve(wrapper.basis(), range, tolerance / std::max(1.0, scale));
        auto curve = std::make_shared<TransformedCurve>(std::move(basis), wrapper.transform());
        verifyConstruction(*source, *curve, range, tolerance);
        return curve;
    }
    if (source->type() == CurveType::Trimmed) {
        const auto &wrapper = static_cast<const TrimmedCurve<3> &>(*source);
        auto basis = constructionCurve(wrapper.basis(), range, tolerance);
        auto curve = std::make_shared<TrimmedCurve<3>>(std::move(basis), range.lo, range.hi);
        verifyConstruction(*source, *curve, range, tolerance);
        return curve;
    }
    auto breaks = source->breakpoints(range);
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(), [&](double t) {
        return !std::isfinite(t) || t < range.lo || t > range.hi;
    }), breaks.end());
    breaks.push_back(range.lo);
    breaks.push_back(range.hi);
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    const Vec3 origin = source->point(range.lo);
    const auto sample = [&](double t, bool left, Vec3 *out) {
        cancellationPoint();
        if (left || t == range.hi) source->evaluateLeft(t, 2, out);
        else source->evaluate(t, 2, out);
        out[0] -= origin; // sistema locale: evita perdita di precisione nella collocazione
    };
    const auto fit = fitQuinticRows(sample, 1, breaks, tolerance / 8, range.length() / 16);
    if (!std::isfinite(fit.error) || fit.error > tolerance / 8)
        throw std::domain_error("loft: sezione implicita fuori dal budget di costruzione 1e-9 mm");
    auto poles = fit.poles.front();
    for (auto &p : poles) p += origin;
    auto curve = std::make_shared<BSplineCurve<3>>(5, fit.knots, std::move(poles));
    // Controlla dopo assemblaggio e ritorno alle coordinate del modello.
    verifyConstruction(*source, *curve, range, tolerance);
    return curve;
}
}

std::vector<PathSegment> prepareLoftSectionPath(const std::vector<PathSegment> &input) {
    auto result = input;
    for (auto &piece : result) piece.curve = constructionCurve(piece.curve, piece.range);
    return result;
}

std::vector<LoftSection> prepareLoftSections(const std::vector<LoftSection> &input) {
    auto sections = input;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        cancellationPoint();
        auto &section = sections[i];
        try {
            if (!section.path.empty()) section.path = prepareLoftSectionPath(section.path);
            else {
                std::vector<PathSegment> spatial;
                bool implicit = false;
                for (const auto &piece : section.loop.segments) {
                    auto curve = embedCurve(piece.curve, section.frame);
                    implicit = implicit || isImplicitSketchProjection(*curve);
                    spatial.push_back({std::move(curve), piece.range});
                }
                if (implicit) section.path = prepareLoftSectionPath(spatial);
            }
        } catch (const std::domain_error &error) {
            throw std::domain_error(std::string(error.what()) + " [[loft-section=" + std::to_string(i) + "]]");
        }
    }
    return sections;
}
}
