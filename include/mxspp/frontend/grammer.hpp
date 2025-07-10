#pragma once
#include <tao/pegtl/contrib/raw_string.hpp>
namespace mxs::frontend::grammar {
    namespace pegtl = tao::pegtl;

    struct expression;
    struct statement;
    struct block;
    struct type_spec;
    struct func_sig;
    struct pattern;
    struct class_member;
    struct annotatable_decl;

    // ===================================================================
    // Primitives & Helpers
    // ===================================================================

    // Basic characters
    struct identifier_first : pegtl::sor<pegtl::alpha, pegtl::one<'_'>> { };
    struct identifier_other : pegtl::sor<pegtl::alnum, pegtl::one<'_'>> { };
    struct identifier : pegtl::seq<identifier_first, pegtl::star<identifier_other>> { };

    // Helper to ensure keywords are not prefixes of identifiers
    // Helper to ensure keywords are not prefixes of identifiers
    template<char... S>// 将 typename... 修改为 char...
    struct keyword : pegtl::seq<pegtl::string<S...>, pegtl::not_at<identifier_other>> { };

    // Literals
    struct integer_literal : pegtl::plus<pegtl::digit> { };
    struct float_literal : pegtl::seq<pegtl::plus<pegtl::digit>, pegtl::one<'.'>,
                                      pegtl::plus<pegtl::digit>> { };
    struct string_literal
        : pegtl::seq<pegtl::one<'"'>,
                     pegtl::until<pegtl::one<'"'>,
                                  pegtl::if_must<pegtl::one<'\\'>, pegtl::any>>,
                     pegtl::one<'"'>> { };
    struct bool_literal
        : pegtl::sor<keyword<'t', 'r', 'u', 'e'>, keyword<'f', 'a', 'l', 's', 'e'>> { };
    struct nil_literal : keyword<'n', 'i', 'l'> { };
    struct literal : pegtl::sor<float_literal, integer_literal, string_literal,
                                bool_literal, nil_literal> { };

    // Comments
    struct line_comment : pegtl::seq<pegtl::string<'/', '/'>, pegtl::until<pegtl::eolf>> {
    };
    struct block_comment
        : pegtl::seq<pegtl::string<'/', '*'>, pegtl::until<pegtl::string<'*', '/'>>> { };
    struct comment : pegtl::sor<line_comment, block_comment> { };

    // Whitespace and skippable content
    struct ignored : pegtl::star<pegtl::sor<pegtl::space, comment>> { };

    struct K_AS : keyword<'a', 's'> { };
    struct K_ASSERT : keyword<'a', 's', 's', 'e', 'r', 't'> { };
    struct K_BREAK : keyword<'b', 'r', 'e', 'a', 'k'> { };
    struct K_CASE : keyword<'c', 'a', 's', 'e'> { };
    struct K_CLASS : keyword<'c', 'l', 'a', 's', 's'> { };
    struct K_CONTINUE : keyword<'c', 'o', 'n', 't', 'i', 'n', 'u', 'e'> { };
    struct K_DEFER : keyword<'d', 'e', 'f', 'e', 'r'> { };
    struct K_DO : keyword<'d', 'o'> { };
    struct K_DYNAMIC : keyword<'d', 'y', 'n', 'a', 'm', 'i', 'c'> { };
    struct K_ELSE : keyword<'e', 'l', 's', 'e'> { };
    struct K_ENUM : keyword<'e', 'n', 'u', 'm'> { };
    struct K_EXPORT : keyword<'e', 'x', 'p', 'o', 'r', 't'> { };
    struct K_FOR : keyword<'f', 'o', 'r'> { };
    struct K_FUNC : keyword<'f', 'u', 'n', 'c'> { };
    struct K_IF : keyword<'i', 'f'> { };
    struct K_IMPORT : keyword<'i', 'm', 'p', 'o', 'r', 't'> { };
    struct K_IN : keyword<'i', 'n'> { };
    struct K_INTERFACE : keyword<'i', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e'> { };
    struct K_LET : keyword<'l', 'e', 't'> { };
    struct K_LOOP : keyword<'l', 'o', 'o', 'p'> { };
    struct K_MATCH : keyword<'m', 'a', 't', 'c', 'h'> { };
    struct K_MUT : keyword<'m', 'u', 't'> { };
    struct K_OPERATOR : keyword<'o', 'p', 'e', 'r', 'a', 't', 'o', 'r'> { };
    struct K_OVERRIDE : keyword<'o', 'v', 'e', 'r', 'r', 'i', 'd', 'e'> { };
    struct K_PRIVATE : keyword<'p', 'r', 'i', 'v', 'a', 't', 'e'> { };
    struct K_PUBLIC : keyword<'p', 'u', 'b', 'l', 'i', 'c'> { };
    struct K_RAISE : keyword<'r', 'a', 'i', 's', 'e'> { };
    struct K_RETURN : keyword<'r', 'e', 't', 'u', 'r', 'n'> { };
    struct K_STATIC : keyword<'s', 't', 'a', 't', 'i', 'c'> { };
    struct K_TYPE : keyword<'t', 'y', 'p', 'e'> { };
    struct K_UNTIL : keyword<'u', 'n', 't', 'i', 'l'> { };

