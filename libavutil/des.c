/*
 * DES encryption/decryption
 * Copyright (c) 2007 Reimar Doeffinger
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <inttypes.h>
#include "avutil.h"
#include "common.h"
#include "intreadwrite.h"
#include "mem.h"
#include "des.h"

#define T(a, b, c, d, e, f, g, h) 64-a,64-b,64-c,64-d,64-e,64-f,64-g,64-h
static const uint8_t IP_shuffle[] = {
    T(58, 50, 42, 34, 26, 18, 10, 2),
    T(60, 52, 44, 36, 28, 20, 12, 4),
    T(62, 54, 46, 38, 30, 22, 14, 6),
    T(64, 56, 48, 40, 32, 24, 16, 8),
    T(57, 49, 41, 33, 25, 17,  9, 1),
    T(59, 51, 43, 35, 27, 19, 11, 3),
    T(61, 53, 45, 37, 29, 21, 13, 5),
    T(63, 55, 47, 39, 31, 23, 15, 7)
};
#undef T

#if CONFIG_SMALL || defined(GENTABLES)
#define T(a, b, c, d) 32-a,32-b,32-c,32-d
static const uint8_t P_shuffle[] = {
    T(16,  7, 20, 21),
    T(29, 12, 28, 17),
    T( 1, 15, 23, 26),
    T( 5, 18, 31, 10),
    T( 2,  8, 24, 14),
    T(32, 27,  3,  9),
    T(19, 13, 30,  6),
    T(22, 11,  4, 25)
};
#undef T
#endif

#define T(a, b, c, d, e, f, g) 64-a,64-b,64-c,64-d,64-e,64-f,64-g
static const uint8_t PC1_shuffle[] = {
    T(57, 49, 41, 33, 25, 17,  9),
    T( 1, 58, 50, 42, 34, 26, 18),
    T(10,  2, 59, 51, 43, 35, 27),
    T(19, 11,  3, 60, 52, 44, 36),
    T(63, 55, 47, 39, 31, 23, 15),
    T( 7, 62, 54, 46, 38, 30, 22),
    T(14,  6, 61, 53, 45, 37, 29),
    T(21, 13,  5, 28, 20, 12,  4)
};
#undef T

#define T(a, b, c, d, e, f) 56-a,56-b,56-c,56-d,56-e,56-f
static const uint8_t PC2_shuffle[] = {
    T(14, 17, 11, 24,  1,  5),
    T( 3, 28, 15,  6, 21, 10),
    T(23, 19, 12,  4, 26,  8),
    T(16,  7, 27, 20, 13,  2),
    T(41, 52, 31, 37, 47, 55),
    T(30, 40, 51, 45, 33, 48),
    T(44, 49, 39, 56, 34, 53),
    T(46, 42, 50, 36, 29, 32)
};
#undef T

#if CONFIG_SMALL
static const uint8_t S_boxes[8][32] = {
    {
    0x0e, 0xf4, 0x7d, 0x41, 0xe2, 0x2f, 0xdb, 0x18, 0xa3, 0x6a, 0xc6, 0xbc, 0x95, 0x59, 0x30, 0x87,
    0xf4, 0xc1, 0x8e, 0x28, 0x4d, 0x96, 0x12, 0x7b, 0x5f, 0xbc, 0x39, 0xe7, 0xa3, 0x0a, 0x65, 0xd0,
    }, {
    0x3f, 0xd1, 0x48, 0x7e, 0xf6, 0x2b, 0x83, 0xe4, 0xc9, 0x07, 0x12, 0xad, 0x6c, 0x90, 0xb5, 0x5a,
    0xd0, 0x8e, 0xa7, 0x1b, 0x3a, 0xf4, 0x4d, 0x21, 0xb5, 0x68, 0x7c, 0xc6, 0x09, 0x53, 0xe2, 0x9f,
    }, {
    0xda, 0x70, 0x09, 0x9e, 0x36, 0x43, 0x6f, 0xa5, 0x21, 0x8d, 0x5c, 0xe7, 0xcb, 0xb4, 0xf2, 0x18,
    0x1d, 0xa6, 0xd4, 0x09, 0x68, 0x9f, 0x83, 0x70, 0x4b, 0xf1, 0xe2, 0x3c, 0xb5, 0x5a, 0x2e, 0xc7,
    }, {
    0xd7, 0x8d, 0xbe, 0x53, 0x60, 0xf6, 0x09, 0x3a, 0x41, 0x72, 0x28, 0xc5, 0x1b, 0xac, 0xe4, 0x9f,
    0x3a, 0xf6, 0x09, 0x60, 0xac, 0x1b, 0xd7, 0x8d, 0x9f, 0x41, 0x53, 0xbe, 0xc5, 0x72, 0x28, 0xe4,
    }, {
    0xe2, 0xbc, 0x24, 0xc1, 0x47, 0x7a, 0xdb, 0x16, 0x58, 0x05, 0xf3, 0xaf, 0x3d, 0x90, 0x8e, 0x69,
    0xb4, 0x82, 0xc1, 0x7b, 0x1a, 0xed, 0x27, 0xd8, 0x6f, 0xf9, 0x0c, 0x95, 0xa6, 0x43, 0x50, 0x3e,
    }, {
    0xac, 0xf1, 0x4a, 0x2f, 0x79, 0xc2, 0x96, 0x58, 0x60, 0x1d, 0xd3, 0xe4, 0x0e, 0xb7, 0x35, 0x8b,
    0x49, 0x3e, 0x2f, 0xc5, 0x92, 0x58, 0xfc, 0xa3, 0xb7, 0xe0, 0x14, 0x7a, 0x61, 0x0d, 0x8b, 0xd6,
    }, {
    0xd4, 0x0b, 0xb2, 0x7e, 0x4f, 0x90, 0x18, 0xad, 0xe3, 0x3c, 0x59, 0xc7, 0x25, 0xfa, 0x86, 0x61,
    0x61, 0xb4, 0xdb, 0x8d, 0x1c, 0x43, 0xa7, 0x7e, 0x9a, 0x5f, 0x06, 0xf8, 0xe0, 0x25, 0x39, 0xc2,
    }, {
    0x1d, 0xf2, 0xd8, 0x84, 0xa6, 0x3f, 0x7b, 0x41, 0xca, 0x59, 0x63, 0xbe, 0x05, 0xe0, 0x9c, 0x27,
    0x27, 0x1b, 0xe4, 0x71, 0x49, 0xac, 0x8e, 0xd2, 0xf0, 0xc6, 0x9a, 0x0d, 0x3f, 0x53, 0x65, 0xb8,
    }
};
#else
/**
 * This table contains the results of applying both the S-box and P-shuffle.
 * It can be regenerated by compiling this file with -DCONFIG_SMALL -DTEST -DGENTABLES
 */
