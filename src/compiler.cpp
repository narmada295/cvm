#include "compiler.h"

#include "opcode.h"

FunctionObj* Compiler::compile(const std::vector<StmtPtr>& program) {
    FuncState script;
    script.function = heap_.allocate<FunctionObj>();
    script.function->name = "";  // anonymous top-level
    script.isScript = true;
    // Slot 0 is reserved for the executing function itself (call convention).
    script.locals.push_back({"", 0});
    current_ = &script;

    for (const auto& stmt : program) {
        compileStmt(stmt.get());
    }
    emitReturn(0);  // implicit `return nil` at end of script
    return script.function;
}

// --- emit helpers ----------------------------------------------------------
void Compiler::emitByte(uint8_t byte, int line) { chunk().write(byte, line); }

void Compiler::emitBytes(uint8_t b1, uint8_t b2, int line) {
    emitByte(b1, line);
    emitByte(b2, line);
}

int Compiler::emitJump(uint8_t instruction, int line) {
    emitByte(instruction, line);
    emitByte(0xff, line);  // placeholder high byte
    emitByte(0xff, line);  // placeholder low byte
    return static_cast<int>(chunk().code.size()) - 2;
}

void Compiler::patchJump(int offset) {
    // Distance from end of the jump operand to the current write position.
    int jump = static_cast<int>(chunk().code.size()) - offset - 2;
    if (jump > 0xffff) throw CompileError("Too much code to jump over.");
    chunk().code[offset] = (jump >> 8) & 0xff;
    chunk().code[offset + 1] = jump & 0xff;
}

void Compiler::emitLoop(int loopStart, int line) {
    emitByte(OP_LOOP, line);
    int offset = static_cast<int>(chunk().code.size()) - loopStart + 2;
    if (offset > 0xffff) throw CompileError("Loop body too large.");
    emitByte((offset >> 8) & 0xff, line);
    emitByte(offset & 0xff, line);
}

void Compiler::emitReturn(int line) {
    emitByte(OP_NIL, line);     // default return value
    emitByte(OP_RETURN, line);
}

uint8_t Compiler::makeConstant(const Value& value) {
    int constant = chunk().addConstant(value);
    if (constant > 255) {
        throw CompileError("Too many constants in one chunk (limit 256).");
    }
    return static_cast<uint8_t>(constant);
}

void Compiler::emitConstant(const Value& value, int line) {
    emitBytes(OP_CONSTANT, makeConstant(value), line);
}

// --- scope / locals --------------------------------------------------------
void Compiler::endScope(int line) {
    current_->scopeDepth--;
    // Pop every local that belonged to the scope we are leaving. Captured
    // locals are "closed" (lifted onto the heap) instead of simply popped.
    while (!current_->locals.empty() &&
           current_->locals.back().depth > current_->scopeDepth) {
        if (current_->locals.back().isCaptured) {
            emitByte(OP_CLOSE_UPVALUE, line);
        } else {
            emitByte(OP_POP, line);
        }
        current_->locals.pop_back();
    }
}

void Compiler::emitPopsToDepth(int depth, int line) {
    // Walk locals from the top, popping (or closing) those deeper than `depth`.
    // Bookkeeping is left untouched: the normal fall-through path still owns them.
    for (int i = static_cast<int>(current_->locals.size()) - 1; i >= 0; i--) {
        if (current_->locals[i].depth <= depth) break;
        emitByte(current_->locals[i].isCaptured ? OP_CLOSE_UPVALUE : OP_POP, line);
    }
}

void Compiler::addLocal(const std::string& name, int /*line*/) {
    if (current_->locals.size() >= 256) {
        throw CompileError("Too many local variables in function (limit 256).");
    }
    current_->locals.push_back({name, -1});  // -1 = declared, not yet initialized
}

