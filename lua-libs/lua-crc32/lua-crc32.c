#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/types.h>

#include <lua.h>
#include <lauxlib.h>

#define VERSION "0.1"
#define AUTORELOAD

/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/*
 * The code and lookup tables are based on the algorithm
 * described at http://www.w3.org/TR/PNG/
 *
 * The 256 element lookup table takes 1024 bytes, and it may be completely
 * cached after processing about 30-60 bytes of data.  So for short data
 * we use the 16 element lookup table that takes only 64 bytes and align it
 * to CPU cache line size.  Of course, the small table adds code inside
 * CRC32 loop, but the cache misses overhead is bigger than overhead of
 * the additional code.  For example, ngx_crc32_short() of 16 bytes of data
 * takes half as much CPU clocks than ngx_crc32_long().
 */


static uint32_t  ngx_crc32_table16[] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};


static uint32_t  ngx_crc32_table256[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};


static uint32_t *ngx_crc32_table_short = ngx_crc32_table16;

static int ngx_cacheline_size = 32;
#if (( __i386__ || __amd64__ ) && ( __GNUC__ || __INTEL_COMPILER ))


static void ngx_cpuid(uint32_t i, uint32_t *buf);


#if ( __i386__ )

static void ngx_cpuid(uint32_t i, uint32_t *buf)
{

    /*
     * we could not use %ebx as output parameter if gcc builds PIC,
     * and we could not save %ebx on stack, because %esp is used,
     * when the -fomit-frame-pointer optimization is specified.
     */

    __asm__(

        "    mov    %%ebx, %%esi;  "

        "    cpuid;                "
        "    mov    %%eax, (%1);   "
        "    mov    %%ebx, 4(%1);  "
        "    mov    %%edx, 8(%1);  "
        "    mov    %%ecx, 12(%1); "

        "    mov    %%esi, %%ebx;  "

        : : "a"(i), "D"(buf) : "ecx", "edx", "esi", "memory");
}


#else /* __amd64__ */


static void ngx_cpuid(uint32_t i, uint32_t *buf)
{
    uint32_t  eax, ebx, ecx, edx;

    __asm__(

        "cpuid"

        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(i));

    buf[0] = eax;
    buf[1] = ebx;
    buf[2] = edx;
    buf[3] = ecx;
}


#endif


/* auto detect the L2 cache line size of modern and widespread CPUs */

void ngx_cpuinfo(void)
{
    u_char    *vendor;
    uint32_t   vbuf[5], cpu[4], model;

    vbuf[0] = 0;
    vbuf[1] = 0;
    vbuf[2] = 0;
    vbuf[3] = 0;
    vbuf[4] = 0;

    ngx_cpuid(0, vbuf);

    vendor = (u_char *) &vbuf[1];

    if(vbuf[0] == 0) {
        return;
    }

    ngx_cpuid(1, cpu);

    if(strcmp(vendor, "GenuineIntel") == 0) {

        switch((cpu[0] & 0xf00) >> 8) {

                /* Pentium */
            case 5:
                ngx_cacheline_size = 32;
                break;

                /* Pentium Pro, II, III */
            case 6:
                ngx_cacheline_size = 32;

                model = ((cpu[0] & 0xf0000) >> 8) | (cpu[0] & 0xf0);

                if(model >= 0xd0) {
                    /* Intel Core, Core 2, Atom */
                    ngx_cacheline_size = 64;
                }

                break;

                /*
                 * Pentium 4, although its cache line size is 64 bytes,
                 * it prefetches up to two cache lines during memory read
                 */
            case 15:
                ngx_cacheline_size = 128;
                break;
        }

    } else if(strcmp(vendor, "AuthenticAMD") == 0) {
        ngx_cacheline_size = 64;
    }
}

#else


void ngx_cpuinfo(void)
{
}


#endif


#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

static int crc32_table_inited = 0;
static int ngx_crc32_table_init(void)
{
    void  *p;
    ngx_cpuinfo();

    if(crc32_table_inited) {
        return 0;
    }

    crc32_table_inited = 1;

    if(((uintptr_t) ngx_crc32_table_short
        & ~((uintptr_t) ngx_cacheline_size - 1))
       == (uintptr_t) ngx_crc32_table_short) {
        return 1;
    }

    p = malloc(16 * sizeof(uint32_t) + ngx_cacheline_size);

    if(p == NULL) {
        return 0;
    }

    p = ngx_align_ptr(p, ngx_cacheline_size);

    memcpy(p, ngx_crc32_table16, 16 * sizeof(uint32_t));

    ngx_crc32_table_short = p;

    return 1;
}


