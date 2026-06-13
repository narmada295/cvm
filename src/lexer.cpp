#include "lexer.h"

#include <cctype>
#include <unordered_map>

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::STRING: return "STRING";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::LET: return "LET";
        case TokenType::FN: return "FN";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::WHILE: return "WHILE";
        case TokenType::FOR: return "FOR";
        case TokenType::PRINT: return "PRINT";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::NIL: return "NIL";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::BANG: return "BANG";
        case TokenType::BANG_EQUAL: return "BANG_EQUAL";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> kw = {
        {"let", TokenType::LET},     {"fn", TokenType::FN},
        {"return", TokenType::RETURN}, {"if", TokenType::IF},
        {"else", TokenType::ELSE},   {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"print", TokenType::PRINT}, {"true", TokenType::TRUE},
        {"false", TokenType::FALSE}, {"nil", TokenType::NIL},
        {"and", TokenType::AND},     {"or", TokenType::OR},
        {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE},
    };
    return kw;
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::scanTokens() {
    std::vector<Token> tokens;
    while (true) {
        skipWhitespaceAndComments();
        start_ = current_;
        if (isAtEnd()) {
            tokens.push_back(makeToken(TokenType::END_OF_FILE));
            break;
        }
        Token tok = scanToken();
        tokens.push_back(tok);
        if (tok.type == TokenType::ERROR) break;  // stop at first lexical error
    }
    return tokens;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) return false;
    current_++;
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\r' || c == '\t') {
            advance();
        } else if (c == '\n') {
            line_++;
            advance();
        } else if (c == '/' && peekNext() == '/') {
            // line comment: consume to end of line
            while (!isAtEnd() && peek() != '\n') advance();
        } else {
            break;
        }
    }
}

Token Lexer::makeToken(TokenType type) const {
    Token t;
    t.type = type;
    t.lexeme = source_.substr(start_, current_ - start_);
    t.line = line_;
    return t;
}

Token Lexer::errorToken(const std::string& message) const {
    Token t;
    t.type = TokenType::ERROR;
    t.lexeme = message;
    t.line = line_;
    return t;
}

Token Lexer::scanToken() {
    char c = advance();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return identifierToken();
    if (std::isdigit(static_cast<unsigned char>(c))) return numberToken();

    switch (c) {
        case '(': return makeToken(TokenType::LPAREN);
        case ')': return makeToken(TokenType::RPAREN);
        case '{': return makeToken(TokenType::LBRACE);
        case '}': return makeToken(TokenType::RBRACE);
        case '[': return makeToken(TokenType::LBRACKET);
        case ']': return makeToken(TokenType::RBRACKET);
        case ',': return makeToken(TokenType::COMMA);
        case ';': return makeToken(TokenType::SEMICOLON);
        case ':': return makeToken(TokenType::COLON);
        case '+': return makeToken(TokenType::PLUS);
        case '-': return makeToken(TokenType::MINUS);
        case '*': return makeToken(TokenType::STAR);
        case '/': return makeToken(TokenType::SLASH);
        case '%': return makeToken(TokenType::PERCENT);
        case '!': return makeToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
        case '=': return makeToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
        case '<': return makeToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
        case '>': return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
        case '"': return stringToken();
    }
    return errorToken(std::string("Unexpected character '") + c + "'.");
}

Token Lexer::stringToken() {
    std::string value;
    while (!isAtEnd() && peek() != '"') {
        char c = advance();
        if (c == '\n') line_++;
        if (c == '\\' && !isAtEnd()) {
            // minimal escape support
            char next = advance();
            switch (next) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                default: value.push_back(next); break;
            }
        } else {
            value.push_back(c);
        }
    }
    if (isAtEnd()) return errorToken("Unterminated string.");
    advance();  // closing quote

    Token t = makeToken(TokenType::STRING);
    t.lexeme = value;  // store the decoded contents, not the quoted text
    return t;
}

Token Lexer::numberToken() {
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance();  // consume '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    return makeToken(TokenType::NUMBER);
}

Token Lexer::identifierToken() {
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    std::string text = source_.substr(start_, current_ - start_);
    auto it = keywords().find(text);
    return makeToken(it != keywords().end() ? it->second : TokenType::IDENTIFIER);
}
