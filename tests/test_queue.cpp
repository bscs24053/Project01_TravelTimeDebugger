#include <iostream>
using namespace std;

template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;


public:
    // Implement these functions:
    Queue()
    {
        front = nullptr;
        rear = nullptr;
        count = 0;
    }
    ~Queue()
    {
        while (front != nullptr)
        {
            Node *temp = front;
            front = front->next;
            delete temp;
        }

    }
    void enqueue(const T &val)
    {
        Node *n = new Node;
        n->data = val;
        n->next = nullptr;
        if (rear == nullptr)
        {
            front = n;
            rear = n;
        }
        else
        {
            rear->next = n;
            rear = n;
        }
        count++;

    }
    T dequeue()
    {
        if (front == nullptr)
        {
            return T();
        }
        Node *temp = front;
        T value = temp->data;
        front = front->next;
        if (front == nullptr)
        {
            rear = nullptr;
        }
        delete temp;
        count--;
        return value;
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int size()
    {
        return count;
    }
};

int main()
{
    Queue<int> q;
    cout << "empty: " << q.isEmpty() << " size: " << q.size() << endl;   
    q.enqueue(10);
    q.enqueue(20);
    q.enqueue(30);
    cout << "size: " << q.size() << endl;                               
    cout << q.dequeue() << " " << q.dequeue() << endl;                    
    q.enqueue(40);
    cout << q.dequeue() << " " << q.dequeue() << endl;                    
    cout << "empty: " << q.isEmpty() << endl;                             
    cout << "dequeue on empty: " << q.dequeue() << endl;                  
    q.enqueue(50);
    cout << q.dequeue() << " size: " << q.size() << endl;                 
    return 0;
}