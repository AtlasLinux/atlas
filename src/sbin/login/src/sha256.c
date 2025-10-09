/* sha256.c - small SHA-256 implementation (public-domain-style) */
#include "sha256.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static inline uint32_t rotr32(uint32_t x, unsigned r) { return (x >> r) | (x << (32 - r)); }

static const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    for (int t = 0; t < 16; ++t) {
        W[t] = (uint32_t)block[t*4]<<24 | (uint32_t)block[t*4+1]<<16 | (uint32_t)block[t*4+2]<<8 | (uint32_t)block[t*4+3];
    }
    for (int t = 16; t < 64; ++t) {
        uint32_t s0 = rotr32(W[t-15],7) ^ rotr32(W[t-15],18) ^ (W[t-15] >> 3);
        uint32_t s1 = rotr32(W[t-2],17) ^ rotr32(W[t-2],19) ^ (W[t-2] >> 10);
        W[t] = W[t-16] + s0 + W[t-7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int t = 0; t < 64; ++t) {
        uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[t] + W[t];
        uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256(const void *data, size_t len, uint8_t digest[32]) {
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    uint8_t block[64];
    size_t rem = len;
    const uint8_t *p = (const uint8_t*)data;
    /* process full blocks */
    while (rem >= 64) {
        memcpy(block, p, 64);
        sha256_compress(state, block);
        p += 64; rem -= 64;
    }
    /* final padding */
    size_t total_bits_hi = (len >> 61);
    size_t total_bits_lo = (len << 3);
    /* copy remainder */
    uint8_t tail[128];
    memset(tail, 0, sizeof(tail));
    if (rem) memcpy(tail, p, rem);
    tail[rem] = 0x80;
    size_t pad_len = (rem + 1 + 8 <= 64) ? (64) : (128);
    /* append 64-bit big-endian length (we'll support up to 2^64-1 bits) */
    uint64_t bits = (uint64_t)len * 8;
    tail[pad_len - 8] = (bits >> 56) & 0xFF;
    tail[pad_len - 7] = (bits >> 48) & 0xFF;
    tail[pad_len - 6] = (bits >> 40) & 0xFF;
    tail[pad_len - 5] = (bits >> 32) & 0xFF;
    tail[pad_len - 4] = (bits >> 24) & 0xFF;
    tail[pad_len - 3] = (bits >> 16) & 0xFF;
    tail[pad_len - 2] = (bits >> 8) & 0xFF;
    tail[pad_len - 1] = (bits >> 0) & 0xFF;
    /* compress final blocks */
    sha256_compress(state, tail);
    if (pad_len == 128) sha256_compress(state, tail + 64);
    /* produce digest big-endian */
    for (int i = 0; i < 8; ++i) {
        digest[i*4+0] = (state[i] >> 24) & 0xFF;
        digest[i*4+1] = (state[i] >> 16) & 0xFF;
        digest[i*4+2] = (state[i] >> 8) & 0xFF;
        digest[i*4+3] = (state[i] >> 0) & 0xFF;
    }
}

void sha256_to_hex(const uint8_t digest[32], char out_hex[65]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out_hex[i*2]   = hex[(digest[i] >> 4) & 0xF];
        out_hex[i*2+1] = hex[digest[i] & 0xF];
    }
    out_hex[64] = '\0';
}

int hex_to_bin(const char *hex, uint8_t *out, size_t outlen) {
    size_t hlen = strlen(hex);
    if (hlen % 2 != 0) return -1;
    size_t need = hlen/2;
    if (need > outlen) return -1;
    for (size_t i = 0; i < need; ++i) {
        char a = hex[i*2], b = hex[i*2+1];
        uint8_t va = (a >= '0' && a <= '9') ? a - '0' : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : (a >= 'A' && a <= 'F') ? a - 'A' + 10 : 255;
        uint8_t vb = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : (b >= 'A' && b <= 'F') ? b - 'A' + 10 : 255;
        if (va == 255 || vb == 255) return -1;
        out[i] = (va << 4) | vb;
    }
    return (int)need;
}

int ct_memcmp(const void *a, const void *b, size_t len) {
    const uint8_t *pa = (const uint8_t*)a, *pb = (const uint8_t*)b;
    unsigned diff = 0;
    for (size_t i = 0; i < len; ++i) diff |= (pa[i] ^ pb[i]);
    return diff == 0;
}
