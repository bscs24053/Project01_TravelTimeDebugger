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
const char *TDBG_TEMP = "ttdb_new.tdbg";


// ---- Structs only the client needs ----
struct TdbgReader
{
    FILE *fp;
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
    r.fp = fopen(path, "rb");

    if(r.fp == nullptr)
    {
        return false;
    }

    bool ok = true;

    if(fread(r.header.magic, 1, 4, r.fp) != 4)
    {
        ok = false;
    }

    else if(fread(&r.header.version, sizeof(int32_t), 1, r.fp) != 1)
    {
        ok = false;
    }

    else if(fread(&r.header.stepCount, sizeof(int32_t), 1, r.fp) != 1)
    {
        ok = false;
    }

    else if(fread(&r.header.indexOffset, sizeof(int64_t), 1, r.fp) != 1)
    {
        ok = false;
    }

    if(ok)
    {
        if(r.header.magic[0] != 'T' || r.header.magic[1] != 'T' || r.header.magic[2] != 'D' || r.header.magic[3] != 'B' || r.header.version != 1 || r.header.stepCount < 1)
        {
            ok = false;
        }
    }

    if(ok)
    {
        fseeko(r.fp, 0, SEEK_END);
        int64_t fileSize = (int64_t)ftello(r.fp);
        int64_t indexBytes = (int64_t)r.header.stepCount * 8;

        if (r.header.indexOffset < 20 || r.header.indexOffset + indexBytes > fileSize)
        {
            ok = false;
        }
    }

    if(ok)
    {
        fseeko(r.fp, (off_t)r.header.indexOffset, SEEK_SET);
        r.index.clear();

        for(int32_t i = 0; i < r.header.stepCount; i++)
        {
            int64_t off;

            if(fread(&off, sizeof(int64_t), 1, r.fp) != 1 || off < 20 || off >= r.header.indexOffset)
            {
                ok = false;
                break;
            }

            r.index.push_back(off);
        }
    }

    if(!ok)
    {
        fclose(r.fp);
        r.fp = nullptr;
        r.index.clear();
        return false;
    }
    return true;
}

void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    if(r.fp != nullptr)
    {
        fclose(r.fp);
        r.fp = nullptr;
    }

    r.index.clear();
}

bool readInt32(FILE *f, int32_t &v)
{
    return fread(&v, sizeof(int32_t), 1, f) == 1;
}

bool readString(FILE *f, string &s)
{
    int32_t len;

    if(!readInt32(f, len))
    {
        return false;
    }

    if (len < 0 || len > 1024 * 1024)
    {
        return false;
    }

    s = "";

    for (int32_t i = 0; i < len; i++)
    {
        int c = fgetc(f);

        if(c == EOF)
        {
            return false;
        }

        s += (char)c;
    }
    return true;
}

bool readVariable(FILE *f, Variable &v)
{
    return readString(f, v.name) && readInt32(f, v.value);
}

bool readFrame(FILE *f, Frame &fr)
{
    if(!readString(f, fr.func_name))
    {
        return false;
    }

    if(!readInt32(f, fr.argc) || fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME)
    {
        return false;
    }

    for(int32_t i = 0; i < fr.argc; i++)
    {
        if(!readVariable(f, fr.argv[i]))
        {
            return false;
        }
    }

    if(!readInt32(f, fr.returnLine))
    {
        return false;
    }

    if(!readInt32(f, fr.localCount) || fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
    {
        return false;
    }

    for(int32_t i = 0; i < fr.localCount; i++)
    {
        if(!readVariable(f, fr.locals[i]))
        {
            return false;
        }
    }

    return true;
}

Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    if (r.fp == nullptr || step < 0 || step >= r.header.stepCount)
    {
        return nullptr;
    }

    fseeko(r.fp, (off_t)r.index[step], SEEK_SET);

    Snapshot *s = new Snapshot;

    if(!readInt32(r.fp, s->stackDepth) || s->stackDepth < 0 || s->stackDepth > MAX_STACK_DEPTH)
    {
        delete s;
        return nullptr;
    }

    for(int32_t i = 0; i < s->stackDepth; i++)
    {
        if(!readFrame(r.fp, s->callStack[i]))
        {
            delete s;
            return nullptr;
        }
    }

    return s;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
    s.active_flag = 0;
    s.reader.fp = nullptr;
    s.currentStep = 0; 
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if(s.active_flag)
    {
        closeTdbg(s.reader);
    }

    s.active_flag = 0;
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot *snap = readSnapshotAt(s.reader, (int32_t)s.currentStep);

    if(snap == nullptr)
    {
        cout << "damaged .tdbg file" << endl;
        return false;
    }

    printSnapshot((int32_t)s.currentStep + 1, s.reader.header.stepCount, *snap);
    delete snap;

    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if(!s.active_flag)
    {
        cout << "no session yet, use: start <source file>" << endl;
        return false;
    }

    int64_t target = s.currentStep + delta;
    int32_t total = s.reader.header.stepCount;

    if(target < 0 || target > total - 1)
    {
        cout << "cannot move there: steps go from 1 to " << total << " and you are at step " << s.currentStep + 1 << endl;
        return false;
    }

    s.currentStep = target;

    return showCurrent(s);
}