static const uint32_t S_boxes_P_shuffle[8][64] = {
    {
    0x00808200, 0x00000000, 0x00008000, 0x00808202, 0x00808002, 0x00008202, 0x00000002, 0x00008000,
    0x00000200, 0x00808200, 0x00808202, 0x00000200, 0x00800202, 0x00808002, 0x00800000, 0x00000002,
    0x00000202, 0x00800200, 0x00800200, 0x00008200, 0x00008200, 0x00808000, 0x00808000, 0x00800202,
    0x00008002, 0x00800002, 0x00800002, 0x00008002, 0x00000000, 0x00000202, 0x00008202, 0x00800000,
    0x00008000, 0x00808202, 0x00000002, 0x00808000, 0x00808200, 0x00800000, 0x00800000, 0x00000200,
    0x00808002, 0x00008000, 0x00008200, 0x00800002, 0x00000200, 0x00000002, 0x00800202, 0x00008202,
    0x00808202, 0x00008002, 0x00808000, 0x00800202, 0x00800002, 0x00000202, 0x00008202, 0x00808200,
    0x00000202, 0x00800200, 0x00800200, 0x00000000, 0x00008002, 0x00008200, 0x00000000, 0x00808002,
    },
    {
    0x40084010, 0x40004000, 0x00004000, 0x00084010, 0x00080000, 0x00000010, 0x40080010, 0x40004010,
    0x40000010, 0x40084010, 0x40084000, 0x40000000, 0x40004000, 0x00080000, 0x00000010, 0x40080010,
    0x00084000, 0x00080010, 0x40004010, 0x00000000, 0x40000000, 0x00004000, 0x00084010, 0x40080000,
    0x00080010, 0x40000010, 0x00000000, 0x00084000, 0x00004010, 0x40084000, 0x40080000, 0x00004010,
    0x00000000, 0x00084010, 0x40080010, 0x00080000, 0x40004010, 0x40080000, 0x40084000, 0x00004000,
    0x40080000, 0x40004000, 0x00000010, 0x40084010, 0x00084010, 0x00000010, 0x00004000, 0x40000000,
    0x00004010, 0x40084000, 0x00080000, 0x40000010, 0x00080010, 0x40004010, 0x40000010, 0x00080010,
    0x00084000, 0x00000000, 0x40004000, 0x00004010, 0x40000000, 0x40080010, 0x40084010, 0x00084000,
    },
    {
    0x00000104, 0x04010100, 0x00000000, 0x04010004, 0x04000100, 0x00000000, 0x00010104, 0x04000100,
    0x00010004, 0x04000004, 0x04000004, 0x00010000, 0x04010104, 0x00010004, 0x04010000, 0x00000104,
    0x04000000, 0x00000004, 0x04010100, 0x00000100, 0x00010100, 0x04010000, 0x04010004, 0x00010104,
    0x04000104, 0x00010100, 0x00010000, 0x04000104, 0x00000004, 0x04010104, 0x00000100, 0x04000000,
    0x04010100, 0x04000000, 0x00010004, 0x00000104, 0x00010000, 0x04010100, 0x04000100, 0x00000000,
    0x00000100, 0x00010004, 0x04010104, 0x04000100, 0x04000004, 0x00000100, 0x00000000, 0x04010004,
    0x04000104, 0x00010000, 0x04000000, 0x04010104, 0x00000004, 0x00010104, 0x00010100, 0x04000004,
    0x04010000, 0x04000104, 0x00000104, 0x04010000, 0x00010104, 0x00000004, 0x04010004, 0x00010100,
    },
    {
    0x80401000, 0x80001040, 0x80001040, 0x00000040, 0x00401040, 0x80400040, 0x80400000, 0x80001000,
    0x00000000, 0x00401000, 0x00401000, 0x80401040, 0x80000040, 0x00000000, 0x00400040, 0x80400000,
    0x80000000, 0x00001000, 0x00400000, 0x80401000, 0x00000040, 0x00400000, 0x80001000, 0x00001040,
    0x80400040, 0x80000000, 0x00001040, 0x00400040, 0x00001000, 0x00401040, 0x80401040, 0x80000040,
    0x00400040, 0x80400000, 0x00401000, 0x80401040, 0x80000040, 0x00000000, 0x00000000, 0x00401000,
    0x00001040, 0x00400040, 0x80400040, 0x80000000, 0x80401000, 0x80001040, 0x80001040, 0x00000040,
    0x80401040, 0x80000040, 0x80000000, 0x00001000, 0x80400000, 0x80001000, 0x00401040, 0x80400040,
    0x80001000, 0x00001040, 0x00400000, 0x80401000, 0x00000040, 0x00400000, 0x00001000, 0x00401040,
    },
    {
    0x00000080, 0x01040080, 0x01040000, 0x21000080, 0x00040000, 0x00000080, 0x20000000, 0x01040000,
    0x20040080, 0x00040000, 0x01000080, 0x20040080, 0x21000080, 0x21040000, 0x00040080, 0x20000000,
    0x01000000, 0x20040000, 0x20040000, 0x00000000, 0x20000080, 0x21040080, 0x21040080, 0x01000080,
    0x21040000, 0x20000080, 0x00000000, 0x21000000, 0x01040080, 0x01000000, 0x21000000, 0x00040080,
    0x00040000, 0x21000080, 0x00000080, 0x01000000, 0x20000000, 0x01040000, 0x21000080, 0x20040080,
    0x01000080, 0x20000000, 0x21040000, 0x01040080, 0x20040080, 0x00000080, 0x01000000, 0x21040000,
    0x21040080, 0x00040080, 0x21000000, 0x21040080, 0x01040000, 0x00000000, 0x20040000, 0x21000000,
    0x00040080, 0x01000080, 0x20000080, 0x00040000, 0x00000000, 0x20040000, 0x01040080, 0x20000080,
    },
    {
    0x10000008, 0x10200000, 0x00002000, 0x10202008, 0x10200000, 0x00000008, 0x10202008, 0x00200000,
    0x10002000, 0x00202008, 0x00200000, 0x10000008, 0x00200008, 0x10002000, 0x10000000, 0x00002008,
    0x00000000, 0x00200008, 0x10002008, 0x00002000, 0x00202000, 0x10002008, 0x00000008, 0x10200008,
    0x10200008, 0x00000000, 0x00202008, 0x10202000, 0x00002008, 0x00202000, 0x10202000, 0x10000000,
    0x10002000, 0x00000008, 0x10200008, 0x00202000, 0x10202008, 0x00200000, 0x00002008, 0x10000008,
    0x00200000, 0x10002000, 0x10000000, 0x00002008, 0x10000008, 0x10202008, 0x00202000, 0x10200000,
    0x00202008, 0x10202000, 0x00000000, 0x10200008, 0x00000008, 0x00002000, 0x10200000, 0x00202008,
    0x00002000, 0x00200008, 0x10002008, 0x00000000, 0x10202000, 0x10000000, 0x00200008, 0x10002008,
    },
    {
    0x00100000, 0x02100001, 0x02000401, 0x00000000, 0x00000400, 0x02000401, 0x00100401, 0x02100400,
    0x02100401, 0x00100000, 0x00000000, 0x02000001, 0x00000001, 0x02000000, 0x02100001, 0x00000401,
    0x02000400, 0x00100401, 0x00100001, 0x02000400, 0x02000001, 0x02100000, 0x02100400, 0x00100001,
    0x02100000, 0x00000400, 0x00000401, 0x02100401, 0x00100400, 0x00000001, 0x02000000, 0x00100400,
    0x02000000, 0x00100400, 0x00100000, 0x02000401, 0x02000401, 0x02100001, 0x02100001, 0x00000001,
    0x00100001, 0x02000000, 0x02000400, 0x00100000, 0x02100400, 0x00000401, 0x00100401, 0x02100400,
    0x00000401, 0x02000001, 0x02100401, 0x02100000, 0x00100400, 0x00000000, 0x00000001, 0x02100401,
    0x00000000, 0x00100401, 0x02100000, 0x00000400, 0x02000001, 0x02000400, 0x00000400, 0x00100001,
    },
    {
    0x08000820, 0x00000800, 0x00020000, 0x08020820, 0x08000000, 0x08000820, 0x00000020, 0x08000000,
    0x00020020, 0x08020000, 0x08020820, 0x00020800, 0x08020800, 0x00020820, 0x00000800, 0x00000020,
    0x08020000, 0x08000020, 0x08000800, 0x00000820, 0x00020800, 0x00020020, 0x08020020, 0x08020800,
    0x00000820, 0x00000000, 0x00000000, 0x08020020, 0x08000020, 0x08000800, 0x00020820, 0x00020000,
    0x00020820, 0x00020000, 0x08020800, 0x00000800, 0x00000020, 0x08020020, 0x00000800, 0x00020820,
    0x08000800, 0x00000020, 0x08000020, 0x08020000, 0x08020020, 0x08000000, 0x00020000, 0x08000820,
    0x00000000, 0x08020820, 0x00020020, 0x08000020, 0x08020000, 0x08000800, 0x08000820, 0x00000000,
    0x08020820, 0x00020800, 0x00020800, 0x00000820, 0x00000820, 0x00020020, 0x08000000, 0x08020800,
    },
};
#endif

