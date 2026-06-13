#ifndef CVM_COMPILER_H
#define CVM_COMPILER_H

#include <stdexcept>
#include <string>
#include <vector>

#include "ast.h"
#include "gc.h"
#include "value.h"

// Raised on a semantic error the compiler can detect statically
// (e.g. too many locals, return outside a function).
struct CompileError : std::runtime_error {
    explicit CompileError(const std::string& msg) : std::runtime_error(msg) {}
};

// Walks the AST and emits stack-machine bytecode into FunctionObj chunks.
//
// The whole program is compiled into an implicit top-level function (the
// "script"); each `fn` declaration is compiled into its own nested FunctionObj.
class Compiler {
public:
    explicit Compiler(Heap& heap) : heap_(heap) {}

    // Compile a program into the top-level script function. Throws CompileError.
    // The returned function (and any nested ones) are owned by the heap.
    FunctionObj* compile(const std::vector<StmtPtr>& program);

private:
    Heap& heap_;
    // Tracks a local variable within a function's scope.
    struct Local {
        std::string name;
        int depth;          // scope depth where declared; -1 means "declared but not ready"
        bool isCaptured = false;  // captured by a nested closure?
    };

    // Tracks an enclosing loop so `break`/`continue` know where to jump.
    struct LoopCtx {
        int continueTarget;            // bytecode offset to jump back to on `continue`
        int scopeDepth;                // scope depth of the loop body
        std::vector<int> breakJumps;   // forward jumps to patch at the loop's end
    };

    // One per function being compiled; chained via `enclosing` for nesting.
    struct FuncState {
        FunctionObj* function = nullptr;
        FuncState* enclosing = nullptr;
        std::vector<Local> locals;
        std::vector<UpvalueDesc> upvalues;  // upvalues this function captures
        std::vector<LoopCtx> loops;         // enclosing loops (innermost last)
        int scopeDepth = 0;
        bool isScript = false;
    };

    FuncState* current_ = nullptr;

    Chunk& chunk() { return current_->function->chunk; }

    // emit helpers
    void emitByte(uint8_t byte, int line);
    void emitBytes(uint8_t b1, uint8_t b2, int line);
    int emitJump(uint8_t instruction, int line);
    void patchJump(int offset);
    void emitLoop(int loopStart, int line);
    void emitReturn(int line);
    uint8_t makeConstant(const Value& value);
    void emitConstant(const Value& value, int line);

    // scope / locals
    void beginScope() { current_->scopeDepth++; }
    void endScope(int line);
    // Emit pops/closes for locals deeper than `depth` WITHOUT removing them from
    // bookkeeping (used by break/continue, where fall-through still owns them).
    void emitPopsToDepth(int depth, int line);
    void addLocal(const std::string& name, int line);
    int resolveLocal(FuncState* state, const std::string& name);
    int resolveUpvalue(FuncState* state, const std::string& name);
    int addUpvalue(FuncState* state, uint8_t index, bool isLocal);
    void declareLocal(const std::string& name, int line);

    // statements
    void compileStmt(const Stmt* s);
    void compileFunction(const Stmt* s);
    void compileLet(const Stmt* s);
    void compileBlock(const std::vector<StmtPtr>& body, int line);
    void compileIf(const Stmt* s);
    void compileWhile(const Stmt* s);
    void compileFor(const Stmt* s);
    void compileReturn(const Stmt* s);
    void compileBreak(const Stmt* s);
    void compileContinue(const Stmt* s);

    // expressions
    void compileExpr(const Expr* e);
    void compileVariable(const Expr* e);
    void compileAssign(const Expr* e);
    void compileBinary(const Expr* e);
    void compileLogical(const Expr* e);
    void compileCall(const Expr* e);
    void compileArrayLiteral(const Expr* e);
    void compileMapLiteral(const Expr* e);
    void compileIndex(const Expr* e);
    void compileIndexSet(const Expr* e);
};

#endif // CVM_COMPILER_H
