# Time-Travel Debugger: Phase 01 Progress

**Platform:** Linux (g++ 13.3, Ubuntu 24.04)
**Build:** `g++ -std=c++17 -Wall -Wextra server.cpp -o server`

## Progress Log

# --------------------------------------------------------------------------------------------------------------------
# Date: 7/10/26
Stage: Stage 2 (Pass 0x2), prerequisite
Implementation: `Stack<T>` : linked list stack with push (rejects at MAX_STACK_DEPTH = 64), pop, peek, isEmpty, depth, snapshot_into (top to bottom copy), destructor frees all nodes | Done, tested (test_stack.cpp) and then integrated into `server.cpp`
`-fsyntax-only` check passes with no Stack errors 
Setup: Removed the compiled `test_stack` binary from git and updated `.gitignore` | Done 

# Status - Done

## Design Decisions
- `Stack<T>` is a singly linked list; `count` tracks depth.
- `push` silently ignores pushes beyond MAX_STACK_DEPTH.
- `pop()`/`peek()` on an empty stack are undefined; the caller must check `isEmpty()` first.

## Known Issues / Next Steps
- Next: Timeline (doubly linked list).
# --------------------------------------------------------------------------------------------------------------------



# --------------------------------------------------------------------------------------------------------------------
# Date: 7/10/26
Stage: Stage 3 (Pass 0x2), prerequisite 
Implementation: `Timeline`: doubly linked list with record (append at tail), begin, getStepCount, destructor frees all snapshots and nodes | Done, tested (test_timeline.cpp) |

# Status - Done

## Design Decisions
- `Timeline` owns the `Snapshot` pointers it records and deletes them in its destructor. The destructor body is defined after `Snapshot`, because `Snapshot` is only forward declared at the point where the class is written

# Next Steps
- Pass 0x0: `readSourceLine`, `firstWord`, `secondWord`, `validateProgram`
- Pass 0x1: resolve.bin writer/reader and CALL patching
- Tokenizer and Pass 0x2 execution
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test

# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/10/26
Stage: Stage 1 (Pass 0x0)
Implementation: `readSourceLine` (skips blank lines, strips `\r`), `firstWord`, `secondWord`, `validateProgram` (rejects nested `func`, unmatched `func_end`, missing `func_end`) | Done, tested (tests/test_validate.cpp) and then integrated into `server.cpp`
Setup: Moved test files into `tests/` folder | Done

# Status - Done

## Design Decisions
- Validation uses a single `isinsideFunc` flag, no stack is needed because nested functions are not allowed

## Known Issues
- Pass 0x0 only validates `func`/`func_end` structure. Instructions outside a function are not rejected. They are never executed because execution starts at `main`.

# Next Steps
- Pass 0x1: resolve.bin writer/reader and CALL patching
- Tokenizer and Pass 0x2 execution
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------




# --------------------------------------------------------------------------------------------------------------------
# Date: 10/10/26
Stage: Stage 2 (Pass 0x1)
Implementation: `writeResolveRecord` (writes [offset 8B][size 4B][text], returns record start position), `readResolveRecord`(reads one record, returns offset field, -1 on EOF/corrupt record) | In progress, tested (test_resolve.cpp), not yet integrated into `server.cpp`

# Status - In progress

## Design Decisions
- Every record's offset field is written as 0 only `call` records get patched later. Line text is never modified, so record sizes never change.
- Record's own position is obtained with `ftell` and returned by `writeResolveRecord`.
- `resolveProgram` error codes: -1 = no main, -2 = call to undefined function, -3 = file error or limit exceeded (MAX_FUNCS / MAX_PATCHES)