static uint64_t shuffle(uint64_t in, const uint8_t *shuffle, int shuffle_len) {
    int i;
    uint64_t res = 0;
    for (i = 0; i < shuffle_len; i++)
        res += res + ((in >> *shuffle++) & 1);
    return res;
}

static uint64_t shuffle_inv(uint64_t in, const uint8_t *shuffle, int shuffle_len) {
    int i;
    uint64_t res = 0;
    shuffle += shuffle_len - 1;
    for (i = 0; i < shuffle_len; i++) {
        res |= (in & 1) << *shuffle--;
        in >>= 1;
    }
    return res;
}

static uint32_t f_func(uint32_t r, uint64_t k) {
    int i;
    uint32_t out = 0;
    // rotate to get first part of E-shuffle in the lowest 6 bits
    r = (r << 1) | (r >> 31);
    // apply S-boxes, those compress the data again from 8 * 6 to 8 * 4 bits
    for (i = 7; i >= 0; i--) {
        uint8_t tmp = (r ^ k) & 0x3f;
#if CONFIG_SMALL
        uint8_t v = S_boxes[i][tmp >> 1];
        if (tmp & 1) v >>= 4;
        out = (out >> 4) | (v << 28);
#else
        out |= S_boxes_P_shuffle[i][tmp];
#endif
        // get next 6 bits of E-shuffle and round key k into the lowest bits
        r = (r >> 4) | (r << 28);
        k >>= 6;
    }
#if CONFIG_SMALL
    out = shuffle(out, P_shuffle, sizeof(P_shuffle));
#endif
    return out;
}

