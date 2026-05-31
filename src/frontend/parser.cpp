#include "mxspp/frontend/parser.h"

#include "mxspp/frontend/ast.h"
#include "mxspp/frontend/grammar.hpp"

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace mxs::frontend::parser {
    namespace pegtl = tao::pegtl;
    namespace pt = tao::pegtl::parse_tree;
    namespace g = mxs::frontend::grammar;

    // ===================================================================
    // Furthest-progress tracking
    // ===================================================================
    // PEGTL reports the position of the `must<>` that threw, which is often far from the
    // real mistake: a missing `;` makes the enclosing block match zero statements, so its
    // `must<'}'>` fails right at the block's opening — blaming `}` at the wrong place. This
    // control records the furthest input position any rule was *attempted* at, a good
    // heuristic for "where parsing actually got stuck". It only observes; it never changes
    // what matches, so it cannot alter the parse result.
    namespace {
        struct furthest_t {
            std::size_t byte = 0, line = 1, column = 1;
        };
        thread_local furthest_t g_furthest;

        template<typename Rule>
        struct error_tracer : pegtl::normal<Rule> {
            template<typename ParseInput, typename... States>
            static void start(const ParseInput &in, States &&...st) noexcept {
                const auto p = in.position();
                if (p.byte >= g_furthest.byte) g_furthest = { p.byte, p.line, p.column };
                pegtl::normal<Rule>::start(in, st...);
            }
        };
    }// namespace

    // ===================================================================
    // parse_tree selectors
    // ===================================================================
    // Left-associative fold for binary-operator list rules. After this runs, a binary
    // expression node IS the operator node (e.g. additive_op) with exactly two children.
    // (Adapted from PEGTL's parse_tree.cpp calculator example.)
    struct rearrange : pt::apply<rearrange> {
        template<typename Node, typename... States>
        static void transform(std::unique_ptr<Node> &n, States &&...st) {
            if (n->children.size() == 1) {
                n = std::move(n->children.back());
            } else {
                n->remove_content();
                auto &c = n->children;
                auto r = std::move(c.back());
                c.pop_back();
                auto o = std::move(c.back());
                c.pop_back();
                o->children.emplace_back(std::move(n));
                o->children.emplace_back(std::move(r));
                n = std::move(o);
                transform(n->children.front(), st...);
            }
        }
    };

    template<typename Rule>
    using selector = pt::selector<
            Rule,
            pt::store_content::on<g::integer_literal, g::float_literal, g::string_literal,
                                  g::bool_literal, g::nil_literal, g::identifier,
                                  g::type_spec, g::op_symbol, g::unary_op,
                                  g::multiplicative_op, g::additive_op, g::range_op,
                                  g::relational_op, g::equality_op, g::logic_and_op,
                                  g::logic_or_op, g::assign_op>,
            pt::remove_content::on<
                    g::K_MUT, g::K_OVERRIDE, g::K_STATIC, g::identifier_list, g::let_stmt,
                    g::return_stmt, g::expression_stmt, g::block, g::if_stmt,
                    g::for_in_stmt, g::loop_stmt, g::until_stmt, g::do_until_stmt,
                    g::break_stmt, g::continue_stmt, g::assert_stmt, g::defer_stmt,
                    g::func_def, g::func_sig, g::param, g::call_args, g::class_def,
                    g::field_def_class, g::constructor_def, g::destructor_def,
                    g::method_def, g::operator_def, g::static_member, g::interface_def,
                    g::interface_member, g::enum_def, g::enum_variant, g::type_def,
                    g::field_decl, g::annotation, g::annotation_arg>,
            // NOTE: fold on `expression`, not `assign_expr` — `struct expression :
            // assign_expr {}` means the matched rule is `expression`, so assignment is
            // only foldable there.
            rearrange::on<g::multiplicative_expr, g::additive_expr, g::range_expr,
                          g::relational_expr, g::equality_expr, g::logic_and_expr,
                          g::logic_or_expr, g::expression>,
            pt::fold_one::on<g::unary_expr, g::postfix_expr>>;

    // ===================================================================
    // parse_tree -> AST transform
    // ===================================================================
    using Node = pt::node;

    template<typename T>
    static std::unique_ptr<T> mk() {
        return std::make_unique<T>();
    }

    static std::string content_of(const Node &n) {
        return n.has_content() ? n.string() : std::string{};
    }

    static std::string unquote(const std::string &s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    static bool is_binop(const Node &n) {
        return n.is_type<g::multiplicative_op>() || n.is_type<g::additive_op>() ||
               n.is_type<g::range_op>() || n.is_type<g::relational_op>() ||
               n.is_type<g::equality_op>() || n.is_type<g::logic_and_op>() ||
               n.is_type<g::logic_or_op>() || n.is_type<g::assign_op>();
    }

    static std::unique_ptr<ast::Expression> to_expr(const Node &n);

    static std::unique_ptr<ast::Expression> to_postfix(const Node &n) {
        // postfix_expr kept only when it has >1 child, e.g. callee + call_args.
        auto call = mk<ast::FunctionCall>();
        if (!n.children.empty() && n.children[0]->is_type<g::identifier>())
            call->name = content_of(*n.children[0]);
        for (std::size_t i = 1; i < n.children.size(); ++i) {
            const Node &c = *n.children[i];
            if (c.is_type<g::call_args>())
                for (const auto &a : c.children) call->args.push_back(to_expr(*a));
        }
        return call;
    }

    static std::unique_ptr<ast::Expression> to_expr(const Node &n) {
        if (n.is_type<g::integer_literal>())
            return std::make_unique<ast::IntegerLiteral>(std::stoll(n.string()), false);
        if (n.is_type<g::float_literal>()) {
            auto e = mk<ast::FloatLiteral>();
            e->value = std::stod(n.string());
            return e;
        }
        if (n.is_type<g::bool_literal>()) {
            auto e = mk<ast::BooleanLiteral>();
            e->value = (content_of(n) == "true");
            return e;
        }
        if (n.is_type<g::string_literal>()) {
            auto e = mk<ast::StringLiteral>();
            e->value = unquote(content_of(n));
            return e;
        }
        if (n.is_type<g::nil_literal>()) return mk<ast::NilLiteral>();
        if (n.is_type<g::identifier>()) {
            auto e = mk<ast::Identifier>();
            e->name = content_of(n);
            return e;
        }
        if (is_binop(n) && n.children.size() == 2) {
            auto e = mk<ast::BinaryOp>();
            e->op = content_of(n);
            e->left = to_expr(*n.children[0]);
            e->right = to_expr(*n.children[1]);
            return e;
        }
        if (n.is_type<g::unary_expr>() && n.children.size() == 2) {
            auto e = mk<ast::UnaryOp>();
            e->op = content_of(*n.children[0]);
            e->operand = to_expr(*n.children[1]);
            return e;
        }
        if (n.is_type<g::postfix_expr>()) return to_postfix(n);
        // Fallback: surface the unhandled rule rather than crashing.
        auto e = mk<ast::Identifier>();
        e->name = "<unhandled:" + std::string(n.type) + ">";
        return e;
    }

    static std::unique_ptr<ast::MXASTNode> to_stmt(const Node &n);

    static std::unique_ptr<ast::Block> to_block(const Node &n) {
        auto b = mk<ast::Block>();
        for (const auto &c : n.children)
            if (auto s = to_stmt(*c)) b->statements.push_back(std::move(s));
        return b;
    }

    static std::unique_ptr<ast::MXASTNode> to_let(const Node &n) {
        // Children: [K_MUT?, identifier_list, type_spec?, initializer-expression?].
        // Selecting identifier_list groups the names, so a bare-identifier initializer
        // (e.g. `let z = y;`) is a distinct child and no longer mistaken for a name.
        auto s = mk<ast::LetStatement>();
        for (const auto &c : n.children) {
            if (c->is_type<g::K_MUT>()) {
                s->isMut = true;
            } else if (c->is_type<g::identifier_list>()) {
                for (const auto &id : c->children) s->names.push_back(content_of(*id));
            } else if (c->is_type<g::type_spec>()) {
                s->typeName = content_of(*c);
            } else {
                s->value = to_expr(*c);
            }
        }
        return s;
    }

    // Append a Parameter per name found in the `param` children of `container`
    // (param = identifier_list ':' type_spec). Shared by func/method/ctor/op/enum-variant.
    static void collect_params(const Node &container,
                               std::vector<std::unique_ptr<ast::Parameter>> &out) {
        for (const auto &c : container.children) {
            if (!c->is_type<g::param>()) continue;
            std::string ty;
            std::vector<std::string> names;
            for (const auto &pc : c->children) {
                if (pc->is_type<g::identifier_list>())
                    for (const auto &id : pc->children) names.push_back(content_of(*id));
                else if (pc->is_type<g::type_spec>())
                    ty = content_of(*pc);
            }
            for (const auto &nm : names) {
                auto p = mk<ast::Parameter>();
                p->name = nm;
                if (!ty.empty()) p->typeName = ty;
                out.push_back(std::move(p));
            }
        }
    }

    // Fill params + (a func_sig's direct) return type.
    static void parse_params(const Node &sig,
                             std::vector<std::unique_ptr<ast::Parameter>> &params,
                             std::optional<std::string> &retType) {
        collect_params(sig, params);
        for (const auto &c : sig.children)
            if (c->is_type<g::type_spec>()) retType = content_of(*c);
    }

    static std::unique_ptr<ast::MXASTNode> to_func_def(const Node &n) {
        auto f = mk<ast::FunctionDef>();
        for (const auto &c : n.children) {
            if (c->is_type<g::identifier>() && f->name.empty()) f->name = content_of(*c);
            else if (c->is_type<g::func_sig>())
                parse_params(*c, f->params, f->returnTypeName);
            else if (c->is_type<g::block>())
                f->body = to_block(*c);
        }
        return f;
    }

    // A class member: field / method / constructor / destructor / operator, or a
    // `static` wrapper around a method/field.
    static std::unique_ptr<ast::MXASTNode> to_member(const Node &n, bool isStatic) {
        if (n.is_type<g::static_member>()) {
            for (const auto &c : n.children)
                if (!c->is_type<g::K_STATIC>()) return to_member(*c, true);
            return nullptr;
        }
        if (n.is_type<g::field_def_class>()) {
            auto f = mk<ast::FieldDecl>();
            f->isStatic = isStatic;
            for (const auto &c : n.children) {
                if (c->is_type<g::K_MUT>()) f->isMut = true;
                else if (c->is_type<g::identifier_list>())
                    for (const auto &id : c->children)
                        f->names.push_back(content_of(*id));
                else if (c->is_type<g::type_spec>())
                    f->typeName = content_of(*c);
                else
                    f->value = to_expr(*c);
            }
            return f;
        }
        if (n.is_type<g::method_def>()) {
            auto m = mk<ast::MethodDef>();
            m->isStatic = isStatic;
            for (const auto &c : n.children) {
                if (c->is_type<g::K_OVERRIDE>()) m->isOverride = true;
                else if (c->is_type<g::identifier>() && m->name.empty())
                    m->name = content_of(*c);
                else if (c->is_type<g::func_sig>())
                    parse_params(*c, m->params, m->returnTypeName);
                else if (c->is_type<g::block>())
                    m->body = to_block(*c);
            }
            return m;
        }
        if (n.is_type<g::constructor_def>()) {
            auto k = mk<ast::ConstructorDef>();
            bool sawSig = false;
            for (const auto &c : n.children) {
                if (c->is_type<g::func_sig>()) {
                    std::optional<std::string> rt;
                    parse_params(*c, k->params, rt);
                    sawSig = true;
                } else if (c->is_type<g::identifier>() && sawSig && !k->baseName) {
                    k->baseName =
                            content_of(*c);// base-class ctor name (after the signature)
                } else if (c->is_type<g::block>()) {
                    k->body = to_block(*c);
                }
            }
            return k;
        }
        if (n.is_type<g::destructor_def>()) {
            auto d = mk<ast::DestructorDef>();
            for (const auto &c : n.children)
                if (c->is_type<g::block>()) d->body = to_block(*c);
            return d;
        }
        if (n.is_type<g::operator_def>()) {
            auto o = mk<ast::OperatorDef>();
            for (const auto &c : n.children) {
                if (c->is_type<g::K_OVERRIDE>()) o->isOverride = true;
                else if (c->is_type<g::op_symbol>())
                    o->op = content_of(*c);
                else if (c->is_type<g::func_sig>())
                    parse_params(*c, o->params, o->returnTypeName);
                else if (c->is_type<g::block>())
                    o->body = to_block(*c);
            }
            return o;
        }
        return nullptr;// access_spec and anything else
    }

    static std::unique_ptr<ast::MXASTNode> to_class(const Node &n) {
        auto c = mk<ast::ClassDef>();
        for (const auto &ch : n.children) {
            if (ch->is_type<g::identifier>() && c->name.empty())
                c->name = content_of(*ch);
            else if (ch->is_type<g::type_spec>() && !c->baseType)
                c->baseType = content_of(*ch);
            else if (auto m = to_member(*ch, false))
                c->members.push_back(std::move(m));
        }
        return c;
    }

    static std::unique_ptr<ast::MXASTNode> to_interface(const Node &n) {
        auto i = mk<ast::InterfaceDef>();
        for (const auto &ch : n.children) {
            if (ch->is_type<g::identifier>() && i->name.empty())
                i->name = content_of(*ch);
            else if (ch->is_type<g::type_spec>() && !i->baseType)
                i->baseType = content_of(*ch);
            else if (ch->is_type<g::interface_member>()) {
                auto m = mk<ast::InterfaceMethod>();
                for (const auto &c : ch->children) {
                    if (c->is_type<g::identifier>() && m->name.empty())
                        m->name = content_of(*c);
                    else if (c->is_type<g::func_sig>())
                        parse_params(*c, m->params, m->returnTypeName);
                    else if (c->is_type<g::block>())
                        m->body = to_block(*c);
                }
                i->methods.push_back(std::move(m));
            }
        }
        return i;
    }

    static std::unique_ptr<ast::MXASTNode> to_enum(const Node &n) {
        auto e = mk<ast::EnumDef>();
        for (const auto &ch : n.children) {
            if (ch->is_type<g::identifier>() && e->name.empty())
                e->name = content_of(*ch);
            else if (ch->is_type<g::enum_variant>()) {
                auto v = mk<ast::EnumVariant>();
                for (const auto &c : ch->children)
                    if (c->is_type<g::identifier>() && v->name.empty())
                        v->name = content_of(*c);
                collect_params(*ch, v->fields);
                e->variants.push_back(std::move(v));
            }
        }
        return e;
    }

    static std::unique_ptr<ast::MXASTNode> to_typedef(const Node &n) {
        auto t = mk<ast::TypeDef>();
        for (const auto &ch : n.children) {
            if (ch->is_type<g::identifier>() && t->name.empty())
                t->name = content_of(*ch);
            else if (ch->is_type<g::field_decl>()) {
                auto f = mk<ast::TypeField>();
                for (const auto &c : ch->children) {
                    if (c->is_type<g::identifier_list>())
                        for (const auto &id : c->children)
                            f->names.push_back(content_of(*id));
                    else if (c->is_type<g::type_spec>())
                        f->typeName = content_of(*c);
                }
                t->fields.push_back(std::move(f));
            }
        }
        return t;
    }

    static std::unique_ptr<ast::MXASTNode> to_stmt(const Node &n) {
        if (n.is_type<g::func_def>()) return to_func_def(n);
        if (n.is_type<g::class_def>()) return to_class(n);
        if (n.is_type<g::interface_def>()) return to_interface(n);
        if (n.is_type<g::enum_def>()) return to_enum(n);
        if (n.is_type<g::type_def>()) return to_typedef(n);
        if (n.is_type<g::let_stmt>()) return to_let(n);
        if (n.is_type<g::return_stmt>()) {
            auto s = mk<ast::ReturnStatement>();
            if (!n.children.empty()) s->value = to_expr(*n.children[0]);
            return s;
        }
        if (n.is_type<g::expression_stmt>()) {
            auto s = mk<ast::ExprStatement>();
            if (!n.children.empty()) s->expr = to_expr(*n.children[0]);
            return s;
        }
        if (n.is_type<g::if_stmt>()) {
            // children: [condition-expr, then-block, (else: block | if_stmt)?]
            auto s = mk<ast::IfStatement>();
            if (!n.children.empty()) s->condition = to_expr(*n.children[0]);
            if (n.children.size() > 1) s->thenBlock = to_block(*n.children[1]);
            if (n.children.size() > 2) {
                const Node &e = *n.children[2];
                if (e.is_type<g::if_stmt>()) {// else-if -> nested IfStatement
                    auto inner = to_stmt(e);
                    s->elseBranch.reset(dynamic_cast<ast::Statement *>(inner.release()));
                } else {
                    s->elseBranch = to_block(e);
                }
            }
            return s;
        }
        if (n.is_type<g::for_in_stmt>()) {
            // children: [K_MUT?, identifier(var), iterable-expr, block]
            auto s = mk<ast::ForInStatement>();
            for (const auto &c : n.children) {
                if (c->is_type<g::K_MUT>()) s->isMut = true;
                else if (c->is_type<g::identifier>() && s->var.empty())
                    s->var = content_of(*c);
                else if (c->is_type<g::block>())
                    s->body = to_block(*c);
                else
                    s->iterable = to_expr(*c);
            }
            return s;
        }
        if (n.is_type<g::loop_stmt>()) {
            auto s = mk<ast::LoopStatement>();
            if (!n.children.empty()) s->body = to_block(*n.children[0]);
            return s;
        }
        if (n.is_type<g::until_stmt>()) {// children: [cond-expr, block]
            auto s = mk<ast::UntilStatement>();
            if (!n.children.empty()) s->condition = to_expr(*n.children[0]);
            if (n.children.size() > 1) s->body = to_block(*n.children[1]);
            return s;
        }
        if (n.is_type<g::do_until_stmt>()) {// children: [block, cond-expr]
            auto s = mk<ast::DoUntilStatement>();
            if (!n.children.empty()) s->body = to_block(*n.children[0]);
            if (n.children.size() > 1) s->condition = to_expr(*n.children[1]);
            return s;
        }
        if (n.is_type<g::break_stmt>()) return mk<ast::BreakStatement>();
        if (n.is_type<g::continue_stmt>()) return mk<ast::ContinueStatement>();
        if (n.is_type<g::assert_stmt>()) {
            auto s = mk<ast::AssertStatement>();
            if (!n.children.empty()) s->expr = to_expr(*n.children[0]);
            return s;
        }
        if (n.is_type<g::defer_stmt>()) {
            auto s = mk<ast::DeferStatement>();
            if (!n.children.empty()) s->body = to_block(*n.children[0]);
            return s;
        }
        if (n.is_type<g::block>()) return to_block(n);
        return nullptr;// still out of scope: class / interface / enum / match / ...
    }

    // A parsed annotation: name + (key, value-as-string) args, e.g.
    // @@foreign(symbol_name="mxs_x") -> {name:"foreign", args:[{"symbol_name","mxs_x"}]}.
    struct AnnotInfo {
        std::string name;
        std::vector<std::pair<std::string, std::string>> args;
    };
    static AnnotInfo parse_annotation(const Node &n) {
        AnnotInfo a;
        for (const auto &c : n.children) {
            if (c->is_type<g::identifier>() && a.name.empty()) {
                a.name = content_of(*c);
            } else if (c->is_type<g::annotation_arg>()) {
                std::string k, v;
                for (const auto &ac : c->children) {
                    if (ac->is_type<g::identifier>() && k.empty()) k = content_of(*ac);
                    else if (ac->has_content())
                        v = unquote(content_of(*ac));
                }
                a.args.push_back({ k, v });
            }
        }
        return a;
    }

    // Translate PEGTL's raw rule-match text into something a user can read:
    //   "parse error matching tao::pegtl::one<'}'>"  ->  "expected '}'"
    //   "...eof"                                      ->  "expected end of input"
    // Falls back to the original text for shapes we don't special-case.
    static std::string friendly_message(const std::string &raw) {
        const std::string oneTag = "one<'";
        const auto p = raw.find(oneTag);
        if (p != std::string::npos && p + oneTag.size() < raw.size())
            return std::string("expected '") + raw[p + oneTag.size()] + "'";
        if (raw.find("eof") != std::string::npos) return "expected end of input";
        return raw;
    }

    // Render a syntax error as `name:line:col: syntax error: <msg>` plus the offending
    // source line and a caret pointing at the column.
    static void report_syntax_error(std::string_view src, std::string_view name,
                                    std::size_t line, std::size_t col,
                                    const std::string &msg) {
        std::cerr << name << ":" << line << ":" << col << ": syntax error: " << msg
                  << "\n";
        std::size_t cur = 1, start = 0;
        for (std::size_t i = 0; i < src.size() && cur < line; ++i)
            if (src[i] == '\n') {
                ++cur;
                start = i + 1;
            }
        std::size_t end = start;
        while (end < src.size() && src[end] != '\n') ++end;
        std::cerr << "  " << src.substr(start, end - start) << "\n  ";
        for (std::size_t i = 1; i < col; ++i) std::cerr << ' ';
        std::cerr << "^\n";
    }

    std::unique_ptr<ast::TranslationUnit> parse_to_ast(std::string_view source,
                                                       std::string_view source_name) {
        g_furthest = furthest_t{};// reset furthest-progress tracking for this parse
        try {
            pegtl::memory_input<> in(source.data(), source.size(),
                                     std::string(source_name));
            auto root = pt::parse<g::grammar, selector, pegtl::nothing, error_tracer>(in);
            if (!root) {
                // The grammar didn't match without any must<> throwing (rare with the
                // must<...eof> top rule); point at the furthest progress.
                report_syntax_error(source, source_name, g_furthest.line,
                                    g_furthest.column, "unexpected token");
                return nullptr;
            }
            auto tu = mk<ast::TranslationUnit>();
            std::vector<AnnotInfo> pending;
            for (const auto &c : root->children) {
                if (c->is_type<g::annotation>()) {
                    pending.push_back(parse_annotation(*c));
                    continue;
                }
                auto s = to_stmt(*c);
                if (s) {
                    // A preceding @@foreign annotation binds this function to an external
                    // symbol — fully generic, no per-function special-casing in codegen.
                    if (auto *fd = dynamic_cast<ast::FunctionDef *>(s.get())) {
                        for (const auto &a : pending) {
                            if (a.name != "foreign") continue;
                            fd->isForeign = true;
                            for (const auto &kv : a.args)
                                if (kv.first == "symbol_name")
                                    fd->foreignSymbol = kv.second;
                        }
                    }
                    tu->statements.push_back(std::move(s));
                }
                pending.clear();
            }
            return tu;
        } catch (const pegtl::parse_error &e) {
            std::size_t line = 1, col = 1;
            std::string msg{ e.message() };// message() yields a string_view here
            if (!e.positions().empty()) {
                const auto &tp = e.positions().front();
                line = tp.line;
                col = tp.column;
                // If parsing actually progressed beyond the must<> throw point, that
                // further point is where the input really stopped making sense.
                if (g_furthest.byte > tp.byte) {
                    line = g_furthest.line;
                    col = g_furthest.column;
                    msg = "unexpected token";
                } else {
                    msg = friendly_message(msg);
                }
            } else {
                msg = friendly_message(msg);
            }
            report_syntax_error(source, source_name, line, col, msg);
            return nullptr;
        }
    }

    // ===================================================================
    // AST dumper
    // ===================================================================
    static void pad(std::ostream &os, int depth) {
        for (int i = 0; i < depth; ++i) os << "  ";
    }

    static void dump(const ast::MXASTNode *node, std::ostream &os, int depth) {
        if (!node) {
            pad(os, depth);
            os << "<null>\n";
            return;
        }
        if (auto *tu = dynamic_cast<const ast::TranslationUnit *>(node)) {
            pad(os, depth);
            os << "TranslationUnit\n";
            for (const auto &s : tu->statements) dump(s.get(), os, depth + 1);
        } else if (auto *fd = dynamic_cast<const ast::FunctionDef *>(node)) {
            pad(os, depth);
            os << "FunctionDef " << fd->name;
            if (fd->returnTypeName) os << " -> " << *fd->returnTypeName;
            os << "\n";
            for (const auto &p : fd->params) dump(p.get(), os, depth + 1);
            if (fd->body) dump(fd->body.get(), os, depth + 1);
        } else if (auto *p = dynamic_cast<const ast::Parameter *>(node)) {
            pad(os, depth);
            os << "Param " << p->name;
            if (p->typeName) os << ": " << *p->typeName;
            os << "\n";
        } else if (auto *cd = dynamic_cast<const ast::ClassDef *>(node)) {
            pad(os, depth);
            os << "Class " << cd->name;
            if (cd->baseType) os << " : " << *cd->baseType;
            os << "\n";
            for (const auto &m : cd->members) dump(m.get(), os, depth + 1);
        } else if (auto *fd = dynamic_cast<const ast::FieldDecl *>(node)) {
            pad(os, depth);
            os << "Field" << (fd->isStatic ? " static" : "") << (fd->isMut ? " mut" : "");
            for (const auto &nm : fd->names) os << " " << nm;
            if (fd->typeName) os << ": " << *fd->typeName;
            os << "\n";
            if (fd->value) dump(fd->value.get(), os, depth + 1);
        } else if (auto *md = dynamic_cast<const ast::MethodDef *>(node)) {
            pad(os, depth);
            os << "Method" << (md->isStatic ? " static" : "")
               << (md->isOverride ? " override" : "") << " " << md->name;
            if (md->returnTypeName) os << " -> " << *md->returnTypeName;
            os << "\n";
            for (const auto &p : md->params) dump(p.get(), os, depth + 1);
            if (md->body) dump(md->body.get(), os, depth + 1);
        } else if (auto *ct = dynamic_cast<const ast::ConstructorDef *>(node)) {
            pad(os, depth);
            os << "Constructor";
            if (ct->baseName) os << " : " << *ct->baseName;
            os << "\n";
            for (const auto &p : ct->params) dump(p.get(), os, depth + 1);
            if (ct->body) dump(ct->body.get(), os, depth + 1);
        } else if (auto *dt = dynamic_cast<const ast::DestructorDef *>(node)) {
            pad(os, depth);
            os << "Destructor\n";
            if (dt->body) dump(dt->body.get(), os, depth + 1);
        } else if (auto *opd = dynamic_cast<const ast::OperatorDef *>(node)) {
            pad(os, depth);
            os << "Operator" << (opd->isOverride ? " override" : "") << " '" << opd->op
               << "'";
            if (opd->returnTypeName) os << " -> " << *opd->returnTypeName;
            os << "\n";
            for (const auto &p : opd->params) dump(p.get(), os, depth + 1);
            if (opd->body) dump(opd->body.get(), os, depth + 1);
        } else if (auto *itf = dynamic_cast<const ast::InterfaceDef *>(node)) {
            pad(os, depth);
            os << "Interface " << itf->name;
            if (itf->baseType) os << " : " << *itf->baseType;
            os << "\n";
            for (const auto &m : itf->methods) dump(m.get(), os, depth + 1);
        } else if (auto *im = dynamic_cast<const ast::InterfaceMethod *>(node)) {
            pad(os, depth);
            os << "IMethod " << im->name;
            if (im->returnTypeName) os << " -> " << *im->returnTypeName;
            os << (im->body ? " (default)" : "") << "\n";
            for (const auto &p : im->params) dump(p.get(), os, depth + 1);
            if (im->body) dump(im->body.get(), os, depth + 1);
        } else if (auto *en = dynamic_cast<const ast::EnumDef *>(node)) {
            pad(os, depth);
            os << "Enum " << en->name << "\n";
            for (const auto &v : en->variants) dump(v.get(), os, depth + 1);
        } else if (auto *ev = dynamic_cast<const ast::EnumVariant *>(node)) {
            pad(os, depth);
            os << "Variant " << ev->name << "\n";
            for (const auto &p : ev->fields) dump(p.get(), os, depth + 1);
        } else if (auto *tdf = dynamic_cast<const ast::TypeDef *>(node)) {
            pad(os, depth);
            os << "Type " << tdf->name << "\n";
            for (const auto &f : tdf->fields) dump(f.get(), os, depth + 1);
        } else if (auto *tf = dynamic_cast<const ast::TypeField *>(node)) {
            pad(os, depth);
            os << "TypeField";
            for (const auto &nm : tf->names) os << " " << nm;
            if (tf->typeName) os << ": " << *tf->typeName;
            os << "\n";
        } else if (auto *b = dynamic_cast<const ast::Block *>(node)) {
            pad(os, depth);
            os << "Block\n";
            for (const auto &s : b->statements) dump(s.get(), os, depth + 1);
        } else if (auto *l = dynamic_cast<const ast::LetStatement *>(node)) {
            pad(os, depth);
            os << "Let" << (l->isMut ? " mut" : "");
            for (const auto &nm : l->names) os << " " << nm;
            if (l->typeName) os << ": " << *l->typeName;
            os << "\n";
            if (l->value) dump(l->value.get(), os, depth + 1);
        } else if (auto *r = dynamic_cast<const ast::ReturnStatement *>(node)) {
            pad(os, depth);
            os << "Return\n";
            if (r->value) dump(r->value.get(), os, depth + 1);
        } else if (auto *e = dynamic_cast<const ast::ExprStatement *>(node)) {
            pad(os, depth);
            os << "ExprStmt\n";
            if (e->expr) dump(e->expr.get(), os, depth + 1);
        } else if (auto *i = dynamic_cast<const ast::IfStatement *>(node)) {
            pad(os, depth);
            os << "If\n";
            dump(i->condition.get(), os, depth + 1);
            if (i->thenBlock) dump(i->thenBlock.get(), os, depth + 1);
            if (i->elseBranch) {
                pad(os, depth + 1);
                os << "Else\n";
                dump(i->elseBranch.get(), os, depth + 2);
            }
        } else if (auto *fr = dynamic_cast<const ast::ForInStatement *>(node)) {
            pad(os, depth);
            os << "For" << (fr->isMut ? " mut" : "") << " " << fr->var << " in\n";
            if (fr->iterable) dump(fr->iterable.get(), os, depth + 1);
            if (fr->body) dump(fr->body.get(), os, depth + 1);
        } else if (auto *lp = dynamic_cast<const ast::LoopStatement *>(node)) {
            pad(os, depth);
            os << "Loop\n";
            if (lp->body) dump(lp->body.get(), os, depth + 1);
        } else if (auto *u = dynamic_cast<const ast::UntilStatement *>(node)) {
            pad(os, depth);
            os << "Until\n";
            if (u->condition) dump(u->condition.get(), os, depth + 1);
            if (u->body) dump(u->body.get(), os, depth + 1);
        } else if (auto *du = dynamic_cast<const ast::DoUntilStatement *>(node)) {
            pad(os, depth);
            os << "DoUntil\n";
            if (du->body) dump(du->body.get(), os, depth + 1);
            if (du->condition) dump(du->condition.get(), os, depth + 1);
        } else if (dynamic_cast<const ast::BreakStatement *>(node)) {
            pad(os, depth);
            os << "Break\n";
        } else if (dynamic_cast<const ast::ContinueStatement *>(node)) {
            pad(os, depth);
            os << "Continue\n";
        } else if (auto *as = dynamic_cast<const ast::AssertStatement *>(node)) {
            pad(os, depth);
            os << "Assert\n";
            if (as->expr) dump(as->expr.get(), os, depth + 1);
        } else if (auto *df = dynamic_cast<const ast::DeferStatement *>(node)) {
            pad(os, depth);
            os << "Defer\n";
            if (df->body) dump(df->body.get(), os, depth + 1);
        } else if (auto *bo = dynamic_cast<const ast::BinaryOp *>(node)) {
            pad(os, depth);
            os << "BinaryOp '" << bo->op << "'\n";
            dump(bo->left.get(), os, depth + 1);
            dump(bo->right.get(), os, depth + 1);
        } else if (auto *uo = dynamic_cast<const ast::UnaryOp *>(node)) {
            pad(os, depth);
            os << "UnaryOp '" << uo->op << "'\n";
            dump(uo->operand.get(), os, depth + 1);
        } else if (auto *fc = dynamic_cast<const ast::FunctionCall *>(node)) {
            pad(os, depth);
            os << "Call " << fc->name << "\n";
            for (const auto &a : fc->args) dump(a.get(), os, depth + 1);
        } else if (auto *id = dynamic_cast<const ast::Identifier *>(node)) {
            pad(os, depth);
            os << "Identifier " << id->name << "\n";
        } else if (auto *il = dynamic_cast<const ast::IntegerLiteral *>(node)) {
            pad(os, depth);
            os << "Int " << il->value << "\n";
        } else if (auto *fl = dynamic_cast<const ast::FloatLiteral *>(node)) {
            pad(os, depth);
            os << "Float " << fl->value << "\n";
        } else if (auto *bl = dynamic_cast<const ast::BooleanLiteral *>(node)) {
            pad(os, depth);
            os << "Bool " << (bl->value ? "true" : "false") << "\n";
        } else if (auto *sl = dynamic_cast<const ast::StringLiteral *>(node)) {
            pad(os, depth);
            os << "String \"" << sl->value << "\"\n";
        } else if (dynamic_cast<const ast::NilLiteral *>(node)) {
            pad(os, depth);
            os << "Nil\n";
        } else {
            pad(os, depth);
            os << "<unknown node>\n";
        }
    }

    void dump_ast(const ast::MXASTNode &node, std::ostream &os) { dump(&node, os, 0); }

}// namespace mxs::frontend::parser
