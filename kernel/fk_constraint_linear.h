#ifndef FORGECAD_FK_CONSTRAINT_LINEAR_H
#define FORGECAD_FK_CONSTRAINT_LINEAR_H

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

// Algebra del solver: Jacobiano sparso e fattorizzazioni delle sole componenti
// accoppiate. Nessuna dipendenza dalla GUI o modifica delle tolleranze geometriche.
namespace ForgeCad::Kernel::ConstraintLinear {

struct Entry { int column; double value; };
using Row = std::vector<Entry>;
using Rows = std::vector<Row>;

inline std::vector<std::vector<int>> components(const Rows &rows, int columns) {
    std::vector<int> parent(rows.size()), owner(std::size_t(columns), -1);
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (std::size_t r = 0; r < rows.size(); ++r)
        for (const Entry &entry : rows[r]) {
            int &previous = owner[std::size_t(entry.column)];
            if (previous < 0) previous = int(r);
            else parent[root(int(r))] = root(previous);
        }
    std::vector<std::vector<int>> result;
    std::vector<int> group(rows.size(), -1);
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const int key = root(int(r));
        if (group[key] < 0) { group[key] = int(result.size()); result.emplace_back(); }
        result[std::size_t(group[key])].push_back(int(r));
    }
    return result;
}

inline double dot(const Row &a, const Row &b) {
    double result = 0;
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].column < b[j].column) ++i;
        else if (b[j].column < a[i].column) ++j;
        else { result += a[i].value * b[j].value; ++i; ++j; }
    }
    return result;
}

struct Statistics { int components = 0, largest = 0; std::size_t nonZeros = 0, matrixEntries = 0; };

struct RowSpace { int rank = 0; std::vector<double> projection; };
inline RowSpace rowSpace(const Rows &rows,int columns) {
    RowSpace result;result.projection.resize(std::size_t(columns),0);
    double largest=0;
    for(const auto &row:rows)for(const auto &entry:row)largest=std::max(largest,std::abs(entry.value));
    for(const auto &group:components(rows,columns)) {
        std::vector<int> active;
        for(int row:group)for(const auto &entry:rows[row])active.push_back(entry.column);
        std::sort(active.begin(),active.end());active.erase(std::unique(active.begin(),active.end()),active.end());
        std::vector<std::vector<double>> basis;
        for(int row:group) {
            std::vector<double> value(active.size(),0);
            for(const auto &entry:rows[row])value[std::lower_bound(active.begin(),active.end(),entry.column)-active.begin()]=entry.value;
            double original=0;for(double v:value)original+=v*v;original=std::sqrt(original);
            if(!(original>1e-12*std::max(largest,1e-300)))continue;
            for(int pass=0;pass<2;++pass)for(const auto &b:basis) {
                double d=0;for(std::size_t k=0;k<value.size();++k)d+=b[k]*value[k];
                for(std::size_t k=0;k<value.size();++k)value[k]-=d*b[k];
            }
            double remaining=0;for(double v:value)remaining+=v*v;remaining=std::sqrt(remaining);
            if(remaining<=1e-8*original)continue;
            for(double &v:value)v/=remaining;
            basis.push_back(std::move(value));
        }
        result.rank+=int(basis.size());
        for(std::size_t k=0;k<active.size();++k)
            for(const auto &b:basis)result.projection[active[k]]+=b[k]*b[k];
    }
    return result;
}

// Stesso passo -J^T(J J^T + mu I)^-1 r del solver originale. Il parametro
// mu e' globale: partizionare non cambia la regolarizzazione delle componenti.
inline bool minimumNormStep(const Rows &rows, const double *residuals, int columns,
                            std::vector<double> &step, Statistics *statistics = nullptr) {
    double trace = 0;
    for (const Row &row : rows) trace += dot(row, row);
    const double mu = 1e-12 * std::max(trace / std::max<std::size_t>(rows.size(), 1), 1e-300);
    const auto groups = components(rows, columns);
    Statistics stats;
    stats.components = int(groups.size());
    for (const Row &row : rows) stats.nonZeros += row.size();
    step.assign(std::size_t(columns), 0);
    for (const auto &group : groups) {
        const std::size_t m = group.size();
        stats.largest = std::max(stats.largest, int(m));
        stats.matrixEntries += m * m;
        std::vector<double> lower(m * m, 0), y(m), z(m);
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j <= i; ++j) {
                double sum = dot(rows[group[i]], rows[group[j]]) + (i == j ? mu : 0);
                for (std::size_t k = 0; k < j; ++k) sum -= lower[i*m+k] * lower[j*m+k];
                if (i == j) {
                    if (!(sum > 0) || !std::isfinite(sum)) return false;
                    lower[i*m+i] = std::sqrt(sum);
                } else lower[i*m+j] = sum / lower[j*m+j];
            }
        for (std::size_t i = 0; i < m; ++i) {
            double sum = residuals[group[i]];
            for (std::size_t k = 0; k < i; ++k) sum -= lower[i*m+k] * y[k];
            y[i] = sum / lower[i*m+i];
        }
        for (std::size_t i = m; i-- > 0;) {
            double sum = y[i];
            for (std::size_t k = i+1; k < m; ++k) sum -= lower[k*m+i] * z[k];
            z[i] = sum / lower[i*m+i];
        }
        for (std::size_t i = 0; i < m; ++i)
            for (const Entry &entry : rows[group[i]]) step[entry.column] -= entry.value * z[i];
    }
    if (statistics) *statistics = stats;
    return std::all_of(step.begin(), step.end(), [](double value) { return std::isfinite(value); });
}

}
#endif
