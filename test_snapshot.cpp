#include <iostream>
#include <string>
#include <cstdint>
using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;

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
        while (!(isEmpty()))
        {
            pop();
        }
    }
    void push(const T &val)
    {
        if (count == MAX_STACK_DEPTH)
        {
            return;
        }
        Node *newNode = new Node;
        newNode->data = val;
        newNode->next = top;
        top = newNode;
        count++;
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
        return count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        Node *temp = top;
        int32_t copied = 0;
        while (temp != nullptr && copied < maxLen)
        {
            out[copied] = temp->data;
            temp = temp->next;
            copied++;
        }
        return copied;
    }
};

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

Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    Snapshot *s = new Snapshot;
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    return s;
}

int main()
{
    Stack<Frame> st;

    Frame one;
    one.func_name = "main";
    one.argc = 0;
    one.returnLine = 0;
    one.localCount = 1;
    one.locals[0].name = "k";
    one.locals[0].value = 10;

    Frame two;
    two.func_name = "foo";
    two.argc = 1;
    two.argv[0].name = "b";
    two.argv[0].value = 10;
    two.returnLine = 122;
    two.localCount = 1;
    two.locals[0].name = "a";
    two.locals[0].value = 10;

    st.push(one);
    st.push(two);

    Snapshot *s = buildSnapshot(st);

    cout << "depth: " << s->stackDepth << endl;                          
    cout << "top: " << s->callStack[0].func_name << endl;              
    cout << "last: " << s->callStack[1].func_name << endl;              
    cout << "foo local a = " << s->callStack[0].locals[0].value << endl; 
    cout << "foo arg b = " << s->callStack[0].argv[0].value << endl;   
    cout << "foo return = " << s->callStack[0].returnLine << endl;      
    cout << "main local k = " << s->callStack[1].locals[0].value << endl; 

    st.peek().locals[0].value = 999;
    cout << "after change, snapshot still: " << s->callStack[0].locals[0].value << endl; 

    Stack<Frame> empty;
    Snapshot *e = buildSnapshot(empty);
    cout << "empty depth: " << e->stackDepth << endl;                   

    delete s;
    delete e;

    
    return 0;
}