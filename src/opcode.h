#ifndef CVM_OPCODE_H
#define CVM_OPCODE_H

#include <cstdint>

// The Instruction Set Architecture (ISA) of the CVM++ virtual machine.
// Every instruction is one byte; some are followed by operand bytes.
enum OpCode : uint8_t {
    OP_CONSTANT,       // [idx]      push constants[idx]
    OP_NIL,            //            push nil
    OP_TRUE,           //            push true
    OP_FALSE,          //            push false
    OP_POP,            //            discard top of stack

    OP_GET_LOCAL,      // [slot]     push frame-local slot
    OP_SET_LOCAL,      // [slot]     store top into frame-local slot (leaves value)
    OP_GET_GLOBAL,     // [idx]      push global named constants[idx]
    OP_SET_GLOBAL,     // [idx]      assign global named constants[idx]
    OP_DEFINE_GLOBAL,  // [idx]      define global named constants[idx] from top
    OP_GET_UPVALUE,    // [idx]      push closure upvalue idx
    OP_SET_UPVALUE,    // [idx]      assign closure upvalue idx (leaves value)

    OP_EQUAL,          //            a == b
    OP_GREATER,        //            a > b
    OP_LESS,           //            a < b
    OP_ADD,            //            a + b   (numbers add, strings concat)
    OP_SUBTRACT,       //            a - b
    OP_MULTIPLY,       //            a * b
    OP_DIVIDE,         //            a / b
    OP_MODULO,         //            a % b
    OP_NOT,            //            !a
    OP_NEGATE,         //            -a

    OP_PRINT,          //            pop and print top of stack

    OP_JUMP,           // [hi][lo]   ip += offset
    OP_JUMP_IF_FALSE,  // [hi][lo]   ip += offset if top is falsey (top left on stack)
    OP_LOOP,           // [hi][lo]   ip -= offset (backward jump)

    OP_BUILD_ARRAY,    // [n]        pop n values, push an array of them
    OP_BUILD_MAP,      // [n]        pop n (key,value) pairs, push a map
    OP_INDEX_GET,      //            obj, idx -> obj[idx]   (arrays, strings, maps)
    OP_INDEX_SET,      //            obj, idx, val -> val   (stores into obj[idx])

    OP_CALL,           // [argc]     call value at stack[top-argc]
    OP_CLOSURE,        // [idx] then [isLocal][index] x upvalueCount: build a closure
    OP_CLOSE_UPVALUE,  //            close the topmost stack slot into its upvalue, pop
    OP_RETURN          //            return from current function
};

#endif // CVM_OPCODE_H