/**
 * @brief rotate the two halves of the expanded 56 bit key each 1 bit left
 *
 * Note: the specification calls this "shift", so I kept it although
 * it is confusing.
 */
static uint64_t key_shift_left(uint64_t CDn) {
    uint64_t carries = (CDn >> 27) & 0x10000001;
    CDn <<= 1;
    CDn &= ~0x10000001;
    CDn |= carries;
    return CDn;
}

static void gen_roundkeys(uint64_t K[16], uint64_t key) {
    int i;
    // discard parity bits from key and shuffle it into C and D parts
    uint64_t CDn = shuffle(key, PC1_shuffle, sizeof(PC1_shuffle));
    // generate round keys
    for (i = 0; i < 16; i++) {
        CDn = key_shift_left(CDn);
        if (i > 1 && i != 8 && i != 15)
            CDn = key_shift_left(CDn);
        K[i] = shuffle(CDn, PC2_shuffle, sizeof(PC2_shuffle));
    }
}

static uint64_t des_encdec(uint64_t in, uint64_t K[16], int decrypt) {
    int i;
    // used to apply round keys in reverse order for decryption
    decrypt = decrypt ? 15 : 0;
    // shuffle irrelevant to security but to ease hardware implementations
    in = shuffle(in, IP_shuffle, sizeof(IP_shuffle));
    for (i = 0; i < 16; i++) {
        uint32_t f_res;
        f_res = f_func(in, K[decrypt ^ i]);
        in = (in << 32) | (in >> 32);
        in ^= f_res;
    }
    in = (in << 32) | (in >> 32);
    // reverse shuffle used to ease hardware implementations
    in = shuffle_inv(in, IP_shuffle, sizeof(IP_shuffle));
    return in;
}

