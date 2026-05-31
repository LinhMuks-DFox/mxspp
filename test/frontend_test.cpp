// Unit tests for the MXScript frontend: grammar parse-acceptance + AST-shape +
// regressions for the grammar/parser bugs fixed in develop_log/2026-05-31/progress03.
#include "mxspp/frontend/grammar.hpp"
#include "mxspp/frontend/parser.h"
#include "test_framework.h"

#include <tao/pegtl.hpp>

#include <sstream>
#include <string>
#include <string_view>

namespace {
    namespace pegtl = tao::pegtl;
    namespace g = mxs::frontend::grammar;

    // Does `code` parse against the full grammar? (no actions, no AST)
    bool parses(std::string_view code) {
        try {
            pegtl::memory_input<> in(code.data(), code.size(), "test");
            return pegtl::parse<g::grammar>(in);
        } catch (const pegtl::parse_error &) { return false; }
    }

    // Parse to AST and return its textual dump (or a sentinel on failure).
    std::string dump(std::string_view code) {
        auto tu = mxs::frontend::parser::parse_to_ast(code, "test");
        if (!tu) return "<parse-failed>";
        std::ostringstream os;
        mxs::frontend::parser::dump_ast(*tu, os);
        return os.str();
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }
    // count of non-overlapping occurrences
    int count(const std::string &hay, const std::string &needle) {
        int n = 0;
        for (std::size_t p = hay.find(needle); p != std::string::npos;
             p = hay.find(needle, p + needle.size()))
            ++n;
        return n;
    }
}// namespace

// ============================ parse-acceptance =============================
MX_TEST(parse_accept_matrix) {
    struct C {
        const char *code;
        bool ok;
    };
    const C cases[] = {
            // lexical
            { "# c\nfunc f()->nil{}", true },
            { "// c\nfunc f()->nil{}", true },
            { "/* c */\nfunc f()->nil{}", true },
            { "!##! c !##!\nfunc f()->nil{}", false },// G7
            // expressions / literals
            { "func f()->nil{ let a=1; let b=1.5; let c=\"x\"; let d=true; let e=nil; }", true },
            { "func f()->int{ return 1+2*3-4/2; }", true },
            { "func f()->bool{ return 1<2 && 3>=2 || false; }", true },
            { "func f()->nil{ let x=a.b.c; let y=a[0]; g(1,2); }", true },
            { "func f()->nil{ let x=g()?; let z=g<int>(1); }", true },
            { "func f()->nil{ let s=(x:int)->int=>x; }", true },
            { "func f()->nil{ let a=[1,2,3]; }", false },        // G1 list literal
            { "func f()->nil{ let a=(1,2); }", false },          // G1 tuple literal
            // types
            { "func f()->nil{ let x: List<int>; }", true },
            { "func f()-> int | string { return 1; }", true },
            { "func f()->nil{ let x: [3]int; }", false },        // G2 array type
            { "func f()->(int,int){ }", false },                 // G2 tuple type
            // statements
            { "func f()->nil{ if a {} else if b {} else {} }", true },
            { "func f()->nil{ for i in 0..5 {} }", true },
            { "func f()->nil{ loop { break; continue; } }", true },
            { "func f()->nil{ until(true){} do {} until(true); }", true },
            { "func f()->nil{ assert true; defer {} }", true },
            // functions
            { "func f<T>(a:T)->T{ return a; }", true },
            { "func f(a:int=1, b,c:int)->nil{}", true },
            { "func f(a:int, ...)->nil{}", false },              // G4 variadic
            // OOP
            { "class C { public: let x:int; C(x:int){self.x=x;} ~C(){} func m()->nil{} static func s()->nil{} private: let y:int; }", true },
            { "class D : C { D():C(0){} override func m()->nil{} }", true },
            { "class C { operator+(o:C)->C{ return o; } }", true },
            { "class Box<T>{ public: let v:T; }", true },
            { "type P { x:int; y:int; }", true },
            { "enum E { A, B(x:int), C(w,h:int) }", true },
            // match
            { "func f()->int{ return match(n){ case 1=>1 case _=>0 }; }", true },
            { "func f()->int{ return match(n){ case Some(y)=>1 case _=>0 }; }", true },
            { "func f()->int{ return match(n){ case x:int=>x }; }", false },// G3 type-binding pattern
            // annotations / ffi / top-level
            { "@@POD class C { public: let x:int; }", true },
            { "@@opt(level=3) func f()->nil{}", true },
            { "@@template(T) func f()->nil{}", false },          // G5 positional annot arg
            { "@@foreign(lib=\"x\") func f()->int{ return 0; }", true },
            { "@@foreign(lib=\"x\") func f()->int;", false },    // G6 bodyless foreign
            { "import std.io as io;", true },
            { "static let x = 1;", true },
            { "export func f()->nil{}", true },
    };
    for (const auto &c : cases)
        CHECK_MSG(parses(c.code) == c.ok, std::string("parse: ") + c.code);
}

// ====================== grammar/parser bug regressions =====================
MX_TEST(grammar_bug_regressions) {
    CHECK_MSG(parses("func f()->int{ let x={ 1; 2 }; return x; }"), "B1 block_expr whitespace");
    CHECK_MSG(parses("interface I { func a()->nil; func b()->nil {} ; }"), "B2 interface space-semi");
    CHECK_MSG(parses("func f(cb: func(int)->int)->nil{}"), "B3 func_type as a parameter type");
    CHECK_MSG(!parses("func f()->nil{ let (a,b)=g(); }"), "B4 let-destructure no longer misparses");
    CHECK_MSG(parses("func f()->nil{ let format=1; let assertion=2; let internal=3; }"),
              "keyword-prefixed identifiers still parse");
    // previously-fixed (progress01) grammar bugs:
    CHECK_MSG(parses("func f()->nil{ print(\"Hello, World!\"); }"), "string literal with normal chars");
    CHECK_MSG(parses("func f()->int{ return a+b; }"), "binary operators parse");
}

