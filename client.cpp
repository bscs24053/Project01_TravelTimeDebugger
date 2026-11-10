// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include "helper.h"
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.1";
int LB_PORT = 5000;
const int CLIENT_TIMEOUT_SEC = 30;


// ---- Structs only the client needs ----
struct TdbgReader
{
    ssize_t file_index;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================


int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.

    int s = socket(AF_INET, SOCK_STREAM, 0);

    if(s < 0)
    {
        return -1;
    }

    timeval tv;
    tv.tv_sec = CLIENT_TIMEOUT_SEC;
    tv.tv_usec = 0;

    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if(inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(s);
        return -1;
    }

    if(connect(s, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(s);
        return -1;
    }

    return s;
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.

    FILE *f = fopen(sourcePath, "rb");

    if(f == nullptr)
    {
        return false;
    }

    char status = 0;
    Crc32 crc;

    crc.update(&status, 1);

    if(!sendAll(sock, &status, 1))
    {
        fclose(f);
        return false;
    }

    char buf[IO_BUFFER_SIZE];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        crc.update(buf, n);

        if(!sendAll(sock, buf, n))
        {
            fclose(f);
            return false;
        }
    }

    fclose(f);

    char trailer[4];
    putU32(crc.value(), trailer);

    if(!sendAll(sock, trailer, 4))
    {
        return false;
    }

    shutdown(sock, SHUT_WR);

    return true;
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.

    TrailerChecker checker;
    char buf[IO_BUFFER_SIZE];
    char released[IO_BUFFER_SIZE];
    bool gotStatus = false;
    char status = 0;
    FILE *out = nullptr;
    string errorText;
    bool gotAnything = false;

    while(true)
    {
        ssize_t r = recv(sock, buf, sizeof(buf), 0);

        if(r < 0)
        {
            if(out != nullptr)
            {
                fclose(out);
                remove(outTdbgPath);
            }

            outError = "no reply from the server (timed out)";
            return false;
        }

        if(r == 0)
        {
            break;
        }

        gotAnything = true;

        size_t n = checker.feed(buf, (size_t)r, released);
        size_t start = 0;

        if(!gotStatus && n > 0)
        {
            status = released[0];
            gotStatus = true;
            start = 1;

            if(status == 0)
            {
                out = fopen(outTdbgPath, "wb");

                if(out == nullptr)
                {
                    outError = "cannot write the .tdbg file";
                    return false;
                }
            }
        }

        if(gotStatus && n > start)
        {
            if(status == 0)
            {
                fwrite(released + start, 1, n - start, out);
            }

            else
            {
                errorText.append(released + start, n - start);
            }
        }
    }

    if(out != nullptr)
    {
        fclose(out);
    }

    if(!gotAnything)
    {
        outError = "no reply (the load balancer closed the connection)";
        return false;
    }

    if(!checker.verify())
    {
        if(status == 0)
        {
            remove(outTdbgPath);
        }

        outError = "checksum mismatch, response corrupted";
        return false;
    }

    if(status != 0)
    {
        outError = errorText;
        return false;
    }

    return true;
}

bool requestDebugFile(const char *host, int port, const char *sourcePath, const char *outPath, string &outError)
{
    int sock = connectToHost(host, port);

    if(sock < 0)
    {
        outError = "cannot connect to the load balancer";
        return false;
    }

    if(!uploadSource(sock, sourcePath))
    {
        outError = "cannot read or send the source file";
        close(sock);
        return false;
    }

    bool ok = receiveResponse(sock, outPath, outError);
    close(sock);

    return ok;
}
// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

bool openTdbg(const char *path, TdbgReader &r)
{
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
}
bool moveBy(DebugSession &s, int32_t delta)
{
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s) {}
bool cmdPrv(DebugSession &s) {}
bool cmdJmpNext(DebugSession &s, int32_t n) {}
bool cmdJmpPrv(DebugSession &s, int32_t n) { }

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
}
void printHelp()
{
    // Print the commands
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

    string err;

    if(argc < 2)
    {
        cout << "usage: ./client <source file>" << endl;
        return 1;
    }

    if(requestDebugFile("127.0.0.1", 5000, argv[1], TDBG_FILE, err))
    {
        cout << "saved " << TDBG_FILE << endl;
    }

    else
    {
        cout << "error: " << err << endl;
    }

    
    return 0;
}


