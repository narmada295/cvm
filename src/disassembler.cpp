#include "disassembler.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "opcode.h"

static int simpleInstruction(std::ostringstream& out, const char* name, int offset) {
    out << name << "\n";
    return offset + 1;
}

static int byteInstruction(std::ostringstream& out, const char* name,
                           const Chunk& chunk, int offset) {
    uint8_t slot = chunk.code[offset + 1];
    out << std::left << std::setw(16) << name << " " << static_cast<int>(slot) << "\n";
    return offset + 2;
}

static int constantInstruction(std::ostringstream& out, const char* name,
                               const Chunk& chunk, int offset) {
    uint8_t constant = chunk.code[offset + 1];
    out << std::left << std::setw(16) << name << " " << static_cast<int>(constant)
        << " '" << valueToString(chunk.constants[constant]) << "'\n";
    return offset + 2;
}

static int jumpInstruction(std::ostringstream& out, const char* name, int sign,
                           const Chunk& chunk, int offset) {
    uint16_t jump = static_cast<uint16_t>(chunk.code[offset + 1] << 8);
    jump |= chunk.code[offset + 2];
    int target = offset + 3 + sign * jump;
    out << std::left << std::setw(16) << name << " " << offset << " -> " << target << "\n";
    return offset + 3;
}

static int disassembleInstruction(std::ostringstream& out, const Chunk& chunk, int offset) {
    out << std::right << std::setfill('0') << std::setw(4) << offset
        << std::setfill(' ') << " ";

    // Show the source line, or "|" if it matches the previous instruction.
    if (offset > 0 && chunk.lines[offset] == chunk.lines[offset - 1]) {
        out << "   | ";
    } else {
        out << std::right << std::setw(4) << chunk.lines[offset] << " ";
    }

    uint8_t instruction = chunk.code[offset];
    switch (instruction) {
        case OP_CONSTANT:    return constantInstruction(out, "CONSTANT", chunk, offset);
        case OP_NIL:         return simpleInstruction(out, "NIL", offset);
        case OP_TRUE:        return simpleInstruction(out, "TRUE", offset);
        case OP_FALSE:       return simpleInstruction(out, "FALSE", offset);
        case OP_POP:         return simpleInstruction(out, "POP", offset);
        case OP_GET_LOCAL:   return byteInstruction(out, "GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:   return byteInstruction(out, "SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:  return constantInstruction(out, "GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:  return constantInstruction(out, "SET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL: return constantInstruction(out, "DEFINE_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE: return byteInstruction(out, "GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE: return byteInstruction(out, "SET_UPVALUE", chunk, offset);
        case OP_EQUAL:       return simpleInstruction(out, "EQUAL", offset);
        case OP_GREATER:     return simpleInstruction(out, "GREATER", offset);
        case OP_LESS:        return simpleInstruction(out, "LESS", offset);
        case OP_ADD:         return simpleInstruction(out, "ADD", offset);
        case OP_SUBTRACT:    return simpleInstruction(out, "SUBTRACT", offset);
        case OP_MULTIPLY:    return simpleInstruction(out, "MULTIPLY", offset);
        case OP_DIVIDE:      return simpleInstruction(out, "DIVIDE", offset);
        case OP_MODULO:      return simpleInstruction(out, "MODULO", offset);
        case OP_NOT:         return simpleInstruction(out, "NOT", offset);
        case OP_NEGATE:      return simpleInstruction(out, "NEGATE", offset);
        case OP_PRINT:       return simpleInstruction(out, "PRINT", offset);
        case OP_JUMP:        return jumpInstruction(out, "JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE: return jumpInstruction(out, "JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:        return jumpInstruction(out, "LOOP", -1, chunk, offset);
        case OP_BUILD_ARRAY: return byteInstruction(out, "BUILD_ARRAY", chunk, offset);
        case OP_BUILD_MAP:   return byteInstruction(out, "BUILD_MAP", chunk, offset);
        case OP_INDEX_GET:   return simpleInstruction(out, "INDEX_GET", offset);
        case OP_INDEX_SET:   return simpleInstruction(out, "INDEX_SET", offset);
        case OP_CALL:        return byteInstruction(out, "CALL", chunk, offset);
        case OP_CLOSE_UPVALUE: return simpleInstruction(out, "CLOSE_UPVALUE", offset);
        case OP_CLOSURE: {
            int idx = offset + 1;
            uint8_t constant = chunk.code[idx++];
            out << std::left << std::setw(16) << "CLOSURE" << " " << static_cast<int>(constant)
                << " '" << valueToString(chunk.constants[constant]) << "'\n";
            // Each captured upvalue has an [isLocal][index] operand pair.
            const Value& c = chunk.constants[constant];
            if (std::holds_alternative<FunctionObj*>(c)) {
                int count = std::get<FunctionObj*>(c)->upvalueCount;
                for (int j = 0; j < count; j++) {
                    int isLocal = chunk.code[idx++];
                    int index = chunk.code[idx++];
                    out << std::right << std::setfill('0') << std::setw(4) << (idx - 2)
                        << std::setfill(' ') << "      |                  "
                        << (isLocal ? "local" : "upvalue")
                        << " " << index << "\n";
                }
            }
            return idx;
        }
        case OP_RETURN:      return simpleInstruction(out, "RETURN", offset);
        default:
            out << "Unknown opcode " << static_cast<int>(instruction) << "\n";
            return offset + 1;
    }
}

static void disassembleChunk(std::ostringstream& out, const FunctionObj& fn) {
    std::string title = fn.name.empty() ? "<script>" : fn.name;
    out << "== " << title << " ==\n";
    const Chunk& chunk = fn.chunk;
    for (int offset = 0; offset < static_cast<int>(chunk.code.size());) {
        offset = disassembleInstruction(out, chunk, offset);
    }
    out << "\n";

    // Recurse into any nested function constants.
    for (const auto& constant : chunk.constants) {
        if (std::holds_alternative<FunctionObj*>(constant)) {
            const auto& nested = std::get<FunctionObj*>(constant);
            if (nested) disassembleChunk(out, *nested);
        }
    }
}

std::string disassemble(const FunctionObj& fn) {
    std::ostringstream out;
    disassembleChunk(out, fn);
    return out.str();
}
