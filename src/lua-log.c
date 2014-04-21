#include "coevent.h"

static char tbuf_1024[1024] = {0};
static char tbuf[8192] = {0};

int lua_f_log(lua_State *L)
{
    update_time();
    lua_Debug ar;
    lua_getstack(L, 1, &ar);
    lua_getinfo(L, "nSl", &ar);

    if(!ar.source) {
        return 0;
    }

    snprintf(tbuf_1024, 1024, "%s:%d", ar.source + 1, ar.currentline);
    int nargs = lua_gettop(L);
    int level = WARN;
    int gn = 1;

    if(nargs > 1 && lua_isnumber(L, 1)) {
        level = lua_tonumber(L, 1);
        gn++;
    }

    int blen = 0;
    size_t len = 0;

    if(lua_istable(L, gn)) {
        blen = lua_calc_strlen_in_table(L, gn, 2, 0 /* strict */);

        if(blen < 1) {
            return;
        }

        char *buf = (char *)&tbuf;

        if(blen > 8192) {
            buf = malloc(blen);

            if(!buf) {
                return;
            }

            lua_copy_str_in_table(L, gn, buf);
            buf[blen] = '\0';
            _LOGF(level, tbuf_1024, "%s", buf);
            free(buf);

        } else {
            lua_copy_str_in_table(L, gn, buf);
            buf[blen] = '\0';
            _LOGF(level, tbuf_1024, "%s", buf);
        }

    } else {
        char *buf = (char *)&tbuf;
        const char *data = NULL;
        int i = 0;

        for(i = gn; i <= nargs; i++) {
            if(lua_isboolean(L, i)) {
                if(blen + 6 > 8192) {
                    break;
                }

                if(lua_toboolean(L, i)) {
                    memcpy(buf + blen, "true", 4);
                    blen += 4;

                } else {
                    memcpy(buf + blen, "false", 5);
                    blen += 5;
                }

            } else if(lua_isnil(L, i)) {
                if(blen + 4 > 8192) {
                    break;
                }

                memcpy(buf + blen, "nil", 3);
                blen += 3;

            } else {
                data = lua_tolstring(L, i, &len);

                if(blen + len + 1 > 8192) {
                    break;
                }

                memcpy(buf + blen, data, len);
                blen += len;
            }

            memcpy(buf + blen, "\t", 1);
            blen += 1;
        }

        buf[blen] = '\0';
        _LOGF(level, tbuf_1024, "%s", buf);
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
