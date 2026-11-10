# Time-Travel Debugger (TTDB) - Phase 01 (Server)

The server reads a program written in our small language (C--), runs it step by step, and saves every step in a `session.tdbg` file. A client can later use this file to move backward and forward through the program's history.

## Build and run

Linux (g++ 13.3, Ubuntu 24.04):

```bash
g++ -std=c++17 -Wall -Wextra server.cpp -o server
./server
```

The server reads `source.bin` from the current folder (Phase 01 assumes the file is already there) and writes `session.tdbg`. If something is wrong it prints an error message and exits with code 1.

## The language (C--)

Every line is: `keyword identifier params...` (at most 16 params).

| Instruction | Meaning |
|---|---|
| `func name p1 p2 ...` | start a function |
| `func_end` | end a function |
| `call name a1 a2 ...` | call a function. Arguments are passed by reference |
| `set x v` | set variable x to v |
| `add / sub / mul / div x v` | x = x op v (result is saved in x) |

`v` can be a number or a variable name. Example:

```
func foo b
set a 10
add b a
func_end
func main
set k 10
call foo k
func_end
```

After `call foo k`, `k` in `main` becomes 20.

## How it works (passes)

1. **Pass 0x0 - validate:** every `func` has one `func_end`, no nested functions
2. **Pass 0x1 - resolve:** every line is written to `resolve.bin` as `[offset 8B][size 4B][text]`. The offset field of a `call` record is patched with the position of the function it calls. Returns the position of `main`.
3. **Pass 0x2 - execute:** reads `resolve.bin` one line at a time, tokenizes it, updates the call stack and saves a snapshot after every executed line.
4. **Pass 0x3 - serialize:** writes the timeline to `session.tdbg`.

my own data structures that i have implemented: `Stack<T>` (linked list, call stack) and `Timeline` (doubly linked list of snapshots). Only `std::string` and the standard file functions are used.

## session.tdbg format

All numbers are little endian, written one field at a time.

```
Header (20 bytes): "TTDB" | version int32 (=1) | stepCount int32 | indexOffset int64
Snapshot stream: for each step -> stackDepth int32, then for each frame: func_name, argc, argc x (name, value), returnLine, localCount, localCount x (name, value) (a string = length int32 + bytes)
Index (at indexOffset): stepCount x int64, the start position of each snapshot
```

`callStack[0]` in a snapshot is the function running now, the last frame is `main`.

## Design decisions

- Snapshot is taken after every executed line (`set/add/sub/mul/div`, `call`, `func_end`), not for the `func main` header
- `returnLine` is the byte position in `resolve.bin` of the line after the `call`
- Call by reference: when a function ends, the final values of its params are copied back to the caller's variables (if the argument was a variable, not a number).
- A variable must be set before it is used with `add/sub/mul/div` or passed to a call (otherwise error).
- Duplicate function names are rejected.
- The `.tdbg` snapshot format is my own design. Only `stackDepth` frames are saved.

## Errors

| Message | Cause |
|---|---|
| invalid program | file missing, nested `func`, or unmatched `func`/`func_end` |
| no main function found | no `func main` |
| call to undefined function | `call` to a function that does not exist |
| resolve failed | file error, too many functions/calls, or duplicate function |
| bad line or unknown instruction | wrong number of values or unknown keyword (also more than 16 params) |
| variable used before it was set | |
| divide by zero | |
| more than 16 variables in one function | |
| call has a different number of arguments | `call` and `func` params do not match |
| call stack too deep | more than 64 nested calls |

## Limits

16 params/variables per function, 64 call depth, 128 functions.
Source file: at most 15 MB.

## Tests

- `test_*.cpp` files test each part on its own (stack, timeline, validation, resolve, tokenizer, snapshot, execute, serialization).
- `tests/edge_cases.sh` runs the full server on many edge cases (run `./server` build first).


## Phase 02: Load balancer and client

Linux only. Build:

    g++ -std=c++17 -Wall -Wextra server.cpp -o server
    g++ -std=c++17 -Wall -Wextra -pthread load_balancer.cpp -o load_balancer
    g++ -std=c++17 -Wall -Wextra client.cpp -o client

Run (the load balancer starts the server by itself if it is down):

    ./load_balancer 127.0.0.1 5001     # listens on port 5000, server on 5001
    ./client 127.0.0.1 5000

Client commands: `start <file>`, `next`, `prv`, `jmp next <n>`, `jmp prv <n>`, `help`, `quit`.

Message format on every hop: `[status 1 byte][body][CRC-32, 4 bytes]`. Status 0 = body is the file, status 1 = body is an error message.

### Error cases

| Case | What happens |
|---|---|
| Queue full (8 clients waiting) | Client gets "server busy, try again later" and is closed |
| Server cannot start or connect | Client gets "backend server failed to start" after about 5 seconds |
| Client waited too long in the queue (30 s) | Socket is closed, no reply |
| Upload checksum mismatch | Client gets "upload checksum mismatch" |
| Reply checksum mismatch | Client deletes the file and prints "checksum mismatch, response corrupted" |
| Bad .tdbg file | Client prints "server sent a damaged .tdbg file" and keeps the old session |