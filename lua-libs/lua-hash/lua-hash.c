#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/timeb.h>

#include <lua.h>
#include <lauxlib.h>

#define VERSION "0.1"

static uint32_t djb2_hash(unsigned char *str)
{
    uint32_t hash = 5381;
    int c = 0;

    while(c = *str++) {
        hash = ((hash << 5) + hash) + c;    /* hash * 33 + c */
    }

    return hash;
}

static uint32_t fnv1a_32(const void *key, uint32_t len)
{
    unsigned char *data = (unsigned char *)key;
    uint32_t rv = 0x811c9dc5U;
    uint32_t i = 0;

    for(i = 0; i < len; i++) {
        rv = (rv ^ (unsigned char) data[i]) * 16777619;
    }

    return rv;
}

static uint32_t fnv1a_64(const void *key, uint32_t len)
{
    unsigned char *data = (unsigned char *)key;
    uint64_t rv = 0xcbf29ce484222325UL;
    uint32_t i = 0;

    for(i = 0; i < len; i++) {
        rv = (rv ^ (unsigned char) data[i]) * 1099511628211UL;
    }

    return (uint32_t) rv;
}

#if defined(__x86_64__)

// -------------------------------------------------------------------
//
// The same caveats as 32-bit MurmurHash2 apply here - beware of alignment
// and endian-ness issues if used across multiple platforms.
//
// 64-bit hash for 64-bit platforms

