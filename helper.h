#pragma once
#include <string>
#include <cstdint>
#include <sys/types.h>
#include <sys/socket.h>
using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int IO_BUFFER_SIZE = 64 * 1024;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024;

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};

class Crc32
{
    uint32_t state;

public:
    Crc32() : state(0xFFFFFFFFu) {}

    void update(const char *buf, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            state ^= (unsigned char)buf[i];
            for (int b = 0; b < 8; b++)
            {
                if(state & 1)
                {
                    state = (state >> 1) ^ 0xEDB88320u;
                }

                else
                {
                    state = state >> 1;
                }
            }
        }
    }
    uint32_t value() const { return ~state; }
};

class TrailerChecker
{
    Crc32 crc;
    unsigned char held[4];
    int heldCount;

public:
    TrailerChecker() : heldCount(0) {}

    size_t feed(const char *in, size_t n, char *out)
    {
        size_t released = 0;
        for (size_t i = 0; i < n; i++)
        {
            if (heldCount == 4)
            {
                char c = (char)held[0];
                crc.update(&c, 1);
                out[released++] = c;
                held[0] = held[1];
                held[1] = held[2];
                held[2] = held[3];
                held[3] = (unsigned char)in[i];
            }

            else
            {
                held[heldCount++] = (unsigned char)in[i];
            }
        }
        return released;
    }

    bool verify() const
    {
        if (heldCount != 4)
        {
            return false;
        }

        uint32_t stored = ((uint32_t)held[0] << 24) | ((uint32_t)held[1] << 16) | ((uint32_t)held[2] << 8) | (uint32_t)held[3];
        return stored == crc.value();
    }
    const unsigned char *trailerBytes() const { return held; }
};

inline void putU32(uint32_t v, char *out)
{
    out[0] = (char)(v >> 24);
    out[1] = (char)(v >> 16);
    out[2] = (char)(v >> 8);
    out[3] = (char)v;
}

inline bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sent = 0;

    while (sent < n)
    {
        ssize_t r = send(sock, buf + sent, n - sent, MSG_NOSIGNAL);

        if (r <= 0)
        {
            return false;
        }

        sent += (size_t)r;
    }
    return true;
}

inline bool recvAll(int sock, char *buf, size_t n)
{
    size_t got = 0;

    while (got < n)
    {
        ssize_t r = recv(sock, buf + got, n - got, 0);

        if (r <= 0)
        {
            return false;
        }

        got += (size_t)r;
    }

    return true;
}

inline bool sendStatusMessage(int sock, char status, const string &payload)
{
    Crc32 c;
    c.update(&status, 1);
    c.update(payload.data(), payload.size());
    char trailer[4];
    putU32(c.value(), trailer);
    return sendAll(sock, &status, 1) && sendAll(sock, payload.data(), payload.size()) && sendAll(sock, trailer, 4);
}