    // ===================================================================
    // General Components
    // ===================================================================
    struct fqdn : pegtl::list<identifier, pegtl::one<'.'>> { };
    struct identifier_list
        : pegtl::list<identifier, pegtl::seq<ignored, pegtl::one<','>, ignored>> { };
    struct generic_param : pegtl::seq<pegtl::one<'<'>, ignored, identifier_list, ignored,
                                      pegtl::one<'>'>> { };
    struct generic_inst
        : pegtl::seq<
                  pegtl::one<'<'>, ignored,
                  pegtl::list<type_spec, pegtl::seq<ignored, pegtl::one<','>, ignored>>,
                  ignored, pegtl::one<'>'>> { };

    struct param;
    struct param_list
        : pegtl::list<param, pegtl::seq<ignored, pegtl::one<','>, ignored>> { };
    struct param
        : pegtl::seq<identifier_list, ignored, pegtl::one<':'>, ignored, type_spec,
                     pegtl::opt<ignored, pegtl::one<'='>, ignored, expression>> { };

    struct func_type
        : pegtl::seq<K_FUNC, ignored, pegtl::one<'('>, ignored,
                     pegtl::opt<pegtl::list<
                             type_spec, pegtl::seq<ignored, pegtl::one<','>, ignored>>>,
                     ignored, pegtl::one<')'>,
                     pegtl::opt<ignored, pegtl::string<'-', '>'>, ignored, type_spec>> {
    };
    struct single_type
        : pegtl::sor<pegtl::seq<fqdn, pegtl::opt<ignored, generic_inst>>, func_type> { };
    struct type_spec
        : pegtl::list<single_type, pegtl::seq<ignored, pegtl::one<'|'>, ignored>> { };

    struct func_sig
        : pegtl::seq<pegtl::one<'('>, ignored, pegtl::opt<param_list>, ignored,
                     pegtl::one<')'>,
                     pegtl::opt<ignored, pegtl::string<'-', '>'>, ignored, type_spec>> {
    };

    // ===================================================================
    // Expressions
    // ===================================================================
    struct call_args;// Forward declare for constructor_def
    struct block_expr;
    struct match_expr;
    struct raise_expr;
    struct lambda_expr;

    struct primary_expr
        : pegtl::sor<literal,
                     pegtl::seq<pegtl::one<'('>, ignored, expression, ignored,
                                pegtl::one<')'>>,
                     block_expr, match_expr, raise_expr, lambda_expr,
                     identifier// Must be last to avoid greedily matching keywords
                     > { };

    struct postfix_op
        : pegtl::sor<pegtl::seq<ignored, pegtl::one<'.'>, ignored, identifier>,
                     pegtl::seq<ignored, pegtl::one<'['>, ignored, expression, ignored,
                                pegtl::one<']'>>,
                     pegtl::seq<ignored, generic_inst>, pegtl::seq<ignored, call_args>,
                     pegtl::seq<ignored, pegtl::one<'?'>>> { };

    struct postfix_expr : pegtl::seq<primary_expr, pegtl::star<postfix_op>> { };

    struct unary_op : pegtl::one<'!', '+', '-'> { };
    struct unary_expr
        : pegtl::sor<pegtl::seq<unary_op, ignored, postfix_expr>, postfix_expr> { };

