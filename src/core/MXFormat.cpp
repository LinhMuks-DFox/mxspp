#include "mxspp/core/MXFormat.h"
#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXString.h"

#include <cstdint>
#include <cstdio>
#include <string>

// ============================================================================================
// MXFormat — the stdio text-output slice (progress12). `format` is a pure string-building engine
// (no IO), so it is portable / self-hostable later; `print`/`println` are the only IO here.
// ============================================================================================
namespace {
    using mxs::builtin::MXArrayList;
    using mxs::builtin::MXFloat;
    using mxs::builtin::MXString;
    using mxs::core::MXObject;

    // A parsed `[[fill]align][width][.precision][?]` spec (D-FORMAT v1 subset).
    struct Spec {
        char fill = ' ';
        char align = '<';// v1 default: left for every type
        int width = 0;
        bool hasPrec = false;
        int prec = 0;
        bool useRepr = false;// a trailing '?' selects the developer form repr()
        bool ok = true;
    };

    bool is_align(char c) { return c == '<' || c == '>' || c == '^'; }

    Spec parse_spec(std::string s) {
        Spec sp;
        if (!s.empty() && s.back() == '?') {
            sp.useRepr = true;
            s.pop_back();
        }
        std::size_t i = 0;
        // [[fill]align]: a fill char is only recognized when it precedes an align char.
        if (s.size() >= 2 && is_align(s[1])) {
            sp.fill = s[0];
            sp.align = s[1];
            i = 2;
        } else if (!s.empty() && is_align(s[0])) {
            sp.align = s[0];
            i = 1;
        }
        bool anyW = false;
        int w = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
            w = w * 10 + (s[i] - '0');
            anyW = true;
        }
        if (anyW) sp.width = w;
        if (i < s.size() && s[i] == '.') {
            ++i;
            int p = 0;
            bool anyP = false;
            for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
                p = p * 10 + (s[i] - '0');
                anyP = true;
            }
            sp.hasPrec = true;
            sp.prec = p;
            if (!anyP) sp.ok = false;// "." with no digits
        }
        if (i != s.size()) sp.ok = false;// leftover junk in the spec
        return sp;
    }

    // Fixed-precision float rendering (two-pass snprintf, no fixed buffer).
    std::string render_float(double v, int prec) {
        const int need = std::snprintf(nullptr, 0, "%.*f", prec, v);
        if (need <= 0) return std::string{ };
        std::string s(static_cast<std::size_t>(need), '\0');
        std::snprintf(s.data(), s.size() + 1, "%.*f", prec, v);
        return s;
    }

    std::string render_value(const MXObject *o, const Spec &sp) {
        if (!o) return "nil";
        if (sp.hasPrec) {
            if (const auto *f = dynamic_cast<const MXFloat *>(o))
                return render_float(f->value(), sp.prec);
            // precision on a non-float is ignored in v1 (string truncation deferred)
        }
        return sp.useRepr ? o->repr() : o->str();
    }

    std::string pad(std::string s, const Spec &sp) {
        const int n = static_cast<int>(s.size());
        if (sp.width <= n) return s;
        const auto extra = static_cast<std::size_t>(sp.width - n);
        switch (sp.align) {
            case '>':
                return std::string(extra, sp.fill) + s;
            case '^': {
                const std::size_t l = extra / 2;
                return std::string(l, sp.fill) + s + std::string(extra - l, sp.fill);
            }
            case '<':
            default:
                return s + std::string(extra, sp.fill);
        }
    }
}// namespace

extern "C" {

MXObject *mxs_format(MXObject *fmtObj, MXObject *argsObj) {
    const auto *fmt = dynamic_cast<const MXString *>(fmtObj);
    if (!fmt)
        return new mxs::core::MXError("TypeError", "format expects a string template");
    auto *args = dynamic_cast<MXArrayList *>(argsObj);
    const std::int64_t nargs = args ? static_cast<std::int64_t>(args->size()) : 0;

    const std::string &f = fmt->value();
    std::string out;
    out.reserve(f.size());
    std::int64_t autoIdx = 0;

    for (std::size_t i = 0; i < f.size();) {
        const char c = f[i];
        if (c == '{') {
            if (i + 1 < f.size() && f[i + 1] == '{') {// "{{" -> "{"
                out.push_back('{');
                i += 2;
                continue;
            }
            // A field: read its body up to the matching '}'.
            std::size_t j = i + 1;
            std::string body;
            bool closed = false;
            for (; j < f.size(); ++j) {
                if (f[j] == '}') {
                    closed = true;
                    break;
                }
                body.push_back(f[j]);
            }
            if (!closed)
                return new mxs::core::MXError("ValueError",
                                              "format: unmatched '{' in template");
            // Split `index : spec`.
            const std::size_t colon = body.find(':');
            const std::string idxPart =
                    colon == std::string::npos ? body : body.substr(0, colon);
            const std::string specStr =
                    colon == std::string::npos ? std::string{ } : body.substr(colon + 1);
            std::int64_t idx = 0;
            if (idxPart.empty()) {
                idx = autoIdx++;
            } else {
                for (const char d : idxPart) {
                    if (d < '0' || d > '9')
                        return new mxs::core::MXError("ValueError",
                                                      "format: bad field index '" +
                                                              idxPart + "'");
                    idx = idx * 10 + (d - '0');
                }
            }
            if (idx < 0 || idx >= nargs)
                return new mxs::core::MXError("IndexError",
                                              "format: field index out of range");
            const Spec sp = parse_spec(specStr);
            if (!sp.ok)
                return new mxs::core::MXError("ValueError", "format: bad format spec '" +
                                                                    specStr + "'");
            out += pad(render_value(args->get(idx), sp), sp);
            i = j + 1;
            continue;
        }
        if (c == '}') {
            if (i + 1 < f.size() && f[i + 1] == '}') {// "}}" -> "}"
                out.push_back('}');
                i += 2;
                continue;
            }
            return new mxs::core::MXError("ValueError", "format: stray '}' in template");
        }
        out.push_back(c);
        ++i;
    }
    return new MXString(out);
}

void mxs_print(MXObject *argsObj) {
    auto *args = dynamic_cast<MXArrayList *>(argsObj);
    if (!args) {// not a list (defensive): print the single value
        if (argsObj) std::fprintf(stdout, "%s", argsObj->str().c_str());
        return;
    }
    for (std::size_t i = 0; i < args->size(); ++i) {
        if (i) std::fputc(' ', stdout);
        const MXObject *e = args->get(static_cast<std::int64_t>(i));
        std::fprintf(stdout, "%s", e ? e->str().c_str() : "nil");
    }
}

void mxs_println(MXObject *argsObj) {
    mxs_print(argsObj);
    std::fputc('\n', stdout);
}

void mxs_repl_echo(MXObject *o) {
    if (!o || dynamic_cast<const mxs::builtin::MXNil *>(o))
        return;// skip nil (Python-style)
    std::fprintf(stdout, "%s\n", o->repr().c_str());
}

}// extern "C"
