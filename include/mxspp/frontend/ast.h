#pragma once
#include "mxspp/backend/codegen.h"
#include "mxspp/core/MXObject.h"

namespace mxs::frontend {
    namespace ast {

        // ============================
        // Base AST Node
        // ============================
        class MXASTNode : public virtual core::MXObject {
        public:
            virtual ~MXASTNode() = default;
            MXASTNode(bool is_static) : core::MXObject(is_static) { }
        };

        // ============================
        // Statement / Expression base
        // ============================
        class Statement : public virtual MXASTNode {
        public:
            virtual void codegen(mxs::backend::codegen::CodegenContext &ctx) const = 0;
        };

        class Expression : public virtual MXASTNode {
        public:
            virtual llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const = 0;
        };

        // ============================
        // Program Top Level (TranslationUnit)
        // ============================
        class TranslationUnit : public virtual MXASTNode {
        public:
            std::vector<std::unique_ptr<Statement>> statements;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const;
        };

        // ============================
        // Block of Statements
        // ============================
        class Block : public virtual Statement {
        public:
            std::vector<std::unique_ptr<Statement>> statements;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // ============================
        // Statement Nodes
        // ============================
        class LetStatement : public virtual Statement {
        public:
            std::vector<std::string> names;
            std::unique_ptr<Expression> value;
            std::optional<std::string> typeName;
            bool isMut = false;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ExprStatement : public virtual Statement {
        public:
            std::unique_ptr<Expression> expr;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class IfStatement : public virtual Statement {
        public:
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> thenBlock;
            std::unique_ptr<Block> elseBlock;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ReturnStatement : public virtual Statement {
        public:
            std::unique_ptr<Expression> value;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ForInStatement : public virtual Statement {
        public:
            std::string var;
            std::unique_ptr<Expression> iterable;
            std::unique_ptr<Block> body;
            bool isMut = false;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class LoopStatement : public virtual Statement {
        public:
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BreakStatement : public virtual Statement {
        public:
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ContinueStatement : public virtual Statement {
        public:
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // ============================
        // Expression Nodes
        // ============================
        class Identifier : public virtual Expression {
        public:
            std::string name;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class IntegerLiteral : public virtual Expression {
        public:
            IntegerLiteral(int64_t value, bool is_static);
            int64_t value;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class FloatLiteral : public virtual Expression {
        public:
            double value;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BooleanLiteral : public virtual Expression {
        public:
            bool value;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class StringLiteral : public virtual Expression {
        public:
            std::string value;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BinaryOp : public virtual Expression {
        public:
            std::unique_ptr<Expression> left;
            std::string op;
            std::unique_ptr<Expression> right;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class UnaryOp : public virtual Expression {
        public:
            std::string op;
            std::unique_ptr<Expression> operand;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class FunctionCall : public virtual Expression {
        public:
            std::string name;
            std::vector<std::unique_ptr<Expression>> args;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class MatchStatment : public virtual Statement { };

    }// namespace mxs::ast
}