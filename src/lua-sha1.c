#include "coevent.h"

int lua_f_sha1bin(lua_State *L)
{
    const unsigned char *src = NULL;
    size_t slen = 0;

    if(lua_isnil(L, 1)) {
        src = (unsigned char *) "";

    } else {
        src = (unsigned char *) luaL_checklstring(L, 1, &slen);
    }

    unsigned char output[20];
    sha1(src, slen, (unsigned char *)&output);

    lua_pushlstring(L, (char *) &output, 20);
    return 1;
}

int lua_f_hmac_sha1(lua_State *L)
{
    const unsigned char *src = NULL;
    const unsigned char *key = NULL;
    size_t slen = 0;
    size_t klen = 0;
    int raw_output = 0;

    if(lua_isnil(L, 1)) {
        src = (unsigned char *) "";

    } else {
        src = (unsigned char *) luaL_checklstring(L, 1, &slen);
    }

    if(lua_isnil(L, 2)) {
        key = (unsigned char *) "";

    } else {
        key = (unsigned char *) luaL_checklstring(L, 2, &klen);
    }

    if(lua_isboolean(L, 3)) {
        raw_output = lua_toboolean(L, 3);
    }

    unsigned char output[20] = {0};
    sha1_hmac(key, klen, src, slen, (unsigned char *)&output);

    if(raw_output) {
        lua_pushlstring(L, (char *) &output, 20);
    }

    unsigned char o2[40] = {0};
    int i = 0;

    for(i = 0; i < 20; i++) {
        sprintf(o2 + (i * 2), "%.2x", (unsigned int) output[i]);
    }

    lua_pushlstring(L, (char *) &o2, 40);
    return 1;
}

