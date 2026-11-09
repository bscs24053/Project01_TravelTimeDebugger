#include <iostream>
#include <cstring>
#include "../helper.h"
using namespace std;

int main()
{
    Crc32 c;
    c.update("123456789", 9);
    cout << "crc of 123456789: " << hex << c.value() << dec << endl;
    cout << (c.value() == 0xCBF43926u ? "PASS" : "FAIL") << endl;

    string body = "hello time travel debugger, this is a test message";
    Crc32 c2;
    c2.update(body.data(), body.size());
    char msg[200];
    memcpy(msg, body.data(), body.size());
    putU32(c2.value(), msg + body.size());
    size_t total = body.size() + 4;

    int chunks[] = {1, 3, 7, 64};
    for (int ci = 0; ci < 4; ci++)
    {
        TrailerChecker tc;
        string got;
        char out[200];
        for (size_t pos = 0; pos < total; pos += chunks[ci])
        {
            size_t n = total - pos < (size_t)chunks[ci] ? total - pos : chunks[ci];
            size_t r = tc.feed(msg + pos, n, out);
            got.append(out, r);
        }
        bool ok = tc.verify() && got == body;
        cout << "chunk " << chunks[ci] << ": " << (ok ? "PASS" : "FAIL") << endl;
    }

    msg[5] ^= 1;
    TrailerChecker tc2;
    char out2[200];
    tc2.feed(msg, total, out2);
    cout << "flipped body byte: " << (!tc2.verify() ? "PASS" : "FAIL") << endl;
    msg[5] ^= 1;

    msg[total - 1] ^= 1;
    TrailerChecker tc3;
    tc3.feed(msg, total, out2);
    cout << "flipped trailer byte: " << (!tc3.verify() ? "PASS" : "FAIL") << endl;

    TrailerChecker tc4;
    tc4.feed("ab", 2, out2);
    cout << "short stream: " << (!tc4.verify() ? "PASS" : "FAIL") << endl;
    return 0;
}