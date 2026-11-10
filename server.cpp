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
#include <cstdio>
using namespace std;
#include "helper.h"
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ---- Constants ----
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = 16 + 2; 
const int32_t MAX_PATCHES = MAX_FUNCS * 4;          
const int32_t SOCKET_TIMEOUT_SEC = 5;                
const int32_t TDBG_VERSION = 1;
const int32_t HEADER_SIZE = 20;                        

#ifdef _WIN32
#define SEEK64(f, pos, origin) _fseeki64((f), (__int64)(pos), (origin))
#define TELL64(f) ((int64_t)_ftelli64(f))
#else
#define SEEK64(f, pos, origin) fseeko((f), (off_t)(pos), (origin))
#define TELL64(f) ((int64_t)ftello(f))
#endif

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
        if (top == nullptr)
        {
            return T(); 
        }
        Node *temp = top;
        top = top->next;
        T data = temp->data;
        delete temp;
        count--;
        return data;
    }

    T &peek()
    {
        if (top == nullptr)
        {
            static T emptyValue = T(); 
            return emptyValue;
        }
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

        while(temp != nullptr && copied < maxLen)
        {
            out[copied] = temp->data;
            temp = temp->next;
            copied++;
        }

        return copied;
    }
};

struct Snapshot; 
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

    ~Timeline(); 

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

void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    
}


// PASS 0x0: READING source.bin + VALIDITY CHECK
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

    int64_t position = TELL64(f);
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

// Every source line becomes one record holding the raw line, as-is.
// resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
// (remember its position) and CALL (remember which function it needs
// and where its offset field sits).
// Once the whole file is written, every CALL's offset field is patched
// with its target's position. Patching happens after the full write.
// Returns the byte offset of main's FUNC header record.
// Errors: -1 = no main, -2 = call to undefined function,
//         -3 = file error, limit exceeded or duplicate function
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
            if (funcArray[i].funcName == patches[p].targetFuncName)
            {
                target = funcArray[i].byteOffsetInResolveBin;
                break;
            }
        }

        if (target < 0)
        {
            fclose(out);
            return -2;
        }

        SEEK64(out, patches[p].byteOffsetOfOffsetField, SEEK_SET);
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
}

// PASS 0x2: EXECUTION 
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

// first word is always a instruction keyword
// instruction set = [func, func_end, call, set, add, sub, mul and div]
// next word is identifier like name of a function, variable name
// after identifier all are the params/arg, space separated
// Returns number of tokens, or -1 if there are too many.
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    int32_t count = 0;
    int i = skipSpaces(line, 0);

    while (i < (int)line.size())
    {
        string word = "";

        while (i < (int)line.size() && line[i] != ' ' && line[i] != '\t')
        {
            word = word + line[i];
            i++;
        }

        if (count >= maxTokens)
        {
            return -1;
        }

        tokens[count].text = word;

        if (count == 0)
        {
            tokens[count].type = KEYWORD;
        }

        else if (count == 1)
        {
            tokens[count].type = IDENTIFIER;
        }

        else
        {
            tokens[count].type = PARAM;
        }

        count++;

        i = skipSpaces(line, i);
    }

    return count;
}

bool parseInt(const string &s, int32_t &out)
{
    if (s.empty())
    {
        return false;
    }

    int i = 0;
    bool negative = false;

    if (s[0] == '-' || s[0] == '+')
    {
        negative = (s[0] == '-');
        i = 1;
    }

    if (i >= (int)s.size())
    {
        return false;
    }

    int64_t value = 0;

    for (; i < (int)s.size(); i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return false;
        }

        value = value * 10 + (s[i] - '0');

        if (value > 2147483648LL)
        {
            return false;
        }
    }

    if (negative)
    {
        value = -value;
    }

    if (value > 2147483647LL || value < -2147483648LL)
    {
        return false;
    }

    out = (int32_t)value;

    return true;
}

Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    Snapshot *s = new Snapshot;
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    return s;
}

Variable *findVar(Frame &frame, const string &name)
{
    for (int32_t i = 0; i < frame.argc; i++)
    {
        if (frame.argv[i].name == name)
        {
            return &frame.argv[i];
        }
    }

    for (int32_t i = 0; i < frame.localCount; i++)
    {
        if (frame.locals[i].name == name)
        {
            return &frame.locals[i];
        }
    }

    return nullptr;
}

