# CVM++

A lightweight scripting language implemented from scratch in C++17. Source code
is **lexed** into tokens, **parsed** into an AST, **compiled** to a compact
bytecode, and executed by a custom **stack-based virtual machine**.

```
  source ──▶ Lexer ──▶ Tokens ──▶ Parser ──▶ AST ──▶ Compiler ──▶ Bytecode ──▶ VM ──▶ result
```

This project demystifies how a real language toolchain works end to end: every
stage is a separate, readable module. The runtime includes first-class
functions, closures, arrays, maps, and a precise **mark-and-sweep garbage
collector**.

## Build & Run

```sh
make                             # builds the ./cvm binary
./cvm examples/functions.cvm     # run a script
make run FILE=examples/maps.cvm  # ...or run via make
./cvm                            # start the interactive REPL
make test                        # run every example script
```

Requires a C++17 compiler (`g++`/`clang++`) and `make`.

## Examples

Each script in [`examples/`](examples/) demonstrates a slice of the language:

| Script | Demonstrates |
|--------|--------------|
| `hello.cvm`        | Strings, numbers, `print`, concatenation |
| `arithmetic.cvm`   | Operator precedence, comparison & logical operators |
| `control_flow.cvm` | `let`, block scoping, `if`/`else`, `while` (FizzBuzz) |
| `functions.cvm`    | Parameters, return values, recursion, first-class functions |
| `closures.cvm`     | Closures capturing and mutating enclosing variables |
| `arrays.cvm`       | Array literals, indexing, mutation, `push`/`pop`, iteration |
| `maps.cvm`         | Map literals, indexing, `keys`/`has`/`remove`, a frequency counter |
| `loops.cvm`        | C-style `for`, `break`/`continue`, `range`, nested loops |
| `natives.cvm`      | Built-in functions: math, `len`, conversions, `clock` |

## Usage

```
cvm [options] [script.cvm]
```

| Option       | Effect                                          |
|--------------|-------------------------------------------------|
| `--tokens`   | Dump the token stream from the lexer            |
| `--ast`      | Dump the abstract syntax tree from the parser   |
| `--bytecode` | Dump the compiled bytecode (disassembly)        |
| `--debug`    | All three of the above                          |
| `--stress-gc`| Run the garbage collector at every safepoint    |
| `--gc-stats` | Print garbage-collector statistics              |
| `-h`         | Help                                            |

With no script file, an interactive REPL starts (globals and functions persist
between lines).

Inspect the full pipeline for any script:

```sh
./cvm --debug examples/hello.cvm
```

## The Language

A dynamically-typed language with numbers (doubles), strings, booleans, `nil`,
**arrays**, **maps**, and **functions / closures**.

```javascript
// variables
let name = "world";
let count = 0;

// arithmetic, comparison, logical operators
print 2 + 3 * 4;          // 14
print 17 % 5;             // 2
print (1 < 2) and !false; // true

// control flow: if / while / C-style for, with break & continue
if (count == 0) { print "zero"; } else { print "nonzero"; }
while (count < 3) { print count; count = count + 1; }
for (let i = 0; i < 5; i = i + 1) {
    if (i == 3) { break; }
    if (i % 2 == 0) { continue; }
    print i;              // 1
}

// arrays: literals, indexing, mutation, growth
let xs = [1, 2, 3];
xs[0] = 99;
push(xs, 4);
print xs[0] + len(xs);    // 103

// maps: string-keyed dictionaries
let m = { "name": "Ada", age: 36 };
m["age"] = m["age"] + 1;
print m["name"];          // Ada
print keys(m);            // ["age", "name"]

// functions, recursion, first-class values
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
print factorial(5);       // 120

// closures: inner functions capture (and can mutate) enclosing variables
fn makeCounter() {
    let count = 0;
    fn next() { count = count + 1; return count; }
    return next;
}
let c = makeCounter();
print c();  // 1
print c();  // 2
```

### Built-in (native) functions

| Function        | Description                                          |
|-----------------|------------------------------------------------------|
| `clock()`       | Seconds elapsed since the program started            |
| `len(x)`        | Length of a string, array, or map                    |
| `str(x)`        | Convert any value to its string form                 |
| `num(s)`        | Parse a string/number into a number                  |
| `type(x)`       | Type name: `"number"`, `"string"`, `"array"`, `"map"`, `"function"`, `"bool"`, `"nil"` |
| `sqrt`,`abs`,`floor` | Math on numbers                                 |
| `min(a,b)`,`max(a,b)` | Smaller / larger of two numbers                |
| `range(n)` / `range(a,b)` | Array `[0..n)` or `[a..b)`                 |
| `push(arr, v)`  | Append `v` to `arr`; returns the new length          |
| `pop(arr)`      | Remove and return the last element                   |
| `keys(map)`     | Array of the map's keys (sorted)                     |
| `has(map, key)` | Whether the map contains `key`                       |
| `remove(map, key)` | Delete an entry; returns whether it existed       |
| `upper(s)`,`lower(s)` | Change string case                             |
| `substr(s, start, count)` | Substring                                  |
| `chr(n)`,`ord(c)` | Char code ↔ one-character string                   |

### Notes / limitations

- Top-level declarations execute in order, so a function must be defined before
  it is *called* at the top level (functions are not hoisted). Functions may
  freely call each other from inside their bodies, since those calls run later.
- `+` adds two numbers or concatenates two strings; it does not coerce across
  types.
