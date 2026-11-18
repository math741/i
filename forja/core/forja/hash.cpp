#include "hash.h"

#include <cstring>
#include <vector>

namespace forja {

namespace {
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::vector<uint8_t> pad_message(const std::string& data) {
    std::vector<uint8_t> m(data.begin(), data.end());
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8u;
    m.push_back(0x80);
    while ((m.size() % 64) != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    return m;
}
}  // namespace

std::string sha256_hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m = pad_message(data);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(m[off + i * 4]) << 24) | (uint32_t(m[off + i * 4 + 1]) << 16) | (uint32_t(m[off + i * 4 + 2]) << 8) | uint32_t(m[off + i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t v : h)
        for (int i = 7; i >= 0; --i) out.push_back(hx[(v >> (i * 4)) & 0xF]);
    return out;
}

std::string sha1_raw(const std::string& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> m = pad_message(data);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(m[off + i * 4]) << 24) | (uint32_t(m[off + i * 4 + 1]) << 16) | (uint32_t(m[off + i * 4 + 2]) << 8) | uint32_t(m[off + i * 4 + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out;
    for (uint32_t v : h)
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
    return out;
}

std::string base64(const std::string& raw) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < raw.size()) {
        uint32_t v = (uint32_t(uint8_t(raw[i])) << 16) | (uint32_t(uint8_t(raw[i + 1])) << 8) | uint32_t(uint8_t(raw[i + 2]));
        out.push_back(t[(v >> 18) & 63]); out.push_back(t[(v >> 12) & 63]); out.push_back(t[(v >> 6) & 63]); out.push_back(t[v & 63]);
        i += 3;
    }
    if (i + 1 == raw.size()) {
        uint32_t v = uint32_t(uint8_t(raw[i])) << 16;
        out.push_back(t[(v >> 18) & 63]); out.push_back(t[(v >> 12) & 63]); out += "==";
    } else if (i + 2 == raw.size()) {
        uint32_t v = (uint32_t(uint8_t(raw[i])) << 16) | (uint32_t(uint8_t(raw[i + 1])) << 8);
        out.push_back(t[(v >> 18) & 63]); out.push_back(t[(v >> 12) & 63]); out.push_back(t[(v >> 6) & 63]); out.push_back('=');
    }
    return out;
}

std::string hex_bytes(const std::string& raw) {
    static const char* hx = "0123456789abcdef";
    std::string o;
    for (unsigned char c : raw) { o.push_back(hx[c >> 4]); o.push_back(hx[c & 15]); }
    return o;
}

}  // namespace forja
