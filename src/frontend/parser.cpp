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
                                  g::type_spec, g::unary_op, g::multiplicative_op,
                                  g::additive_op, g::range_op, g::relational_op,
                                  g::equality_op, g::logic_and_op, g::logic_or_op,
                                  g::assign_op>,
            pt::remove_content::on<g::K_MUT, g::let_stmt, g::return_stmt,
                                   g::expression_stmt, g::block, g::func_def, g::func_sig,
                                   g::param, g::call_args>,
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
        auto s = mk<ast::LetStatement>();
        std::vector<const Node *> rest;
        for (const auto &c : n.children) {
            if (c->is_type<g::K_MUT>()) {
                s->isMut = true;
                continue;
            }
            rest.push_back(c.get());
        }
        std::size_t idx = 0;
        while (idx < rest.size() && rest[idx]->is_type<g::identifier>())
            s->names.push_back(content_of(*rest[idx++]));
        if (idx < rest.size() && rest[idx]->is_type<g::type_spec>())
            s->typeName = content_of(*rest[idx++]);
        if (idx < rest.size()) s->value = to_expr(*rest[idx]);
        return s;
    }

    static void parse_sig(const Node &sig, ast::FunctionDef &f) {
        for (const auto &c : sig.children) {
            if (c->is_type<g::param>()) {
                std::string ty;
                std::vector<std::string> names;
                for (const auto &pc : c->children) {
                    if (pc->is_type<g::identifier>())
                        names.push_back(content_of(*pc));
                    else if (pc->is_type<g::type_spec>())
                        ty = content_of(*pc);
                }
                for (const auto &nm : names) {
                    auto p = mk<ast::Parameter>();
                    p->name = nm;
                    if (!ty.empty()) p->typeName = ty;
                    f.params.push_back(std::move(p));
                }
            } else if (c->is_type<g::type_spec>()) {
                f.returnTypeName = content_of(*c);
            }
        }
    }

    static std::unique_ptr<ast::MXASTNode> to_func_def(const Node &n) {
        auto f = mk<ast::FunctionDef>();
        for (const auto &c : n.children) {
            if (c->is_type<g::identifier>() && f->name.empty())
                f->name = content_of(*c);
            else if (c->is_type<g::func_sig>())
                parse_sig(*c, *f);
            else if (c->is_type<g::block>())
                f->body = to_block(*c);
        }
        return f;
    }

    static std::unique_ptr<ast::MXASTNode> to_stmt(const Node &n) {
        if (n.is_type<g::func_def>()) return to_func_def(n);
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
        if (n.is_type<g::block>()) return to_block(n);
        return nullptr;// out of task01 scope (control flow / class / match / ...)
    }

    std::unique_ptr<ast::TranslationUnit> parse_to_ast(std::string_view source,
                                                       std::string_view source_name) {
        try {
            pegtl::memory_input<> in(source.data(), source.size(),
                                     std::string(source_name));
            auto root = pt::parse<g::grammar, selector>(in);
            if (!root) return nullptr;
            auto tu = mk<ast::TranslationUnit>();
            for (const auto &c : root->children)
                if (auto s = to_stmt(*c)) tu->statements.push_back(std::move(s));
            return tu;
        } catch (const pegtl::parse_error &e) {
            std::cerr << "parse error: " << e.what() << "\n";
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
