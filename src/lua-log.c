#include "coevent.h"

static char buf[1024] = {0};

int lua_f_log(lua_State *L)
{
    update_time();
    lua_Debug ar;
    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "nSl", &ar);

    if(!ar.source) {
        return 0;
    }

    snprintf(buf, 1024, "%s:%d", ar.source + 1, ar.currentline);
    int level = WARN;
    int gn = 1;

    if(lua_isnumber(L, 1)) {
        level = lua_tonumber(L, 1);
        gn++;
    }

    if(lua_isstring(L, gn)) {
        _LOGF(level, buf, "%s", lua_tostring(L, gn));
    }

    sync_logs(LOGF_T);
    return 0;
}

int lua_f_open_log(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need filepath");
        return 2;
    }

    LOGF_T = open_log(lua_tostring(L, 1), 4096);
    lua_pushboolean(L, LOGF_T ? 1 : 0);
    return 1;
}
