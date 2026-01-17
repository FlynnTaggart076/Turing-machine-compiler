# Copilot Instructions – Turing Machine Compiler & Simulator

## Project Overview
A C++17 SFML-based Turing Machine (TM) simulator with a custom mini-language compiler. Compiles high-level code (procedures, loops, variables) into TM transition tables and runs them visually.

## Architecture (Compilation Pipeline)
```
Source Code → Lexer → Parser (in Compiler.cpp) → IR → Flatten → TransitionGenerator → TransitionTable
```

### Key Components
| Module | Role |
|--------|------|
| [Lexer.h](include/Lexer.h) | Tokenizes source into `Token` stream |
| [Compiler.cpp](src/Compiler.cpp) | Single-file parser/compiler (~1300 lines), produces `CompileResult` |
| [IR.h](include/IR.h) | Intermediate representation (`IRInstruction`, `Procedure`) |
| [Flatten.h](include/Flatten.h) | Inlines procedure calls (no recursion allowed) |
| [TransitionGenerator.h](include/TransitionGenerator.h) | Converts flat IR to TM transitions |
| [CodegenPrimitives.h](include/CodegenPrimitives.h) | Low-level TM state machine generators for variable operations |
| [TuringMachine.h](include/TuringMachine.h) | Runtime: `Tape` + head position + state |
| [App.h](include/App.h) | SFML UI orchestrator, FSM with modes: `IdleEditing`, `Running`, `Halted`, etc. |

## Memory Invariants (CRITICAL)
See [MEMORY_INVARIANTS.md](MEMORY_INVARIANTS.md) and [MemoryLayout.h](include/MemoryLayout.h).

The tape has a **system zone** at positions `[-10..-1]`:
- `tape[-10]` = `"BOM"` (Begin Of Memory marker)
- `tape[-9..-2]` = 8-bit two's complement variable `x` (MSB at -9, LSB at -2)
- `tape[-1]` = `"EOM"` (End Of Memory marker)
- Position `0+` = user zone

**ABI**: After any variable operation (`x = N`, `x++`, `x--`, `if(x < N)`), head returns to position 0.

## Mini-Language Syntax
```
Set_alphabet "a b c";       // Space-separated symbols (blank=" " is implicit)
Setup "a b blank";          // Initial tape content

proc main() {
    move_left;              // Head movement
    move_right;
    write "a";              // Write symbol
    
    if (read == "a") { }    // Condition on current cell
    while (read != "b") { }
    
    x = 42;                 // Variable assignment [-128..127]
    x++;                    // Increment
    x--;                    // Decrement
    if (x < 10) { }         // Variable comparison
    
    call other_proc;        // Non-recursive procedure call
}
```

## Build Commands (MinGW)
```powershell
# Configure
cmake -G "MinGW Makefiles" -B build-mingw -S .

# Build
cd build-mingw && mingw32-make

# Run (needs SFML DLLs in PATH)
$env:PATH = "...\include\SFML-3.0.2\bin;C:\MinGW\bin;$env:PATH"
.\turing_machine.exe
```

## Conventions & Patterns

### Error Handling
Use `result.diagnostics` vector with `Diagnostic{level, line, column, message}`. Never throw – accumulate errors and set `result.ok = false`.

### State Allocation
In code generation, use `CodegenContext::allocState()` to get fresh `StateId`. Never hardcode state IDs.

### Adding New IR Instructions
1. Add enum in [IR.h](include/IR.h) `IRType`
2. Add factory method in `IRInstruction`
3. Handle in [Flatten.cpp](src/Flatten.cpp) (pass-through)
4. Generate transitions in [TransitionGenerator.cpp](src/TransitionGenerator.cpp)
5. For variable ops: use primitives from [CodegenPrimitives.h](include/CodegenPrimitives.h)

### Types
- `StateId = int` – TM state identifier
- `Symbol = std::string` – tape alphabet symbol (can be multi-char like `"BOM"`)
- `Move` – `Left`, `Right`, `Stay`

## Common Pitfalls
- System symbols `"BOM"`, `"EOM"`, `"0"`, `"1"` must be in alphabet for codegen but hidden from user
- `Tape` uses `unordered_map<long long, Symbol>` – sparse infinite tape
- Movement at `"BOM"` → `Stay` (cannot go further left)
- UI state machine in [finite state machine.txt](finite%20state%20machine.txt) documents all `AppMode` transitions
