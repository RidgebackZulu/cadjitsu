#include "base/Base64.h"

#include <array>
#include <cstdint>

namespace cad {

namespace {
const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

std::string base64Encode(const std::string &bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for(; i + 2 < bytes.size(); i += 3) {
        const unsigned v = (unsigned(uint8_t(bytes[i])) << 16) | (unsigned(uint8_t(bytes[i + 1])) << 8) | uint8_t(bytes[i + 2]);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if(i < bytes.size()) {
        unsigned v = unsigned(uint8_t(bytes[i])) << 16;
        if(i + 1 < bytes.size()) v |= unsigned(uint8_t(bytes[i + 1])) << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string &text, std::string &bytes) {
    std::array<int, 256> value;
    value.fill(-1);
    for(int k = 0; k < 64; ++k) value[uint8_t(kAlphabet[k])] = k;
    bytes.clear();
    bytes.reserve(text.size() / 4 * 3);
    unsigned acc = 0;
    int bits = 0, pad = 0;
    for(char ch : text) {
        if(ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        if(ch == '=') {
            ++pad;
            continue;
        }
        if(pad) return false; // data after padding
        const int v = value[uint8_t(ch)];
        if(v < 0) return false;
        acc = (acc << 6) | unsigned(v);
        bits += 6;
        if(bits >= 8) {
            bits -= 8;
            bytes += char((acc >> bits) & 0xFF);
        }
    }
    return pad <= 2;
}

} // namespace cad