    struct multiplicative_op
        : pegtl::sor<pegtl::one<'*'>, pegtl::one<'/'>, pegtl::one<'%'>> { };
    struct multiplicative_expr
        : pegtl::list<unary_expr, pegtl::seq<ignored, multiplicative_op, ignored>,
                      unary_expr> { };

    struct additive_op : pegtl::sor<pegtl::one<'+'>, pegtl::one<'-'>> { };
    struct additive_expr
        : pegtl::list<multiplicative_expr, pegtl::seq<ignored, additive_op, ignored>,
                      multiplicative_expr> { };

    struct range_op : pegtl::string<'.', '.'> { };
    struct range_expr : pegtl::list<additive_expr, pegtl::seq<ignored, range_op, ignored>,
                                    additive_expr> { };

    struct relational_op : pegtl::sor<pegtl::string<'<', '='>, pegtl::string<'>', '='>,
                                      pegtl::one<'<'>, pegtl::one<'>'>> { };
    struct relational_expr
        : pegtl::list<range_expr, pegtl::seq<ignored, relational_op, ignored>,
                      range_expr> { };

    struct equality_op : pegtl::sor<pegtl::string<'=', '='>, pegtl::string<'!', '='>> { };
    struct equality_expr
        : pegtl::list<relational_expr, pegtl::seq<ignored, equality_op, ignored>,
                      relational_expr> { };

    struct logic_and_op : pegtl::string<'&', '&'> { };
    struct logic_and_expr
        : pegtl::list<equality_expr, pegtl::seq<ignored, logic_and_op, ignored>,
                      equality_expr> { };

    struct logic_or_op : pegtl::string<'|', '|'> { };
    struct logic_or_expr
        : pegtl::list<logic_and_expr, pegtl::seq<ignored, logic_or_op, ignored>,
                      logic_and_expr> { };

    // Right-associative assignment
    struct assign_op
        : pegtl::sor<pegtl::string<'+', '='>, pegtl::string<'-', '='>,
                     pegtl::string<'*', '='>, pegtl::string<'/', '='>, pegtl::one<'='>> {
    };
    struct assign_expr
        : pegtl::seq<logic_or_expr, pegtl::opt<ignored, assign_op, ignored, expression>> {
    };

    struct expression : assign_expr { };

    // Expression sub-components
    struct arg;
    struct arg_list : pegtl::list<arg, pegtl::seq<ignored, pegtl::one<','>, ignored>> { };
    struct argument
        : pegtl::sor<
                  pegtl::seq<identifier, ignored, pegtl::one<'='>, ignored, expression>,
                  expression> { };
    struct call_args : pegtl::seq<pegtl::one<'('>, ignored, pegtl::opt<arg_list>, ignored,
                                  pegtl::one<')'>> { };

    struct raise_expr : pegtl::seq<K_RAISE, ignored, expression> { };
    struct lambda_expr : pegtl::seq<func_sig, ignored, pegtl::string<'=', '>'>, ignored,
                                    pegtl::sor<expression, block>> { };
    struct block_expr : pegtl::seq<pegtl::one<'{'>, ignored, pegtl::star<statement>,
                                   pegtl::opt<expression>, ignored, pegtl::one<'}'>> { };

    struct case_clause
        : pegtl::seq<K_CASE, ignored, pattern, ignored, pegtl::string<'=', '>'>, ignored,
                     pegtl::sor<expression, block>,
                     pegtl::opt<ignored, pegtl::one<','>>> { };
    struct match_expr
        : pegtl::seq<K_MATCH, ignored, pegtl::one<'('>, ignored, expression, ignored,
                     pegtl::one<')'>, ignored, pegtl::one<'{'>, ignored,
                     pegtl::star<case_clause, ignored>, pegtl::one<'}'>> { };

    struct pattern_list;// Forward declare for recursion
    struct pattern
        : pegtl::sor<literal,
                     pegtl::seq<identifier,
                                pegtl::opt<ignored, pegtl::one<'('>, ignored,
                                           pattern_list, ignored, pegtl::one<')'>>>,
                     pegtl::seq<pegtl::one<'('>, ignored, pattern_list, ignored,
                                pegtl::one<')'>>,
                     pegtl::one<'_'>> { };
    struct pattern_list
        : pegtl::list<pattern, pegtl::seq<ignored, pegtl::one<','>, ignored>> { };