bool copyText(const string &from, char *to, int size)
{
    if((int)from.size() >= size)
    {
        return false;
    }

    for(size_t i = 0; i < from.size(); i++)
    {
        to[i] = from[i];
    }

    to[from.size()] = '\0';

    return true;
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

    char host[256];
    char path[1024];

    if(!copyText(LB_HOST, host, 256) || !copyText(sourcePath, path, 1024))
    {
        cout << "error: host or file name is too long" << endl;
        return false;
    }

    string err;

    if(!requestDebugFile(host, LB_PORT, path, TDBG_TEMP, err))
    {
        cout << "error: " << err << endl;
        return false;
    }

    TdbgReader fresh;
    fresh.fp = nullptr;

    if(!openTdbg(TDBG_TEMP, fresh))
    {
        cout << "server sent a damaged .tdbg file" << endl;
        remove(TDBG_TEMP);
        return false;
    }

    if(s.active_flag)
    {
        closeTdbg(s.reader);
    }

    rename(TDBG_TEMP, TDBG_FILE);
    s.reader = fresh;
    s.currentStep = 0;
    s.active_flag = 1;

    return showCurrent(s);
}
bool cmdNext(DebugSession &s)
{
    return moveBy(s, 1);
}
bool cmdPrv(DebugSession &s)
{
    return moveBy(s, -1);
}
bool cmdJmpNext(DebugSession &s, int32_t n)
{
    return moveBy(s, n);
}
bool cmdJmpPrv(DebugSession &s, int32_t n)
{
    return moveBy(s, -n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout << "Step " << step << "/" << totalSteps << endl;
    cout << "Call stack (current function first):" << endl;

    for(int32_t d = 0; d < s.stackDepth; d++)
    {
        const Frame &fr = s.callStack[d];
        cout << "  " << fr.func_name << "(";

        bool first = true;

        for(int32_t i = 0; i < fr.argc; i++)
        {
            if (!first)
            {
                cout << ", ";
            }

            cout << fr.argv[i].name << "=" << fr.argv[i].value;
            first = false;
        }
        cout << ")";

        if(fr.localCount > 0)
        {
            cout << "  locals: ";

            for(int32_t i = 0; i < fr.localCount; i++)
            {
                if(i > 0)
                {
                    cout << ", ";
                }
                
                cout << fr.locals[i].name << "=" << fr.locals[i].value;
            }
        }
        cout << endl;
    }
}
void printHelp()
{
    // Print the commands
    cout << "Commands:" << endl;
    cout << "  start <source file> - send the program to the server and show step 1" << endl;
    cout << "  next - go to the next step" << endl;
    cout << "  prv - go to the previous step" << endl;
    cout << "  jmp next <n> - go n steps forward" << endl;
    cout << "  jmp prv <n> - go n steps backward" << endl;
    cout << "  help - show this help" << endl;
    cout << "  quit (or exit) - leave the program" << endl;
}


// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================
vector<string> splitWords(const string &line)
{
    vector<string> words;
    string word = "";

    for(size_t i = 0; i < line.size(); i++)
    {
        char c = line[i];

        if(c == ' ' || c == '\t' || c == '\r')
        {
            if(word != "")
            {
                words.push_back(word);
                word = "";
            }
        }

        else
        {
            word += c;
        }
    }

    if(word != "")
    {
        words.push_back(word);
    }

    return words;
}

bool parsePositive(const string &text, int32_t &value)
{
    if(text == "" || text.size() > 10)
    {
        return false;
    }

    int64_t number = 0;

    for(size_t i = 0; i < text.size(); i++)
    {
        if(text[i] < '0' || text[i] > '9')
        {
            return false;
        }

        number = number * 10 + (text[i] - '0');
    }

    if(number < 1 || number > MAX_JUMP)
    {
        return false;
    }

    value = (int32_t)number;

    return true;
}
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

    vector<string> words = splitWords(line);

    if(words.size() == 0)
    {
        return true;
    }

    if(words[0] == PROGRAM_NAME)
    {
        words.erase(words.begin());

        if(words.size() == 0)
        {
            printHelp();
            return false;
        }
    }

    string cmd = words[0];

    if(cmd == "start" && words.size() == 2)
    {
        return cmdStart(s, words[1]);
    }

    if(cmd == "next" && words.size() == 1)
    {
        return cmdNext(s);
    }

    if(cmd == "prv" && words.size() == 1)
    {
        return cmdPrv(s);
    }

    if(cmd == "jmp" && words.size() == 3 && (words[1] == "next" || words[1] == "prv"))
    {
        int32_t n;
        if(!parsePositive(words[2], n))
        {
            cout << "n must be a whole number from 1 to " << MAX_JUMP << endl;
            return false;
        }

        if(words[1] == "next")
        {
            return cmdJmpNext(s, n);
        }

        return cmdJmpPrv(s, n);
    }

    if(cmd == "help" && words.size() == 1)
    {
        printHelp();
        return true;
    }

    if((cmd == "quit" || cmd == "exit") && words.size() == 1)
    {
        quitRequested = true;
        return true;
    }

    cout << "unknown command" << endl;
    printHelp();

    return false;
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

    DebugSession session;
    initSession(session);

    cout << "next without session" << endl;
    cmdNext(session);
    cout << "tart with an invalid program" << endl;
    cmdStart(session, "demo_bad.txt");
    cout << "start with the demo" << endl;
    cmdStart(session, "demo_ok.txt");
    cout << "next, next, prv" << endl;
    cmdNext(session);
    cmdNext(session);
    cmdPrv(session);
    cout << "jmp next 3, jmp prv 2" << endl;
    cmdJmpNext(session, 3);
    cmdJmpPrv(session, 2);
    cout << "jmp next 10 (too far)" << endl;
    cmdJmpNext(session, 10);
    cout << "go back to step 1, then prv once more" << endl;
    cmdJmpPrv(session, 2);
    cmdPrv(session);
    cout << "failed start must keep the old session, then next" << endl;
    cmdStart(session, "demo_bad.txt");
    cmdNext(session);

    closeSession(session);
   
    return 0;
}