bool getValue(Frame &frame1, const string &text, int32_t &out)
{
    if (parseInt(text, out))
    {
        return true;
    }

    Variable *v = findVar(frame1, text);

    if (v == nullptr)
    {
        return false;
    }

    out = v->value;

    return true;
}

int32_t executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    FILE *file = fopen(resolveBinPath, "rb");

    if (file == nullptr)
    {
        return -1;
    }

    Stack<Frame> callStack;
    string argNames[MAX_STACK_DEPTH + 1][MAX_VARS_PER_FRAME];

    SEEK64(file, mainOffset, SEEK_SET);
    string line;

    if (readResolveRecord(file, line) == -1)
    {
        fclose(file);
        return -1;
    }

    Frame mainFrame;
    mainFrame.func_name = secondWord(line);
    mainFrame.argc = 0;
    mainFrame.returnLine = -1;
    mainFrame.localCount = 0;
    callStack.push(mainFrame);

    int64_t off;

    while ((off = readResolveRecord(file, line)) != -1)
    {
        Token tokens[MAX_TOKENS];
        int32_t n = tokenizeLine(line, tokens, MAX_TOKENS);

        if (n < 1)
        {
            fclose(file);
            return -2;
        }

        string keyword = tokens[0].text;

        if (keyword == "func_end")
        {
            if (callStack.depth() == 1)
            {
                timeline.record(buildSnapshot(callStack));
                break;
            }

            int32_t calleeDepth = callStack.depth();
            Frame callee = callStack.pop();
            Frame &caller = callStack.peek();

            for (int32_t i = 0; i < callee.argc; i++)
            {
                if (argNames[calleeDepth][i] != "")
                {
                    Variable *v = findVar(caller, argNames[calleeDepth][i]);

                    if (v != nullptr)
                    {
                        v->value = callee.argv[i].value;
                    }
                }
            }

            SEEK64(file, callee.returnLine, SEEK_SET);
            timeline.record(buildSnapshot(callStack));
        }

        else if (keyword == "call")
        {
            if (n < 2)
            {
                fclose(file);
                return -2;
            }

            if (callStack.depth() >= MAX_STACK_DEPTH)
            {
                fclose(file);
                return -7;
            }

            int32_t argCount = n - 2;
            int64_t returnPos = TELL64(file);

            SEEK64(file, off, SEEK_SET);
            string funcLine;

            if (readResolveRecord(file, funcLine) == -1)
            {
                fclose(file);
                return -1;
            }

            Token funcTokens[MAX_TOKENS];
            int32_t fn = tokenizeLine(funcLine, funcTokens, MAX_TOKENS);

            if (fn < 2 || funcTokens[0].text != "func")
            {
                fclose(file);
                return -1;
            }

            int32_t paramCount = fn - 2;

            if (paramCount != argCount)
            {
                fclose(file);
                return -6;
            }

            Frame &caller = callStack.peek();
            Frame newFrame;
            newFrame.func_name = funcTokens[1].text;
            newFrame.argc = paramCount;
            newFrame.returnLine = (int32_t)returnPos;
            newFrame.localCount = 0;

            int32_t newDepth = callStack.depth() + 1;

            for (int32_t i = 0; i < paramCount; i++)
            {
                string argText = tokens[2 + i].text;
                int32_t value;

                if (parseInt(argText, value))
                {
                    argNames[newDepth][i] = "";
                }

                else
                {
                    Variable *v = findVar(caller, argText);

                    if (v == nullptr)
                    {
                        fclose(file);
                        return -3;
                    }

                    value = v->value;
                    argNames[newDepth][i] = argText;
                }

                newFrame.argv[i].name = funcTokens[2 + i].text;
                newFrame.argv[i].value = value;
            }

            callStack.push(newFrame);
            timeline.record(buildSnapshot(callStack));
        }

        else if (keyword == "set" || keyword == "add" || keyword == "sub" || keyword == "mul" || keyword == "div")
        {
            if (n != 3)
            {
                fclose(file);
                return -2;
            }

            Frame &cur = callStack.peek();
            int32_t value;

            if (!getValue(cur, tokens[2].text, value))
            {
                fclose(file);
                return -3;
            }

            Variable *target = findVar(cur, tokens[1].text);

            if (keyword == "set")
            {
                if (target == nullptr)
                {
                    if (cur.localCount >= MAX_VARS_PER_FRAME)
                    {
                        fclose(file);
                        return -5;
                    }

                    target = &cur.locals[cur.localCount];
                    target->name = tokens[1].text;
                    cur.localCount++;
                }

                target->value = value;
            }

            else
            {
                if (target == nullptr)
                {
                    fclose(file);
                    return -3;
                }

                if (keyword == "add")
                {
                    target->value = target->value + value;
                }

                else if (keyword == "sub")
                {
                    target->value = target->value - value;
                }

                else if (keyword == "mul")
                {
                    target->value = target->value * value;
                }

                else
                {
                    if (value == 0)
                    {
                        fclose(file);
                        return -4;
                    }

                    target->value = target->value / value;
                }
            }

            timeline.record(buildSnapshot(callStack));
        }

        else
        {
            fclose(file);
            return -2;
        }
    }

    fclose(file);

    return 0;
}

