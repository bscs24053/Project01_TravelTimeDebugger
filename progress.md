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



# --------------------------------------------------------------------------------------------------------------------
# Date: 12/10/26
Stage: Stage 4 (Pass 0x3)
Implementation: `writeTdbg` saves the timeline into a file. The file has a header, then all the snapshots, then a list that says where each snapshot starts. I write the header first with empty values and fill it in at the end. I also wrote `readSnapshotAt`, which reads any one snapshot directly using that list. `main()` now runs all the passes one after another and checks the result of each. | Done, tested (test_tdbg.cpp), then added to `server.cpp`
Testing: PDF demo written and read back (6 steps, same values as when it ran), file with wrong magic, step number too big, empty timeline, and error cases run through `./server`

# Status - Done

## Design Decisions
- The spec only says "write the Snapshot structs", so I made my own file format. Text is saved as its length (int32) and then its bytes. Only the frames that are in use (`stackDepth`) are saved, not all 64. The header is 20 bytes: magic, version (1), stepCount, indexOffset. Each index entry is an int64.
- Every value is written one by one, so the file is the same on Windows and Linux.

## Known Issues
- `argNames` in `executeProgram` is a big array on the stack. It may need to move to the heap on Windows.
# --------------------------------------------------------------------------------------------------------------------
# --------------------------------------------------------------------------------------------------------------------
# Date: 12/10/26
Stage: Stage 4 (Pass 0x3)
Implementation: `writeTdbg` saves the timeline into a file. The file has a header, then all the snapshots, then a list that says where each snapshot starts. I write the header first with empty values and fill it in at the end. I also wrote `readSnapshotAt`, which reads any one snapshot directly using that list. `main()` now runs all the passes one after another and checks the result of each. | Done, tested (test_tdbg.cpp), then added to `server.cpp`
Testing: demo written and read back (6 steps, same values as when it ran), file with wrong magic, step number too big, empty timeline, and error cases run through `./server`

# Status - Done

## Design Decisions
- The document only says "write the Snapshot structs", so I made my own file format. Text is saved as its length (int32) and then its bytes. Only the frames that are in use (`stackDepth`) are saved, not all 64. The header is 20 bytes: magic, version (1), stepCount, indexOffset. Each index entry is an int64
- Every value is written one by one, so the file is the same on Windows and Linux

# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 13/10/26
Stage: Testing and documentation
Implementation: Ran the full server on many edge cases and saved them in `tests/edge_cases.sh`. Wrote `README.md`. I have skipped the receive stage (socket) for now, because we were told to assume that `source.bin` already exists
Testing: empty file, Windows line endings (\r\n), blank lines and tabs, unknown instruction, recursion deeper than 64, duplicate function, wrong argument count, 16 params (works), 17 params (error), 17 variables (error), no main, undefined call, nested func, divide by zero, variable not set. All gave the right message and no crash
Setup: Removed the unused socket includes (needed for the Windows build) | Done

# Status - Done

## Design Decisions
- Receive stage is not done in this phase because we assume that we have a source.bin file

## Known Issues
- Windows build is not tested yet
- `fseek`/`ftell` use `long` (32-bit on Windows) - this is only a problem for files that are larger than 2gb.
# --------------------------------------------------------------------------------------------------------------------

Implementation:
- Removed Linux-only headers so the code builds on Windows too.
- Added SEEK64 / TELL64 so file positions work on both systems.
- pop() and peek() on an empty stack now return a safe value.
- Added a check that rejects source files bigger than 15 MB.
Testing:
- Compiled with -Wall -Wextra: no warnings.
- All 15 edge cases in tests/edge_cases.sh pass.
- PDF demo still gives k = 20. A 16 MB file is rejected.
# Status - Done
## Design Decisions
- Used macros for the file position functions because Windows and Linux use different names.
- The empty stack guard is only a safety net. The caller already checks the depth.
## Known Issues
- A line outside any function is not rejected in Pass 0x0.
# -----------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 09/11/26
# PHASE 02
Stage: Phase 02, Step 1 (helper.h)
Implementation: helper.h now holds the shared constants (MAX_VARS_PER_FRAME, MAX_STACK_DEPTH, IO_BUFFER_SIZE, MAX_SOURCE_BYTES), FuncEntry, PendingPatch, and the checksum code. Crc32 calculates CRC-32 piece by piece. TrailerChecker holds back the last 4 bytes of a stream and checks them against the CRC of everything before. sendAll, recvAll and sendStatusMessage are shared by the server, load balancer and client
| Done, tested (tests/test_checksum.cpp)
Testing: CRC of "123456789" is cbf43926, message fed in chunks of 1, 3, 7 and 64 bytes, flipped body byte, flipped trailer byte, stream shorter than 4 bytes => all pass

