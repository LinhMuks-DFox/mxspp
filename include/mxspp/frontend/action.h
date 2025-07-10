#pragma once
#include "ast.h"
#include "grammer.hpp"
#include <tao/pegtl.hpp>

namespace mxs::frontend::actions {
    namespace pegtl = tao::pegtl;
    namespace grammar = mxs::frontend::grammar;
    namespace ast = mxs::frontend::ast;
    using NodePtr = std::unique_ptr<mxs::frontend::ast::MXASTNode>;
    struct AstBuilderState {
        std::vector<NodePtr> node_stack;

        // 你可以在这里添加其他状态，例如符号表、作用域栈等
        // ScopeManager scope_manager;
    };

    template<typename Rule>
    struct action : pegtl::nothing<Rule> { };
    template<>
    struct action<grammar::integer_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, AstBuilderState &state) {
            // --- 步骤 1: 从输入中提取数据 ---
            // in.string() 返回匹配到的文本，例如 "123"
            const int64_t val = std::stoll(in.string());

            // --- 步骤 2: 创建你的 AST 节点 ---
            // 这里我们调用 IntegerLiteral 的构造函数来创建一个实例。
            // 注意：你需要确保你的 IntegerLiteral 类有一个接收 int64_t 的构造函数。
            // 例如:
            // explicit IntegerLiteral(int64_t v) : value(v) {}
            auto node = std::make_unique<ast::IntegerLiteral>(val);

            // --- 步骤 3: 将新节点压入状态栈 ---
            // 这个节点现在位于栈顶，等待被它的父节点（例如一个二元表达式）使用。
            state.node_stack.push_back(std::move(node));
        }
    };
}