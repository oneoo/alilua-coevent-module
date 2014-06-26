#include "coevent.h"

int lua_f_escape_uri(lua_State *L)
{
    size_t  len = 0,
            dlen = 0;
    uintptr_t escape = 0;
    u_char  *src = NULL,
             *dst = NULL;

    if(lua_gettop(L) < 1) {
        return luaL_error(L, "expecting one argument");
    }

    src = (u_char *) luaL_checklstring(L, 1, &len);

    if(len == 0) {
        return 1;
    }

    int raw_encode = 0;

    if(lua_gettop(L) == 2 && lua_isboolean(L, 2)) {
        raw_encode = lua_toboolean(L, 2);
    }

    escape = 3 * len;

    if(escape) {
        dst = lua_newuserdata(L, escape);
        dlen = urlencode(dst, src, len, raw_encode == 0 ? ESCAPE_URL : RAW_ESCAPE_URL);
        lua_pushlstring(L, (char *) dst, dlen);
    }

    return 1;
}

int lua_f_unescape_uri(lua_State *L)
{
    size_t  len = 0,
            dlen = 0;
    u_char  *p = NULL,
             *src = NULL;

    if(lua_gettop(L) < 1) {
        return luaL_error(L, "expecting one argument");
    }

    int raw_encode = 0;

    if(lua_gettop(L) == 2 && lua_isboolean(L, 2)) {
        raw_encode = lua_toboolean(L, 2);
    }

    src = (u_char *) luaL_checklstring(L, 1, &len);
    /* the unescaped string can only be smaller */
    p = lua_newuserdata(L, len);
    dlen = urldecode(&p, &src, len, raw_encode == 0 ? UNESCAPE_URL : RAW_UNESCAPE_URL);
    lua_pushlstring(L, (char *) p, dlen);
    return 1;
}
