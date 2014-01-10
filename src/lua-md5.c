#include "coevent.h"

int lua_f_md5(lua_State *L)
{
    const unsigned char *src = NULL;
    size_t slen = 0;

    if(lua_isnil(L, 1)) {
        src = (unsigned char *) "";

    } else {
        src = (unsigned char *) luaL_checklstring(L, 1, &slen);
    }

    unsigned char output[32];
    md5(src, slen, (unsigned char *)&output);

    lua_pushlstring(L, (unsigned char *) &output, 32);
    return 1;
}
