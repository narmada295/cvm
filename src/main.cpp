#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ast.h"
#include "compiler.h"
#include "disassembler.h"
#include "gc.h"
#include "lexer.h"
#include "parser.h"
#include "vm.h"

// Bundles the optional debug dumps requested on the command line.
struct Options {
    bool showTokens = false;
    bool showAst = false;
    bool showBytecode = false;
    bool stressGc = false;
    bool gcStats = false;
};

// Lexer -> Parser -> Compiler -> VM. Returns the exit status (0 == success).
// Returns -1 to signal a *compile-time* failure (used by the REPL).
static int interpret(const std::string& source, const Options& opts, Heap& heap, VM& vm) {
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.scanTokens();

    // Surface lexical errors before going further.
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::ERROR) {
            std::cerr << "[line " << tok.line << "] Lex error: " << tok.lexeme << "\n";
            return -1;
        }
    }

    if (opts.showTokens) {
        std::cout << "=== TOKENS ===\n";
        for (const auto& tok : tokens) {
            std::cout << tokenTypeName(tok.type);
            if (tok.type == TokenType::NUMBER || tok.type == TokenType::STRING ||
                tok.type == TokenType::IDENTIFIER)
                std::cout << "(" << tok.lexeme << ")";
            std::cout << " ";
        }
        std::cout << "\n\n";
    }

    std::vector<StmtPtr> program;
    try {
        Parser parser(std::move(tokens));
        program = parser.parse();
    } catch (const ParseError& e) {
        std::cerr << e.what() << "\n";
        return -1;
    }

    if (opts.showAst) {
        std::cout << "=== AST ===\n" << astToString(program) << "\n";
    }

    FunctionObj* script = nullptr;
    try {
        Compiler compiler(heap);
        script = compiler.compile(program);
    } catch (const CompileError& e) {
        std::cerr << "Compile error: " << e.what() << "\n";
        return -1;
    }

    if (opts.showBytecode) {
        std::cout << "=== BYTECODE ===\n" << disassemble(*script) << "\n";
    }

    InterpretResult result = vm.run(script);
    return result == InterpretResult::OK ? 0 : 1;
}

static int runFile(const std::string& path, const Options& opts) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Could not open file '" << path << "'.\n";
        return 74;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    Heap heap;
    heap.setStress(opts.stressGc);
    heap.setVerbose(opts.gcStats);
    VM vm(heap);
    int status = interpret(buffer.str(), opts, heap, vm);
    if (opts.gcStats) {
        std::cerr << "[gc] total collections: " << heap.collections()
                  << ", live objects: " << heap.objectCount()
                  << ", bytes: " << heap.bytesAllocated() << "\n";
    }
    return status < 0 ? 65 : status;  // 65 == compile error convention
}

static void repl(const Options& opts) {
    std::cout << "CVM++ REPL. Type a statement and press Enter. Ctrl-D to exit.\n";
    std::cout << "Tip: expressions need a trailing ';'. Try: print 1 + 2 * 3;\n\n";
    Heap heap;  // shared so globals/functions persist between lines
    heap.setStress(opts.stressGc);
    heap.setVerbose(opts.gcStats);
    VM vm(heap);
    std::string line;
    while (true) {
        std::cout << "cvm> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        if (line.empty()) continue;
        interpret(line, opts, heap, vm);
    }
}

static void printUsage() {
    std::cout <<
        "CVM++ — a stack-based VM and compiler for a small scripting language.\n\n"
        "Usage:\n"
        "  cvm [options] [script.cvm]\n\n"
        "With no script file, an interactive REPL starts.\n\n"
        "Options:\n"
        "  --tokens      Dump the token stream from the lexer\n"
        "  --ast         Dump the abstract syntax tree from the parser\n"
        "  --bytecode    Dump the compiled bytecode (disassembly)\n"
        "  --debug       Shorthand for --tokens --ast --bytecode\n"
        "  --stress-gc   Run the garbage collector at every safepoint\n"
        "  --gc-stats    Print garbage-collector statistics\n"
        "  -h, --help    Show this help\n";
}

int main(int argc, char** argv) {
    Options opts;
    std::string path;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--tokens") opts.showTokens = true;
        else if (arg == "--ast") opts.showAst = true;
        else if (arg == "--bytecode") opts.showBytecode = true;
        else if (arg == "--debug") { opts.showTokens = opts.showAst = opts.showBytecode = true; }
        else if (arg == "--stress-gc") opts.stressGc = true;
        else if (arg == "--gc-stats") opts.gcStats = true;
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option '" << arg << "'.\n";
            return 64;
        } else {
            path = arg;  // first non-flag arg is the script path
        }
    }

    if (path.empty()) {
        repl(opts);
        return 0;
    }
    return runFile(path, opts);
}
