#include "kernel/fk_constraint_linear.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

using namespace ForgeCad::Kernel::ConstraintLinear;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static Rows chain(int count, bool shuffled = false) {
    Rows rows;
    for (int i = 0; i < count; ++i) rows.push_back({{i,2.0},{i+1,-1.0}});
    if (shuffled) { std::mt19937 random(1299); std::shuffle(rows.begin(), rows.end(), random); }
    return rows;
}

struct Problem { std::vector<double> residuals, expected; };
static Problem problem(const Rows &rows, int columns) {
    Problem p;
    p.expected.assign(std::size_t(columns), 0);
    // x=J^T*z: soluzione di norma minima nota anche nei sistemi sottodeterminati.
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (const auto &entry : rows[i]) p.expected[entry.column] += entry.value * std::sin(.31 * double(i+1));
    for (const auto &row : rows) {
        double r = 0;
        for (const auto &entry : row) r -= entry.value * p.expected[entry.column];
        p.residuals.push_back(r);
    }
    return p;
}

// Riferimento indipendente: matrice globale J*J^T, eliminazione di Gauss
// con pivot parziale, poi -J^T*z. Non usa dot, componenti o Cholesky produttivi.
static std::vector<double> reference(const Rows &rows, const std::vector<double> &r, int columns) {
    const std::size_t m = rows.size();
    std::vector<double> a(m*m, 0), b = r, x(std::size_t(columns), 0);
    double trace = 0;
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < m; ++j) {
            for (const auto &u : rows[i]) for (const auto &v : rows[j])
                if (u.column == v.column) a[i*m+j] += u.value * v.value;
            if (i == j) trace += a[i*m+j];
        }
    const double mu = 1e-12 * std::max(trace / std::max<std::size_t>(m,1), 1e-300);
    for (std::size_t i = 0; i < m; ++i) a[i*m+i] += mu;
    for (std::size_t k = 0; k < m; ++k) {
        std::size_t pivot = k;
        for (std::size_t i = k+1; i < m; ++i)
            if (std::abs(a[i*m+k]) > std::abs(a[pivot*m+k])) pivot = i;
        require(std::isfinite(a[pivot*m+k]) && a[pivot*m+k] != 0, "pivot riferimento non valido");
        for (std::size_t j = k; j < m; ++j) std::swap(a[k*m+j], a[pivot*m+j]);
        std::swap(b[k], b[pivot]);
        for (std::size_t i = k+1; i < m; ++i) {
            const double ratio = a[i*m+k] / a[k*m+k];
            for (std::size_t j = k+1; j < m; ++j) a[i*m+j] -= ratio * a[k*m+j];
            b[i] -= ratio * b[k];
        }
    }
    for (std::size_t i = m; i-- > 0;) {
        for (std::size_t j = i+1; j < m; ++j) b[i] -= a[i*m+j] * b[j];
        b[i] /= a[i*m+i];
    }
    for (std::size_t i = 0; i < m; ++i)
        for (const auto &entry : rows[i]) x[entry.column] -= entry.value * b[i];
    return x;
}

static void near(const std::vector<double> &actual, const std::vector<double> &expected, double tolerance) {
    require(actual.size() == expected.size(), "dimensione soluzione");
    for (std::size_t i = 0; i < actual.size(); ++i)
        require(std::isfinite(actual[i]) && std::abs(actual[i]-expected[i]) <= tolerance * std::max(1.0,std::abs(expected[i])),
                "passo diverso dal riferimento indipendente");
}

static Statistics check(const Rows &rows, int columns, bool knownSolution = true) {
    const auto original = rows;
    const auto p = problem(rows, columns);
    std::vector<double> step, dense;
    Statistics stats, denseStats;
    require(minimumNormStep(rows, p.residuals.data(), columns, step, &stats), "fattorizzazione automatica fallita");
    require(minimumNormStep(rows, p.residuals.data(), columns, dense, &denseStats, StepMode::DenseComponents), "riferimento locale fallito");
    near(step, reference(rows,p.residuals,columns), 1e-7);
    near(step,dense,1e-7);
    if (knownSolution) near(step,p.expected,1e-7);
    require(stats.components == denseStats.components && stats.largest == denseStats.largest
        && stats.nonZeros == denseStats.nonZeros, "partizione invariata");
    require(rows.size() == original.size(), "input immutato");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        require(rows[i].size() == original[i].size(), "input immutato");
        for (std::size_t j = 0; j < rows[i].size(); ++j)
            require(rows[i][j].column == original[i][j].column && rows[i][j].value == original[i][j].value, "input immutato");
    }
    return stats;
}

