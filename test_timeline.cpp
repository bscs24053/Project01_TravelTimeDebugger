#include <iostream>
#include <cstdint>
using namespace std;

struct Snapshot { int id; };  

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
    ~Timeline()
    {
        TimelineNode *temp = head;
        while(temp != nullptr)
        {
            TimelineNode *nextNode = temp->next;
            delete temp->data;  
            delete temp;        
            temp = nextNode;
        }
    }
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
    TimelineNode *getTail()  
    {
        return tail;
    }
};

int main()
{
    Timeline t;

    for (int i = 1; i <= 3; i++)
    {
        Snapshot *s = new Snapshot;
        s->id = i;
        t.record(s);
    }
    cout << "steps: " << t.getStepCount() << endl;   

    for (TimelineNode *n = t.begin(); n != nullptr; n = n->next)
    {
        cout << n->data->id << " ";   
    }    

    cout << endl;

    for (TimelineNode *n = t.getTail(); n != nullptr; n = n->prev)
    {
        cout << n->data->id << " ";    
    }

    cout << endl;
    
    return 0;
}