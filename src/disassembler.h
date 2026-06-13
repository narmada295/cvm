#ifndef CVM_DISASSEMBLER_H
#define CVM_DISASSEMBLER_H

#include <string>

#include "value.h"

// Renders a chunk's bytecode in human-readable form. Recurses into any
// nested function constants so the whole program is shown.
std::string disassemble(const FunctionObj& fn);

#endif // CVM_DISASSEMBLER_H