int Compiler::resolveLocal(FuncState* state, const std::string& name) {
    for (int i = static_cast<int>(state->locals.size()) - 1; i >= 0; i--) {
        if (state->locals[i].name == name) {
            if (state->locals[i].depth == -1) {
                throw CompileError("Cannot read local variable '" + name +
                                   "' in its own initializer.");
            }
            return i;
        }
    }
    return -1;  // not a local -> try upvalue, then global
}

// Records that `state` captures an upvalue, returning its index (deduplicated).
int Compiler::addUpvalue(FuncState* state, uint8_t index, bool isLocal) {
    for (size_t i = 0; i < state->upvalues.size(); i++) {
        const UpvalueDesc& uv = state->upvalues[i];
        if (uv.index == index && uv.isLocal == isLocal) return static_cast<int>(i);
    }
    if (state->upvalues.size() >= 256) {
        throw CompileError("Too many closure variables in function (limit 256).");
    }
    state->upvalues.push_back({isLocal, index});
    state->function->upvalueCount = static_cast<int>(state->upvalues.size());
    return static_cast<int>(state->upvalues.size() - 1);
}

// Resolves `name` as an upvalue of `state`, recursing through enclosing
// functions. Marks the originating local as captured. Returns -1 if not found.
int Compiler::resolveUpvalue(FuncState* state, const std::string& name) {
    if (state->enclosing == nullptr) return -1;  // reached the top: must be global

    int local = resolveLocal(state->enclosing, name);
    if (local != -1) {
        state->enclosing->locals[local].isCaptured = true;
        return addUpvalue(state, static_cast<uint8_t>(local), /*isLocal=*/true);
    }

    int upvalue = resolveUpvalue(state->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(state, static_cast<uint8_t>(upvalue), /*isLocal=*/false);
    }
    return -1;
}

void Compiler::declareLocal(const std::string& name, int line) {
    // Guard against redeclaring the same name in the same scope.
    for (int i = static_cast<int>(current_->locals.size()) - 1; i >= 0; i--) {
        const Local& local = current_->locals[i];
        if (local.depth != -1 && local.depth < current_->scopeDepth) break;
        if (local.name == name) {
            throw CompileError("Variable '" + name + "' already declared in this scope.");
        }
    }
    addLocal(name, line);
}

// --- statements ------------------------------------------------------------
void Compiler::compileStmt(const Stmt* s) {
    switch (s->kind) {
        case StmtKind::Expression:
            compileExpr(s->expr.get());
            emitByte(OP_POP, s->line);  // discard the unused result
            break;
        case StmtKind::Print:
            compileExpr(s->expr.get());
            emitByte(OP_PRINT, s->line);
            break;
        case StmtKind::Let:
            compileLet(s);
            break;
        case StmtKind::Block:
            beginScope();
            compileBlock(s->body, s->line);
            endScope(s->line);
            break;
        case StmtKind::If:
            compileIf(s);
            break;
        case StmtKind::While:
            compileWhile(s);
            break;
        case StmtKind::For:
            compileFor(s);
            break;
        case StmtKind::Function:
            compileFunction(s);
            break;
        case StmtKind::Return:
            compileReturn(s);
            break;
        case StmtKind::Break:
            compileBreak(s);
            break;
        case StmtKind::Continue:
            compileContinue(s);
            break;
    }
}

void Compiler::compileBlock(const std::vector<StmtPtr>& body, int /*line*/) {
    for (const auto& stmt : body) compileStmt(stmt.get());
}

void Compiler::compileLet(const Stmt* s) {
    if (current_->scopeDepth > 0) {
        // Local variable: declare, then evaluate initializer onto the stack.
        declareLocal(s->name, s->line);
        if (s->expr) {
            compileExpr(s->expr.get());
        } else {
            emitByte(OP_NIL, s->line);
        }
        // Mark initialized: the value now lives in the local's slot.
        current_->locals.back().depth = current_->scopeDepth;
    } else {
        // Global variable.
        if (s->expr) {
            compileExpr(s->expr.get());
        } else {
            emitByte(OP_NIL, s->line);
        }
        uint8_t nameConst = makeConstant(std::string(s->name));
        emitBytes(OP_DEFINE_GLOBAL, nameConst, s->line);
    }
}

