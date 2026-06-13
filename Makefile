# CVM++ build
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
# -MMD -MP emit .d files so editing a header recompiles dependent sources.
DEPFLAGS := -MMD -MP
SRCDIR   := src
BUILDDIR := build
BIN      := cvm

SOURCES  := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS  := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Run a script: `make run FILE=examples/maps.cvm`. Defaults to the REPL.
run: $(BIN)
	./$(BIN) $(FILE)

# Run every example script end to end.
test: $(BIN)
	@for f in examples/*.cvm; do \
		echo "=================================================="; \
		echo "RUN $$f"; \
		echo "=================================================="; \
		./$(BIN) $$f; \
		echo ""; \
	done

clean:
	rm -rf $(BUILDDIR) $(BIN)

# Pull in the generated header-dependency rules (ignored if absent).
-include $(DEPS)
