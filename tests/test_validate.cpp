#include <iostream>
#include <string>
#include <fstream>
using namespace std;

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

int main()
{
    cout << "valid.txt: " << validateProgram("valid.txt") << endl;     
    cout << "invalid1.txt: " << validateProgram("invalid1.txt") << endl;  
    cout << "invalid2.txt: " << validateProgram("invalid2.txt") << endl;  
    cout << "missing.txt: " << validateProgram("missing.txt") << endl;   

    ifstream in("valid.txt");

    string line;
    readSourceLine(in, line);
    
    cout << "first: [" << firstWord(line) << "] second: [" << secondWord(line) << "]" << endl;
   
    return 0;
}