void Compiler::compileIf(const Stmt* s) {
    compileExpr(s->expr.get());
    int thenJump = emitJump(OP_JUMP_IF_FALSE, s->line);
    emitByte(OP_POP, s->line);  // pop the condition (true branch)
    compileStmt(s->thenBranch.get());

    int elseJump = emitJump(OP_JUMP, s->line);
    patchJump(thenJump);
    emitByte(OP_POP, s->line);  // pop the condition (false branch)
    if (s->elseBranch) compileStmt(s->elseBranch.get());
    patchJump(elseJump);
}

void Compiler::compileWhile(const Stmt* s) {
    int loopStart = static_cast<int>(chunk().code.size());
    compileExpr(s->expr.get());
    int exitJump = emitJump(OP_JUMP_IF_FALSE, s->line);
    emitByte(OP_POP, s->line);            // pop condition (entering body)

    current_->loops.push_back({loopStart, current_->scopeDepth, {}});
    compileStmt(s->thenBranch.get());     // body stored in thenBranch
    LoopCtx loop = std::move(current_->loops.back());
    current_->loops.pop_back();

    emitLoop(loopStart, s->line);
    patchJump(exitJump);
    emitByte(OP_POP, s->line);            // pop condition (exiting loop)
    for (int j : loop.breakJumps) patchJump(j);  // break lands here, condition already popped
}

// for (init; cond; incr) body
//
// The increment is emitted *before* the body and jumped over on entry, so that
// after the body we fall straight into the increment and then loop back to the
// condition. This makes the increment the `continue` target.
void Compiler::compileFor(const Stmt* s) {
    beginScope();  // scopes the loop variable from the initializer
    if (s->init) compileStmt(s->init.get());

    int loopStart = static_cast<int>(chunk().code.size());

    // Condition (optional).
    int exitJump = -1;
    if (s->expr) {
        compileExpr(s->expr.get());
        exitJump = emitJump(OP_JUMP_IF_FALSE, s->line);
        emitByte(OP_POP, s->line);  // pop condition (entering body)
    }

    // Increment: emit it now, but jump over it to reach the body first.
    if (s->incr) {
        int bodyJump = emitJump(OP_JUMP, s->line);
        int incrStart = static_cast<int>(chunk().code.size());
        compileExpr(s->incr.get());
        emitByte(OP_POP, s->line);          // discard the increment's value
        emitLoop(loopStart, s->line);       // back to the condition
        loopStart = incrStart;              // body now loops back to the increment
        patchJump(bodyJump);
    }

    current_->loops.push_back({loopStart, current_->scopeDepth, {}});
    compileStmt(s->thenBranch.get());       // body
    LoopCtx loop = std::move(current_->loops.back());
    current_->loops.pop_back();

    emitLoop(loopStart, s->line);
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP, s->line);          // pop condition (exiting loop)
    }
    for (int j : loop.breakJumps) patchJump(j);
    endScope(s->line);
}

void Compiler::compileBreak(const Stmt* s) {
    if (current_->loops.empty()) throw CompileError("'break' used outside of a loop.");
    LoopCtx& loop = current_->loops.back();
    emitPopsToDepth(loop.scopeDepth, s->line);  // discard body locals
    loop.breakJumps.push_back(emitJump(OP_JUMP, s->line));
}

void Compiler::compileContinue(const Stmt* s) {
    if (current_->loops.empty()) throw CompileError("'continue' used outside of a loop.");
    LoopCtx& loop = current_->loops.back();
    emitPopsToDepth(loop.scopeDepth, s->line);  // discard body locals
    emitLoop(loop.continueTarget, s->line);
}

void Compiler::compileReturn(const Stmt* s) {
    if (current_->isScript) {
        throw CompileError("Cannot 'return' from top-level code.");
    }
    if (s->expr) {
        compileExpr(s->expr.get());
        emitByte(OP_RETURN, s->line);
    } else {
        emitReturn(s->line);
    }
}

