#ifndef CVM_VM_H
#define CVM_VM_H

#include <array>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "gc.h"
#include "value.h"

// Result of running a program.
enum class InterpretResult { OK, RUNTIME_ERROR };

// A stack-based virtual machine that executes compiled bytecode.
class VM {
public:
    explicit VM(Heap& heap);

    // Execute a compiled top-level (script) function.
    InterpretResult run(FunctionObj* script);

private:
    static constexpr int FRAMES_MAX = 256;
    static constexpr int STACK_MAX = FRAMES_MAX * 256;

    // A single function activation record.
    struct CallFrame {
        ClosureObj* closure = nullptr;
        size_t ip = 0;        // instruction pointer into closure->function->chunk.code
        size_t slotBase = 0;  // index into `stack_` of this frame's slot 0
    };

    Heap& heap_;
    std::array<Value, STACK_MAX> stack_;
    int stackTop_ = 0;
    std::array<CallFrame, FRAMES_MAX> frames_;
    int frameCount_ = 0;
    std::unordered_map<std::string, Value> globals_;

    // Upvalues that still alias a live stack slot (not yet closed).
    std::vector<Upvalue*> openUpvalues_;

    std::chrono::steady_clock::time_point startTime_;

    // stack ops
    void push(const Value& v) { stack_[stackTop_++] = v; }
    Value pop() { return stack_[--stackTop_]; }
    const Value& peek(int distance) const { return stack_[stackTop_ - 1 - distance]; }
    void resetStack() { stackTop_ = 0; frameCount_ = 0; openUpvalues_.clear(); }

    bool callValue(const Value& callee, int argCount);
    bool call(ClosureObj* closure, int argCount);

    // Upvalue lifecycle.
    Upvalue* captureUpvalue(int stackIndex);
    void closeUpvalues(int fromIndex);

    void defineNatives();
    bool runtimeError(const std::string& message);

    // Marks the VM's roots for the garbage collector.
    void markRoots();

    InterpretResult execute();  // the core dispatch loop
};

#endif // CVM_VM_H
