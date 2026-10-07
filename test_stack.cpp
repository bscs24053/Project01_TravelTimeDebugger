#include <iostream>
#include <cstdint>
using namespace std;

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

int main()
{
    Stack<int> s;

    s.push(1); 
    s.push(2); 
    s.push(3);
    cout << s.depth() << endl;  
    cout << s.peek() << endl;    
    cout << s.pop() << endl; 
        
    int arr[10];
    int n = s.snapshot_into(arr, 10);
    cout << n << ": " << arr[0] << " " << arr[1] << endl;  

    Stack<int> big;
    for (int i = 0; i < 70; i++) big.push(i);
    cout << big.depth() << endl;  
    return 0;
}