void Compiler::compileFunction(const Stmt* s) {
    // Bind the name first so the function can recurse / reference itself.
    bool isLocal = current_->scopeDepth > 0;
    if (isLocal) {
        declareLocal(s->name, s->line);
        current_->locals.back().depth = current_->scopeDepth;  // mark initialized
    }

    // Compile the body into a fresh nested function state.
    FuncState fnState;
    fnState.function = heap_.allocate<FunctionObj>();
    fnState.function->name = s->name;
    fnState.function->arity = static_cast<int>(s->params.size());
    fnState.enclosing = current_;
    fnState.locals.push_back({"", 0});  // slot 0 = the function itself
    current_ = &fnState;

    beginScope();
    for (const auto& param : s->params) {
        declareLocal(param, s->line);
        current_->locals.back().depth = current_->scopeDepth;
    }
    for (const auto& stmt : s->body) compileStmt(stmt.get());
    emitReturn(s->line);  // safety net if the body falls off the end

    FunctionObj* compiled = fnState.function;
    current_ = fnState.enclosing;

    // Emit OP_CLOSURE referencing the compiled template, followed by one
    // [isLocal][index] pair per captured upvalue so the VM can wire them up.
    uint8_t fnConst = makeConstant(Value(compiled));
    emitBytes(OP_CLOSURE, fnConst, s->line);
    for (const UpvalueDesc& uv : fnState.upvalues) {
        emitByte(uv.isLocal ? 1 : 0, s->line);
        emitByte(uv.index, s->line);
    }

    if (!isLocal) {
        uint8_t nameConst = makeConstant(std::string(s->name));
        emitBytes(OP_DEFINE_GLOBAL, nameConst, s->line);
    }
    // If local, the function value stays on the stack in its slot.
}

// --- expressions -----------------------------------------------------------
void Compiler::compileExpr(const Expr* e) {
    switch (e->kind) {
        case ExprKind::Literal:
            switch (e->litType) {
                case Expr::LitType::Number: emitConstant(Value(e->number), e->line); break;
                case Expr::LitType::String: emitConstant(Value(e->str), e->line); break;
                case Expr::LitType::Bool: emitByte(e->boolean ? OP_TRUE : OP_FALSE, e->line); break;
                case Expr::LitType::Nil: emitByte(OP_NIL, e->line); break;
            }
            break;
        case ExprKind::Variable: compileVariable(e); break;
        case ExprKind::Assign: compileAssign(e); break;
        case ExprKind::Unary:
            compileExpr(e->left.get());
            if (e->op == TokenType::MINUS) emitByte(OP_NEGATE, e->line);
            else emitByte(OP_NOT, e->line);  // BANG
            break;
        case ExprKind::Binary: compileBinary(e); break;
        case ExprKind::Logical: compileLogical(e); break;
        case ExprKind::Call: compileCall(e); break;
        case ExprKind::ArrayLiteral: compileArrayLiteral(e); break;
        case ExprKind::MapLiteral: compileMapLiteral(e); break;
        case ExprKind::Index: compileIndex(e); break;
        case ExprKind::IndexSet: compileIndexSet(e); break;
    }
}

void Compiler::compileArrayLiteral(const Expr* e) {
    if (e->args.size() > 255) throw CompileError("Array literal too large (limit 255).");
    for (const auto& el : e->args) compileExpr(el.get());
    emitBytes(OP_BUILD_ARRAY, static_cast<uint8_t>(e->args.size()), e->line);
}

void Compiler::compileMapLiteral(const Expr* e) {
    if (e->args.size() > 255) throw CompileError("Map literal too large (limit 255).");
    // Push (key, value) pairs left-to-right; the VM pops them into the map.
    for (size_t i = 0; i < e->args.size(); i++) {
        emitConstant(Value(e->mapKeys[i]), e->line);
        compileExpr(e->args[i].get());
    }
    emitBytes(OP_BUILD_MAP, static_cast<uint8_t>(e->args.size()), e->line);
}