# Status - Done

## Design Decisions
- Checksum method: CRC-32
- The CRC also covers the status byte, so a wrong status byte is detected
- The CRC is stored as 4 bytes, big endian, so it is the same on every machine
- TrailerChecker always holds the last 4 bytes, because the end of the stream is only known when the connection closes. Because of this, a whole file never has to be loaded in memory

## Known Issues
- For now, im only working on making sure that the project runs on linux. In phase 01, i tried that it runs on both, linus and windows.

# Next Steps
- Move `Variable`, `Frame`, `Snapshot`, `TTDBHeader` into `helper.h`
- Server Receive stage
- Load balancer
- Client
- Testing and README
# --------------------------------------------------------------------------------------------------------------------



# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, Step 2 (shared structs)
Implementation: Moved Variable, Frame, Snapshot, TTDBHeader and the shared constants from server.cpp into helper.h
Testing: server.cpp compiles with -Wall -Wextra with no warnings. All edge cases in tests/edge_cases.sh still pass. PDF demo still gives k = 20.

# Status - Done

## Design Decisions
- Anything used by more than one of server, load balancer and client lives in helper.h => This makes sure the client reads the .tdbg file with exactly the same structs that the server wrote it with

## Next Steps
- Server Receive stage: turn Phase 01 main() into processSource(), then read source.bin from the socket
# --------------------------------------------------------------------------------------------------------------------
# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, Step 3 (processSource)
Implementation: The passes that were in main()=>(validate, resolve, execute, writeTdbg) are now in bool processSource(string &err). It returns true when session.tdbg was written. On failure it returns false and puts the error text in err
main() calls it and prints the error for now. 
Testing: all edge cases in tests/edge_cases.sh give the same results as before, pdf demo still writes session.tdbg (368 bytes), exit 0

# Status - Done

## Design Decisions
- The errors are returned as strings instead of being printed in each pass because the server must send them to the client over the socket

## Next Steps
- Server Receive stage: listen on a port, receive the upload, check the checksum, call processSource, send the reply
# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02 - server Receive stage
Implementation: server now takes its port as the first argument (default 5001), listens, and serves one client at a time,handleClient reads the upload through TrailerChecker, writes the source to source.bin, checks the checksum at the end, runs processSource, and sends the reply, sendTdbgReply sends [0][session.tdbg][crc32] in chunks, so the file is never fully loaded in memory
Errors go back as [1][message][crc32] through sendStatusMessage
Testing: pdf demo (status 0, 368 byte .tdbg), invalid program (status 1 with message), upload with a wrong checksum (status 1, "upload checksum mismatch"), then a good request again to show the server keeps running

# Status - Done

## Design Decisions
- The server serves one client at a time, because the load balancer sends one client at a time
- The first byte of the upload is the status byte (0). The last 4 bytes are the CRC-32
- The source size limit (15 MB) is checked while receiving. If the upload is too big, the extra bytes are read but not written and the server replies "source file is too large"
- Old source.bin and session.tdbg are deleted at the start of each request so that an old file is never sent by mistake
- SIGPIPE is ignored so a client that disconnects early does not kill the server

# Next Steps
- Load balancer (queue, relay, wake up the server)
- Client
- Testing and README
# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, load balancer - part 1
Implementation: Queue<T>, sendError, createListenSocket, connectToServer, startServer(runs ./server <port> & with system()), connectToServerWithWakeup(tries to connect, starts the server if it is down, then retries 50 times with 100 ms between tries) 
Queue tested (tests/test_queue.cpp)

# Status - Done

## Design Decisions
- Sockets are created with SOCK_CLOEXEC, so the server started by system() does not inherit the load balancer's listening socket
- & at the end of the command is needed, otherwise system() waits until the server exits and the load balancer freezes
- Server host default is 127.0.0.1 
- Removed the duplicate IO_BUFFER_SIZE and sendAll from load_balancer.cpp, they are in helper.h

## Known Issues
- system() returns 0 even if ./server does not exist=>it is because the connect retries fail and the client gets "backend server failed to start".

# Next Steps
- Relay for one client (relayOneClient) with checksum checking
- acceptLoop, dispatcherLoop, main
- Client
- Testing and README
# --------------------------------------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02 load balancer - relay
Implementation: relayOneClient serves one client. It connects to the server (starting it if needed), then copies the client's bytes to the server through a `TrailerChecker`. When the upload is finished it checks the checksum, and only a good upload gets its trailer forwarded. Then it half-closes the server socket and copies the server's reply back to the client, again through a `TrailerChecker`. The reply's trailer is sent only if the reply checksum is correct
Testing: `-fsyntax-only` with -Wall -Wextra shows no warning for `relayOneClient`.

