// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstdio>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    Stack()
    {
        top = nullptr;
        count = 0;
    }
    ~Stack()
    {
        while(!(isEmpty()))
        {
            pop();
        }
    }
    void push(const T &val)
    {
        if(count == MAX_STACK_DEPTH)
        {
            return;
        }
        else
        {
            Node *newNode = new Node;
            newNode->data = val;
            newNode->next = top;
            top = newNode;
            count++;
        }
    }
    T pop()
    {
        Node *temp = top;
        top = top->next;
        T data = temp->data;
        delete temp;
        count--;
        return data;
    }
    T &peek()
    {
       return top->data;
    }
    bool isEmpty()
    {
        if(count == 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        Node *temp = top;
        int32_t copied = 0;
        while(temp!=nullptr && copied < maxLen)
        {
           out[copied] = temp->data;
           temp = temp->next;
           copied++;
        }

        return copied;
    }
};

// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
    ~Timeline();    // body Snapshot ke baad likhi hai
    void record(Snapshot *s)
    {
        TimelineNode *newNode = new TimelineNode;
        newNode->data = s;
        newNode->next = nullptr;
        newNode->prev = tail;

        if (head == nullptr)
        {
            head = newNode;
        }
        else
        {
            tail->next = newNode;
        }
        tail = newNode;
        stepCount++;
    }
    TimelineNode *begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

Timeline::~Timeline()
{
    TimelineNode *temp = head;
    while (temp != nullptr)
    {
        TimelineNode *nextNode = temp->next;
        delete temp->data;
        delete temp;
        temp = nextNode;
    }
}

struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};


int skipSpaces(const string &line, int i)
{
    while (i < (int)line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
        i++;
    }
    return i;
}

bool readSourceLine(ifstream &in, string &out)
{
    while (getline(in, out))
    {
        if (!out.empty() && out.back() == '\r')
        {
            out.pop_back();
        }

        bool islineblank = true;
        for (int i = 0; i < (int)out.size(); i++)
        {
            if (out[i] != ' ' && out[i] != '\t')
            {
                islineblank = false;
                break;
            }
        }

        if (!islineblank)
        {
            return true;
        }
    }
    return false;
}

string firstWord(const string &line)
{
    int i = skipSpaces(line, 0);
    string result = "";

    while (i < (int)line.size() && line[i] != ' ' && line[i] != '\t')
    {
        result = result + line[i];
        i++;
    }

    return result;
}

string secondWord(const string &line)
{
    int i = skipSpaces(line, 0);

    while (i < (int)line.size() && line[i] != ' ' && line[i] != '\t')
    {
        i++;
    }

    i = skipSpaces(line, i);

    string result = "";

    while (i < (int)line.size() && line[i] != ' ' && line[i] != '\t')
    {
        result = result + line[i];
        i++;
    }
    return result;
}

bool validateProgram(const char *sourcePath)
{
    ifstream in(sourcePath);

    if (!in.is_open())
    {
        return false;
    }

    bool isinsideFunc = false;
    string line;

    while (readSourceLine(in, line))
    {
        string kw = firstWord(line);

        if (kw == "func")
        {
            if (isinsideFunc)
            {
                return false;   
            }
            isinsideFunc = true;
        }

        else if (kw == "func_end")
        {
            if (!isinsideFunc)
            {
                return false;   
            }
            isinsideFunc = false;
        }
    }

    if (isinsideFunc)
    {
        return false;           
    }

    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    if (f == nullptr)
    {
        return -1;
    }

    int64_t position = ftell(f);
    int32_t size = (int32_t)text.size();

    fwrite(&offsetField, sizeof(int64_t), 1, f);
    fwrite(&size, sizeof(int32_t), 1, f);
    fwrite(text.c_str(), 1, size, f);

    return position;
}

int64_t readResolveRecord(FILE *f, string &outText)
{
    if (f == nullptr)
    {
        return -1;
    }

    int64_t offsetField;
    int32_t size;

    if (fread(&offsetField, sizeof(int64_t), 1, f) != 1)
    {
        return -1;   
    }

    if (fread(&size, sizeof(int32_t), 1, f) != 1)
    {
        return -1;
    }

    if (size < 0 || size > 1024 * 1024)
    {
        return -1;   
    }

    outText = "";

    for (int32_t i = 0; i < size; i++)
    {
        int c = fgetc(f);
        if (c == EOF)
        {
            return -1;   
        }
        outText = outText + (char)c;
    }

    return offsetField;
}

int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;

    ifstream in(sourcePath);

    if (!in.is_open())
    {
        return -3;
    }

    FILE *out = fopen(resolveBinPath, "w+b");

    if (out == nullptr)
    {
        return -3;
    }

    string line;

    while (readSourceLine(in, line))
    {
        int64_t position = writeResolveRecord(out, 0, line);
        if (position < 0)
        {
            fclose(out);
            return -3;
        }

        string kw = firstWord(line);

        if (kw == "func")
        {
            if (funcCount >= MAX_FUNCS)
            {
                fclose(out);
                return -3;
            }

            string name = secondWord(line);

            for (int32_t i = 0; i < funcCount; i++)
            {
                if (funcArray[i].funcName == name)
                {
                    fclose(out);
                    return -3;
                }
            }

            funcArray[funcCount].funcName = name;
            funcArray[funcCount].byteOffsetInResolveBin = position;
            funcCount++;
        }
        else if (kw == "call")
        {
            if (patchCount >= MAX_PATCHES)
            {
                fclose(out);
                return -3;
            }

            patches[patchCount].byteOffsetOfOffsetField = position;
            patches[patchCount].targetFuncName = secondWord(line);
            patchCount++;
        }
    }

    for (int32_t p = 0; p < patchCount; p++)
    {
        int64_t target = -1;
        for (int32_t i = 0; i < funcCount; i++)
        {
            if(funcArray[i].funcName == patches[p].targetFuncName)
            {
                target = funcArray[i].byteOffsetInResolveBin;
                break;
            }
        }

        if(target < 0)
        {
            fclose(out);
            return -2;
        }

        fseek(out, (long)patches[p].byteOffsetOfOffsetField, SEEK_SET);
        fwrite(&target, sizeof(int64_t), 1, out);
    }

    fclose(out);

    for (int32_t i = 0; i < funcCount; i++)
    {
        if (funcArray[i].funcName == "main")
        {
            return funcArray[i].byteOffsetInResolveBin;
        }
    }
    return -1;


    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    // build the snapshot based on the callStack given
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{
    if (!validateProgram("source.bin"))
    {
        cerr << "Error: invalid program (file missing, nested func, or unmatched func/func_end)" << endl;
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    if (mainOffset < 0)
    {
        if (mainOffset == -1)
        {
            cerr << "Error: no main function found" << endl;
        }
        else if (mainOffset == -2)
        {
            cerr << "Error: call to undefined function" << endl;
        }
        else
        {
            cerr << "Error: resolve failed (file error, limit exceeded, or duplicate function)" << endl;
        }
        return 1;
    }

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}