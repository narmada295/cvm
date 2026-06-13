#ifndef CVM_TOKEN_H
#define CVM_TOKEN_H

#include <string>

// All token categories the lexer can produce.
enum class TokenType {
    // Literals
    NUMBER, STRING, IDENTIFIER,

    // Keywords
    LET, FN, RETURN, IF, ELSE, WHILE, FOR, PRINT,
    TRUE, FALSE, NIL, AND, OR, BREAK, CONTINUE,

    // Single / double character operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    LESS, LESS_EQUAL,
    GREATER, GREATER_EQUAL,

    // Punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, SEMICOLON, COLON,

    // Control
    END_OF_FILE,
    ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;  // the raw text
    int line;
};

// Human-readable name, used by the --tokens debug mode.
const char* tokenTypeName(TokenType type);

#endif // CVM_TOKEN_H
