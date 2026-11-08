#include "fk_loft_knots.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
std::vector<double> unite(const std::vector<double> &a, const std::vector<double> &b) {
    std::vector<double> result;
    std::set_union(a.begin(),a.end(),b.begin(),b.end(),std::back_inserter(result));
    return result;
}
BSplineCurve<3> refine(BSplineCurve<3> curve, const std::vector<double> &knots) {
    for (auto first = knots.begin(); first != knots.end();) {
        const double t = *first;
        const auto last = std::upper_bound(first,knots.end(),t);
        const int wanted = int(last-first); first = last;
        if (t > curve.domain().lo && t < curve.domain().hi && curve.multiplicity(t) < wanted)
            curve = curve.insertKnot(t,wanted-curve.multiplicity(t));
    }
    return curve;
}
void verifyRounding(const BSplineCurve<3> &before, const BSplineCurve<3> &after) {
    const auto knots = unite(before.knots(),after.knots());
    const auto a = refine(before,knots), b = refine(after,knots);
    double scale = 1., minimumWeight = std::numeric_limits<double>::infinity();
    for (int i = 0; i < a.poleCount(); ++i) {
        scale = std::max({scale,norm(a.poles()[i]),norm(b.poles()[i])});
        minimumWeight = std::min({minimumWeight,a.weight(i),b.weight(i)});
    }
    // Stessa base positiva: limite dal poligono di controllo della differenza
    // omogenea, poi divisione per il minimo peso. Budget di arrotondamento
    // sulle coordinate, distinto dai budget di fit o di connessione.
    const double budget = 256*std::numeric_limits<double>::epsilon()*scale;
    for (int i = 0; i < a.poleCount(); ++i) {
        const double numerator = norm(a.weight(i)*a.poles()[i]-b.weight(i)*b.poles()[i]);
        const double weight = std::abs(a.weight(i)-b.weight(i));
        if ((numerator+scale*weight)/minimumWeight > budget)
            throw std::domain_error("loft: nodi vicini delle guide distinguono un dettaglio oltre l'arrotondamento");
    }
}
}

std::vector<double> harmonizeLoftGuideKnots(std::vector<BSplineCurvePtr<3>> &curves,
                                           const std::vector<double> &anchors) {
    if (anchors.size() < 2 || !(anchors.front() < anchors.back())
        || !std::is_sorted(anchors.begin(),anchors.end()))
        throw std::invalid_argument("loft: nodi delle sezioni non validi");
    struct Entry { double value; int curve; std::size_t first, last; };
    std::vector<Entry> entries;
    const auto append = [&](const std::vector<double> &knots, int source) {
        for (std::size_t first = 0; first < knots.size();) {
            const std::size_t last = std::size_t(std::upper_bound(knots.begin()+first,knots.end(),knots[first])-knots.begin());
            entries.push_back({knots[first],source,first,last}); first = last;
        }
    };
    append(anchors,-1);
    std::vector<std::vector<double>> mapped(curves.size());
    for (std::size_t i = 0; i < curves.size(); ++i) if (curves[i]) {
        mapped[i] = curves[i]->knots(); append(mapped[i],int(i));
    }
    std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b) {
        return a.value < b.value || (a.value == b.value && a.curve < b.curve);
    });
    const double domainScale = std::max({std::abs(anchors.front()),std::abs(anchors.back()),anchors.back()-anchors.front()});
    const double parameterRounding = 64*std::numeric_limits<double>::epsilon()*domainScale;
    for (std::size_t first = 0; first < entries.size();) {
        std::size_t last = first+1;
        double representative = entries[first].value;
        std::set<int> sources{entries[first].curve};
        while (last < entries.size() && entries[last].value-entries[first].value <= parameterRounding
               && !sources.count(entries[last].curve)) {
            sources.insert(entries[last].curve);
            if (entries[last].curve == -1) representative = entries[last].value;
            ++last;
        }
        for (std::size_t j = first; j < last; ++j) if (entries[j].curve >= 0) {
            const auto &entry = entries[j];
            std::fill(mapped[entry.curve].begin()+entry.first,mapped[entry.curve].begin()+entry.last,representative);
        }
        first = last;
    }
    auto adjusted = curves;
    std::vector<double> common = anchors;
    for (std::size_t i = 0; i < curves.size(); ++i) if (curves[i]) {
        const auto &curve = *curves[i];
        if (mapped[i] != curve.knots()) {
            auto next = std::make_shared<BSplineCurve<3>>(curve.degree(),mapped[i],curve.poles(),curve.weights());
            verifyRounding(curve,*next); adjusted[i] = std::move(next);
        }
        common = unite(common,adjusted[i]->knots());
    }
    curves = std::move(adjusted);
    return common;
}
}
