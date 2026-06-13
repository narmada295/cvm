#include "vm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "opcode.h"

VM::VM(Heap& heap) : heap_(heap), startTime_(std::chrono::steady_clock::now()) {
    // Let the GC find our roots, then install the built-in functions.
    heap_.setRootMarker([this]() { markRoots(); });
    defineNatives();
}

InterpretResult VM::run(FunctionObj* script) {
    resetStack();
    // Note: globals_ is intentionally NOT cleared here so a shared VM (the REPL)
    // keeps variables and functions defined on previous lines. Native functions
    // were installed once in the constructor.

    // Wrap the script in a closure (the uniform callable representation).
    auto* closure = heap_.allocate<ClosureObj>();
    closure->function = script;
    push(Value(closure));     // slot 0 of the bottom frame
    call(closure, 0);
    return execute();
}

// Marks everything reachable from the VM so the GC can sweep the rest.
void VM::markRoots() {
    for (int i = 0; i < stackTop_; i++) heap_.markValue(stack_[i]);
    for (int i = 0; i < frameCount_; i++) heap_.markObject(frames_[i].closure);
    for (Upvalue* uv : openUpvalues_) heap_.markObject(uv);
    for (auto& [name, value] : globals_) heap_.markValue(value);
}

// ---------------------------------------------------------------------------
// Native (built-in) functions.
// ---------------------------------------------------------------------------
void VM::defineNatives() {
    auto define = [&](const std::string& name, int arity,
                      std::function<bool(std::vector<Value>&, Value&, std::string&)> fn) {
        auto* obj = heap_.allocate<NativeObj>();
        obj->name = name;
        obj->arity = arity;
        obj->fn = std::move(fn);
        globals_[name] = Value(obj);
    };

    auto start = startTime_;
    define("clock", 0, [start](std::vector<Value>&, Value& out, std::string&) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start;
        out = Value(elapsed.count());
        return true;
    });

    define("len", 1, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (std::holds_alternative<std::string>(args[0])) {
            out = Value(static_cast<double>(std::get<std::string>(args[0]).size()));
            return true;
        }
        if (std::holds_alternative<ArrayObj*>(args[0])) {
            out = Value(static_cast<double>(
                std::get<ArrayObj*>(args[0])->elements.size()));
            return true;
        }
        if (std::holds_alternative<MapObj*>(args[0])) {
            out = Value(static_cast<double>(
                std::get<MapObj*>(args[0])->entries.size()));
            return true;
        }
        err = "len() expects a string, array, or map.";
        return false;
    });

    define("str", 1, [](std::vector<Value>& args, Value& out, std::string&) {
        out = Value(valueToString(args[0]));
        return true;
    });

    define("num", 1, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (std::holds_alternative<double>(args[0])) { out = args[0]; return true; }
        if (std::holds_alternative<std::string>(args[0])) {
            const std::string& s = std::get<std::string>(args[0]);
            try {
                size_t pos = 0;
                double d = std::stod(s, &pos);
                if (pos != s.size()) throw std::invalid_argument("trailing");
                out = Value(d);
                return true;
            } catch (...) {
                err = "num() could not parse '" + s + "'.";
                return false;
            }
        }
        err = "num() expects a string or number.";
        return false;
    });

    auto math1 = [&](const std::string& name, double (*f)(double)) {
        define(name, 1, [f, name](std::vector<Value>& args, Value& out, std::string& err) {
            if (!std::holds_alternative<double>(args[0])) {
                err = name + "() expects a number.";
                return false;
            }
            out = Value(f(std::get<double>(args[0])));
            return true;
        });
    };
    math1("sqrt", [](double x) { return std::sqrt(x); });
    math1("abs", [](double x) { return std::fabs(x); });
    math1("floor", [](double x) { return std::floor(x); });

    define("push", 2, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<ArrayObj*>(args[0])) {
            err = "push() expects an array as its first argument.";
            return false;
        }
        auto arr = std::get<ArrayObj*>(args[0]);
        arr->elements.push_back(args[1]);
        out = Value(static_cast<double>(arr->elements.size()));  // new length
        return true;
    });

    define("pop", 1, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<ArrayObj*>(args[0])) {
            err = "pop() expects an array.";
            return false;
        }
        auto arr = std::get<ArrayObj*>(args[0]);
        if (arr->elements.empty()) { err = "pop() from an empty array."; return false; }
        out = arr->elements.back();
        arr->elements.pop_back();
        return true;
    });

    // type(x) -> a string naming the value's type.
    define("type", 1, [](std::vector<Value>& args, Value& out, std::string&) {
        const Value& v = args[0];
        const char* t = "unknown";
        if (std::holds_alternative<std::monostate>(v)) t = "nil";
        else if (std::holds_alternative<bool>(v)) t = "bool";
        else if (std::holds_alternative<double>(v)) t = "number";
        else if (std::holds_alternative<std::string>(v)) t = "string";
        else if (std::holds_alternative<ArrayObj*>(v)) t = "array";
        else if (std::holds_alternative<MapObj*>(v)) t = "map";
        else t = "function";  // closure or native
        out = Value(std::string(t));
        return true;
    });

    // keys(map) -> array of its keys (sorted).
    define("keys", 1, [this](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<MapObj*>(args[0])) {
            err = "keys() expects a map."; return false;
        }
        auto* result = heap_.allocate<ArrayObj>();
        for (const auto& [k, v] : std::get<MapObj*>(args[0])->entries) {
            (void)v;
            result->elements.push_back(Value(k));
        }
        out = Value(result);
        return true;
    });

    // has(map, key) -> bool
    define("has", 2, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<MapObj*>(args[0]) ||
            !std::holds_alternative<std::string>(args[1])) {
            err = "has() expects (map, string key)."; return false;
        }
        auto& m = std::get<MapObj*>(args[0])->entries;
        out = Value(m.find(std::get<std::string>(args[1])) != m.end());
        return true;
    });

    // remove(map, key) -> removes the entry, returns true if it existed.
    define("remove", 2, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<MapObj*>(args[0]) ||
            !std::holds_alternative<std::string>(args[1])) {
            err = "remove() expects (map, string key)."; return false;
        }
        auto& m = std::get<MapObj*>(args[0])->entries;
        out = Value(m.erase(std::get<std::string>(args[1])) > 0);
        return true;
    });

    // range(n) or range(start, end) -> array [start, start+1, ..., end-1].
    define("range", -1, [this](std::vector<Value>& args, Value& out, std::string& err) {
        if (args.size() < 1 || args.size() > 2) {
            err = "range() expects 1 or 2 arguments."; return false;
        }
        for (const auto& a : args)
            if (!std::holds_alternative<double>(a)) { err = "range() expects numbers."; return false; }
        double start = args.size() == 2 ? std::get<double>(args[0]) : 0;
        double end = std::get<double>(args.size() == 2 ? args[1] : args[0]);
        auto* result = heap_.allocate<ArrayObj>();
        if (end > start) result->elements.reserve(static_cast<size_t>(end - start));
        for (double i = start; i < end; i += 1) result->elements.emplace_back(i);
        out = Value(result);
        return true;
    });

    define("min", 2, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<double>(args[0]) || !std::holds_alternative<double>(args[1])) {
            err = "min() expects two numbers."; return false;
        }
        out = Value(std::min(std::get<double>(args[0]), std::get<double>(args[1])));
        return true;
    });
    define("max", 2, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<double>(args[0]) || !std::holds_alternative<double>(args[1])) {
            err = "max() expects two numbers."; return false;
        }
        out = Value(std::max(std::get<double>(args[0]), std::get<double>(args[1])));
        return true;
    });

    auto strMap = [&](const std::string& name, int (*f)(int)) {
        define(name, 1, [f, name](std::vector<Value>& args, Value& out, std::string& err) {
            if (!std::holds_alternative<std::string>(args[0])) {
                err = name + "() expects a string."; return false;
            }
            std::string s = std::get<std::string>(args[0]);
            for (char& c : s) c = static_cast<char>(f(static_cast<unsigned char>(c)));
            out = Value(s);
            return true;
        });
    };
    strMap("upper", [](int c) { return std::toupper(c); });
    strMap("lower", [](int c) { return std::tolower(c); });

    // substr(s, start, count) -> substring.
    define("substr", 3, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<double>(args[1]) ||
            !std::holds_alternative<double>(args[2])) {
            err = "substr() expects (string, number start, number count)."; return false;
        }
        const std::string& s = std::get<std::string>(args[0]);
        int start = static_cast<int>(std::get<double>(args[1]));
        int count = static_cast<int>(std::get<double>(args[2]));
        if (start < 0 || count < 0 || start > static_cast<int>(s.size())) {
            err = "substr() index out of range."; return false;
        }
        out = Value(s.substr(start, count));
        return true;
    });

    // chr(code) -> one-character string; ord(char) -> its code.
    define("chr", 1, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<double>(args[0])) { err = "chr() expects a number."; return false; }
        out = Value(std::string(1, static_cast<char>(static_cast<int>(std::get<double>(args[0])))));
        return true;
    });
    define("ord", 1, [](std::vector<Value>& args, Value& out, std::string& err) {
        if (!std::holds_alternative<std::string>(args[0]) ||
            std::get<std::string>(args[0]).size() != 1) {
            err = "ord() expects a one-character string."; return false;
        }
        out = Value(static_cast<double>(
            static_cast<unsigned char>(std::get<std::string>(args[0])[0])));
        return true;
    });
}

