#ifndef CVM_LEXER_H
#define CVM_LEXER_H

#include <string>
#include <vector>

#include "token.h"

// Converts raw source text into a flat list of tokens.
class Lexer {
public:
    explicit Lexer(std::string source);

    // Tokenize the whole input. The final token is always END_OF_FILE.
    // On a lexical error an ERROR token is emitted (lexeme = message).
    std::vector<Token> scanTokens();

private:
    std::string source_;
    size_t start_ = 0;    // start of the token currently being scanned
    size_t current_ = 0;  // current scan position
    int line_ = 1;

    bool isAtEnd() const { return current_ >= source_.size(); }
    char advance() { return source_[current_++]; }
    char peek() const { return isAtEnd() ? '\0' : source_[current_]; }
    char peekNext() const {
        return current_ + 1 >= source_.size() ? '\0' : source_[current_ + 1];
    }
    bool match(char expected);

    void skipWhitespaceAndComments();
    Token scanToken();
    Token makeToken(TokenType type) const;
    Token errorToken(const std::string& message) const;
    Token stringToken();
    Token numberToken();
    Token identifierToken();
};

#endif // CVM_LEXER_H