AVDES *av_des_alloc(void)
{
    return av_mallocz(sizeof(struct AVDES));
}

int av_des_init(AVDES *d, const uint8_t *key, int key_bits, av_unused int decrypt) {
    if (key_bits != 64 && key_bits != 192)
        return -1;
    d->triple_des = key_bits > 64;
    gen_roundkeys(d->round_keys[0], AV_RB64(key));
    if (d->triple_des) {
        gen_roundkeys(d->round_keys[1], AV_RB64(key +  8));
        gen_roundkeys(d->round_keys[2], AV_RB64(key + 16));
    }
    return 0;
}

static void av_des_crypt_mac(AVDES *d, uint8_t *dst, const uint8_t *src, int count, uint8_t *iv, int decrypt, int mac) {
    uint64_t iv_val = iv ? AV_RB64(iv) : 0;
    while (count-- > 0) {
        uint64_t dst_val;
        uint64_t src_val = src ? AV_RB64(src) : 0;
        if (decrypt) {
            uint64_t tmp = src_val;
            if (d->triple_des) {
                src_val = des_encdec(src_val, d->round_keys[2], 1);
                src_val = des_encdec(src_val, d->round_keys[1], 0);
            }
            dst_val = des_encdec(src_val, d->round_keys[0], 1) ^ iv_val;
            iv_val = iv ? tmp : 0;
        } else {
            dst_val = des_encdec(src_val ^ iv_val, d->round_keys[0], 0);
            if (d->triple_des) {
                dst_val = des_encdec(dst_val, d->round_keys[1], 1);
                dst_val = des_encdec(dst_val, d->round_keys[2], 0);
            }
            iv_val = iv ? dst_val : 0;
        }
        AV_WB64(dst, dst_val);
        src += 8;
        if (!mac)
            dst += 8;
    }
    if (iv)
        AV_WB64(iv, iv_val);
}

