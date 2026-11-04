#include "fk_loft_guide_constraints.h"
#include "fk_bspline_basis.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
std::string guideTag(const LoftGuideCondition &condition) {
    return condition.sourceIndex >= 0 ? " [[loft-guide=" + std::to_string(condition.sourceIndex) + "]]" : "";
}
Vec3 contactNormal(const LoftGuideCondition &condition, Vec3 point, double tolerance) {
    Vec3 normal; bool found = false;
    for (const auto &contact : condition.faces) {
        cancellationPoint();
        if (!contact.body || !contact.face.valid()) throw std::domain_error("faccia adiacente alla guida non valida");
        const auto &face = contact.body->face(contact.face);
        double nearest = 1e300;
        for (LoopId loop : face.loops) for (FinId fin : contact.body->loopFins(loop)) {
            const auto &edge = contact.body->edge(contact.body->fin(fin).edge);
            nearest = std::min(nearest, projectPoint(*edge.curve, point, edge.range).distance);
        }
        if (nearest > tolerance) continue;
        const auto uv = projectPoint(*face.surface, point);
        if (uv.distance > tolerance) continue;
        const auto candidate = face.surface->normal(uv.u, uv.v);
        if (!(norm(candidate) > 1e-12)) throw std::domain_error("normale nulla sulla faccia adiacente alla guida");
        if (found && norm(cross(normal, candidate)) > 1e-4)
            throw std::domain_error("facce adiacenti alla guida con tangenti incompatibili");
        normal = normalized(candidate); found = true;
    }
    if (!found) throw std::domain_error("la guida deve coincidere con il bordo delle facce adiacenti scelte");
    return normal;
}
Vec3 desiredDerivative(const BSplineSurface &surface, bool right, double v,
                       const LoftGuideCondition &condition, double scale) {
    try {
    const double u = right ? surface.uDomain().hi : surface.uDomain().lo;
    Vec3 jet[4]; surface.evaluate(u, v, 1, jet);
    const Vec3 natural = jet[Surface::derivativeIndex(1, 0, 1)];
    const Vec3 along = jet[Surface::derivativeIndex(0, 1, 1)];
    if (!(norm(natural) > 1e-12) || !(norm(along) > 1e-12))
        throw std::domain_error("tangente nulla lungo la guida con condizione trasversale");
    if (condition.crossMode == 1) {
        const Vec3 direction = normalized(condition.profileNormal);
        if (std::fabs(dot(direction, normalized(along))) > 1e-4)
            throw std::domain_error("la guida non giace nel piano del profilo scelto");
        return (condition.reverse ? -1.0 : 1.0) * norm(natural) * direction;
    }
    const Vec3 normal = contactNormal(condition, jet[0], 1e-6 * scale);
    if (std::fabs(dot(normal, normalized(along))) > 1e-4)
        throw std::domain_error("la tangente della guida non appartiene alla faccia adiacente");
    const Vec3 projected = natural - dot(natural, normal) * normal;
    if (norm(projected) <= 1e-9 * norm(natural))
        throw std::domain_error("direzione trasversale nulla sul piano tangente della faccia adiacente");
    return norm(natural) * normalized(projected);
    } catch (const std::exception &error) { throw std::domain_error(std::string(error.what()) + guideTag(condition)); }
}
// Lift cubico di Hermite sull'intero dominio U. La sua rappresentazione
// nella base B-spline usa i momenti simmetrici dei nodi (blossom): resta
// identica se il grado viene elevato o si inseriscono nodi lisci.
// Correggere solo la prima riga interna concentra invece la curvatura in
// una fascia sempre piu' stretta al crescere del grado della loft.
double transverseLift(const BSplineSurface &surface, int row, bool right) {
    const int degree = surface.uDegree();
    const auto domain = surface.uDomain();
    double e1 = 0, e2 = 0, e3 = 0;
    for (int j = 1; j <= degree; ++j) {
        const double x = (surface.uKnots()[row+j] - domain.lo) / domain.length();
        e3 += x * e2;
        e2 += x * e1;
        e1 += x;
    }
    const double m1 = e1 / degree;
    const double m2 = e2 / (double(degree) * (degree-1) / 2);
    const double m3 = e3 / (double(degree) * (degree-1) * (degree-2) / 6);
    return domain.length() * (right ? m3-m2 : m1-2*m2+m3);
}
std::vector<double> greville(const BSplineSurface &surface) {
    std::vector<double> result(surface.vPoleCount());
    const int degree = surface.vDegree(); const auto &knots = surface.vKnots();
    for (int i = 0; i < surface.vPoleCount(); ++i) {
        const double anchor = knots[i + 1];
        double offset = 0;
        for (int k = 1; k <= degree; ++k) offset += (knots[i + k] - anchor) / degree;
        result[i] = anchor + offset;
        if (i && result[i] <= result[i - 1]) throw std::domain_error("condizione della guida non risolta alla precisione numerica");
    }
    return result;
}
// Collocazione totalmente positiva alle ascisse di Greville: eliminazione
// nella sola banda q, senza formare una matrice densa per migliaia di righe.
std::vector<Vec3> interpolateField(const BSplineSurface &s, const std::vector<double> &parameters,
                                  std::vector<Vec3> values) {
    const int n = s.vPoleCount(), q = s.vDegree(), width = 2 * q + 1;
    std::vector<double> matrix(std::size_t(n) * width, 0.0), basis(q + 1);
    const auto at = [&](int row, int column) -> double & { return matrix[std::size_t(row) * width + column - row + q]; };
    for (int i = 0; i < n; ++i) {
        const int span = findSpan(s.vKnots(), q, n, parameters[i]);
        basisFunctionDerivatives(s.vKnots(), span, parameters[i], q, 0, basis.data());
        for (int j = 0; j <= q; ++j) {
            if (basis[j] == 0) continue;
            const int column = span - q + j;
            if (column < i - q || column > i + q)
                throw std::domain_error("condizione della guida non risolta alla precisione numerica");
            at(i, column) = basis[j];
        }
    }
    for (int i = 0; i < n; ++i) {
        cancellationPoint();
        if (std::fabs(at(i, i)) < 1e-14) throw std::domain_error("condizione della guida numericamente singolare");
        for (int row = i + 1; row <= std::min(n - 1, i + q); ++row) {
            const double ratio = at(row, i) / at(i, i);
            if (ratio == 0) continue;
            for (int col = i + 1; col <= std::min(n - 1, i + q); ++col) at(row, col) -= ratio * at(i, col);
            values[row] -= ratio * values[i];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        for (int col = i + 1; col <= std::min(n - 1, i + q); ++col) values[i] -= at(i, col) * values[col];
        values[i] /= at(i, i);
    }
    return values;
}
// I nodi completi della loft quintica condividono il punto ma non impongono
// da soli C1/C2. Un fit per collocazione senza questa ricucitura creerebbe
// salti di normale all'interno della superficie, pur rispettando il bordo.
void smoothFieldJoins(const std::vector<double> &knots, std::vector<Vec3> &field) {
    constexpr int q=5;
    for(std::size_t first=q+1;first+q+1<knots.size();) {
        const double knot=knots[first];
        const auto after=std::upper_bound(knots.begin()+std::ptrdiff_t(first),knots.end(),knot);
        const std::size_t last=std::size_t(after-knots.begin());
        if(last-first==q && knot>knots.front() && knot<knots.back()) {
            const int i=int(first)-1;
            const double left=knot-knots[i],leftBefore=knot-knots[i-1];
            const double right=knots[i+q+1]-knot,rightAfter=knots[i+q+2]-knot;
            const Vec3 dl=q*(field[i]-field[i-1])/left;
            const Vec3 dr=q*(field[i+1]-field[i])/right;
            const Vec3 cl=(q-1)*(dl-q*(field[i-1]-field[i-2])/leftBefore)/left;
            const Vec3 cr=(q-1)*(q*(field[i+2]-field[i+1])/rightAfter-dr)/right;
            const Vec3 derivative=.5*(dl+dr),second=.5*(cl+cr);
            field[i-1]=field[i]-left*derivative/q;
            field[i-2]=field[i-1]-leftBefore*(derivative-left*second/(q-1))/q;
            field[i+1]=field[i]+right*derivative/q;
            field[i+2]=field[i+1]+rightAfter*(derivative+right*second/(q-1))/q;
        }
        first=last;
    }
}
BSplineSurfacePtr refine(const BSplineSurface &s, const std::vector<double> &knots) {
    std::vector<Vec3> poles; std::vector<double> weights, refinedKnots; int count = 0;
    for (int u = 0; u < s.uPoleCount(); ++u) {
        std::vector<Vec3> row; std::vector<double> rowWeights;
        for (int v = 0; v < s.vPoleCount(); ++v) { row.push_back(s.pole(u,v)); rowWeights.push_back(s.weight(u,v)); }
        BSplineCurve<3> curve(s.vDegree(), s.vKnots(), row, rowWeights);
        for (double knot : knots) curve = curve.insertKnot(knot);
        refinedKnots = curve.knots(); count = curve.poleCount();
        poles.insert(poles.end(), curve.poles().begin(), curve.poles().end());
        for (int v = 0; v < count; ++v) weights.push_back(curve.weight(v));
    }
    return std::make_shared<BSplineSurface>(s.uDegree(), s.vDegree(), s.uKnots(), refinedKnots,
        s.uPoleCount(), count, poles, s.isRational() ? weights : std::vector<double>{});
}
}
BSplineSurfacePtr constrainLoftGuides(BSplineSurfacePtr original,
    const LoftGuideCondition *left, const LoftGuideCondition *right,
    const std::vector<double> &sections, double scale, int startOrder, int endOrder) {
    const LoftGuideCondition *conditions[2] = {left, right};
    if ((!left || !left->crossMode) && (!right || !right->crossMode)) return original;
    if (original->uDegree() < 3 || original->uPoleCount() < 4 || original->vDegree()!=5)
        throw std::domain_error("le condizioni trasversali delle guide richiedono una loft liscia");
    // La tangente U negli incontri e' gia' fissata dalla sezione esatta.
    for (int side = 0; side < 2; ++side) if (conditions[side] && conditions[side]->crossMode)
        for (std::size_t k = 0; k < sections.size(); ++k) {
            const double u = side ? original->uDomain().hi : original->uDomain().lo;
            Vec3 jet[4]; original->evaluate(u, sections[k], 1, jet);
            const auto natural = jet[Surface::derivativeIndex(1, 0, 1)];
            const auto wanted = desiredDerivative(*original, side, sections[k], *conditions[side], scale);
            if (dot(natural, wanted) <= 0 || norm(cross(normalized(natural), normalized(wanted))) > 1e-4)
                throw std::domain_error("condizione trasversale della guida incompatibile con la tangente della sezione [[loft-section=" + std::to_string(k) + "]]" + guideTag(*conditions[side]));
        }
    auto base = original; int failedSide=0;
    for (int attempt = 0; attempt < 9; ++attempt) {
        cancellationPoint();
        const int nu = base->uPoleCount(), nv = base->vPoleCount();
        std::vector<Vec3> poles(std::size_t(nu) * nv); std::vector<double> weights(poles.size());
        for (int u = 0; u < nu; ++u) for (int v = 0; v < nv; ++v) {
            poles[std::size_t(u) * nv + v] = base->pole(u,v); weights[std::size_t(u) * nv + v] = base->weight(u,v);
        }
        const auto parameters = greville(*base);
        for (int side = 0; side < 2; ++side) if (conditions[side] && conditions[side]->crossMode) {
            const int boundary = side ? nu - 1 : 0;
            const double u = side ? base->uDomain().hi : base->uDomain().lo;
            std::vector<Vec3> corrections; corrections.reserve(nv);
            for (double v : parameters) {
                Vec3 jet[4]; base->evaluate(u,v,1,jet);
                const Vec3 delta = desiredDerivative(*base, side, v, *conditions[side], scale)
                    - jet[Surface::derivativeIndex(1,0,1)];
                // Peso omogeneo della riga di bordo, stessa base V.
                const int span = findSpan(base->vKnots(), base->vDegree(), nv, v);
                std::vector<double> basis(base->vDegree() + 1);
                basisFunctionDerivatives(base->vKnots(),span,v,base->vDegree(),0,basis.data());
                double weight = 0;
                for (int j=0;j<=base->vDegree();++j) weight += basis[j]*base->weight(boundary,span-base->vDegree()+j);
                // Derivata del numeratore omogeneo; il lift conserva
                // punti e pesi su entrambi i bordi e il jet opposto.
                corrections.push_back(weight * delta);
            }
            auto field = interpolateField(*base, parameters, std::move(corrections));
            // Con condizioni sui profili non alterare neppure il jet V gia'
            // imposto: delta e derivate fino all'ordine richiesto sono nulle.
            for (int j=0;j<=startOrder;++j) field[j]=Vec3();
            for (int j=0;j<=endOrder;++j) field[nv-1-j]=Vec3();
            smoothFieldJoins(base->vKnots(),field);
            for (int u=1;u<nu-1;++u) {
                const double lift = transverseLift(*base,u,side);
                for (int v=0;v<nv;++v) poles[std::size_t(u)*nv+v] += lift*field[v]/base->weight(u,v);
            }
        }
        auto candidate = std::make_shared<BSplineSurface>(base->uDegree(),base->vDegree(),base->uKnots(),base->vKnots(),nu,nv,poles,
            base->isRational()?weights:std::vector<double>{});
        // Non usare una condizione di guida per deformare una sezione.
        for (std::size_t k=0;k<sections.size();++k) for(int sample=0;sample<=32;++sample) {
            const double u=original->uDomain().lo+original->uDomain().length()*sample/32.0;
            if(distance(candidate->point(u,sections[k]),original->point(u,sections[k]))>1e-9)
                throw std::domain_error("condizione trasversale della guida altera la sezione esatta [[loft-section="+std::to_string(k)+"]]");
        }
        std::vector<double> split;
        const auto breaks=base->vBreakpoints(base->vDomain());
        for(std::size_t k=1;k<breaks.size();++k) {
            bool failed=false;
            for(int side=0;side<2;++side) if(conditions[side] && conditions[side]->crossMode)
                for(int sample=0;sample<=8;++sample) {
                    const double v=breaks[k-1]+(breaks[k]-breaks[k-1])*sample/8.0;
                    const double u=side?base->uDomain().hi:base->uDomain().lo;
                    Vec3 jet[4];candidate->evaluate(u,v,1,jet);
                    const auto actual=jet[Surface::derivativeIndex(1,0,1)];
                    const auto wanted=desiredDerivative(*base,side,v,*conditions[side],scale);
                    if(norm(actual)<=1e-12 || dot(actual,wanted)<=0 || norm(cross(normalized(actual),normalized(wanted)))>1e-4) {failed=true;failedSide=side;}
                }
            if(failed) split.push_back((breaks[k-1]+breaks[k])/2);
        }
        if(split.empty())return candidate;
        if(base->vPoleCount()+int(split.size())>4096)break;
        base=refine(*base,split);
    }
    throw std::domain_error("impossibile rispettare la condizione trasversale della guida entro tolleranza" + guideTag(*conditions[failedSide]));
}
}