# Next Steps
- `resolveProgram`: write all lines, record FuncEntry / PendingPatch, patch CALL offsets
- Tokenizer and Pass 0x2 execution
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------------------------------------
# Date: 10/10/26
Stage: Stage 2 (Pass 0x1)
Implementation: `resolveProgram` (writes every line to resolve.bin, saves function names and call positions, then fixes the call offsets, returns main's offset) | Done, tested (test_resolveprogram.cpp)
Integration: `writeResolveRecord`, `readResolveRecord` and `resolveProgram` are now in `server.cpp` | Done
Setup: `main()` now checks what resolve returns and prints an error message | Done

# Status - Done

## Design Decisions
- If two functions have the same name, we stop with an error, because a call would not know which one to use.
- `validateProgram` only returns true or false. `main()` prints the error message.

## Known Issues
- `fseek`/`ftell` use `long`, which is 32-bit on Windows. We need a 64-bit version when we port to Windows.

# Next Steps
- Tokenizer and Pass 0x2 execution
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------------------------------------
# Date: 10/10/26
Stage: Stage 3 (Pass 0x2), prerequisite
Implementation: `tokenizeLine` splits a line into words. The first word is the instruction (KEYWORD), the second is the name (IDENTIFIER), and the rest are values (PARAM). It ignores extra spaces and tabs. `parseInt` changes text like "10" into a number, without using `stoi`. | Done, tested (test_tokenizer.cpp), then added to `server.cpp`
Testing: normal lines, extra spaces and tabs, 16 params (works), 17 params (gives -1), and numbers like 10, -5, abc, empty text, "-", 99999999999, 12x

# Status - Done

## Design Decisions
- `tokenizeLine` returns -1 if there are too many tokens (more than 16 params). Execution will report an error instead of crashing
- `parseInt` returns false for bad or too big numbers, so `set a abc` will not crash.

# Next Steps
- Pass 0x2 execution (`buildSnapshot`, `executeProgram`)
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------------------------------------
# Date: 11/10/26
Stage: Stage 3 (Pass 0x2), prerequisite
Implementation: `buildSnapshot` makes a new Snapshot on the heap and copies the whole call stack into it | Done, tested (test_snapshot.cpp), then added to `server.cpp`
Testing: 2 frames (order, locals, args, returnLine), snapshot stays the same when the stack changes later, empty stack gives depth 0

# Status - Done

## Design Decisions
- Snapshot order: `callStack[0]` is the function running now, the last entry is `main`.
- A variable that was never set gives an error (it is not treated as 0).

# Next Steps
- `executeProgram` (set/add/sub/mul/div in main, then call and func_end)
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------
# --------------------------------------------------------------------------------------------------------------------
# Date: 11/10/26
Stage: Stage 3 (Pass 0x2)
Implementation: `findVar` this looks for a variable in the current function, `getValue` a number or a variable's value, `executeProgram` part 1 runs `set`, `add`, `sub`, `mul`, `div` in `main` and takes a snapshot after each line| In progress, tested (test_execute.cpp), not yet in `server.cpp`
Testing: 6 normal lines give 7 snapshot with correct values
error cases: divide by zero, variable not set, bad line, unknown instruction

# Status - In progress

## Design Decisions
- Snapshot is taken after every executed line (set/add/sub/mul/div, call, func_end). Not taken for the `func main` header line
- `set`, `add`, `sub`, `mul`, `div` accept a number or a variable name as the value. The number is tried first.
- `executeProgram` returns int32_t: 0 = ok, -1 = file problem, -2 = bad line, -3 = variable not found, -4 = divide by zero, -5 = more than 16 variables.

# Next Steps
- `call` and `func_end` for other functions (pass by reference)
- Add `executeProgram` to `server.cpp` and check its result in `main()`
- Pass 0x3 serialization
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------------------------------------
# Date: 12/10/26
Stage: Stage 3 (Pass 0x2)
Implementation: `executeProgram` now also handles `call` and `func_end`. `call` makes a new frame and copies the argument values into it. `func_end` removes the frame, copies the final values back to the caller's variables (pass by reference), and jumps back to the line after the `call`. | Done, tested (test_execute.cpp), then added to `server.cpp`
Testing: PDF demo (k becomes 20) is tested, number argument, nested calls (k becomes 11), recursion too deep, wrong argument count, variable not set
Setup: `main()` now checks the result of `executeProgram` and prints an error | Done

# Status - Done

## Design Decisions
- `returnLine` is the byte position in resolve.bin of the line after the `call`
- If an argument is a number like call foo 5, nothing is copied back, because there is no variable
- Error codes: -6 = wrong number of arguments, -7 = call stack deeper than 64

## Known Issues
- The error messages are printed on screen. They will be sent to the client in the Receive stage

# Next Steps
- Pass 0x3 serialization (`writeTdbg`)
- Receive stage (socket) and error responses
- Windows build and test
# --------------------------------------------------------------------------------------------------------------------

















## Issues Faced and Fixes
- 2026-10-07: Compiled binary `test_stack` was committed by mistake. Fixed with `git rm --cached` and a `.gitignore` rule
- 2026-10-07: `undefined reference to main` while compiling the test: the file had not been saved in VS Code. Fixed by saving and re-running.
- 2026-10-07: `TimelineNode` was accidentally deleted while replacing the Timeline class in `server.cpp`. Caught with `g++ -fsyntax-only` and restored.