void av_des_crypt(AVDES *d, uint8_t *dst, const uint8_t *src, int count, uint8_t *iv, int decrypt) {
    av_des_crypt_mac(d, dst, src, count, iv, decrypt, 0);
}

void av_des_mac(AVDES *d, uint8_t *dst, const uint8_t *src, int count) {
    av_des_crypt_mac(d, dst, src, count, (uint8_t[8]){0}, 0, 1);
}

#ifdef TEST
#include <stdlib.h>
#include <stdio.h>

#include "avutil_time.h"

static uint64_t rand64(void) {
    uint64_t r = rand();
    r = (r << 32) | rand();
    return r;
}

static const uint8_t test_key[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
static const DECLARE_ALIGNED(8, uint8_t, plain)[] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
static const DECLARE_ALIGNED(8, uint8_t, crypt)[] = {0x4a, 0xb6, 0x5b, 0x3d, 0x4b, 0x06, 0x15, 0x18};
static DECLARE_ALIGNED(8, uint8_t, tmp)[8];
static DECLARE_ALIGNED(8, uint8_t, large_buffer)[10002][8];
static const uint8_t cbc_key[] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
    0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23
};

static int run_test(int cbc, int decrypt) {
    AVDES d;
    int delay = cbc && !decrypt ? 2 : 1;
    uint64_t res;
    AV_WB64(large_buffer[0], 0x4e6f772069732074ULL);
    AV_WB64(large_buffer[1], 0x1234567890abcdefULL);
    AV_WB64(tmp,             0x1234567890abcdefULL);
    av_des_init(&d, cbc_key, 192, decrypt);
    av_des_crypt(&d, large_buffer[delay], large_buffer[0], 10000, cbc ? tmp : NULL, decrypt);
    res = AV_RB64(large_buffer[9999 + delay]);
    if (cbc) {
        if (decrypt)
            return res == 0xc5cecf63ecec514cULL;
        else
            return res == 0xcb191f85d1ed8439ULL;
    } else {
        if (decrypt)
            return res == 0x8325397644091a0aULL;
        else
            return res == 0xdd17e8b8b437d232ULL;
    }
}

int main(void) {
    AVDES d;
    int i;
#ifdef GENTABLES
    int j;
#endif
    uint64_t key[3];
    uint64_t data;
    uint64_t ct;
    uint64_t roundkeys[16];
    srand(av_gettime());
    key[0] = AV_RB64(test_key);
    data = AV_RB64(plain);
    gen_roundkeys(roundkeys, key[0]);
    if (des_encdec(data, roundkeys, 0) != AV_RB64(crypt)) {
        printf("Test 1 failed\n");
        return 1;
    }
    av_des_init(&d, test_key, 64, 0);
    av_des_crypt(&d, tmp, plain, 1, NULL, 0);
    if (memcmp(tmp, crypt, sizeof(crypt))) {
        printf("Public API decryption failed\n");
        return 1;
    }
    if (!run_test(0, 0) || !run_test(0, 1) || !run_test(1, 0) || !run_test(1, 1)) {
        printf("Partial Monte-Carlo test failed\n");
        return 1;
    }
    for (i = 0; i < 1000; i++) {
        key[0] = rand64(); key[1] = rand64(); key[2] = rand64();
        data = rand64();
        av_des_init(&d, (uint8_t*)key, 192, 0);
        av_des_crypt(&d, (uint8_t*)&ct, (uint8_t*)&data, 1, NULL, 0);
        av_des_init(&d, (uint8_t*)key, 192, 1);
        av_des_crypt(&d, (uint8_t*)&ct, (uint8_t*)&ct, 1, NULL, 1);
        if (ct != data) {
            printf("Test 2 failed\n");
            return 1;
        }
    }
#ifdef GENTABLES
    printf("static const uint32_t S_boxes_P_shuffle[8][64] = {\n");
    for (i = 0; i < 8; i++) {
        printf("    {");
        for (j = 0; j < 64; j++) {
            uint32_t v = S_boxes[i][j >> 1];
            v = j & 1 ? v >> 4 : v & 0xf;
            v <<= 28 - 4 * i;
            v = shuffle(v, P_shuffle, sizeof(P_shuffle));
            printf((j & 7) == 0 ? "\n    " : " ");
            printf("0x%08X,", v);
        }
        printf("\n    },\n");
    }
    printf("};\n");
#endif
    return 0;
}
#endif
