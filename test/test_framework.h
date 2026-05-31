#pragma once
// Minimal, dependency-free unit-test harness for mxspp.
//   MX_TEST(name) { ... CHECK(cond); ... }   // auto-registers a test case
//   int main() { return mxtest::run_all(); }
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace mxtest {
    struct Case {
        std::string name;
        std::function<void()> fn;
    };
    inline std::vector<Case> &registry() {
        static std::vector<Case> r;
        return r;
    }
    inline int &failures() {
        static int f = 0;
        return f;
    }
    inline int &checks() {
        static int c = 0;
        return c;
    }
    struct Registrar {
        Registrar(std::string n, std::function<void()> f) {
            registry().push_back({ std::move(n), std::move(f) });
        }
    };
    inline void check(bool cond, const std::string &msg, const char *file, int line) {
        ++checks();
        if (!cond)
            std::cerr << "    FAIL: " << msg << "  (" << file << ":" << line << ")\n",
                    ++failures();
    }
    inline int run_all() {
        int failed_cases = 0;
        for (auto &c : registry()) {
            const int before = failures();
            c.fn();
            const bool ok = failures() == before;
            std::cout << (ok ? "[ ok ] " : "[FAIL] ") << c.name << "\n";
            failed_cases += ok ? 0 : 1;
        }
        std::cout << "\n"
                  << registry().size() << " cases, " << checks() << " checks, "
                  << failures() << " failures (" << failed_cases << " cases failed)\n";
        return failures() == 0 ? 0 : 1;
    }
}// namespace mxtest

#define MX_TEST(name)                                                                    \
    static void name();                                                                  \
    static ::mxtest::Registrar mx_reg_##name(#name, name);                               \
    static void name()
#define CHECK(cond) ::mxtest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) ::mxtest::check((cond), (msg), __FILE__, __LINE__)