    // ===================================================================
    // Statements
    // ===================================================================
    struct let_stmt
        : pegtl::seq<K_LET, ignored, pegtl::opt<K_MUT, ignored>, identifier_list,
                     pegtl::opt<ignored, pegtl::one<':'>, ignored, type_spec>,
                     pegtl::opt<ignored, pegtl::one<'='>, ignored, expression>,
                     pegtl::one<';'>> { };
    struct expression_stmt : pegtl::seq<expression, pegtl::one<';'>> { };
    struct if_stmt;// Forward declare for else if
    struct if_stmt
        : pegtl::seq<K_IF, ignored, expression, ignored, block,
                     pegtl::opt<ignored, K_ELSE, ignored, pegtl::sor<if_stmt, block>>> {
    };
    struct for_in_stmt
        : pegtl::seq<K_FOR, ignored, pegtl::opt<K_MUT, ignored>, identifier, ignored,
                     K_IN, ignored, expression, ignored, block> { };
    struct loop_stmt : pegtl::seq<K_LOOP, ignored, block> { };
    struct do_until_stmt
        : pegtl::seq<K_DO, ignored, block, ignored, K_UNTIL, ignored, pegtl::one<'('>,
                     ignored, expression, ignored, pegtl::one<')'>, pegtl::one<';'>> { };
    struct until_stmt : pegtl::seq<K_UNTIL, ignored, pegtl::one<'('>, ignored, expression,
                                   ignored, pegtl::one<')'>, ignored, block> { };
    struct break_stmt : pegtl::seq<K_BREAK, pegtl::one<';'>> { };
    struct continue_stmt : pegtl::seq<K_CONTINUE, pegtl::one<';'>> { };
    struct return_stmt
        : pegtl::seq<K_RETURN, pegtl::opt<ignored, expression>, pegtl::one<';'>> { };
    struct assert_stmt : pegtl::seq<K_ASSERT, ignored, expression, pegtl::one<';'>> { };
    struct defer_stmt : pegtl::seq<K_DEFER, ignored, block> { };
    struct control_stmt : pegtl::sor<if_stmt, for_in_stmt, loop_stmt, do_until_stmt,
                                     until_stmt, break_stmt, continue_stmt, return_stmt> {
    };

    struct statement
        : pegtl::sor<let_stmt, control_stmt, expression_stmt, assert_stmt, defer_stmt> {
    };
    struct block
        : pegtl::seq<pegtl::one<'{'>, ignored,
                     pegtl::star<pegtl::seq<statement, ignored>>, pegtl::one<'}'>> { };

    // ===================================================================
    // Definitions
    // ===================================================================
    struct func_def
        : pegtl::seq<K_FUNC, ignored, identifier, pegtl::opt<ignored, generic_param>,
                     ignored, func_sig, ignored, block> { };
    struct field_def_class : let_stmt { };
    struct method_def : pegtl::seq<pegtl::opt<K_OVERRIDE, ignored>, K_FUNC, ignored,
                                   identifier, pegtl::opt<ignored, generic_param>,
                                   ignored, func_sig, ignored, block> { };
    struct op_symbol
        : pegtl::sor<pegtl::string<'+', '='>, pegtl::string<'-', '='>,
                     pegtl::string<'*', '='>, pegtl::string<'/', '='>,
                     pegtl::string<'=', '='>, pegtl::string<'!', '='>,
                     pegtl::string<'<', '='>, pegtl::string<'>', '='>, pegtl::one<'+'>,
                     pegtl::one<'-'>, pegtl::one<'!'>, pegtl::one<'*'>, pegtl::one<'/'>,
                     pegtl::one<'%'>, pegtl::one<'<'>, pegtl::one<'>'>> { };
    struct operator_def : pegtl::seq<pegtl::opt<K_OVERRIDE, ignored>, K_OPERATOR, ignored,
                                     op_symbol, ignored, func_sig, ignored, block> { };
    struct static_member
        : pegtl::seq<K_STATIC, ignored, pegtl::sor<method_def, field_def_class>> { };
    struct constructor_def : pegtl::seq<identifier, ignored, func_sig,
                                        pegtl::opt<ignored, pegtl::one<':'>, ignored,
                                                   identifier, ignored, call_args>,
                                        ignored, block> { };
    struct destructor_def
        : pegtl::seq<pegtl::one<'~'>, identifier, pegtl::one<'('>, pegtl::one<')'>,
                     pegtl::opt<ignored, pegtl::one<':'>, ignored, pegtl::one<'~'>,
                                identifier>,
                     ignored, block> { };
    struct access_spec
        : pegtl::seq<pegtl::sor<K_PUBLIC, K_PRIVATE>, ignored, pegtl::one<':'>> { };
    struct class_member
        : pegtl::sor<access_spec, constructor_def, destructor_def, static_member,
                     method_def, operator_def, field_def_class> { };
    struct class_def
        : pegtl::seq<K_CLASS, ignored, identifier, pegtl::opt<ignored, generic_param>,
                     pegtl::opt<ignored, pegtl::one<':'>, ignored, type_spec>, ignored,
                     pegtl::one<'{'>, ignored,
                     pegtl::star<pegtl::seq<class_member, ignored>>, pegtl::one<'}'>> { };