// PASS 0x3: SERIALIZE TIMELINE
bool writeInt32(FILE *file, int32_t v)
{
    return fwrite(&v, sizeof(int32_t), 1, file) == 1;
}

bool writeInt64(FILE *file, int64_t v)
{
    return fwrite(&v, sizeof(int64_t), 1, file) == 1;
}
bool writeString(FILE *file, const string &s)
{
    int32_t len = (int32_t)s.size();

    if (!writeInt32(file, len))
    {
        return false;
    }

    if (len > 0 && fwrite(s.c_str(), 1, len, file) != (size_t)len)
    {
        return false;
    }

    return true;
}

bool writeVariable(FILE *file, const Variable &v)
{
    return writeString(file, v.name) && writeInt32(file, v.value);
}

bool writeFrame(FILE *file, const Frame &fr)
{
    if (!writeString(file, fr.func_name))
    {
        return false;
    }

    if (!writeInt32(file, fr.argc))
    {
        return false;
    }

    for (int32_t i = 0; i < fr.argc; i++)
    {
        if (!writeVariable(file, fr.argv[i]))
        {
            return false;
        }
    }

    if (!writeInt32(file, fr.returnLine))
    {
        return false;
    }

    if (!writeInt32(file, fr.localCount))
    {
        return false;
    }

    for (int32_t i = 0; i < fr.localCount; i++)
    {
        if (!writeVariable(file, fr.locals[i]))
        {
            return false;
        }
    }

    return true;
}

bool writeSnapshot(FILE *file, const Snapshot &s)
{
    if (!writeInt32(file, s.stackDepth))
    {
        return false;
    }

    for (int32_t i = 0; i < s.stackDepth; i++)
    {
        if (!writeFrame(file, s.callStack[i]))
        {
            return false;
        }
    }

    return true;
}

bool writeHeaderFull(FILE *file, const TTDBHeader &h)
{
    if (fwrite(h.magic, 1, 4, file) != 4)
    {
        return false;
    }

    if (!writeInt32(file, h.version))
    {
        return false;
    }

    if (!writeInt32(file, h.stepCount))
    {
        return false;
    }

    if (!writeInt64(file, h.indexOffset))
    {
        return false;
    }

    return true;
}

