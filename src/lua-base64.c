#include "coevent.h"

int lua_f_base64_encode(lua_State *L)
{
    const unsigned char *src = NULL;
    size_t slen = 0;

    if(lua_isnil(L, 1)) {
        src = (const unsigned char *) "";

    } else {
        src = (const unsigned char *) luaL_checklstring(L, 1, &slen);
    }

    unsigned char *end = large_malloc(base64_encoded_length(slen));
    int nlen = base64_encode(end, src, slen);
    lua_pushlstring(L, (char *) end, nlen);
    free(end);
    return 1;
}

int lua_f_base64_decode(lua_State *L)
{
    const unsigned char *src = NULL;
    size_t slen = 0;

    if(lua_isnil(L, 1)) {
        src = (const unsigned char *) "";

    } else {
        src = (unsigned char *) luaL_checklstring(L, 1, &slen);
    }

    unsigned char *end = large_malloc(base64_decoded_length(slen));
    int nlen = base64_decode(end, src, slen);
    lua_pushlstring(L, (char *) end, nlen);
    free(end);
    return 1;
}
