#include "mxspp/_std/io.h"

#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"

#include <cstdint>
#include <cstdio>

// ============================================================================================
// std.io — the stdio text-output slice (progress17: relocated from src/core/MXFormat.cpp).
// `print`/`println` are the IO leaves; `mxs_repl_echo` is the REPL value echo.
// ============================================================================================
namespace {
    using mxs::builtin::MXArrayList;
    using mxs::core::MXObject;
}// namespace

extern "C" {

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
