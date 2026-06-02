#pragma once
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
        // exists. Codegen consumes the AST via dynamic_cast (see backend/codegen.cpp).
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
        };

        class Expression : public virtual MXASTNode {
        public:
            Expression() : core::MXObject(false), MXASTNode(false) { }
        };

        // ============================
        // Program Top Level (TranslationUnit)
        // ============================
        class TranslationUnit : public virtual MXASTNode {
        public:
            TranslationUnit() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<MXASTNode>> statements;
        };

        // ============================
        // Block of Statements
        // ============================
        class Block : public virtual Statement {
        public:
            Block() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<MXASTNode>> statements;
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
        };

        class ExprStatement : public virtual Statement {
        public:
            ExprStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> expr;
        };

        class IfStatement : public virtual Statement {
        public:
            IfStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> thenBlock;
            std::unique_ptr<Statement>
                    elseBranch;// a Block, or another IfStatement (else-if)
        };

        class ReturnStatement : public virtual Statement {
        public:
            ReturnStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> value;
        };

        class ForInStatement : public virtual Statement {
        public:
            ForInStatement() : core::MXObject(false), MXASTNode(false) { }
            std::string var;
            std::unique_ptr<Expression> iterable;
            std::unique_ptr<Block> body;
            bool isMut = false;
        };

        class LoopStatement : public virtual Statement {
        public:
            LoopStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
        };

        class BreakStatement : public virtual Statement {
        public:
            BreakStatement() : core::MXObject(false), MXASTNode(false) { }
        };

        class ContinueStatement : public virtual Statement {
        public:
            ContinueStatement() : core::MXObject(false), MXASTNode(false) { }
        };

        class UntilStatement
            : public virtual Statement {// pre-test: runs while cond is false
        public:
            UntilStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> body;
        };

        class DoUntilStatement
            : public virtual Statement {// post-test: runs at least once
        public:
            DoUntilStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
            std::unique_ptr<Expression> condition;
        };

        class AssertStatement : public virtual Statement {
        public:
            AssertStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> expr;
        };

        class DeferStatement : public virtual Statement {
        public:
            DeferStatement() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
        };

        // An `import` statement (progress13 D2, "1+3"). `path` is the dotted fqdn split into
        // segments (`std.io` -> {"std","io"}). The three forms:
        //   `import std.io;`              -> alias=nil, selected=nil  (qualified by last segment)
        //   `import std.io as o;`         -> alias="o", selected=nil  (qualified by `o`)
        //   `import std.io.{println, …};` -> selected={"println",…}   (those names unqualified)
        // The loader (driver) resolves `path` to an `std/<path>.mxs` file, parses it, and merges
        // its top-level @@foreign declarations into the importing TU per the form. Import nodes are
        // removed before codegen (codegen never sees them).
        class Import : public virtual Statement {
        public:
            Import() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::string> path;
            std::optional<std::string> alias;
            std::optional<std::vector<std::string>> selected;
        };

        // A function parameter: name + optional type annotation. Not a Statement/Expression.
        // `isRest` marks a variadic rest parameter `...name: type` (progress12 D-VARARG): at a call,
        // the surplus arguments are packed into an MXArrayList bound to this name. Only the last
        // parameter of a signature may be a rest.
        class Parameter : public virtual MXASTNode {
        public:
            Parameter() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::optional<std::string> typeName;
            bool isRest = false;
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
        };

        class ConstructorDef : public virtual Statement {
        public:
            ConstructorDef() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> baseName;// base-class ctor invoked, if any
            std::unique_ptr<Block> body;
        };

        class DestructorDef : public virtual Statement {
        public:
            DestructorDef() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Block> body;
        };

        class OperatorDef : public virtual Statement {
        public:
            OperatorDef() : core::MXObject(false), MXASTNode(false) { }
            std::string op;
            std::vector<std::unique_ptr<Parameter>> params;
            std::optional<std::string> returnTypeName;
            std::unique_ptr<Block> body;
            bool isOverride = false;
        };

        class ClassDef : public virtual Statement {
        public:
            ClassDef() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::optional<std::string> baseType;
            std::vector<std::unique_ptr<MXASTNode>>
                    members;// FieldDecl/Method/Ctor/Dtor/Operator
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
        };

        // ============================
        // Expression Nodes
        // ============================
        class Identifier : public virtual Expression {
        public:
            Identifier() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
        };

        class IntegerLiteral : public virtual Expression {
        public:
            IntegerLiteral(int64_t value, bool is_static)
                : core::MXObject(is_static), MXASTNode(is_static), value(value) { }
            int64_t value;
        };

        class FloatLiteral : public virtual Expression {
        public:
            FloatLiteral() : core::MXObject(false), MXASTNode(false) { }
            double value = 0.0;
        };

        class BooleanLiteral : public virtual Expression {
        public:
            BooleanLiteral() : core::MXObject(false), MXASTNode(false) { }
            bool value = false;
        };

        class StringLiteral : public virtual Expression {
        public:
            StringLiteral() : core::MXObject(false), MXASTNode(false) { }
            std::string value;
        };

        class NilLiteral : public virtual Expression {
        public:
            NilLiteral() : core::MXObject(false), MXASTNode(false) { }
        };

        class BinaryOp : public virtual Expression {
        public:
            BinaryOp() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> left;
            std::string op;
            std::unique_ptr<Expression> right;
        };

        class UnaryOp : public virtual Expression {
        public:
            UnaryOp() : core::MXObject(false), MXASTNode(false) { }
            std::string op;
            std::unique_ptr<Expression> operand;
        };

        class FunctionCall : public virtual Expression {
        public:
            FunctionCall() : core::MXObject(false), MXASTNode(false) { }
            std::string name;
            std::vector<std::unique_ptr<Expression>> args;
            // For a method call `obj.m(args)`, `receiver` is the object expression and `name` is
            // the method name; for a plain / `@@foreign` call `f(args)`, `receiver` is null.
            std::unique_ptr<Expression> receiver;
        };

        // An ArrayList literal: `[a, b, c]` (docs §3.3). Object-mode only.
        class ListLiteral : public virtual Expression {
        public:
            ListLiteral() : core::MXObject(false), MXASTNode(false) { }
            std::vector<std::unique_ptr<Expression>> elements;
        };

        // Member access (read): `target.name` (e.g. `err.msg`). Object-mode only.
        class MemberExpr : public virtual Expression {
        public:
            MemberExpr() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> target;
            std::string name;
        };

        // A subscript: `target[index]` (e.g. ArrayList element access). Object-mode only.
        class IndexExpr : public virtual Expression {
        public:
            IndexExpr() : core::MXObject(false), MXASTNode(false) { }
            std::unique_ptr<Expression> target;
            std::unique_ptr<Expression> index;
        };

        class MatchStatement : public virtual Statement {
        public:
            MatchStatement() : core::MXObject(false), MXASTNode(false) { }
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
        };

    }// namespace ast
}// namespace mxs::frontend
