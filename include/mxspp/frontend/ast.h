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
            std::unique_ptr<Statement>
                    elseBranch;// a Block, or another IfStatement (else-if)

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

        class UntilStatement
            : public virtual Statement {// pre-test: runs while cond is false
        public:
            UntilStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class DoUntilStatement
            : public virtual Statement {// post-test: runs at least once
        public:
            DoUntilStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
            std::unique_ptr<Expression> condition;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class AssertStatement : public virtual Statement {
        public:
            AssertStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> expr;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class DeferStatement : public virtual Statement {
        public:
            DeferStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
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
            bool isForeign = false;// @@foreign: bodyless, bound to an external symbol
            std::string foreignSymbol;// external symbol (defaults to `name` if empty)
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // ============================
        // OOP: definitions and members
        // ============================
        // A class field (or @@POD-style member). isStatic set for `static let`.
        class FieldDecl : public virtual Statement {
        public:
            FieldDecl() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::string> names;
            std::optional<std::string> typeName;
            std::unique_ptr<Expression> value;
            bool isMut = false;
            bool isStatic = false;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class MethodDef : public virtual Statement {
        public:
            MethodDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> returnTypeName;
            std::unique_ptr<Block> body;
            bool isOverride = false;
            bool isStatic = false;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ConstructorDef : public virtual Statement {
        public:
            ConstructorDef() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> baseName;// base-class ctor invoked, if any
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class DestructorDef : public virtual Statement {
        public:
            DestructorDef() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class OperatorDef : public virtual Statement {
        public:
            OperatorDef() : core::MXObject(false), MXASTNode(false) { }
            std::string op;
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> returnTypeName;
            std::unique_ptr<Block> body;
            bool isOverride = false;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        class ClassDef : public virtual Statement {
        public:
            ClassDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::optional<std::string> baseType;
            std::vector<std::unique_ptr<MXASTNode>>
                    members;// FieldDecl/Method/Ctor/Dtor/Operator
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // An interface method (signature, with an optional default body). Not a Statement.
        class InterfaceMethod : public virtual MXASTNode {
        public:
            InterfaceMethod() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> returnTypeName;
            std::unique_ptr<Block> body;
        };

        class InterfaceDef : public virtual Statement {
        public:
            InterfaceDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::optional<std::string> baseType;
            std::vector<std::unique_ptr<InterfaceMethod>> methods;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // An enum variant, optionally carrying typed fields. Not a Statement.
        class EnumVariant : public virtual MXASTNode {
        public:
            EnumVariant() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Parameter>> fields;
        };

        class EnumDef : public virtual Statement {
        public:
            EnumDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<EnumVariant>> variants;
            void codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

        // A plain-struct (`type`) field. Not a Statement.
        class TypeField : public virtual MXASTNode {
        public:
            TypeField() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::string> names;
            std::optional<std::string> typeName;
        };

        class TypeDef : public virtual Statement {
        public:
            TypeDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<TypeField>> fields;
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

        // A match expression (docs §6 error model / pattern matching): evaluates `subject`,
        // tries each case in order, and yields the matching arm's body value. Patterns:
        // type-binding (`name: Type` — binds when the subject is of that type; central to the
        // match-based error handling, e.g. `case err: Error => ...`), wildcard `_`, a plain
        // binding `name` (always matches), or a literal (matches by equality). An arm body is
        // an expression or a block (a block yields its last expression-statement's value).
        class MatchExpr : public virtual Expression {
        public:
            MatchExpr() : core::MXObject(false), MXASTNode(false) { }
            struct Case {
                std::string binding;// bound name ("" if none/wildcard/literal)
                std::optional<std::string> typeName;// type-binding pattern's type
                std::unique_ptr<Expression> literal;// literal pattern (else null)
                bool isWildcard = false;
                std::unique_ptr<MXASTNode> body;// an Expression or a Block
            };
            std::unique_ptr<Expression> subject;
            std::vector<Case> cases;

            llvm::Value *
            codegen(mxs::backend::codegen::CodegenContext &ctx) const override;
        };

    }// namespace ast
}// namespace mxs::frontend
