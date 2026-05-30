#pragma once
#include "mxspp/frontend/ast.h"

#include <iosfwd>
#include <memory>
#include <string_view>

namespace mxs::frontend::parser {

    // Parse MXScript source into an AST. Returns nullptr on parse failure (a diagnostic
    // is written to std::cerr). source must outlive the call.
    std::unique_ptr<ast::TranslationUnit>
    parse_to_ast(std::string_view source, std::string_view source_name = "<input>");

    // Print an indented textual representation of an AST (for --dump-ast / debugging).
    void dump_ast(const ast::MXASTNode &node, std::ostream &os);

}// namespace mxs::frontend::parser
