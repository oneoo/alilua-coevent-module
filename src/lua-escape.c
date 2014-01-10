#include "coevent.h"

int lua_f_escape_uri(lua_State *L)
{
    size_t  len = 0,
            dlen = 0;
    uintptr_t escape = 0;
    u_char  *src = NULL,
             *dst = NULL;

    if(lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }

    src = (u_char *) luaL_checklstring(L, 1, &len);

    if(len == 0) {
        return 1;
    }

    escape = 2 * urlencode(NULL, src, len, ESCAPE_URI);

    if(escape) {
        dlen = escape + len;
        dst = lua_newuserdata(L, dlen);
        urlencode(dst, src, len, ESCAPE_URI);
        lua_pushlstring(L, (char *) dst, dlen);
    }

    return 1;
}

int lua_f_unescape_uri(lua_State *L)
{
    size_t  len = 0,
            dlen = 0;
    u_char  *p = NULL,
             *src = NULL,
              *dst = NULL;

    if(lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }

    src = (u_char *) luaL_checklstring(L, 1, &len);
    /* the unescaped string can only be smaller */
    dlen = len;
    p = lua_newuserdata(L, dlen);
    dst = p;
    urldecode(&dst, &src, len, UNESCAPE_URI_COMPONENT);
    lua_pushlstring(L, (char *) p, dst - p);
    return 1;
}