static uint64_t MurmurHash64A(const void *key, int len, unsigned int seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t *data = (const uint64_t *)key;
    const uint64_t *end = data + (len / 8);

    while(data != end) {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char *data2 = (const unsigned char *)data;

    switch(len & 7) {
        case 7:
            h ^= ((uint64_t)data2[6]) << 48;

        case 6:
            h ^= ((uint64_t)data2[5]) << 40;

        case 5:
            h ^= ((uint64_t)data2[4]) << 32;

        case 4:
            h ^= ((uint64_t)data2[3]) << 24;

        case 3:
            h ^= ((uint64_t)data2[2]) << 16;

        case 2:
            h ^= ((uint64_t)data2[1]) << 8;

        case 1:
            h ^= ((uint64_t)data2[0]);
            h *= m;
    };

    h ^= h >> r;

    h *= m;

    h ^= h >> r;

    return h;
}

#elif defined(__i386__)

// -------------------------------------------------------------------
//
// Note - This code makes a few assumptions about how your machine behaves -
//
// 1. We can read a 4-byte value from any address without crashing
// 2. sizeof(int) == 4
//
// And it has a few limitations -
//
// 1. It will not work incrementally.
// 2. It will not produce the same results on little-endian and big-endian
//    machines.

static unsigned int MurmurHash2(const void *key, int len, unsigned int seed)
{
    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well.

    const unsigned int m = 0x5bd1e995;
    const int r = 24;

    // Initialize the hash to a 'random' value

    unsigned int h = seed ^ len;

    // Mix 4 bytes at a time into the hash

    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        unsigned int k = *(unsigned int *)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    // Handle the last few bytes of the input array

    switch(len) {
        case 3:
            h ^= data[2] << 16;

        case 2:
            h ^= data[1] << 8;

        case 1:
            h ^= data[0];
            h *= m;
    };

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated.

    h ^= h >> 13;

    h *= m;

    h ^= h >> 15;

    return h;
}

#else

// -------------------------------------------------------------------
//
// Same as MurmurHash2, but endian- and alignment-neutral.
// Half the speed though, alas.

static unsigned int MurmurHashNeutral2(const void *key, int len, unsigned int seed)
{
    const unsigned int m = 0x5bd1e995;
    const int r = 24;

    unsigned int h = seed ^ len;

    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        unsigned int k;

        k  = data[0];
        k |= data[1] << 8;
        k |= data[2] << 16;
        k |= data[3] << 24;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    switch(len) {
        case 3:
            h ^= data[2] << 16;

        case 2:
            h ^= data[1] << 8;

        case 1:
            h ^= data[0];
            h *= m;
    };

    h ^= h >> 13;

    h *= m;

    h ^= h >> 15;

    return h;
}

#endif

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

#define __i686__
inline u32 CrapWow(const u8 *key, u32 len, u32 seed)
{
#if  ( defined(__i686__) )
    // esi = k, ebx = h
    u32 hash = 0;
    asm(
        "leal 0x5052acdb(%%ecx,%%esi), %%esi\n"
        "movl %%ecx, %%ebx\n"
        "cmpl $8, %%ecx\n"
        "jb DW%=\n"
        "QW%=:\n"
        "movl $0x5052acdb, %%eax\n"
        "mull (%%edi)\n"
        "addl $-8, %%ecx\n"
        "xorl %%eax, %%ebx\n"
        "xorl %%edx, %%esi\n"
        "movl $0x57559429, %%eax\n"
        "mull 4(%%edi)\n"
        "xorl %%eax, %%esi\n"
        "xorl %%edx, %%ebx\n"
        "addl $8, %%edi\n"
        "cmpl $8, %%ecx\n"
        "jae QW%=\n"
        "DW%=:\n"
        "cmpl $4, %%ecx\n"
        "jb B%=\n"
        "movl $0x5052acdb, %%eax\n"
        "mull (%%edi)\n"
        "addl $4, %%edi\n"
        "xorl %%eax, %%ebx\n"
        "addl $-4, %%ecx\n"
        "xorl %%edx, %%esi\n"
        "B%=:\n"
        "testl %%ecx, %%ecx\n"
        "jz F%=\n"
        "shll $3, %%ecx\n"
        "movl $1, %%edx\n"
        "movl $0x57559429, %%eax\n"
        "shll %%cl, %%edx\n"
        "addl $-1, %%edx\n"
        "andl (%%edi), %%edx\n"
        "mull %%edx\n"
        "xorl %%eax, %%esi\n"
        "xorl %%edx, %%ebx\n"
        "F%=:\n"
        "leal 0x5052acdb(%%esi), %%edx\n"
        "xorl %%ebx, %%edx\n"
        "movl $0x5052acdb, %%eax\n"
        "mull %%edx\n"
        "xorl %%ebx, %%eax\n"
        "xorl %%edx, %%esi\n"
        "xorl %%esi, %%eax\n"
        : "=a"(hash), "=c"(len), "=S"(len), "=D"(key)
        : "c"(len), "S"(seed), "D"(key)
        : "%ebx", "%edx", "cc"
    );
    return hash;
#else
#define cwfold( a, b, lo, hi ) { p = (u32)(a) * (u64)(b); lo ^= (u32)p; hi ^= (u32)(p >> 32); }
#define cwmixa( in ) { cwfold( in, m, k, h ); }
#define cwmixb( in ) { cwfold( in, n, h, k ); }
    const u32 m = 0x57559429, n = 0x5052acdb, *key4 = (const u32 *)key;
    u32 h = len, k = len + seed + n;
    u64 p = 0;

    while(len >= 8) {
        cwmixb(key4[0]) cwmixa(key4[1]) key4 += 2;
        len -= 8;
    }

    if(len >= 4) {
        cwmixb(key4[0]) key4 += 1;
        len -= 4;
    }

    if(len) {
        cwmixa(key4[0] & ((1 << (len * 8)) - 1))
    }

    cwmixb(h ^ (k + n))
    return k ^ h;
#endif
}

#define cast(t, exp)    ((t)(exp))
#define cast_byte(i)    cast(unsigned char, (i))
static unsigned int luaS_hash(const char *str, size_t l, unsigned int seed)
{
    unsigned int h = seed ^ cast(unsigned int, l);
    size_t l1;
    size_t step = (l >> 5) + 1;

    for(l1 = l; l1 >= step; l1 -= step) {
        h = h ^ ((h << 5) + (h >> 2) + cast_byte(str[l1 - 1]));
    }

    return h;
}


int lua_f_djb2_hash(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    const char *str = lua_tostring(L, 1);

    uint32_t hash = 5381;
    int c = 0;

    while(c = *str++) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    lua_pushnumber(L, hash);

    return 1;
}

int lua_f_fnv1a_32(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    size_t len = 0;
    const char *str = lua_tolstring(L, 1, &len);

    lua_pushnumber(L, fnv1a_32(str, len));

    return 1;
}

int lua_f_fnv1a_64(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    size_t len = 0;
    const char *str = lua_tolstring(L, 1, &len);

    lua_pushnumber(L, fnv1a_64(str, len));

    return 1;
}

int lua_f_murmurhash(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    size_t len = 0;
    const char *str = lua_tolstring(L, 1, &len);

#if defined(__x86_64__)
    lua_pushnumber(L, (uint32_t)MurmurHash64A(str, len, 0));

#elif defined(__i386__)
    lua_pushnumber(L, (uint32_t)MurmurHash2(str, len, 0));

#else
    lua_pushnumber(L, (uint32_t)MurmurHashNeutral2(str, len, 0));

#endif

    return 1;
}

int lua_f_crapwow_hash(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    size_t len = 0;
    const char *str = lua_tolstring(L, 1, &len);

    u32 seed = 0xabcdef11;
    lua_pushnumber(L, CrapWow((uint8_t *)str, len, seed));

    return 1;
}

int lua_f_lua_hash(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need string");
        return 2;
    }

    size_t len = 0;
    const char *str = lua_tolstring(L, 1, &len);

    lua_pushnumber(L, luaS_hash(str, len, 0x57559429));

    return 1;
}


LUALIB_API int luaopen_hash(lua_State *L)
{
    lua_register(L, "djb2_hash", lua_f_djb2_hash);
    lua_register(L, "fnv1a_32", lua_f_fnv1a_32);
    lua_register(L, "fnv1a_64", lua_f_fnv1a_64);
    lua_register(L, "murmurhash", lua_f_murmurhash);
    lua_register(L, "crapwow_hash", lua_f_crapwow_hash);
    lua_register(L, "lua_hash", lua_f_lua_hash);

    return 0;
}
