#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../helper.h"
using namespace std;

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        cout << "usage: test_upload <port> <file> [corrupt]" << endl;
        return 1;
    }

    int port = atoi(argv[1]);
    bool corrupt = (argc > 3);

    ifstream in(argv[2], ios::binary);
    stringstream ss;
    ss << in.rdbuf();
    string body = ss.str();

    char status = 0;
    Crc32 crc;
    crc.update(&status, 1);
    crc.update(body.data(), body.size());
    char trailer[4];
    putU32(crc.value(), trailer);

    if(corrupt)
    {
        trailer[3] ^= 1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);

    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);

    if(connect(s, (sockaddr *)&a, sizeof(a)) < 0)
    {
        perror("connect");
        return 1;
    }

    sendAll(s, &status, 1);
    sendAll(s, body.data(), body.size());
    sendAll(s, trailer, 4);
    shutdown(s, SHUT_WR);

    TrailerChecker tc;
    string reply;
    char buf[4096];
    char rel[4096];

    while(true)
    {
        ssize_t r = recv(s, buf, sizeof(buf), 0);

        if(r <= 0)
        {
            break;
        }

        size_t n = tc.feed(buf, (size_t)r, rel);
        reply.append(rel, n);
    }

    close(s);

    cout << "checksum ok: " << (tc.verify() ? "yes" : "no") << endl;

    if(reply.empty())
    {
        cout << "empty reply" << endl;
        return 1;
    }

    cout << "status: " << (int)reply[0] << endl;

    if(reply[0] == 0)
    {
        cout << "tdbg bytes: " << reply.size() - 1 << endl;
    }

    else
    {
        cout << "error: " << reply.substr(1) << endl;
    }
    
    return 0;
}