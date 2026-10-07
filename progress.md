# Time-Travel Debugger: Phase 01 Progress

**Platform:** Linux (g++ 13.3, Ubuntu 24.04)
**Build:** `g++ -std=c++17 -Wall -Wextra server.cpp -o server`

## Progress Log

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




## Issues Faced and Fixes
- 2026-10-07: Compiled binary `test_stack` was committed by mistake. Fixed with `git rm --cached` and a `.gitignore` rule
- 2026-10-07: `undefined reference to main` while compiling the test: the file had not been saved in VS Code. Fixed by saving and re-running.