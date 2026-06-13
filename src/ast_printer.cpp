#include <sstream>

#include "ast.h"
#include "token.h"

// Renders the AST as an indented tree so users can see the parse result.

static const char* opLexeme(TokenType t) {
    switch (t) {
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::PERCENT: return "%";
        case TokenType::BANG: return "!";
        case TokenType::BANG_EQUAL: return "!=";
        case TokenType::EQUAL_EQUAL: return "==";
        case TokenType::LESS: return "<";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::GREATER: return ">";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::AND: return "and";
        case TokenType::OR: return "or";
        default: return "?";
    }
}

static void indent(std::ostringstream& out, int depth) {
    for (int i = 0; i < depth; i++) out << "  ";
}

static void printExpr(std::ostringstream& out, const Expr* e, int depth);
static void printStmt(std::ostringstream& out, const Stmt* s, int depth);

static void printExpr(std::ostringstream& out, const Expr* e, int depth) {
    indent(out, depth);
    switch (e->kind) {
        case ExprKind::Literal:
            switch (e->litType) {
                case Expr::LitType::Number: out << "Number(" << e->number << ")\n"; break;
                case Expr::LitType::String: out << "String(\"" << e->str << "\")\n"; break;
                case Expr::LitType::Bool: out << "Bool(" << (e->boolean ? "true" : "false") << ")\n"; break;
                case Expr::LitType::Nil: out << "Nil\n"; break;
            }
            break;
        case ExprKind::Variable:
            out << "Variable(" << e->str << ")\n";
            break;
        case ExprKind::Assign:
            out << "Assign(" << e->str << ")\n";
            printExpr(out, e->left.get(), depth + 1);
            break;
        case ExprKind::Unary:
            out << "Unary(" << opLexeme(e->op) << ")\n";
            printExpr(out, e->left.get(), depth + 1);
            break;
        case ExprKind::Binary:
            out << "Binary(" << opLexeme(e->op) << ")\n";
            printExpr(out, e->left.get(), depth + 1);
            printExpr(out, e->right.get(), depth + 1);
            break;
        case ExprKind::Logical:
            out << "Logical(" << opLexeme(e->op) << ")\n";
            printExpr(out, e->left.get(), depth + 1);
            printExpr(out, e->right.get(), depth + 1);
            break;
        case ExprKind::Call:
            out << "Call\n";
            indent(out, depth + 1);
            out << "callee:\n";
            printExpr(out, e->left.get(), depth + 2);
            if (!e->args.empty()) {
                indent(out, depth + 1);
                out << "args:\n";
                for (const auto& a : e->args) printExpr(out, a.get(), depth + 2);
            }
            break;
        case ExprKind::ArrayLiteral:
            out << "ArrayLiteral\n";
            for (const auto& a : e->args) printExpr(out, a.get(), depth + 1);
            break;
        case ExprKind::MapLiteral:
            out << "MapLiteral\n";
            for (size_t i = 0; i < e->args.size(); i++) {
                indent(out, depth + 1);
                out << "\"" << e->mapKeys[i] << "\":\n";
                printExpr(out, e->args[i].get(), depth + 2);
            }
            break;
        case ExprKind::Index:
            out << "Index\n";
            indent(out, depth + 1); out << "object:\n";
            printExpr(out, e->left.get(), depth + 2);
            indent(out, depth + 1); out << "index:\n";
            printExpr(out, e->right.get(), depth + 2);
            break;
        case ExprKind::IndexSet:
            out << "IndexSet\n";
            indent(out, depth + 1); out << "object:\n";
            printExpr(out, e->left.get(), depth + 2);
            indent(out, depth + 1); out << "index:\n";
            printExpr(out, e->right.get(), depth + 2);
            indent(out, depth + 1); out << "value:\n";
            printExpr(out, e->args[0].get(), depth + 2);
            break;
    }
}

static void printStmt(std::ostringstream& out, const Stmt* s, int depth) {
    indent(out, depth);
    switch (s->kind) {
        case StmtKind::Expression:
            out << "ExprStmt\n";
            printExpr(out, s->expr.get(), depth + 1);
            break;
        case StmtKind::Print:
            out << "Print\n";
            printExpr(out, s->expr.get(), depth + 1);
            break;
        case StmtKind::Let:
            out << "Let(" << s->name << ")\n";
            if (s->expr) printExpr(out, s->expr.get(), depth + 1);
            break;
        case StmtKind::Block:
            out << "Block\n";
            for (const auto& st : s->body) printStmt(out, st.get(), depth + 1);
            break;
        case StmtKind::If:
            out << "If\n";
            indent(out, depth + 1); out << "cond:\n";
            printExpr(out, s->expr.get(), depth + 2);
            indent(out, depth + 1); out << "then:\n";
            printStmt(out, s->thenBranch.get(), depth + 2);
            if (s->elseBranch) {
                indent(out, depth + 1); out << "else:\n";
                printStmt(out, s->elseBranch.get(), depth + 2);
            }
            break;
        case StmtKind::While:
            out << "While\n";
            indent(out, depth + 1); out << "cond:\n";
            printExpr(out, s->expr.get(), depth + 2);
            indent(out, depth + 1); out << "body:\n";
            printStmt(out, s->thenBranch.get(), depth + 2);
            break;
        case StmtKind::For:
            out << "For\n";
            if (s->init) { indent(out, depth + 1); out << "init:\n"; printStmt(out, s->init.get(), depth + 2); }
            if (s->expr) { indent(out, depth + 1); out << "cond:\n"; printExpr(out, s->expr.get(), depth + 2); }
            if (s->incr) { indent(out, depth + 1); out << "incr:\n"; printExpr(out, s->incr.get(), depth + 2); }
            indent(out, depth + 1); out << "body:\n";
            printStmt(out, s->thenBranch.get(), depth + 2);
            break;
        case StmtKind::Break:
            out << "Break\n";
            break;
        case StmtKind::Continue:
            out << "Continue\n";
            break;
        case StmtKind::Function: {
            out << "Function(" << s->name << ", params=[";
            for (size_t i = 0; i < s->params.size(); i++) {
                out << s->params[i];
                if (i + 1 < s->params.size()) out << ", ";
            }
            out << "])\n";
            for (const auto& st : s->body) printStmt(out, st.get(), depth + 1);
            break;
        }
        case StmtKind::Return:
            out << "Return\n";
            if (s->expr) printExpr(out, s->expr.get(), depth + 1);
            break;
    }
}

std::string astToString(const std::vector<StmtPtr>& program) {
    std::ostringstream out;
    out << "Program\n";
    for (const auto& s : program) printStmt(out, s.get(), 1);
    return out.str();
}