static void tests() {
    check({},0);
    check(Rows(3),4);
    Rows independent;
    for (int i = 0; i < 64; ++i) independent.push_back({{i,1.0}});
    auto stats = check(independent,64);
    require(stats.components == 64 && stats.bandedComponents == 0 && stats.matrixEntries == 64, "componenti piccole conservate");
    stats = check(chain(128),129);
    require(stats.components == 1 && stats.bandedComponents == 1 && stats.reorderedComponents == 0
        && stats.matrixEntries == 256, "catena ordinata a banda uno");
    stats = check(chain(128,true),129);
    require(stats.bandedComponents == 1 && stats.reorderedComponents == 1 && stats.matrixEntries == 256, "catena disordinata ricondotta a banda uno");
    Rows fill;
    for (int i = 0; i < 96; ++i) fill.push_back({{i,2.0},{i+2,-.3},{i+3,.4}});
    stats = check(fill,99);
    require(stats.bandedComponents == 1 && stats.reorderedComponents == 0 && stats.matrixEntries == 96*4,
        "banda tre con fill-in durante Cholesky");
    auto redundant = chain(96,true);
    redundant.push_back(redundant[0]); redundant.push_back(redundant[43]);
    require(check(redundant,97).bandedComponents == 1, "righe ridondanti regolarizzate a banda");
    Rows wide;
    for (int i = 0; i < 64; ++i) wide.push_back({{0,1.0},{i+1,2.0}});
    stats = check(wide,65);
    require(stats.denseComponents == 1 && stats.bandedComponents == 0 && stats.matrixEntries == 64*64, "clique resta densa");
    auto mixed = chain(64,true);
    for (auto row : chain(40)) { for (auto &entry : row) { entry.column += 65; entry.value *= .001; } mixed.push_back(row); }
    mixed.push_back({}); mixed.push_back({{106,100.0}});
    stats = check(mixed,107,false);
    require(stats.components == 4 && stats.bandedComponents == 2, "componenti miste e mu globale");
    std::mt19937 random(44);
    for (int repeat = 0; repeat < 24; ++repeat) {
        Rows rows = chain(40,true);
        for (auto &row : rows) for (auto &entry : row) entry.value *= .5 + double(random()%100)/100.;
        // Archi aggiuntivi, fill-in e bande intermedie, senza cambiare le tolleranze.
        for (int i = 0; i < 6; ++i) {
            int a = int(random()%41), b = int(random()%41);
            if (a > b) std::swap(a,b);
            if (a != b) rows.push_back({{a,1.0},{b,-.3}});
        }
        check(rows,41);
    }
    std::vector<double> step;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double residual = 1;
    require(!minimumNormStep({{{0,nan}}},&residual,1,step), "NaN rifiutato");
    auto invalidBand = chain(40);
    invalidBand[20][0].value = nan;
    std::vector<double> invalidResiduals(40,1);
    require(!minimumNormStep(invalidBand,invalidResiduals.data(),41,step), "NaN rifiutato anche dopo ripiego dalla banda");
    std::cout << "constraint-linear: PASS (banda, riordino, fill-in, ridondanza, scale, ripiego e input)\n";
}

static void benchmark() {
    std::cout << std::setprecision(12) << "{\"protocol\":\"5 ripetizioni alternate, mediana; verifica fuori timer; stessa regolarizzazione\",\"cases\":[";
    bool first = true;
    for (int count : {128,384,768}) for (bool shuffled : {false,true}) {
        const auto rows = chain(count,shuffled);
        const auto p = problem(rows,count+1);
        std::vector<double> times[2];
        Statistics stats[2];
        for (int repeat = -1; repeat < 5; ++repeat) for (int pass = 0; pass < 2; ++pass) {
            const int dense = (std::max(repeat,0)%2)^pass;
            std::vector<double> step;
            const auto start = std::chrono::steady_clock::now();
            const bool ok = minimumNormStep(rows,p.residuals.data(),count+1,step,&stats[dense],
                dense ? StepMode::DenseComponents : StepMode::Automatic);
            const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            require(ok,"benchmark: fattorizzazione fallita"); near(step,p.expected,1e-7);
            if (repeat >= 0) times[dense].push_back(ms);
        }
        if (!first) std::cout << ',';
        first = false;
        for (auto &v : times) std::sort(v.begin(),v.end());
        std::cout << "{\"rows\":" << count << ",\"shuffled\":" << (shuffled?"true":"false")
            << ",\"dense_ms\":" << times[1][2] << ",\"band_ms\":" << times[0][2]
            << ",\"dense_entries\":" << stats[1].matrixEntries << ",\"band_entries\":" << stats[0].matrixEntries
            << ",\"reordered\":" << stats[0].reorderedComponents << ",\"dense_samples_ms\":[";
        for (int i=0;i<5;++i) std::cout << (i?",":"") << times[1][i];
        std::cout << "],\"band_samples_ms\":[";
        for (int i=0;i<5;++i) std::cout << (i?",":"") << times[0][i];
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--benchmark") benchmark();
        else if (argc == 1) tests();
        else throw std::runtime_error("uso: forgecad_constraint_linear_tests [--benchmark]");
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