void Compiler::compileIndex(const Expr* e) {
    compileExpr(e->left.get());   // object
    compileExpr(e->right.get());  // index
    emitByte(OP_INDEX_GET, e->line);
}

void Compiler::compileIndexSet(const Expr* e) {
    compileExpr(e->left.get());      // object
    compileExpr(e->right.get());     // index
    compileExpr(e->args[0].get());   // value
    emitByte(OP_INDEX_SET, e->line); // leaves the assigned value on the stack
}

void Compiler::compileVariable(const Expr* e) {
    int slot = resolveLocal(current_, e->str);
    if (slot != -1) {
        emitBytes(OP_GET_LOCAL, static_cast<uint8_t>(slot), e->line);
    } else if ((slot = resolveUpvalue(current_, e->str)) != -1) {
        emitBytes(OP_GET_UPVALUE, static_cast<uint8_t>(slot), e->line);
    } else {
        uint8_t nameConst = makeConstant(std::string(e->str));
        emitBytes(OP_GET_GLOBAL, nameConst, e->line);
    }
}

void Compiler::compileAssign(const Expr* e) {
    compileExpr(e->left.get());  // evaluate the value first
    int slot = resolveLocal(current_, e->str);
    if (slot != -1) {
        emitBytes(OP_SET_LOCAL, static_cast<uint8_t>(slot), e->line);
    } else if ((slot = resolveUpvalue(current_, e->str)) != -1) {
        emitBytes(OP_SET_UPVALUE, static_cast<uint8_t>(slot), e->line);
    } else {
        uint8_t nameConst = makeConstant(std::string(e->str));
        emitBytes(OP_SET_GLOBAL, nameConst, e->line);
    }
}

void Compiler::compileBinary(const Expr* e) {
    compileExpr(e->left.get());
    compileExpr(e->right.get());
    switch (e->op) {
        case TokenType::PLUS:  emitByte(OP_ADD, e->line); break;
        case TokenType::MINUS: emitByte(OP_SUBTRACT, e->line); break;
        case TokenType::STAR:  emitByte(OP_MULTIPLY, e->line); break;
        case TokenType::SLASH: emitByte(OP_DIVIDE, e->line); break;
        case TokenType::PERCENT: emitByte(OP_MODULO, e->line); break;
        case TokenType::EQUAL_EQUAL: emitByte(OP_EQUAL, e->line); break;
        case TokenType::BANG_EQUAL:  emitBytes(OP_EQUAL, OP_NOT, e->line); break;
        case TokenType::GREATER:     emitByte(OP_GREATER, e->line); break;
        case TokenType::GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT, e->line); break;
        case TokenType::LESS:        emitByte(OP_LESS, e->line); break;
        case TokenType::LESS_EQUAL:  emitBytes(OP_GREATER, OP_NOT, e->line); break;
        default: throw CompileError("Unknown binary operator.");
    }
}

void Compiler::compileLogical(const Expr* e) {
    // Short-circuiting via jumps, leaving the deciding operand on the stack.
    compileExpr(e->left.get());
    if (e->op == TokenType::AND) {
        int endJump = emitJump(OP_JUMP_IF_FALSE, e->line);
        emitByte(OP_POP, e->line);
        compileExpr(e->right.get());
        patchJump(endJump);
    } else {  // OR
        int elseJump = emitJump(OP_JUMP_IF_FALSE, e->line);
        int endJump = emitJump(OP_JUMP, e->line);
        patchJump(elseJump);
        emitByte(OP_POP, e->line);
        compileExpr(e->right.get());
        patchJump(endJump);
    }
}

void Compiler::compileCall(const Expr* e) {
    compileExpr(e->left.get());  // the callee
    if (e->args.size() > 255) throw CompileError("Cannot have more than 255 arguments.");
    for (const auto& arg : e->args) compileExpr(arg.get());
    emitBytes(OP_CALL, static_cast<uint8_t>(e->args.size()), e->line);
}
