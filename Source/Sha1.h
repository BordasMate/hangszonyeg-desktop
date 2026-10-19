#pragma once
#include <cstdint>
#include <cstring>
#include <string>

// Kis, önálló SHA-1 a WebSocket-kézfogáshoz (RFC 6455: Sec-WebSocket-Accept).
namespace hz
{
inline void sha1 (const void* data, size_t len, uint8_t out[20])
{
    auto rol = [] (uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::string msg (static_cast<const char*> (data), len);
    const uint64_t bitLen = static_cast<uint64_t> (len) * 8;
    msg.push_back (static_cast<char> (0x80));
    while (msg.size() % 64 != 56)
        msg.push_back (0);
    for (int i = 7; i >= 0; --i)
        msg.push_back (static_cast<char> ((bitLen >> (i * 8)) & 0xff));

    for (size_t off = 0; off < msg.size(); off += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            const auto* p = reinterpret_cast<const uint8_t*> (msg.data() + off + i * 4);
            w[i] = (uint32_t (p[0]) << 24) | (uint32_t (p[1]) << 16) | (uint32_t (p[2]) << 8) | uint32_t (p[3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
            const uint32_t t = rol (a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol (b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = static_cast<uint8_t> ((hs[i] >> (24 - j * 8)) & 0xff);
}
} // namespace hz