bool writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    FILE *file = fopen(tdbgPath, "wb");

    if (file == nullptr)
    {
        return false;
    }

    int32_t steps = timeline.getStepCount();
    TTDBHeader h;

    h.magic[0] = 'T';
    h.magic[1] = 'T';
    h.magic[2] = 'D';
    h.magic[3] = 'B';
    h.version = TDBG_VERSION;
    h.stepCount = 0;
    h.indexOffset = 0;

    if (!writeHeaderFull(file, h))
    {
        fclose(file);
        return false;
    }

    int64_t *index = new int64_t[steps > 0 ? steps : 1];
    int32_t i = 0;

    for (TimelineNode *n = timeline.begin(); n != nullptr; n = n->next)
    {
        index[i] = TELL64(file);

        if (!writeSnapshot(file, *(n->data)))
        {
            delete[] index;
            fclose(file);
            return false;
        }

        i++;
    }

    int64_t indexStart = TELL64(file);

    for (int32_t k = 0; k < steps; k++)
    {
        if (!writeInt64(file, index[k]))
        {
            delete[] index;
            fclose(file);
            return false;
        }
    }

    delete[] index;

    h.stepCount = steps;
    h.indexOffset = indexStart;

    SEEK64(file, 0, SEEK_SET);

    if (!writeHeaderFull(file, h))
    {
        fclose(file);
        return false;
    }

    fclose(file);

    return true;
}

bool readInt32(FILE *file, int32_t &v)
{
    return fread(&v, sizeof(int32_t), 1, file) == 1;
}

bool readInt64(FILE *file, int64_t &v)
{
    return fread(&v, sizeof(int64_t), 1, file) == 1;
}

bool readString(FILE *file, string &s)
{
    int32_t len;

    if (!readInt32(file, len))
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
        int c = fgetc(file);

        if (c == EOF)
        {
            return false;
        }

        s += (char)c;
    }

    return true;
}

bool readVariable(FILE *file, Variable &v)
{
    return readString(file, v.name) && readInt32(file, v.value);
}

