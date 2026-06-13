#include "parser.h"

#include <cstdlib>

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> statements;
    while (!isAtEnd()) {
        statements.push_back(declaration());
    }
    return statements;
}

// --- cursor helpers --------------------------------------------------------
const Token& Parser::advance() {
    if (!isAtEnd()) current_++;
    return previous();
}

bool Parser::check(TokenType type) const {
    return !isAtEnd() && peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(peek(), message);
}

void Parser::error(const Token& token, const std::string& message) {
    std::string where = token.type == TokenType::END_OF_FILE
                            ? "end of input"
                            : "'" + token.lexeme + "'";
    throw ParseError("[line " + std::to_string(token.line) + "] Syntax error at " +
                     where + ": " + message);
}

// --- declarations & statements --------------------------------------------
StmtPtr Parser::declaration() {
    if (match(TokenType::FN)) return funDeclaration();
    if (match(TokenType::LET)) return varDeclaration();
    return statement();
}

StmtPtr Parser::funDeclaration() {
    int line = previous().line;
    const Token& name = consume(TokenType::IDENTIFIER, "Expected function name.");
    auto stmt = std::make_unique<Stmt>(StmtKind::Function, line);
    stmt->name = name.lexeme;

    consume(TokenType::LPAREN, "Expected '(' after function name.");
    if (!check(TokenType::RPAREN)) {
        do {
            const Token& param = consume(TokenType::IDENTIFIER, "Expected parameter name.");
            stmt->params.push_back(param.lexeme);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected ')' after parameters.");

    consume(TokenType::LBRACE, "Expected '{' before function body.");
    StmtPtr body = block();
    stmt->body = std::move(body->body);  // unwrap the block's statements
    return stmt;
}

StmtPtr Parser::varDeclaration() {
    int line = previous().line;
    const Token& name = consume(TokenType::IDENTIFIER, "Expected variable name.");
    auto stmt = std::make_unique<Stmt>(StmtKind::Let, line);
    stmt->name = name.lexeme;
    if (match(TokenType::EQUAL)) {
        stmt->expr = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
    return stmt;
}

StmtPtr Parser::statement() {
    if (match(TokenType::PRINT)) return printStatement();
    if (match(TokenType::IF)) return ifStatement();
    if (match(TokenType::WHILE)) return whileStatement();
    if (match(TokenType::FOR)) return forStatement();
    if (match(TokenType::RETURN)) return returnStatement();
    if (match(TokenType::BREAK)) return breakStatement();
    if (match(TokenType::CONTINUE)) return continueStatement();
    if (match(TokenType::LBRACE)) return block();
    return expressionStatement();
}

StmtPtr Parser::printStatement() {
    int line = previous().line;
    auto stmt = std::make_unique<Stmt>(StmtKind::Print, line);
    stmt->expr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after value.");
    return stmt;
}

StmtPtr Parser::ifStatement() {
    int line = previous().line;
    consume(TokenType::LPAREN, "Expected '(' after 'if'.");
    auto stmt = std::make_unique<Stmt>(StmtKind::If, line);
    stmt->expr = expression();
    consume(TokenType::RPAREN, "Expected ')' after condition.");
    stmt->thenBranch = statement();
    if (match(TokenType::ELSE)) {
        stmt->elseBranch = statement();
    }
    return stmt;
}

StmtPtr Parser::whileStatement() {
    int line = previous().line;
    consume(TokenType::LPAREN, "Expected '(' after 'while'.");
    auto stmt = std::make_unique<Stmt>(StmtKind::While, line);
    stmt->expr = expression();
    consume(TokenType::RPAREN, "Expected ')' after condition.");
    stmt->thenBranch = statement();  // reuse thenBranch as the loop body
    return stmt;
}

// `for (init; cond; incr) body`. Compiled natively (see Compiler::compileFor)
// so that `continue` correctly runs the increment clause.
StmtPtr Parser::forStatement() {
    int line = previous().line;
    consume(TokenType::LPAREN, "Expected '(' after 'for'.");
    auto stmt = std::make_unique<Stmt>(StmtKind::For, line);

    // Initializer: a let-declaration, an expression, or nothing.
    if (match(TokenType::SEMICOLON)) {
        stmt->init = nullptr;
    } else if (match(TokenType::LET)) {
        stmt->init = varDeclaration();        // consumes its own ';'
    } else {
        stmt->init = expressionStatement();   // consumes its own ';'
    }

    // Condition (optional). Absent means an always-true loop.
    if (!check(TokenType::SEMICOLON)) {
        stmt->expr = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after loop condition.");

    // Increment (optional).
    if (!check(TokenType::RPAREN)) {
        stmt->incr = expression();
    }
    consume(TokenType::RPAREN, "Expected ')' after for clauses.");

    stmt->thenBranch = statement();  // body
    return stmt;
}

StmtPtr Parser::returnStatement() {
    int line = previous().line;
    auto stmt = std::make_unique<Stmt>(StmtKind::Return, line);
    if (!check(TokenType::SEMICOLON)) {
        stmt->expr = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after return value.");
    return stmt;
}

StmtPtr Parser::breakStatement() {
    auto stmt = std::make_unique<Stmt>(StmtKind::Break, previous().line);
    consume(TokenType::SEMICOLON, "Expected ';' after 'break'.");
    return stmt;
}

StmtPtr Parser::continueStatement() {
    auto stmt = std::make_unique<Stmt>(StmtKind::Continue, previous().line);
    consume(TokenType::SEMICOLON, "Expected ';' after 'continue'.");
    return stmt;
}

StmtPtr Parser::block() {
    int line = previous().line;
    auto stmt = std::make_unique<Stmt>(StmtKind::Block, line);
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        stmt->body.push_back(declaration());
    }
    consume(TokenType::RBRACE, "Expected '}' after block.");
    return stmt;
}

StmtPtr Parser::expressionStatement() {
    int line = peek().line;
    auto stmt = std::make_unique<Stmt>(StmtKind::Expression, line);
    stmt->expr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression.");
    return stmt;
}

// --- expressions -----------------------------------------------------------
ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    ExprPtr expr = logicalOr();

    if (match(TokenType::EQUAL)) {
        const Token& equals = previous();
        ExprPtr value = assignment();  // right-associative
        if (expr->kind == ExprKind::Variable) {
            auto assign = std::make_unique<Expr>(ExprKind::Assign, equals.line);
            assign->str = expr->str;          // target name
            assign->left = std::move(value);  // value to assign
            return assign;
        }
        if (expr->kind == ExprKind::Index) {
            // Turn `object[index]` into an index-store node.
            auto set = std::make_unique<Expr>(ExprKind::IndexSet, equals.line);
            set->left = std::move(expr->left);    // object
            set->right = std::move(expr->right);  // index
            set->args.push_back(std::move(value));// value
            return set;
        }
        error(equals, "Invalid assignment target.");
    }
    return expr;
}

ExprPtr Parser::logicalOr() {
    ExprPtr expr = logicalAnd();
    while (match(TokenType::OR)) {
        const Token& op = previous();
        ExprPtr right = logicalAnd();
        auto logical = std::make_unique<Expr>(ExprKind::Logical, op.line);
        logical->op = op.type;
        logical->left = std::move(expr);
        logical->right = std::move(right);
        expr = std::move(logical);
    }
    return expr;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr expr = equality();
    while (match(TokenType::AND)) {
        const Token& op = previous();
        ExprPtr right = equality();
        auto logical = std::make_unique<Expr>(ExprKind::Logical, op.line);
        logical->op = op.type;
        logical->left = std::move(expr);
        logical->right = std::move(right);
        expr = std::move(logical);
    }
    return expr;
}

// Helper to build a left-associative binary node.
static ExprPtr makeBinary(ExprPtr left, TokenType op, int line, ExprPtr right) {
    auto bin = std::make_unique<Expr>(ExprKind::Binary, line);
    bin->op = op;
    bin->left = std::move(left);
    bin->right = std::move(right);
    return bin;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    while (check(TokenType::BANG_EQUAL) || check(TokenType::EQUAL_EQUAL)) {
        const Token& op = advance();
        expr = makeBinary(std::move(expr), op.type, op.line, comparison());
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = term();
    while (check(TokenType::LESS) || check(TokenType::LESS_EQUAL) ||
           check(TokenType::GREATER) || check(TokenType::GREATER_EQUAL)) {
        const Token& op = advance();
        expr = makeBinary(std::move(expr), op.type, op.line, term());
    }
    return expr;
}

ExprPtr Parser::term() {
    ExprPtr expr = factor();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        const Token& op = advance();
        expr = makeBinary(std::move(expr), op.type, op.line, factor());
    }
    return expr;
}

ExprPtr Parser::factor() {
    ExprPtr expr = unary();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT)) {
        const Token& op = advance();
        expr = makeBinary(std::move(expr), op.type, op.line, unary());
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (check(TokenType::BANG) || check(TokenType::MINUS)) {
        const Token& op = advance();
        auto expr = std::make_unique<Expr>(ExprKind::Unary, op.line);
        expr->op = op.type;
        expr->left = unary();
        return expr;
    }
    return call();
}

ExprPtr Parser::call() {
    ExprPtr expr = primary();
    while (true) {
        if (match(TokenType::LPAREN)) {
            expr = finishCall(std::move(expr));
        } else if (match(TokenType::LBRACKET)) {
            // Subscript: object[index]
            int line = previous().line;
            ExprPtr index = expression();
            consume(TokenType::RBRACKET, "Expected ']' after index.");
            auto idx = std::make_unique<Expr>(ExprKind::Index, line);
            idx->left = std::move(expr);
            idx->right = std::move(index);
            expr = std::move(idx);
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
    auto call = std::make_unique<Expr>(ExprKind::Call, previous().line);
    call->left = std::move(callee);
    if (!check(TokenType::RPAREN)) {
        do {
            call->args.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected ')' after arguments.");
    return call;
}

ExprPtr Parser::primary() {
    if (match(TokenType::NUMBER)) {
        auto e = std::make_unique<Expr>(ExprKind::Literal, previous().line);
        e->litType = Expr::LitType::Number;
        e->number = std::strtod(previous().lexeme.c_str(), nullptr);
        return e;
    }
    if (match(TokenType::STRING)) {
        auto e = std::make_unique<Expr>(ExprKind::Literal, previous().line);
        e->litType = Expr::LitType::String;
        e->str = previous().lexeme;
        return e;
    }
    if (match(TokenType::TRUE)) {
        auto e = std::make_unique<Expr>(ExprKind::Literal, previous().line);
        e->litType = Expr::LitType::Bool;
        e->boolean = true;
        return e;
    }
    if (match(TokenType::FALSE)) {
        auto e = std::make_unique<Expr>(ExprKind::Literal, previous().line);
        e->litType = Expr::LitType::Bool;
        e->boolean = false;
        return e;
    }
    if (match(TokenType::NIL)) {
        auto e = std::make_unique<Expr>(ExprKind::Literal, previous().line);
        e->litType = Expr::LitType::Nil;
        return e;
    }
    if (match(TokenType::IDENTIFIER)) {
        auto e = std::make_unique<Expr>(ExprKind::Variable, previous().line);
        e->str = previous().lexeme;
        return e;
    }
    if (match(TokenType::LPAREN)) {
        ExprPtr expr = expression();
        consume(TokenType::RPAREN, "Expected ')' after expression.");
        return expr;
    }
    if (match(TokenType::LBRACKET)) {
        // Array literal: [ e1, e2, ... ]
        auto arr = std::make_unique<Expr>(ExprKind::ArrayLiteral, previous().line);
        if (!check(TokenType::RBRACKET)) {
            do {
                arr->args.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACKET, "Expected ']' after array elements.");
        return arr;
    }
    if (match(TokenType::LBRACE)) {
        // Map literal: { "key": value, ident: value, ... }
        // (A '{' that begins a *statement* is parsed as a block, not a map.)
        auto map = std::make_unique<Expr>(ExprKind::MapLiteral, previous().line);
        if (!check(TokenType::RBRACE)) {
            do {
                std::string key;
                if (match(TokenType::STRING) || match(TokenType::IDENTIFIER)) {
                    key = previous().lexeme;
                } else {
                    error(peek(), "Expected a string or identifier map key.");
                }
                consume(TokenType::COLON, "Expected ':' after map key.");
                map->mapKeys.push_back(key);
                map->args.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries.");
        return map;
    }
    error(peek(), "Expected an expression.");
}