    struct interface_member
        : pegtl::seq<K_FUNC, ignored, identifier, pegtl::opt<ignored, generic_param>,
                     ignored, func_sig, pegtl::opt<ignored, block>, pegtl::one<';'>> { };
    struct interface_def
        : pegtl::seq<K_INTERFACE, ignored, identifier, pegtl::opt<ignored, generic_param>,
                     pegtl::opt<ignored, pegtl::one<':'>, ignored, type_spec>, ignored,
                     pegtl::one<'{'>, ignored,
                     pegtl::star<pegtl::seq<interface_member, ignored>>,
                     pegtl::one<'}'>> { };

    struct field_decl : pegtl::seq<identifier_list, ignored, pegtl::one<':'>, ignored,
                                   type_spec, pegtl::one<';'>> { };
    struct type_def
        : pegtl::seq<K_TYPE, ignored, identifier, ignored, pegtl::one<'{'>, ignored,
                     pegtl::star<pegtl::seq<field_decl, ignored>>, pegtl::one<'}'>> { };

    struct enum_variant
        : pegtl::seq<identifier, pegtl::opt<ignored, pegtl::one<'('>, ignored, param_list,
                                            ignored, pegtl::one<')'>>> { };
    struct enum_def
        : pegtl::seq<K_ENUM, ignored, identifier, pegtl::opt<ignored, generic_param>,
                     ignored, pegtl::one<'{'>, ignored,
                     pegtl::list<enum_variant,
                                 pegtl::seq<ignored, pegtl::one<','>, ignored>>,
                     ignored, pegtl::one<'}'>> { };

    // ===================================================================
    // Top-Level
    // ===================================================================
    struct annotation_arg
        : pegtl::seq<identifier, ignored, pegtl::one<'='>, ignored, expression> { };
    struct annotation
        : pegtl::seq<
                  pegtl::string<'@', '@'>, ignored, identifier,
                  pegtl::opt<ignored, pegtl::one<'('>, ignored,
                             pegtl::list<annotation_arg,
                                         pegtl::seq<ignored, pegtl::one<','>, ignored>>,
                             ignored, pegtl::one<')'>>> { };

    struct import_stmt
        : pegtl::seq<K_IMPORT, ignored, fqdn,
                     pegtl::opt<ignored, K_AS, ignored, identifier>, pegtl::one<';'>> { };
    struct binding_stmt
        : pegtl::seq<pegtl::sor<K_STATIC, K_DYNAMIC>, ignored, K_LET, ignored, identifier,
                     ignored, pegtl::one<'='>, ignored, expression, pegtl::one<';'>> { };

    struct annotatable_decl
        : pegtl::sor<func_def, class_def, interface_def, type_def, enum_def> { };
    struct top_level_decl
        : pegtl::seq<pegtl::opt<K_EXPORT, ignored>,
                     pegtl::sor<import_stmt, binding_stmt,
                                pegtl::seq<annotation, ignored, annotatable_decl>,
                                annotatable_decl>> { };

    // ===================================================================
    // Entry Point
    // ===================================================================
    struct mxscript : pegtl::star<pegtl::seq<top_level_decl, ignored>> { };
    struct grammar : pegtl::must<ignored, mxscript, pegtl::eof> { };
}