bool readFrame(FILE *file, Frame &fr)
{
    if (!readString(file, fr.func_name))
    {
        return false;
    }

    if (!readInt32(file, fr.argc))
    {
        return false;
    }

    if (fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME)
    {
        return false;
    }

    for (int32_t i = 0; i < fr.argc; i++)
    {
        if (!readVariable(file, fr.argv[i]))
        {
            return false;
        }
    }

    if (!readInt32(file, fr.returnLine))
    {
        return false;
    }

    if (!readInt32(file, fr.localCount))
    {
        return false;
    }

    if (fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
    {
        return false;
    }

    for (int32_t i = 0; i < fr.localCount; i++)
    {
        if (!readVariable(file, fr.locals[i]))
        {
            return false;
        }
    }

    return true;
}

bool readSnapshot(FILE *file, Snapshot &s)
{
    if (!readInt32(file, s.stackDepth))
    {
        return false;
    }

    if (s.stackDepth < 0 || s.stackDepth > MAX_STACK_DEPTH)
    {
        return false;
    }

    for (int32_t i = 0; i < s.stackDepth; i++)
    {
        if (!readFrame(file, s.callStack[i]))
        {
            return false;
        }
    }

    return true;
}

bool readSnapshotAt(const char *path, int32_t n, Snapshot &out, int32_t &totalSteps)
{
    FILE *file = fopen(path, "rb");

    if (file == nullptr)
    {
        return false;
    }

    char magic[4];
    int32_t version, steps;
    int64_t indexOffset;

    if (fread(magic, 1, 4, file) != 4 || !readInt32(file, version) || !readInt32(file, steps) || !readInt64(file, indexOffset))
    {
        fclose(file);
        return false;
    }

    if (magic[0] != 'T' || magic[1] != 'T' || magic[2] != 'D' || magic[3] != 'B')
    {
        fclose(file);
        return false;
    }

    totalSteps = steps;

    if (n < 0 || n >= steps)
    {
        fclose(file);
        return false;
    }

    SEEK64(file, indexOffset + (int64_t)n * 8, SEEK_SET);

    int64_t snapPos;

    if (!readInt64(file, snapPos))
    {
        fclose(file);
        return false;
    }

    SEEK64(file, snapPos, SEEK_SET);
    bool ok = readSnapshot(file, out);

    fclose(file);

    return ok;
}

//runs all the passes on source.bin and writes session.tdbg => returns true if success, else false and puts the msg in err
bool processSource(string &err)
{
    if (!validateProgram("source.bin"))
    {
        err = "invalid program (file missing, nested func, or unmatched func/func_end)";
        return false;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
    Timeline timeline;
    int32_t rc = executeProgram("resolve.bin", mainOffset, timeline);

    if (mainOffset < 0)
    {
        if(mainOffset == -1)
        {
            err = "no main function found";
        }

        else if(mainOffset == -2)
        {
            err = "call to undefined function";
        }

        else
        {
            err = "resolve failed (file error, limit exceeded, or duplicate function)";
        }

        return false;
    }

    if(rc != 0)
    {
        if(rc == -1)
        {
            err = "cannot read resolve.bin";
        }

        else if(rc == -2)
        {
            err = "bad line or unknown instruction";
        }

        else if(rc == -3)
        {
            err = "variable used before it was set";
        }  

        else if(rc == -4)
        {
            err = "divide by zero";
        }

        else if(rc == -5)
        {
            err = "more than 16 variables in one function";
        }

        else if(rc == -6)
        {
            err = "call has a different number of arguments than the function has params";
        }

        else if(rc == -7)
        {
            err = "call stack too deep (more than 64)";
        }

        else
        {
            err = "execution failed";
        }

        return false;
    }

    writeTdbg(timeline, "session.tdbg");
    return true;
}

void sendTdbgReply(int sock) 
{
    FILE *f = fopen("session.tdbg", "rb");
    char status = 0;
    Crc32 crc;
    char buf[IO_BUFFER_SIZE];
    size_t n;
    char trailer[4];

    if(f == nullptr)
    {
        sendStatusMessage(sock, 1, "server could not read session.tdbg");

        return;
    }

    crc.update(&status, 1);
    sendAll(sock, &status, 1);

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        crc.update(buf, n);

        if(!sendAll(sock, buf, n))
        {
            fclose(f);
            return;
        }
    }

    fclose(f);

    putU32(crc.value(), trailer);
    sendAll(sock, trailer, 4);
}

void handleClient(int sock)
{
    remove("source.bin");
    remove("session.tdbg");

    FILE *out = fopen("source.bin", "wb");
    TrailerChecker checker;
    char in[IO_BUFFER_SIZE];
    char released[IO_BUFFER_SIZE];
    bool gotStatus = false;
    char status = 0;
    bool tooBig = false;
    bool recvFailed = false;
    uint64_t written = 0;
    string err;

    if(out == nullptr)
    {
        sendStatusMessage(sock, 1, "server could not create source.bin");
        return;
    }

    while(true)
    {
        ssize_t r = recv(sock, in, sizeof(in), 0);

        if(r < 0)
        {
            recvFailed = true;
            break;
        }

        if(r == 0)
        {
            break;
        }

        size_t n = checker.feed(in, (size_t)r, released);
        size_t start = 0;

        if(!gotStatus && n > 0)
        {
            status = released[0];
            gotStatus = true;
            start = 1;
        }

        size_t body = n - start;
        written += body;

        if(written > MAX_SOURCE_BYTES)
        {
            tooBig = true;
        }

        else if(body > 0)
        {
            fwrite(released + start, 1, body, out);
        }
    }

    fclose(out);

    if(recvFailed)
    {
        return;   
    }

    if(!checker.verify())
    {
        sendStatusMessage(sock, 1, "upload checksum mismatch");
        return;
    }

    if(!gotStatus || status != 0)
    {
        sendStatusMessage(sock, 1, "bad request");
        return;
    }

    if(tooBig)
    {
        sendStatusMessage(sock, 1, "source file is too large");
        return;
    }

    if(!processSource(err))
    {
        sendStatusMessage(sock, 1, err);
        return;
    }
    
    sendTdbgReply(sock);
}

int main(int argc, char *argv[])
{
    int port = 5001;
    int yes = 1;
    sockaddr_in addr = {};

    if(argc > 1)
    {
        port = atoi(argv[1]);
    }

    signal(SIGPIPE, SIG_IGN);

    int listenSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if(listenSock < 0)
    {
        perror("socket");
        return 1;
    }

    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if(bind(listenSock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    if(listen(listenSock, 8) < 0)
    {
        perror("listen");
        return 1;
    }

    cout << "server listening on port " << port << endl;

    while(true)
    {
        int client = accept(listenSock, nullptr, nullptr);

        if(client < 0)
        {
            perror("accept");
            continue;
        }

        handleClient(client);
        close(client);
    }
    return 0;
}