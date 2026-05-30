#pragma once
#include "mxspp/backend/codegen.h"
#include "mxspp/core/MXObject.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mxs::frontend {
    namespace ast {

        // ============================
        // Base AST Node
        // ============================
        // Every concrete node initializes the virtual core::MXObject base directly
        // (it has no default ctor); is_static defaults to false until binding analysis
        // exists. codegen() bodies are stubbed in ast.cpp (real codegen is a later progress).
        class MXASTNode : public virtual core::MXObject {
        public:
            virtual ~MXASTNode() = default;
            explicit MXASTNode(bool is_static) : core::MXObject(is_static) { }
        };

        // ============================
        // Statement / Expression base
        // ============================
        class Statement : public virtual MXASTNode {
        public:
            Statement() : core::MXObject(false), MXASTNode(false) { }
            virtual void codegen(mxs::backend::codegen::CodegenContext &ctx) const = 0;
        };

        class Expression : public virtual MXASTNode {
        public:
            Expression() : core::MXObject(false), MXASTNode(false) { }
            virtual llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const = 0;
        };

        // ============================
        // Program Top Level (TranslationUnit)
        // ============================
        class TranslationUnit : public virtual MXASTNode {
        public:
            TranslationUnit() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<MXASTNode>> statements;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const;
        };

        // ============================
        // Block of Statements
        // ============================
        class Block : public virtual Statement {
        public:
            Block() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<MXASTNode>> statements;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // ============================
        // Statement Nodes
        // ============================
        class LetStatement : public virtual Statement {
        public:
            LetStatement() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::string> names;
            std::unique_ptr<Expression> value;
            std::optional<std::string> typeName;
            bool isMut = false;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ExprStatement : public virtual Statement {
        public:
            ExprStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> expr;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class IfStatement : public virtual Statement {
        public:
            IfStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> thenBlock;
            std::unique_ptr<Block> elseBlock;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ReturnStatement : public virtual Statement {
        public:
            ReturnStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> value;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ForInStatement : public virtual Statement {
        public:
            ForInStatement() : core::MXObject(false), MXASTNode(false) { }
            std::string var;
            std::unique_ptr<Expression> iterable;
            std::unique_ptr<Block> body;
            bool isMut = false;

            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class LoopStatement : public virtual Statement {
        public:
            LoopStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BreakStatement : public virtual Statement {
        public:
            BreakStatement() : core::MXObject(false), MXASTNode(false) { }
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ContinueStatement : public virtual Statement {
        public:
            ContinueStatement() : core::MXObject(false), MXASTNode(false) { }
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // A function parameter: name + optional type annotation. Not a Statement/Expression.
        class Parameter : public virtual MXASTNode {
        public:
            Parameter() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::optional<std::string> typeName;
        };

        // A top-level (or nested) function definition.
        class FunctionDef : public virtual Statement {
        public:
            FunctionDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> returnTypeName;
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // ============================
        // Expression Nodes
        // ============================
        class Identifier : public virtual Expression {
        public:
            Identifier() : core::MXObject(false), MXASTNode(false) { }
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
            FloatLiteral() : core::MXObject(false), MXASTNode(false) { }
            double value = 0.0;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BooleanLiteral : public virtual Expression {
        public:
            BooleanLiteral() : core::MXObject(false), MXASTNode(false) { }
            bool value = false;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class StringLiteral : public virtual Expression {
        public:
            StringLiteral() : core::MXObject(false), MXASTNode(false) { }
            std::string value;
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class NilLiteral : public virtual Expression {
        public:
            NilLiteral() : core::MXObject(false), MXASTNode(false) { }
            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class BinaryOp : public virtual Expression {
        public:
            BinaryOp() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> left;
            std::string op;
            std::unique_ptr<Expression> right;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class UnaryOp : public virtual Expression {
        public:
            UnaryOp() : core::MXObject(false), MXASTNode(false) { }
            std::string op;
            std::unique_ptr<Expression> operand;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class FunctionCall : public virtual Expression {
        public:
            FunctionCall() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Expression>> args;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class MatchStatement : public virtual Statement {
        public:
            MatchStatement() : core::MXObject(false), MXASTNode(false) { }
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

    }// namespace ast
}// namespace mxs::frontend
