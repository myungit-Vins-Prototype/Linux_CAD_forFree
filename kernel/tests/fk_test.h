#ifndef FORGECAD_FK_TEST_H
#define FORGECAD_FK_TEST_H

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

// Micro framework di test: FK_TEST registra una funzione, i controlli
// contano i fallimenti senza interrompere il test.
namespace fktest {

struct TestCase {
    const char *name;
    void (*function)();
};

std::vector<TestCase> &registry();
void reportFailure(const char *file, int line, const std::string &message);

struct Registrar {
    Registrar(const char *name, void (*function)()) { registry().push_back({name, function}); }
};

}

#define FK_TEST(name)                                                   \
    static void name();                                                 \
    static const fktest::Registrar name##_registrar(#name, name);       \
    static void name()

#define FK_CHECK(condition)                                                              \
    do {                                                                                 \
        if (!(condition)) fktest::reportFailure(__FILE__, __LINE__, "FK_CHECK(" #condition ")"); \
    } while (0)

#define FK_CHECK_NEAR(actual, expected, tolerance)                                                   \
    do {                                                                                             \
        const double fk_actual = (actual), fk_expected = (expected), fk_tolerance = (tolerance);     \
        if (!(std::fabs(fk_actual - fk_expected) <= fk_tolerance)) {                                 \
            std::ostringstream fk_stream;                                                            \
            fk_stream.precision(17);                                                                 \
            fk_stream << #actual << " = " << fk_actual << ", atteso " << fk_expected                 \
                      << " (differenza " << std::fabs(fk_actual - fk_expected) << " > " << fk_tolerance << ")"; \
            fktest::reportFailure(__FILE__, __LINE__, fk_stream.str());                              \
        }                                                                                            \
    } while (0)

#define FK_CHECK_THROWS(expression)                                                            \
    do {                                                                                       \
        bool fk_thrown = false;                                                                \
        try { (void)(expression); } catch (...) { fk_thrown = true; }                          \
        if (!fk_thrown) fktest::reportFailure(__FILE__, __LINE__, "nessuna eccezione da " #expression); \
    } while (0)

#endif