// ---------------------------------------------------------------------------
// Errors & call setup.
// ---------------------------------------------------------------------------
bool VM::runtimeError(const std::string& message) {
    std::cerr << "Runtime error: " << message << "\n";
    for (int i = frameCount_ - 1; i >= 0; i--) {
        const CallFrame& frame = frames_[i];
        const FunctionObj& fn = *frame.closure->function;
        size_t instr = frame.ip - 1;
        std::cerr << "  [line " << fn.chunk.lines[instr] << "] in "
                  << (fn.name.empty() ? "script" : fn.name + "()") << "\n";
    }
    resetStack();
    return false;
}

bool VM::call(ClosureObj* closure, int argCount) {
    const FunctionObj& fn = *closure->function;
    if (argCount != fn.arity) {
        return runtimeError("Expected " + std::to_string(fn.arity) +
                            " arguments but got " + std::to_string(argCount) + ".");
    }
    if (frameCount_ == FRAMES_MAX) {
        return runtimeError("Stack overflow (call depth exceeded).");
    }
    CallFrame& frame = frames_[frameCount_++];
    frame.closure = closure;
    frame.ip = 0;
    frame.slotBase = stackTop_ - argCount - 1;  // callee value + args
    return true;
}

bool VM::callValue(const Value& callee, int argCount) {
    if (std::holds_alternative<ClosureObj*>(callee)) {
        return call(std::get<ClosureObj*>(callee), argCount);
    }
    if (std::holds_alternative<NativeObj*>(callee)) {
        const auto& native = std::get<NativeObj*>(callee);
        if (native->arity >= 0 && native->arity != argCount) {
            return runtimeError(native->name + "() expected " +
                                std::to_string(native->arity) + " arguments but got " +
                                std::to_string(argCount) + ".");
        }
        std::vector<Value> args(stack_.begin() + (stackTop_ - argCount),
                                stack_.begin() + stackTop_);
        Value result;
        std::string err;
        if (!native->fn(args, result, err)) return runtimeError(err);
        stackTop_ -= argCount + 1;  // pop args and the native value
        push(result);
        return true;
    }
    return runtimeError("Can only call functions. Got: " + valueToString(callee) + ".");
}