static uint32_t ngx_crc32_short(u_char *p, size_t len)
{
    u_char    c;
    uint32_t  crc;

    crc = 0xffffffff;

    while(len--) {
        c = *p++;
        crc = ngx_crc32_table_short[(crc ^ (c & 0xf)) & 0xf] ^ (crc >> 4);
        crc = ngx_crc32_table_short[(crc ^ (c >> 4)) & 0xf] ^ (crc >> 4);
    }

    return crc ^ 0xffffffff;
}

static uint32_t ngx_crc32_long(u_char *p, size_t len)
{
    uint32_t  crc;

    crc = 0xffffffff;

    while(len--) {
        crc = ngx_crc32_table256[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    }

    return crc ^ 0xffffffff;
}


#define ngx_crc32_init(crc) crc = 0xffffffff


static void ngx_crc32_update(uint32_t *crc, u_char *p, size_t len)
{
    uint32_t  c;

    c = *crc;

    while(len--) {
        c = ngx_crc32_table256[(c ^ *p++) & 0xff] ^ (c >> 8);
    }

    *crc = c;
}


#define ngx_crc32_final(crc) crc ^= 0xffffffff

static int ngx_http_lua_ngx_crc32_short(lua_State *L)
{
    u_char                  *p;
    size_t                   len;

    if(lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument, but got %d",
                          lua_gettop(L));
    }

    p = (u_char *) luaL_checklstring(L, 1, &len);

    lua_pushnumber(L, (lua_Number) ngx_crc32_short(p, len));
    return 1;
}


static int ngx_http_lua_ngx_crc32_long(lua_State *L)
{
    u_char                  *p;
    size_t                   len;

    if(lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument, but got %d",
                          lua_gettop(L));
    }

    p = (u_char *) luaL_checklstring(L, 1, &len);

    lua_pushnumber(L, (lua_Number) ngx_crc32_long(p, len));
    return 1;
}

static int lua_f_crc32_update(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    uint32_t *crc = (uint32_t *) lua_touserdata(L, 1);
    size_t len = 0;

    u_char *p = (u_char *) luaL_checklstring(L, 2, &len);

    ngx_crc32_update(crc, p, len);

    return 0;
}
static int lua_f_crc32_final(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    uint32_t *crc = (uint32_t *) lua_touserdata(L, 1);

    ngx_crc32_final(*crc);

    lua_pushnumber(L, (lua_Number)*crc);

    return 1;
}
static int lua_f_crc32_gc(lua_State *L)
{
    return 0;
}
static int lua_f_crc32_init(lua_State *L)
{
    uint32_t *crc = (uint32_t *) lua_newuserdata(L, sizeof(uint32_t));
    luaL_getmetatable(L, "crc32:init");
    lua_setmetatable(L, -2);

    //memset(crc,0,sizeof(uint32_t));
    ngx_crc32_init(*crc);

    return 1;
}
static const luaL_reg M[] = {
    {"update", lua_f_crc32_update},
    {"final", lua_f_crc32_final},

    {"__gc", lua_f_crc32_gc},

    {NULL, NULL}
};

static const struct luaL_reg thislib[] = {
    {"init", lua_f_crc32_init},

    {"short", ngx_http_lua_ngx_crc32_short},
    {"long", ngx_http_lua_ngx_crc32_long},
    {NULL, NULL}
};

/* This is luaL_setfuncs() from Lua 5.2 alpha */
static void setfuncs(lua_State *L, const luaL_Reg *l, int nup)
{
    luaL_checkstack(L, nup, "too many upvalues");

    for(; l && l->name; l++) {    /* fill the table with given functions */
        int i = 0;

        for(i = 0; i < nup; i++) {    /* copy upvalues to the top */
            lua_pushvalue(L, -nup);
        }

        lua_pushcclosure(L, l->func, nup);    /* closure with those upvalues */
        lua_setfield(L, - (nup + 2), l->name);
    }

    lua_pop(L, nup);    /* remove upvalues */
}
/* End of luaL_setfuncs() from Lua 5.2 alpha */
LUALIB_API int luaopen_crc32(lua_State *L)
{
    luaL_register(L, "crc32", thislib);

    lua_pushstring(L, VERSION);
    lua_setfield(L, -2, "_VERSION");

    luaL_newmetatable(L, "crc32:init");
    lua_pushvalue(L, lua_upvalueindex(1));
    setfuncs(L, M, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    ngx_crc32_table_init();

    return 1;
}
