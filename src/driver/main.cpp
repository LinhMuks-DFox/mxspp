#include "mxspp/core/MXError.h"
#include "mxspp/core/MXPopulationManager.h"

int main() {
    mxs::core::MXError *error[21] = {
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },
        new mxs::core::MXError{ "SyntaxError", "This is a messange", false },

    };
    // std::print("{}\n", error);
    std::print("{}\n", mxs::core::MXPopulationManager::get_manager());

    for (int i = 0; i < 21; i++) {
        delete error[i];
        if (i % 5 == 0) std::print("{}\n", mxs::core::MXPopulationManager::get_manager());
    }
    return 0;
}