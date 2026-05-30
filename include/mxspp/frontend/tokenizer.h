#pragma once

#include "grammar.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <utility>

#include <tao/pegtl.hpp>

namespace mxs::frontend::tokenizer {
    namespace pegtl = tao::pegtl;
    namespace grammar = mxs::frontend::grammar;

    struct SourcePosition {
        std::size_t line = 1;
        std::size_t column = 1;
        std::size_t offset = 0;
    };

    enum class TokenKind {
        Identifier,
        IntegerLiteral,
        FloatLiteral,
        StringLiteral,
        BooleanLiteral,
        NilLiteral,
        Keyword,
        Operator,
        Punctuation,
        Annotation,
        EndOfFile
    };

    using TokenValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

    struct Token {
        TokenKind kind;
        std::string lexeme;
        TokenValue value;
        SourcePosition position;
    };

    namespace detail {
        inline std::string unescape_string(std::string_view lexeme) {
            if (lexeme.size() < 2) {
                return std::string(lexeme);
            }
            std::string result;
            result.reserve(lexeme.size());
            for (std::size_t i = 1; i + 1 < lexeme.size(); ++i) {
                const char ch = lexeme[i];
                if (ch == '\\' && i + 1 < lexeme.size() - 1) {
                    const char next = lexeme[++i];
                    switch (next) {
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case '\\': result.push_back('\\'); break;
                    case '"': result.push_back('"'); break;
                    case '0': result.push_back('\0'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    default: result.push_back(next); break;
                    }
                } else {
                    result.push_back(ch);
                }
            }
            return result;
        }

        struct annotation_marker : pegtl::string<'@', '@'> { };

        struct keyword_token
            : pegtl::sor<grammar::K_AS, grammar::K_ASSERT, grammar::K_BREAK,
                         grammar::K_CASE, grammar::K_CLASS, grammar::K_CONTINUE,
                         grammar::K_DEFER, grammar::K_DO, grammar::K_DYNAMIC,
                         grammar::K_ELSE, grammar::K_ENUM, grammar::K_EXPORT,
                         grammar::K_FOR, grammar::K_FUNC, grammar::K_IF,
                         grammar::K_IMPORT, grammar::K_IN, grammar::K_INTERFACE,
                         grammar::K_LET, grammar::K_LOOP, grammar::K_MATCH,
                         grammar::K_MUT, grammar::K_OPERATOR, grammar::K_OVERRIDE,
                         grammar::K_PRIVATE, grammar::K_PUBLIC, grammar::K_RAISE,
                         grammar::K_RETURN, grammar::K_STATIC, grammar::K_TYPE,
                         grammar::K_UNTIL> { };

        struct operator_token
            : pegtl::sor<pegtl::string<'=', '>'>, pegtl::string<'-', '>'>,
                         pegtl::string<'+', '='>, pegtl::string<'-', '='>,
                         pegtl::string<'*', '='>, pegtl::string<'/', '='>,
                         pegtl::string<'=', '='>, pegtl::string<'!', '='>,
                         pegtl::string<'<', '='>, pegtl::string<'>', '='>,
                         pegtl::string<'|', '|'>, pegtl::string<'&', '&'>,
                         pegtl::string<'.', '.'>, pegtl::one<'+'>, pegtl::one<'-'>,
                         pegtl::one<'*'>, pegtl::one<'/'>, pegtl::one<'%'>,
                         pegtl::one<'!'>, pegtl::one<'?'>, pegtl::one<'='>,
                         pegtl::one<'<'>, pegtl::one<'>'>, pegtl::one<'~'>
            > { };

        struct punctuation_token
            : pegtl::sor<pegtl::one<'('>, pegtl::one<')'>, pegtl::one<'{'>,
                         pegtl::one<'}'>, pegtl::one<'['>, pegtl::one<']'>,
                         pegtl::one<','>, pegtl::one<';'>, pegtl::one<':'>,
                         pegtl::one<'.'>
            > { };

        struct token
            : pegtl::sor<annotation_marker, grammar::float_literal,
                         grammar::integer_literal, grammar::string_literal,
                         grammar::bool_literal, grammar::nil_literal, keyword_token,
                         operator_token, punctuation_token, grammar::identifier> { };

        struct token_sequence
            : pegtl::star<pegtl::seq<token, grammar::ignored>> { };

        struct grammar : pegtl::must<grammar::ignored, token_sequence, pegtl::eof> { };
    }// namespace detail

    struct TokenizerState {
        std::vector<Token> tokens;

        template<typename ActionInput>
        void push(TokenKind kind, const ActionInput &in) {
            push(kind, in, std::monostate{});
        }

        template<typename ActionInput, typename Value>
        void push(TokenKind kind, const ActionInput &in, Value &&value) {
            const auto pos = in.position();
            tokens.push_back(Token{
                kind,
                std::string(in.string()),
                TokenValue(std::forward<Value>(value)),
                SourcePosition{pos.line(), pos.column(), pos.byte()}
            });
        }
    };

    template<typename Rule>
    struct action : pegtl::nothing<Rule> { };

    template<>
    struct action<detail::annotation_marker> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::Annotation, in);
        }
    };

    template<>
    struct action<grammar::integer_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            const int64_t value = std::stoll(in.string());
            state.push(TokenKind::IntegerLiteral, in, value);
        }
    };

    template<>
    struct action<grammar::float_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            const double value = std::stod(in.string());
            state.push(TokenKind::FloatLiteral, in, value);
        }
    };

    template<>
    struct action<grammar::bool_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::BooleanLiteral, in, in.string() == "true");
        }
    };

    template<>
    struct action<grammar::nil_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::NilLiteral, in);
        }
    };

    template<>
    struct action<grammar::string_literal> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::StringLiteral, in,
                       detail::unescape_string(in.string()));
        }
    };

    template<>
    struct action<detail::keyword_token> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::Keyword, in);
        }
    };

    template<>
    struct action<detail::operator_token> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::Operator, in);
        }
    };

    template<>
    struct action<detail::punctuation_token> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::Punctuation, in);
        }
    };

    template<>
    struct action<grammar::identifier> {
        template<typename ActionInput>
        static void apply(const ActionInput &in, TokenizerState &state) {
            state.push(TokenKind::Identifier, in);
        }
    };

    inline std::vector<Token>
    tokenize(std::string_view source, std::string_view source_name = "<memory>") {
        pegtl::memory_input input(source, source_name);
        TokenizerState state;
        pegtl::parse<detail::grammar, action>(input, state);

        const auto pos = input.position();
        state.tokens.push_back(Token{
            TokenKind::EndOfFile,
            std::string{},
            TokenValue{},
            SourcePosition{pos.line(), pos.column(), pos.byte()}
        });

        return state.tokens;
    }
}// namespace mxs::frontend::tokenizer
