#include "coevent.h"

int lua_f_time(lua_State *L)
{
    lua_pushnumber(L, time(NULL));
    return 1;
}

int lua_f_longtime(lua_State *L)
{
    struct timeb t;
    ftime(&t);
    lua_pushnumber(L, 1000 * t.time + t.millitm);
    return 1;
}


typedef struct {
    void *L;
    long timeout;
    void *uper;
    void *next;
} sleep_timeout_t;

#define TIME_BUCKET_SIZE 6000
static sleep_timeout_t *timeout_links[TIME_BUCKET_SIZE] = {0};
static sleep_timeout_t *timeout_link_ends[TIME_BUCKET_SIZE] = {0};
static unsigned long now_4sleep = 0;
static unsigned long l_now = 0;

int check_lua_sleep_timeouts()
{
    l_now = longtime() / 10;

    if(now_4sleep == 0) {
        now_4sleep = l_now - 5;
    }

    do {
        int k = now_4sleep % TIME_BUCKET_SIZE;

        sleep_timeout_t *m = timeout_links[k], *n = NULL;
        lua_State *L = NULL;

        while(m) {
            n = m;
            m = m->next;

            if(l_now >= n->timeout) { // timeout
                {
                    if(n->uper) {
                        ((sleep_timeout_t *) n->uper)->next = n->next;

                    } else {
                        timeout_links[k] = n->next;
                    }

                    if(n->next) {
                        ((sleep_timeout_t *) n->next)->uper = n->uper;

                    } else {
                        timeout_link_ends[k] = n->uper;
                    }

                    L = n->L;
                    free(n);
                }

                if(L) {
                    lua_f_lua_uthread_resume_in_c(L, 0);
                    l_now = longtime() / 10;
                }

                L = NULL;
            }
        }

        if(now_4sleep < l_now) {
            now_4sleep++;
            continue;
        }
    } while(now_4sleep < l_now);

    return 1;
}

int _lua_sleep(lua_State *L, int msec)
{
    sleep_timeout_t *n = malloc(sizeof(sleep_timeout_t));

    if(!n) {
        return 0;
    }

    n->timeout = (longtime() + msec) / 10;
    n->uper = NULL;
    n->next = NULL;
    n->L = L;

    int k = n->timeout % TIME_BUCKET_SIZE;

    if(timeout_link_ends[k] == NULL) {
        timeout_links[k] = n;
        timeout_link_ends[k] = n;

    } else { // add to link end
        timeout_link_ends[k]->next = n;
        n->uper = timeout_link_ends[k];
        timeout_link_ends[k] = n;
    }

    return lua_yield(L, 0);
}

int lua_f_sleep(lua_State *L)
{
    if(!lua_isnumber(L, 1)) {
        return 0;
    }

    int msec = lua_tonumber(L, 1);

    if(msec < 1) {
        return lua_f_coroutine_swop(L);
    }

    return _lua_sleep(L, msec);
}

size_t lua_calc_strlen_in_table(lua_State *L, int index, int arg_i, unsigned strict)
{
    double key = 0;
    int max = 0;
    int i = 0;
    int type = 0;
    size_t size = 0;
    size_t len = 0;
    const char *msg = NULL;

    if(index < 0) {
        index = lua_gettop(L) + index + 1;
    }

    max = 0;
    lua_pushnil(L);    /* stack: table key */

    while(lua_next(L, index) != 0) {       /* stack: table key value */
        if(lua_type(L, -2) == LUA_TNUMBER) {
            key = lua_tonumber(L, -2);

            if(floor(key) == key && key >= 1) {
                if(key > max) {
                    max = key;
                }

                lua_pop(L, 1);    /* stack: table key */
                continue;
            }
        }

        /* not an array (non positive integer key) */
        lua_pop(L, 2);    /* stack: table */
        //msg = lua_pushfstring(L, "non-array table found"); /// commented by oneoo
        //luaL_argerror(L, arg_i, msg);
        return 0;
    }

    size = 0;

    for(i = 1; i <= max; i++) {
        lua_rawgeti(L, index, i);    /* stack: table value */
        type = lua_type(L, -1);

        switch(type) {
            case LUA_TNUMBER:
            case LUA_TSTRING:
                lua_tolstring(L, -1, &len);
                size += len;
                break;

            case LUA_TNIL:
                if(strict) {
                    goto bad_type;
                }

                size += sizeof("nil") - 1;
                break;

            case LUA_TBOOLEAN:
                if(strict) {
                    goto bad_type;
                }

                if(lua_toboolean(L, -1)) {
                    size += sizeof("true") - 1;

                } else {
                    size += sizeof("false") - 1;
                }

                break;

            case LUA_TTABLE:
                size += lua_calc_strlen_in_table(L, -1, arg_i, strict);
                break;

            case LUA_TLIGHTUSERDATA:
                if(strict) {
                    goto bad_type;
                }

                if(lua_touserdata(L, -1) == NULL) {
                    size += sizeof("null") - 1;
                    break;
                }

                continue;

            default:
bad_type:
                msg = lua_pushfstring(L, "bad data type %s found", lua_typename(L, type));
                return luaL_argerror(L, arg_i, msg);
        }

        lua_pop(L, 1);    /* stack: table */
    }

    return size;
}