// ---------------------------------------------------------------------------
// Upvalues.
// ---------------------------------------------------------------------------
Upvalue* VM::captureUpvalue(int stackIndex) {
    // Reuse an existing open upvalue for the same slot so captures share state.
    for (auto& uv : openUpvalues_) {
        if (!uv->closed && uv->stackIndex == stackIndex) return uv;
    }
    auto uv = heap_.allocate<Upvalue>();
    uv->closed = false;
    uv->stackIndex = stackIndex;
    openUpvalues_.push_back(uv);
    return uv;
}

void VM::closeUpvalues(int fromIndex) {
    for (auto it = openUpvalues_.begin(); it != openUpvalues_.end();) {
        if (!(*it)->closed && (*it)->stackIndex >= fromIndex) {
            (*it)->closedValue = stack_[(*it)->stackIndex];
            (*it)->closed = true;
            it = openUpvalues_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// The core dispatch loop.
// ---------------------------------------------------------------------------
InterpretResult VM::execute() {
    CallFrame* frame = &frames_[frameCount_ - 1];

    auto chunkOf = [&]() -> const Chunk& { return frame->closure->function->chunk; };
    auto readByte = [&]() -> uint8_t { return chunkOf().code[frame->ip++]; };
    auto readShort = [&]() -> uint16_t {
        frame->ip += 2;
        return static_cast<uint16_t>((chunkOf().code[frame->ip - 2] << 8) |
                                     chunkOf().code[frame->ip - 1]);
    };
    auto readConstant = [&]() -> const Value& { return chunkOf().constants[readByte()]; };

    for (;;) {
        // Safepoint: a collection here is always safe because the entire live
        // set is reachable from the stack, frames, globals, and open upvalues.
        if (heap_.shouldCollect()) heap_.collect();

        uint8_t instruction = readByte();
        switch (instruction) {
            case OP_CONSTANT: push(readConstant()); break;
            case OP_NIL:   push(Value(std::monostate{})); break;
            case OP_TRUE:  push(Value(true)); break;
            case OP_FALSE: push(Value(false)); break;
            case OP_POP:   pop(); break;

            case OP_GET_LOCAL: {
                uint8_t slot = readByte();
                push(stack_[frame->slotBase + slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = readByte();
                stack_[frame->slotBase + slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                const std::string& name = std::get<std::string>(readConstant());
                auto it = globals_.find(name);
                if (it == globals_.end()) { runtimeError("Undefined variable '" + name + "'."); return InterpretResult::RUNTIME_ERROR; }
                push(it->second);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                const std::string& name = std::get<std::string>(readConstant());
                globals_[name] = peek(0);
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                const std::string& name = std::get<std::string>(readConstant());
                if (globals_.find(name) == globals_.end()) { runtimeError("Undefined variable '" + name + "'."); return InterpretResult::RUNTIME_ERROR; }
                globals_[name] = peek(0);
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = readByte();
                const auto& uv = frame->closure->upvalues[slot];
                push(uv->closed ? uv->closedValue : stack_[uv->stackIndex]);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = readByte();
                auto& uv = frame->closure->upvalues[slot];
                if (uv->closed) uv->closedValue = peek(0);
                else stack_[uv->stackIndex] = peek(0);
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(Value(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:
            case OP_LESS: {
                if (!std::holds_alternative<double>(peek(0)) ||
                    !std::holds_alternative<double>(peek(1))) { runtimeError("Comparison operands must be numbers."); return InterpretResult::RUNTIME_ERROR; }
                double b = std::get<double>(pop());
                double a = std::get<double>(pop());
                push(Value(instruction == OP_GREATER ? a > b : a < b));
                break;
            }

            case OP_ADD: {
                if (std::holds_alternative<double>(peek(0)) &&
                    std::holds_alternative<double>(peek(1))) {
                    double b = std::get<double>(pop());
                    double a = std::get<double>(pop());
                    push(Value(a + b));
                } else if (std::holds_alternative<std::string>(peek(0)) &&
                           std::holds_alternative<std::string>(peek(1))) {
                    std::string b = std::get<std::string>(pop());
                    std::string a = std::get<std::string>(pop());
                    push(Value(a + b));
                } else {
                    runtimeError("Operands to '+' must be two numbers or two strings.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT:
            case OP_MULTIPLY:
            case OP_DIVIDE:
            case OP_MODULO: {
                if (!std::holds_alternative<double>(peek(0)) ||
                    !std::holds_alternative<double>(peek(1))) { runtimeError("Arithmetic operands must be numbers."); return InterpretResult::RUNTIME_ERROR; }
                double b = std::get<double>(pop());
                double a = std::get<double>(pop());
                if ((instruction == OP_DIVIDE || instruction == OP_MODULO) && b == 0) { runtimeError("Division by zero."); return InterpretResult::RUNTIME_ERROR; }
                switch (instruction) {
                    case OP_SUBTRACT: push(Value(a - b)); break;
                    case OP_MULTIPLY: push(Value(a * b)); break;
                    case OP_DIVIDE:   push(Value(a / b)); break;
                    case OP_MODULO:   push(Value(std::fmod(a, b))); break;
                }
                break;
            }

            case OP_NOT:
                push(Value(isFalsey(pop())));
                break;
            case OP_NEGATE:
                if (!std::holds_alternative<double>(peek(0))) { runtimeError("Operand to unary '-' must be a number."); return InterpretResult::RUNTIME_ERROR; }
                push(Value(-std::get<double>(pop())));
                break;

            case OP_PRINT:
                std::cout << valueToString(pop()) << "\n";
                break;

            case OP_JUMP: {
                uint16_t offset = readShort();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = readShort();
                frame->ip -= offset;
                break;
            }

            case OP_BUILD_ARRAY: {
                int n = readByte();
                auto arr = heap_.allocate<ArrayObj>();
                arr->elements.resize(n);
                // Elements were pushed left-to-right; pop them back into place.
                for (int i = n - 1; i >= 0; i--) arr->elements[i] = pop();
                push(Value(arr));
                break;
            }
            case OP_BUILD_MAP: {
                int n = readByte();
                auto map = heap_.allocate<MapObj>();
                // Pairs are on the stack as k1 v1 k2 v2 ...; consume from the top.
                stackTop_ -= 2 * n;
                for (int i = 0; i < n; i++) {
                    const Value& key = stack_[stackTop_ + 2 * i];
                    const Value& val = stack_[stackTop_ + 2 * i + 1];
                    map->entries[std::get<std::string>(key)] = val;
                }
                push(Value(map));
                break;
            }
            case OP_INDEX_GET: {
                Value index = pop();
                Value object = pop();
                if (std::holds_alternative<MapObj*>(object)) {
                    if (!std::holds_alternative<std::string>(index)) { runtimeError("Map keys must be strings."); return InterpretResult::RUNTIME_ERROR; }
                    const auto& map = std::get<MapObj*>(object);
                    auto it = map->entries.find(std::get<std::string>(index));
                    if (it == map->entries.end()) { runtimeError("Undefined map key '" + std::get<std::string>(index) + "'."); return InterpretResult::RUNTIME_ERROR; }
                    push(it->second);
                    break;
                }
                if (!std::holds_alternative<double>(index)) { runtimeError("Index must be a number."); return InterpretResult::RUNTIME_ERROR; }
                int i = static_cast<int>(std::get<double>(index));
                if (std::holds_alternative<ArrayObj*>(object)) {
                    const auto& arr = std::get<ArrayObj*>(object);
                    if (i < 0 || i >= static_cast<int>(arr->elements.size())) { runtimeError("Array index out of bounds."); return InterpretResult::RUNTIME_ERROR; }
                    push(arr->elements[i]);
                } else if (std::holds_alternative<std::string>(object)) {
                    const std::string& s = std::get<std::string>(object);
                    if (i < 0 || i >= static_cast<int>(s.size())) { runtimeError("String index out of bounds."); return InterpretResult::RUNTIME_ERROR; }
                    push(Value(std::string(1, s[i])));
                } else {
                    runtimeError("Can only index arrays, strings, and maps.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OP_INDEX_SET: {
                Value value = pop();
                Value index = pop();
                Value object = pop();
                if (std::holds_alternative<MapObj*>(object)) {
                    if (!std::holds_alternative<std::string>(index)) { runtimeError("Map keys must be strings."); return InterpretResult::RUNTIME_ERROR; }
                    std::get<MapObj*>(object)->entries[std::get<std::string>(index)] = value;
                    push(value);
                    break;
                }
                if (!std::holds_alternative<ArrayObj*>(object)) { runtimeError("Can only assign into arrays and maps."); return InterpretResult::RUNTIME_ERROR; }
                if (!std::holds_alternative<double>(index)) { runtimeError("Index must be a number."); return InterpretResult::RUNTIME_ERROR; }
                int i = static_cast<int>(std::get<double>(index));
                const auto& arr = std::get<ArrayObj*>(object);
                if (i < 0 || i >= static_cast<int>(arr->elements.size())) { runtimeError("Array index out of bounds."); return InterpretResult::RUNTIME_ERROR; }
                arr->elements[i] = value;
                push(value);  // assignment is an expression
                break;
            }

            case OP_CALL: {
                int argCount = readByte();
                if (!callValue(peek(argCount), argCount)) return InterpretResult::RUNTIME_ERROR;
                frame = &frames_[frameCount_ - 1];
                break;
            }

            case OP_CLOSURE: {
                auto fn = std::get<FunctionObj*>(readConstant());
                auto closure = heap_.allocate<ClosureObj>();
                closure->function = fn;
                closure->upvalues.resize(fn->upvalueCount);
                for (int i = 0; i < fn->upvalueCount; i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();
                    if (isLocal) {
                        // Capture a local of the *current* frame.
                        closure->upvalues[i] = captureUpvalue(static_cast<int>(frame->slotBase) + index);
                    } else {
                        // Inherit an upvalue from the enclosing closure.
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                push(Value(closure));
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(stackTop_ - 1);
                pop();
                break;

            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(static_cast<int>(frame->slotBase));  // close the frame's captured locals
                frameCount_--;
                if (frameCount_ == 0) {
                    pop();  // discard the script closure
                    return InterpretResult::OK;
                }
                stackTop_ = static_cast<int>(frame->slotBase);
                push(result);
                frame = &frames_[frameCount_ - 1];
                break;
            }

            default:
                runtimeError("Unknown opcode encountered.");
                return InterpretResult::RUNTIME_ERROR;
        }
    }
}