# Status - Done

## Design Decisions
- The load balancer checks every stream while forwarding. If a check fails, it does not send the trailer, so the next receiver rejects the message. For a bad upload, the client gets the error "upload checksum mismatch" from the load balancer, and the server's reply is ignored.
- Only 64 KB chunks are in memory at a time, the whole file is never loaded
- `relayOneClient` closes only the server socket. The client socket is closed by `dispatcherLoop`, so each socket has exactly one owner
- If the server connection breaks during the upload, the client gets "backend server connection lost".

## Known Issues
- For now, the relay has no timeout while it waits for the client's upload. A client that connects and sends nothing will block the queue. I will handle this thing in the next step

# Next Steps
- `acceptLoop`, `dispatcherLoop`, `main`, handle the queue blockage problem
- Client
- Testing and README
# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, load balancer - threads and main
Implementation: `acceptLoop` takes new clients and puts them in the queue. If 8 clients are already waiting, it sends "server busy, try again later" and closes that client. `dispatcherLoop` takes one client at a time, calls `relayOneClient`, then closes the client. A client that waited more than 30 seconds is closed with no reply in this way i have fixed the queue blockage problem. `main` reads the server ip and port, sets up Ctrl+C, starts the accept thread and runs the dispatcher. 
Testing: server down (load balancer starts it), server already running, invalid program, wrong checksum, 5 clients at the same time, Ctrl+C stops the load balancer

# Status - Done

## Design Decisions
- Ctrl+C sets `flag` to 0 using `signal(SIGINT, handler)`. The dispatcher checks `flag`, so the program ends
- The queue lock is held only while using the queue. Errors are sent after the lock is released
- Each socket is closed in one place only: `acceptLoop` closes a client it refuses, the dispatcher closes every other client
- Clients are accepted with `accept4(..., SOCK_CLOEXEC)`, so the server started by `system()` does not keep the client's socket

# Next Steps
- Client
- Testing and README
# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, client - talking to the load balancer
Implementation: `connectToHost` (waits at most 30 seconds for a reply), `uploadSource` (sends [0][source][crc32], then `shutdown`), `receiveResponse` (checks the checksum, saves the .tdbg file or collects the error text), `requestDebugFile` (connect, upload, receive). The structs and `sendAll`/`recvAll` now come from `helper.h`. I did the testing with a temporary main for now
Testing: demo program (file saved, 368 bytes), invalid program (error shown), missing source file, load balancer not running

# Status - Done

## Design Decisions
- The client reads the reply through `TrailerChecker`, so the file is written in chunks. If the checksum is wrong, the file is deleted and the error is "checksum mismatch, response corrupted".
- If the load balancer closes the connection with no reply,for example, the client waited too long, the client says "no reply".

## Next Steps
- Read the .tdbg file-part B
- Commands, printing, main loop
- Testing and README
# --------------------------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------------------------
# Date: 10/11/26
Stage: Phase 02, client - opening the .tdbg file
Implementation: `openTdbg` reads the 20 byte header (magic, version, stepCount, indexOffset), checks it, then reads the index (one 8 byte offset for each snapshot) into a vector. `closeTdbg` closes the file and clears the index. `TdbgReader` now keeps a `FILE *`
Testing: the demo file (6 steps, indexOffset 320), a file with the wrong magic, a file that is cut short, a file that does not exist

# Status - Done

## Design Decisions
- `openTdbg` checks the magic "TTDB", the version, that stepCount is at least 1, that the index fits inside the file, and that every offset is inside the snapshot area. A damaged file returns false, so the client can say "server sent a damaged .tdbg file" and keep the old session
- The file stays open for the whole session, and only the index is kept in memory.

## Next Steps
- `readSnapshotAt`
- Session, commands, printing, main loop
- Testing and README
# --------------------------------------------------------------------------------------------------------------------

















## Issues Faced and Fixes
- 2026-10-07: Compiled binary `test_stack` was committed by mistake. Fixed with `git rm --cached` and a `.gitignore` rule
- 2026-10-07: `undefined reference to main` while compiling the test: the file had not been saved in VS Code. Fixed by saving and re-running.
- 2026-10-07: `TimelineNode` was accidentally deleted while replacing the Timeline class in `server.cpp`. Caught with `g++ -fsyntax-only` and restored.