unsigned char *lua_copy_str_in_table(lua_State *L, int index, u_char *dst)
{
    double key = 0;
    int max = 0;
    int i = 0;
    int type = 0;
    size_t len = 0;
    const u_char *p = NULL;

    if(index < 0) {
        index = lua_gettop(L) + index + 1;
    }

    max = 0;
    lua_pushnil(L);    /* stack: table key */

    while(lua_next(L, index) != 0) {       /* stack: table key value */
        key = lua_tonumber(L, -2);

        if(key > max) {
            max = key;
        }

        lua_pop(L, 1);    /* stack: table key */
    }

    for(i = 1; i <= max; i++) {
        lua_rawgeti(L, index, i);    /* stack: table value */
        type = lua_type(L, -1);

        switch(type) {
            case LUA_TNUMBER:
            case LUA_TSTRING:
                p = (u_char *) lua_tolstring(L, -1, &len);
                memcpy(dst, p, len);
                dst += len;
                break;

            case LUA_TNIL:
                *dst++ = 'n';
                *dst++ = 'i';
                *dst++ = 'l';
                break;

            case LUA_TBOOLEAN:
                if(lua_toboolean(L, -1)) {
                    *dst++ = 't';
                    *dst++ = 'r';
                    *dst++ = 'u';
                    *dst++ = 'e';

                } else {
                    *dst++ = 'f';
                    *dst++ = 'a';
                    *dst++ = 'l';
                    *dst++ = 's';
                    *dst++ = 'e';
                }

                break;

            case LUA_TTABLE:
                dst = lua_copy_str_in_table(L, -1, dst);
                break;

            case LUA_TLIGHTUSERDATA:
                *dst++ = 'n';
                *dst++ = 'u';
                *dst++ = 'l';
                *dst++ = 'l';
                break;

            default:
                luaL_error(L, "impossible to reach here");
                return NULL;
        }

        lua_pop(L, 1);    /* stack: table */
    }

    return dst;
}

static char _1_temp_buf[4096];
//Characters encoded are NUL (ASCII 0), \n, \r, \, ', ", and Control-Z.
int cosocket_lua_f_escape(lua_State *L)
{
    const char *src = NULL;
    size_t slen = 0;

    if(lua_isnil(L, 1)) {
        src = "";

    } else {
        src = luaL_checklstring(L, 1, &slen);
    }

    if(src == 0) {
        lua_pushstring(L, "");
        return 1;
    }

    char *dst = _1_temp_buf;

    if(slen > 2048) {
        dst = large_malloc(slen * 2);
    }

    int i = 0, j = 0, has = 0;

    for(i = 0; i < slen; i++) {
        if(j >= 4) {
            lua_pushlstring(L, dst, j);

            if(has == 1) {
                lua_concat(L, 2);
            }

            has = 1;
            j = 0;
        }

        switch(src[i]) {
            case '\r':
                dst[j++] = '\\';
                dst[j++] = 'r';
                continue;
                break;

            case '\n':
                dst[j++] = '\\';
                dst[j++] = 'n';
                continue;
                break;

            case '\\':
                dst[j++] = '\\';
                break;

            case '\'':
                dst[j++] = '\\';
                break;

            case '"':
                dst[j++] = '\\';
                break;

            case '\b':
                dst[j++] = '\\';
                dst[j++] = 'b';
                continue;
                break;

            case '\t':
                dst[j++] = '\\';
                dst[j++] = 't';
                continue;
                break;

            case '\0':
                dst[j++] = '\\';
                dst[j++] = '0';
                continue;
                break;

            case '\032':
                dst[j++] = '\\';
                dst[j++] = 'Z';
                continue;
                break;

            default:
                break;
        }

        dst[j++] = src[i];
    }

    lua_pushlstring(L, dst, j);

    if(dst != _1_temp_buf) {
        free(dst);
    }

    if(has == 1) {
        lua_concat(L, 2);
    }

    return 1;
}