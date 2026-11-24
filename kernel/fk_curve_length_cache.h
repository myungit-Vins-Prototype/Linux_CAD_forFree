#ifndef FORGECAD_FK_CURVE_LENGTH_CACHE_H
#define FORGECAD_FK_CURVE_LENGTH_CACHE_H

#include "fk_curve_algo.h"
#include "fk_quadrature.h"
#include "fk_timing.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>

// Riuso interno e locale degli integrali di lunghezza, senza approssimare la
// parametrizzazione. Le curve prese in prestito devono restare immutabili e
// vive per tutta la cache; non condividerla fra thread. Stessi breakpoint,
// errore per tratto, quadratura e somme di arcLength. Limite in voci, non byte.
namespace ForgeCad::Kernel::detail {

// Per un loft le curve delle sezioni sono immutabili e restano vive.
// Cache limitata degli integrali sugli stessi intervalli e con identica
// tolleranza assoluta (bit del double). Nessuna tabella approssimata di s(t).
class CurveLengthCache {
    struct Key {
        const Curve<3> *curve;
        std::uint64_t lo, hi, tolerance;
        bool operator<(const Key &other) const {
            if (curve != other.curve) return std::less<const Curve<3> *>{}(curve, other.curve);
            return std::tie(lo, hi, tolerance) < std::tie(other.lo, other.hi, other.tolerance);
        }
    };
    std::map<Key, double> values_;
    std::size_t maximumEntries_;
public:
    struct Statistics { std::size_t requests = 0, hits = 0, integrations = 0, peak = 0, clears = 0; };
    const Statistics &statistics() const { return statistics_; }
private:
    Statistics statistics_;
    static Key key(const Curve<3> &curve, const Interval &range, double tolerance) {
        Key result{&curve, 0, 0, 0};
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        std::memcpy(&result.lo, &range.lo, sizeof(double));
        std::memcpy(&result.hi, &range.hi, sizeof(double));
        std::memcpy(&result.tolerance, &tolerance, sizeof(double));
        return result;
    }
    void put(const Key &k, double value) {
        if (values_.size() >= maximumEntries_) { values_.clear(); ++statistics_.clears; }
        values_.emplace(k, value);
        statistics_.peak = std::max(statistics_.peak, values_.size());
    }
public:
    explicit CurveLengthCache(std::size_t maximumEntries = 8192) : maximumEntries_(maximumEntries) {
        if (!maximumEntries) throw std::invalid_argument("CurveLengthCache: limite nullo");
    }
    CurveLengthCache(const CurveLengthCache &) = delete;
    CurveLengthCache &operator=(const CurveLengthCache &) = delete;
    ~CurveLengthCache() {
        if (timingEnabled())
            std::fprintf(stderr, "[loft] lunghezze: %zu richieste, %zu riusi, %zu integrali, picco %zu voci\n",
                         statistics_.requests, statistics_.hits, statistics_.integrations, statistics_.peak);
    }
    double get(const Curve<3> &curve, const Interval &range, double tolerance) {
        cancellationPoint();
        ++statistics_.requests;
        if (!range.isFinite() || !(range.lo <= range.hi))
            throw std::invalid_argument("intervallo non finito o vuoto");
        if (!std::isfinite(range.length())) throw std::domain_error("arcLength: ampiezza non finita");
        const Key whole = key(curve, range, tolerance);
        const auto cached = values_.find(whole);
        if (cached != values_.end()) { ++statistics_.hits; return cached->second; }
        const auto speed = [&curve](double t) { return arcLengthSpeed(curve, t); };
        const auto breaks = curve.breakpoints(range);
        if (breaks.empty() || breaks.front() != range.lo || breaks.back() != range.hi)
            throw std::domain_error("arcLength: estremi dei tratti non validi");
        for (std::size_t i = 0; i < breaks.size(); ++i) {
            cancellationPoint();
            if (!std::isfinite(breaks[i]) || breaks[i] < range.lo || breaks[i] > range.hi
                || (i > 0 && breaks[i] < breaks[i - 1]))
                throw std::domain_error("arcLength: suddivisione non valida");
        }
        const double spanTolerance = tolerance / std::max<std::size_t>(1, breaks.size() - 1);
        double length = 0.0;
        // Stessi tratti, stessa tolleranza per tratto, stesso ordine di somme
        // di arcLength. Un prefisso nuovo riusa soltanto i tratti identici.
        for (std::size_t span = 0; span + 1 < breaks.size(); ++span) {
            cancellationPoint();
            if (!(breaks[span + 1] > breaks[span])) continue;
            const Key local = key(curve, {breaks[span], breaks[span + 1]}, spanTolerance);
            const auto found = values_.find(local);
            if (found != values_.end()) { ++statistics_.hits; length += found->second; }
            else {
                const double value = integrate(speed, breaks[span], breaks[span + 1], spanTolerance);
                if (!std::isfinite(value)) throw std::domain_error("arcLength: integrale non finito");
                ++statistics_.integrations; put(local, value); length += value;
            }
            if (!std::isfinite(length)) throw std::domain_error("arcLength: lunghezza non finita");
        }
        cancellationPoint();
        put(whole, length);
        return length;
    }
};

}
#endif
