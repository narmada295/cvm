#ifndef CVM_AST_H
#define CVM_AST_H

#include <memory>
#include <string>
#include <vector>

#include "token.h"

// ---------------------------------------------------------------------------
// Abstract Syntax Tree.
//
// Expressions produce a value; statements perform an action. Both hierarchies
// are visited by the compiler to emit bytecode, and by AstPrinter for --ast.
// ---------------------------------------------------------------------------

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ----- Expressions ---------------------------------------------------------
enum class ExprKind {
    Literal,       // number / string / bool / nil
    Variable,      // identifier read
    Assign,        // name = value
    Unary,         // op operand
    Binary,        // left op right
    Logical,       // left (and|or) right  (short-circuiting)
    Call,          // callee(args...)
    ArrayLiteral,  // [ a, b, c ]            (elements in `args`)
    MapLiteral,    // { k: v, ... }          (keys in `mapKeys`, values in `args`)
    Index,         // object[index]          (left=object, right=index)
    IndexSet       // object[index] = value  (left=object, right=index, args[0]=value)
};

struct Expr {
    ExprKind kind;
    int line = 0;

    // Literal
    enum class LitType { Number, String, Bool, Nil } litType{};
    double number = 0;
    std::string str;     // string literal value, or identifier/variable name
    bool boolean = false;

    // Unary / Binary / Logical operator token
    TokenType op{};

    // Children
    ExprPtr left;        // binary/logical left, unary operand, call callee, assign target value
    ExprPtr right;       // binary/logical right
    std::vector<ExprPtr> args;       // call/array/map arguments (map: the values)
    std::vector<std::string> mapKeys;  // MapLiteral keys, parallel to args

    explicit Expr(ExprKind k, int ln) : kind(k), line(ln) {}
};

// ----- Statements ----------------------------------------------------------
enum class StmtKind {
    Expression,  // expr ;
    Print,       // print expr ;
    Let,         // let name (= expr)? ;
    Block,       // { ... }
    If,          // if (cond) then (else)?
    While,       // while (cond) body
    For,         // for (init; cond; incr) body
    Function,    // fn name(params) body
    Return,      // return expr? ;
    Break,       // break ;
    Continue     // continue ;
};

struct Stmt {
    StmtKind kind;
    int line = 0;

    ExprPtr expr;                  // Expression/Print/Let-init/Return/If+While+For condition
    std::string name;              // Let/Function name
    std::vector<StmtPtr> body;     // Block contents / Function body
    StmtPtr thenBranch;            // If then / While+For body
    StmtPtr elseBranch;            // If else
    StmtPtr init;                  // For initializer
    ExprPtr incr;                  // For increment
    std::vector<std::string> params;  // Function parameters

    explicit Stmt(StmtKind k, int ln) : kind(k), line(ln) {}
};

// Pretty-prints the AST as an indented tree (used by `--ast`).
std::string astToString(const std::vector<StmtPtr>& program);

#endif // CVM_AST_H
