#include "mxspp/frontend/parser.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() == 2 && args[0] == "--dump-ast") {
        std::ifstream file(args[1]);
        if (!file) {
            std::cerr << "error: cannot open " << args[1] << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        const std::string source = ss.str();

        auto tu = mxs::frontend::parser::parse_to_ast(source, args[1]);
        if (!tu) {
            std::cerr << "error: failed to parse " << args[1] << "\n";
            return 1;
        }
        mxs::frontend::parser::dump_ast(*tu, std::cout);
        return 0;
    }

    std::cout << "mxs (MXScript)\n"
              << "  usage: mxs --dump-ast <file.mxs>\n";
    return 0;
}
