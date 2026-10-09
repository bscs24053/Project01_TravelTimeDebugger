#include <iostream>
#include <string>
#include <cstdint>
using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2;

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

int skipSpaces(const string &line, int i)
{
    while (i < (int)line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
        i++;
    }
    return i;
}

int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    int32_t count = 0;
    int i = skipSpaces(line, 0);

    while (i < (int)line.size())
    {
        string word = "";

        while(i < (int)line.size() && line[i] != ' ' && line[i] != '\t')
        {
            word = word + line[i];
            i++;
        }

        if(count >= maxTokens)
        {
            return -1;
        }

        tokens[count].text = word;

        if(count == 0)
        {
            tokens[count].type = KEYWORD;
        }

        else if(count == 1)
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
    if(s.empty())
    {
        return false;
    }

    int i = 0;
    bool negative = false;
    
    if(s[0] == '-' || s[0] == '+')
    {
        negative = (s[0] == '-');
        i = 1;
    }

    if (i >= (int)s.size())
    {
        return false;  
    }

    int64_t value = 0;

    for(; i < (int)s.size(); i++)
    {
        if(s[i] < '0' || s[i] > '9')
        {
            return false;
        }

        value = value * 10 + (s[i] - '0');

        if(value > 2147483648LL)
        {
            return false;   
        }
    }

    if(negative)
    {
        value = -value;
    }

    if(value > 2147483647LL || value < -2147483648LL)
    {
        return false;
    }

    out = (int32_t)value;

    return true;
}

void printTokens(const string &line)
{
    Token tokens[MAX_TOKENS];
    int32_t n = tokenizeLine(line, tokens, MAX_TOKENS);

    cout << "[" << line << "] -> " << n << " tokens: ";

    for (int32_t i = 0; i < n; i++)
    {
        cout << tokens[i].text << "(" << tokens[i].type << ") ";
    }
    cout << endl;
}

int main()
{
    printTokens("set a 10");             
    printTokens("func_end");             
    printTokens("call foo a b c");       
    printTokens("  add \t b   a  ");     
    printTokens("func main");            

    printTokens("call f 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16");
    printTokens("call f 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17");

    int32_t v = 0;

    cout << "parseInt 10:" << parseInt("10", v) << " v=" << v << endl;    
    cout << "parseInt -5: " << parseInt("-5", v) << " v=" << v << endl;    
    cout << "parseInt abc: " << parseInt("abc", v) << endl;                 
    cout << "parseInt empty: " << parseInt("", v) << endl;                
    cout << "parseInt -: " << parseInt("-", v) << endl;                
    cout << "parseInt 99999999999: " << parseInt("99999999999", v) << endl;        
    cout << "parseInt 12x: " << parseInt("12x", v) << endl; 
                     
    return 0;
}