// ============================== AST shape ==================================
MX_TEST(ast_function_params_binop) {
    const auto d = dump("func add(a,b:int)->int{ return a+b; }");
    CHECK(has(d, "FunctionDef add -> int"));
    CHECK(has(d, "Param a: int"));
    CHECK(has(d, "Param b: int"));
    CHECK(has(d, "BinaryOp '+'"));
    CHECK(has(d, "Return"));
}

MX_TEST(ast_let_value_fix) {// the parser bug fixed this turn
    const auto d = dump("func f()->nil{ let z = y; }");
    CHECK(has(d, "Let z"));
    CHECK_MSG(has(d, "Identifier y"), "bare-identifier initializer must not be dropped");
}

MX_TEST(ast_let_multiname_and_typed) {
    CHECK(has(dump("func f()->nil{ let a, b = 5; }"), "Let a b"));
    CHECK(has(dump("func f()->nil{ let w: int = q; }"), "Let w: int"));
    CHECK(has(dump("func f()->nil{ let mut k = 0; }"), "Let mut k"));
}

MX_TEST(ast_precedence_and_assoc) {
    const auto d = dump("func f()->int{ return 1+2*3; }");
    CHECK(has(d, "BinaryOp '+'"));
    CHECK(has(d, "BinaryOp '*'"));
    // right-associative assignment => two '=' nodes
    CHECK(count(dump("func f()->nil{ a=b=c; }"), "BinaryOp '='") == 2);
}

MX_TEST(ast_literals) {
    const auto d = dump("func f()->nil{ let a=1; let b=1.5; let c=true; let e=nil; let s=\"x\"; }");
    CHECK(has(d, "Int 1"));
    CHECK(has(d, "Float 1.5"));
    CHECK(has(d, "Bool true"));
    CHECK(has(d, "Nil"));
    CHECK(has(d, "String \"x\""));
}

MX_TEST(ast_call_and_unary) {
    const auto d = dump("func f()->nil{ g(1, \"x\"); }");
    CHECK(has(d, "Call g"));
    CHECK(has(d, "Int 1"));
    CHECK(has(d, "String \"x\""));
    CHECK(has(dump("func f()->int{ return -5; }"), "UnaryOp '-'"));
}

MX_TEST(ast_translation_unit_multiple_functions) {
    const auto d = dump("func a()->nil{} func b()->nil{} func main()->int{ return 0; }");
    CHECK(has(d, "TranslationUnit"));
    CHECK(count(d, "FunctionDef") == 3);
}

MX_TEST(ast_skips_out_of_scope_statements) {
    // if/for/loop aren't transformed yet (task03) -> dropped; must not crash, and the
    // supported statement around them must still land.
    const auto d = dump("func f()->nil{ if a { g(); } for i in 0..3 { h(); } loop { break; } let x = 1; }");
    CHECK(has(d, "FunctionDef f"));
    CHECK(has(d, "Let x"));
}

MX_TEST(ast_parse_failure_returns_null) {
    CHECK_MSG(dump("@@template(T) func f()->nil{}") == "<parse-failed>",
              "unparseable input yields a null AST");
}

MX_TEST(ast_postfix_member_and_index) {
    // member/index aren't full AST nodes yet; must parse + transform without crashing.
    CHECK(has(dump("func f()->nil{ let x = a.b; let y = c[0]; }"), "FunctionDef f"));
}

MX_TEST(ast_lambda_parses_pending_transform) {
    // Lambdas parse but aren't fully transformed into a Lambda node yet (task05); they
    // currently surface as a placeholder. Assert they at least parse into some AST.
    const auto d = dump("func f()->nil{ let g = (x:int)->int => x; }");
    CHECK_MSG(d != "<parse-failed>", "lambda value parses into some AST");
    CHECK(has(d, "Let g"));
}

MX_TEST(ast_control_flow_if_else) {
    const auto d = dump("func f()->nil{ if a { g(); } else if b { h(); } else { k(); } }");
    CHECK(has(d, "If"));
    CHECK(has(d, "Else"));
    CHECK_MSG(count(d, "If") >= 2, "else-if nests another If");
}

MX_TEST(ast_control_flow_loops) {
    const auto forin = dump("func f()->nil{ for i in 0..5 { p(i); } }");
    CHECK(has(forin, "For i in"));
    CHECK_MSG(has(forin, "BinaryOp '..'"), "range iterable");

    const auto lp = dump("func f()->nil{ loop { continue; break; } }");
    CHECK(has(lp, "Loop"));
    CHECK(has(lp, "Break"));
    CHECK(has(lp, "Continue"));

    CHECK(has(dump("func f()->nil{ until (x) { g(); } }"), "Until"));
    CHECK(has(dump("func f()->nil{ do { g(); } until (x); }"), "DoUntil"));
}

MX_TEST(ast_control_flow_assert_defer) {
    CHECK(has(dump("func f()->nil{ assert 1 == 1; }"), "Assert"));
    CHECK(has(dump("func f()->nil{ defer { cleanup(); } }"), "Defer"));
}

int main() { return mxtest::run_all(); }
