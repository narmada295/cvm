#ifndef CVM_VALUE_H
#define CVM_VALUE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// Garbage-collected object model.
//
// All heap objects derive from GCObject and are owned by the Heap (see gc.h),
// which links them into an intrusive list and frees them during collection.
// Values hold *raw* pointers to these objects; the GC keeps live ones alive.
// Strings are kept inline (value semantics) and are NOT GC-managed.
// ---------------------------------------------------------------------------
enum class ObjType { Function, Closure, Upvalue, Native, Array, Map };

struct GCObject {
    ObjType type;
    bool marked = false;     // set during the GC mark phase
    size_t size = 0;         // bytes charged to the heap (set by Heap::allocate)
    GCObject* next = nullptr;  // intrusive all-objects list
    explicit GCObject(ObjType t) : type(t) {}
    virtual ~GCObject() = default;
};

// Forward declarations of the concrete object kinds.
struct FunctionObj;  // compiled function template (lives in the constant pool)
struct ClosureObj;   // runtime function value (template + captured upvalues)
struct NativeObj;    // built-in function implemented in C++
struct ArrayObj;     // dynamic array
struct MapObj;       // string-keyed dictionary

// A CVM++ runtime value. nil is represented by std::monostate.
using Value = std::variant<
    std::monostate,   // nil
    bool,             // boolean
    double,           // number
    std::string,      // string (inline, not GC-managed)
    FunctionObj*,     // function template (compile time)
    ClosureObj*,      // callable closure (run time)
    NativeObj*,       // native function
    ArrayObj*,        // array
    MapObj*>;         // map / dictionary

// --- A Chunk is a compiled blob of bytecode plus its constant pool. ---
struct Chunk {
    std::vector<uint8_t> code;     // the raw opcodes + operands
    std::vector<Value> constants;  // literal pool referenced by index
    std::vector<int> lines;        // source line per byte (for errors)

    void write(uint8_t byte, int line) {
        code.push_back(byte);
        lines.push_back(line);
    }
    int addConstant(const Value& v) {
        constants.push_back(v);
        return static_cast<int>(constants.size() - 1);
    }
};

// Describes, at compile time, how one upvalue is captured.
struct UpvalueDesc {
    bool isLocal;   // capture a local of the *immediately* enclosing function?
    uint8_t index;  // its slot (if local) or upvalue index (if from outer closure)
};

// A compiled function: its own chunk of bytecode plus metadata.
struct FunctionObj : GCObject {
    int arity = 0;
    int upvalueCount = 0;
    Chunk chunk;
    std::string name;
    FunctionObj() : GCObject(ObjType::Function) {}
};

// A live upvalue: while "open" it aliases a slot on the VM stack; once the
// variable goes out of scope it is "closed" and owns its own copy of the value.
struct Upvalue : GCObject {
    bool closed = false;
    int stackIndex = 0;
    Value closedValue;
    Upvalue() : GCObject(ObjType::Upvalue) {}
};

// A runtime function value: a template plus the upvalues it captured.
struct ClosureObj : GCObject {
    FunctionObj* function = nullptr;
    std::vector<Upvalue*> upvalues;
    ClosureObj() : GCObject(ObjType::Closure) {}
};

// A built-in function. Returns false (with `error` set) to raise a runtime error.
struct NativeObj : GCObject {
    std::string name;
    int arity;  // exact arg count, or -1 for variadic
    std::function<bool(std::vector<Value>& args, Value& result, std::string& error)> fn;
    NativeObj() : GCObject(ObjType::Native) {}
};

// A dynamic array of values.
struct ArrayObj : GCObject {
    std::vector<Value> elements;
    ArrayObj() : GCObject(ObjType::Array) {}
};

// A string-keyed dictionary. std::map keeps keys sorted, so iteration (and
// thus `keys()` / printing) is deterministic.
struct MapObj : GCObject {
    std::map<std::string, Value> entries;
    MapObj() : GCObject(ObjType::Map) {}
};

// Helpers shared by the VM and the disassembler.
std::string valueToString(const Value& v);  // user-facing (strings unquoted)
bool valuesEqual(const Value& a, const Value& b);
bool isFalsey(const Value& v);  // nil and false are falsey; everything else truthy

#endif // CVM_VALUE_H
