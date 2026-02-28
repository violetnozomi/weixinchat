#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cctype>

// ── 轻量 base64 编解码（服务端文件传输用）──────────────────────────────────────

static const std::string kB64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string b64_encode(const std::vector<uint8_t>& data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16)
                        | (static_cast<uint32_t>(data[i + 1]) << 8)
                        | (static_cast<uint32_t>(data[i + 2]));
        out.push_back(kB64Chars[(triple >> 18) & 0x3F]);
        out.push_back(kB64Chars[(triple >> 12) & 0x3F]);
        out.push_back(kB64Chars[(triple >>  6) & 0x3F]);
        out.push_back(kB64Chars[(triple      ) & 0x3F]);
        i += 3;
    }

    const size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kB64Chars[(triple >> 18) & 0x3F]);
        out.push_back(kB64Chars[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16)
                        | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kB64Chars[(triple >> 18) & 0x3F]);
        out.push_back(kB64Chars[(triple >> 12) & 0x3F]);
        out.push_back(kB64Chars[(triple >>  6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

inline std::vector<uint8_t> b64_decode(const std::string& s)
{
    auto decode_char = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    auto is_base64 = [](unsigned char c) {
        return (isalnum(c) || c == '+' || c == '/');
    };

    std::vector<uint8_t> out;
    out.reserve((s.size() / 4) * 3);

    int i = 0;
    unsigned char arr4[4];
    unsigned char arr3[3];
    size_t in_len = s.size();
    size_t pos    = 0;

    while (in_len-- && s[pos] != '=' && is_base64((unsigned char)s[pos])) {
        arr4[i++] = (unsigned char)s[pos++];
        if (i == 4) {
            for (int k = 0; k < 4; k++) {
                int v = decode_char(arr4[k]);
                if (v < 0) return out;
                arr4[k] = static_cast<unsigned char>(v);
            }
            arr3[0] = (arr4[0] << 2) | (arr4[1] >> 4);
            arr3[1] = (arr4[1] << 4) | (arr4[2] >> 2);
            arr3[2] = (arr4[2] << 6) | arr4[3];

            for (int k = 0; k < 3; k++) out.push_back(arr3[k]);
            i = 0;
        }
    }

    if (i) {
        for (int k = i; k < 4; k++) arr4[k] = 0;
        for (int k = 0; k < 4; k++) {
            int v = decode_char(arr4[k]);
            if (v < 0) v = 0;
            arr4[k] = static_cast<unsigned char>(v);
        }

        arr3[0] = (arr4[0] << 2) | (arr4[1] >> 4);
        arr3[1] = (arr4[1] << 4) | (arr4[2] >> 2);
        arr3[2] = (arr4[2] << 6) | arr4[3];

        for (int k = 0; k < i - 1; k++) out.push_back(arr3[k]);
    }

    return out;
}
