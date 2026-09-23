#include "fk_test.h"

#include <cstring>
#include <exception>
#include <iostream>

namespace fktest {
namespace {
int failures = 0;
constexpr int kMaxReportsPerTest = 10;
}

std::vector<TestCase> &registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char *file, int line, const std::string &message) {
    if (++failures <= kMaxReportsPerTest) std::cerr << "    " << file << ":" << line << ": " << message << "\n";
    else if (failures == kMaxReportsPerTest + 1) std::cerr << "    (altri fallimenti omessi)\n";
}

}

// Uso: forgekernel_tests [filtro]  (esegue i test il cui nome contiene il filtro)
int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const fktest::TestCase &test : fktest::registry()) {
        if (filter && !std::strstr(test.name, filter)) continue;
        ++run;
        fktest::failures = 0;
        try {
            test.function();
        } catch (const std::exception &error) {
            fktest::reportFailure(__FILE__, __LINE__, std::string("eccezione: ") + error.what());
        } catch (...) {
            fktest::reportFailure(__FILE__, __LINE__, "eccezione sconosciuta");
        }
        const bool ok = fktest::failures == 0;
        if (!ok) ++failed;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << test.name;
        if (!ok) std::cout << " (" << fktest::failures << " controlli falliti)";
        std::cout << std::endl;
    }
    std::cout << run - failed << "/" << run << " test superati" << std::endl;
    return failed == 0 ? 0 : 1;
}
