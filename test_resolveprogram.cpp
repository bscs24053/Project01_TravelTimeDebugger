#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <cstdio>
using namespace std;

const int32_t MAX_FUNCS = 128;
const int32_t MAX_PATCHES = MAX_FUNCS * 4;

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin;
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField;
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
}

int main()
{

    FILE *f = fopen("demo.txt", "w");
    fputs("func foo b\nset a 10\nadd b a\nfunc_end\nfunc main\nset k 10\ncall foo k\nfunc_end\n", f);
    fclose(f);
    cout << "demo: main offset = " << resolveProgram("demo.txt", "demo_resolve.bin") << endl;

    f = fopen("nomain.txt", "w");
    fputs("func foo a\nset a 1\nfunc_end\n", f);
    fclose(f);
    cout << "no main: " << resolveProgram("nomain.txt", "r2.bin") << endl;           

    
    f = fopen("undef.txt", "w");
    fputs("func main\ncall xyz 1\nfunc_end\n", f);
    fclose(f);
    cout << "undefined: " << resolveProgram("undef.txt", "r3.bin") << endl;           

    
    f = fopen("dup.txt", "w");
    fputs("func foo a\nfunc_end\nfunc foo b\nfunc_end\nfunc main\nfunc_end\n", f);
    fclose(f);
    cout << "duplicate: " << resolveProgram("dup.txt", "r4.bin") << endl;             

   
    f = fopen("empty.txt", "w");
    fclose(f);
    cout << "empty: " << resolveProgram("empty.txt", "r5.bin") << endl;            

    
    f = fopen("demo_resolve.bin", "rb");

    string text;
    int64_t off;
    int64_t pos = 0;

    while (true)
    {
        pos = ftell(f);
        off = readResolveRecord(f, text);
        if (off == -1) break;
        cout << "pos " << pos << "  offset field " << off << "  [" << text << "]" << endl;
    }
    fclose(f);

    f = fopen("demo2.txt", "w");
    fputs("func main\nset k 10\ncall foo k\nfunc_end\nfunc foo b\nadd b b\nfunc_end\n", f);
    fclose(f);

    cout << "demo2 main offset = " << resolveProgram("demo2.txt", "demo2_resolve.bin") << endl;   
    f = fopen("demo2_resolve.bin", "rb");

    while (true)
    {
        pos = ftell(f);
        off = readResolveRecord(f, text);
        if (off == -1) break;
        cout << "pos " << pos << "  offset field " << off << "  [" << text << "]" << endl;
    }
    fclose(f);

    return 0;
}