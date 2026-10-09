#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
using namespace std;

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

int main()
{
    FILE *f = fopen("t.bin", "wb");
    cout << writeResolveRecord(f, 0, "func main") << endl;   
    cout << writeResolveRecord(f, 0, "set a 10") << endl;    
    cout << writeResolveRecord(f, 0, "func_end") << endl;    
    fclose(f);

    f = fopen("t.bin", "rb");
    string text;
    int64_t off;

    while ((off = readResolveRecord(f, text)) != -1)
    {
        cout << "offset field: " << off << "  text: [" << text << "]" << endl;
    }
    
    fclose(f);

    return 0;
}