- Up to 256 constants, locals, and upvalues per function (single-byte operands).
- Indexing a string returns a one-character string; arrays and maps are
  reference types (assigning one to a new variable shares the same storage).
- Map keys are strings; reading a missing key is a runtime error (use `has`).
- A `{` at the start of a statement is parsed as a block, not a map literal —
  write `let m = {...};` (map literals are an expression form).
- `break` / `continue` apply to the innermost enclosing loop.

### Grammar (EBNF)

```
program     → declaration* EOF
declaration → funDecl | varDecl | statement
funDecl     → "fn" IDENT "(" params? ")" block
varDecl     → "let" IDENT ( "=" expression )? ";"
statement   → exprStmt | printStmt | ifStmt | whileStmt | forStmt
            | returnStmt | breakStmt | continueStmt | block
block       → "{" declaration* "}"
ifStmt      → "if" "(" expression ")" statement ( "else" statement )?
whileStmt   → "while" "(" expression ")" statement
forStmt     → "for" "(" ( varDecl | exprStmt | ";" ) expression? ";" expression? ")" statement
returnStmt  → "return" expression? ";"
breakStmt   → "break" ";"
continueStmt→ "continue" ";"
expression  → assignment
assignment  → ( call "[" expression "]" | IDENT ) "=" assignment | logic_or
logic_or    → logic_and ( "or" logic_and )*
logic_and   → equality ( "and" equality )*
equality    → comparison ( ( "==" | "!=" ) comparison )*
comparison  → term ( ( "<" | "<=" | ">" | ">=" ) term )*
term        → factor ( ( "+" | "-" ) factor )*
factor      → unary ( ( "*" | "/" | "%" ) unary )*
unary       → ( "!" | "-" ) unary | call
call        → primary ( "(" arguments? ")" | "[" expression "]" )*
primary     → NUMBER | STRING | "true" | "false" | "nil" | IDENT
            | "(" expression ")" | "[" ( expression ( "," expression )* )? "]"
            | "{" ( mapEntry ( "," mapEntry )* )? "}"
mapEntry    → ( STRING | IDENT ) ":" expression
```

## Architecture

| Module | File | Responsibility |
|--------|------|----------------|
| Lexer    | `src/lexer.*`        | Source text → `Token` stream |
| Parser   | `src/parser.*`       | Tokens → AST (recursive descent + precedence climbing) |
| AST      | `src/ast.*`          | Node definitions + pretty-printer |
| Compiler | `src/compiler.*`     | AST → bytecode `Chunk`s (one per function) |
| ISA      | `src/opcode.h`       | The instruction set |
| VM       | `src/vm.*`           | Stack-based execution loop with call frames |
| Value    | `src/value.*`        | Runtime value type + bytecode chunk container |
| GC       | `src/gc.*`           | Mark-and-sweep garbage collector + heap |
| Tools    | `src/disassembler.*`, `src/ast_printer.cpp` | Debug dumps |
| CLI      | `src/main.cpp`       | REPL, file runner, flag parsing |

### How execution works

The whole program compiles into an implicit top-level "script" function. Each
`fn` becomes its own `FunctionObj` with an independent bytecode chunk. At
runtime the VM maintains:

- a **value stack** for operands and locals, and
- a stack of **call frames**, each recording the running closure, its
  instruction pointer, and the base of its slot window on the value stack.

Locals are addressed by slot offset from the frame base; globals live in a
name→value map. `OP_CALL` pushes a new frame; `OP_RETURN` pops it, unwinds the
callee's slots, and leaves the return value on top.

**Closures.** Every runtime function is a `ClosureObj` wrapping a compiled
`FunctionObj` plus the *upvalues* it captures. While a captured variable is
still on the stack its upvalue is "open" (it aliases the slot); when the
variable goes out of scope `OP_CLOSE_UPVALUE` "closes" it, lifting the value
onto the heap so the closure keeps working after its defining function returns.
Multiple closures capturing the same variable share one upvalue, so they see
each other's mutations.

**Native functions** are `NativeObj` values holding a C++ callback; `OP_CALL`
dispatches to them directly without pushing a bytecode frame.

### Garbage collection

Heap objects (functions, closures, upvalues, arrays, maps, natives) are managed
by a precise **mark-and-sweep** garbage collector (`src/gc.*`) — there are no
`shared_ptr`s. Strings are kept inline as value types and are not GC-managed.

The collector runs **only at safepoints** — the top of the VM's dispatch loop,
between bytecode instructions. That is the key invariant: between instructions
the entire live object graph is reachable from a small, fixed root set
(the value stack, call frames, globals, and open upvalues), so a mid-instruction
allocation can never be collected out from under the code that just made it.

A cycle does the standard three steps:

1. **Mark roots** — the VM marks its roots (`VM::markRoots`).
2. **Trace** — a worklist (grey stack) blackens each object, marking everything
   it references (a closure marks its function and upvalues, an array marks its
   elements, a function marks the constants in its chunk, …).
3. **Sweep** — every unmarked object on the intrusive all-objects list is freed.

Collection is triggered when `bytesAllocated` crosses a threshold that grows
with the live-set size. Two flags help you see it work:

```sh
./cvm --gc-stats  script.cvm    # report collections, live objects, bytes
./cvm --stress-gc script.cvm    # collect at EVERY safepoint (root-bug detector)
```

`--stress-gc` is how the GC is validated: every sample produces byte-identical
output with and without it. If a root were missing, stress mode would free a
live object and the output would diverge or crash.
