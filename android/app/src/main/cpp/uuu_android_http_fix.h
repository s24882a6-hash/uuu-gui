#pragma once
// Принудительно подключается (-include) только в libuuu/http.cpp.
//
// В upstream base64_encode объявлена внутри #ifdef UUUSSL (на OpenSSL BIO), а
// используется для HTTP Basic-авторизации безусловно — без OpenSSL http.cpp
// не компилируется. OpenSSL мы не тянем, поэтому даём свою реализацию.

#ifndef UUUSSL
#include <string>

static std::string base64_encode(std::string in) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += tbl[v >> 6 & 63]; out += tbl[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        out += tbl[v >> 18 & 63]; out += tbl[v >> 12 & 63]; out += tbl[v >> 6 & 63]; out += '=';
    }
    return out;
}
#endif
