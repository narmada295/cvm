#ifndef CVM_PARSER_H
#define CVM_PARSER_H

#include <stdexcept>
#include <string>
#include <vector>

#include "ast.h"
#include "token.h"

// Raised when the input does not match the grammar.
struct ParseError : std::runtime_error {
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Recursive-descent parser: tokens -> AST (a list of top-level statements).
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse a whole program. Throws ParseError on the first syntax error.
    std::vector<StmtPtr> parse();

private:
    std::vector<Token> tokens_;
    size_t current_ = 0;

    // --- token cursor helpers ---
    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& message);
    [[noreturn]] void error(const Token& token, const std::string& message);

    // --- grammar: declarations & statements ---
    StmtPtr declaration();
    StmtPtr funDeclaration();
    StmtPtr varDeclaration();
    StmtPtr statement();
    StmtPtr printStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr forStatement();
    StmtPtr returnStatement();
    StmtPtr breakStatement();
    StmtPtr continueStatement();
    StmtPtr block();
    StmtPtr expressionStatement();

    // --- grammar: expressions (precedence climbing) ---
    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr call();
    ExprPtr finishCall(ExprPtr callee);
    ExprPtr primary();
};

#endif // CVM_PARSER_H
