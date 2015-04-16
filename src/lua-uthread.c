#include "lua-uthread.h"


static rb_tree_t coroutine_tree;
static lua_State *vm = NULL;

static int lua_f_lua_uthread_resume_waitings(lua_State *L, int deaded);

static int corotinue_rbtree_compare(const void *lhs, const void *rhs)
{
    int ret = 0;

    const rb_key_t *l = (const rb_key_t *)lhs;
    const rb_key_t *r = (const rb_key_t *)rhs;

    if(l->co > r->co) {
        ret = 1;

    } else if(l->co < r->co) {
        ret = -1;

    }

    return ret;
}

static rb_key_t *get_thread_rbtree_key(lua_State *co)
{
    rb_key_t *key = NULL, _key;
    rb_tree_node_t *tnode = NULL;
    _key.co = co;

    if(rb_tree_find(&coroutine_tree, &_key, &tnode) == RB_OK) {
        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

    } else {
        tnode = malloc(sizeof(rb_tree_node_t) + sizeof(rb_key_t));
        memset(tnode, 0, sizeof(rb_tree_node_t) + sizeof(rb_key_t));

        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

        key->co = co;

        if(rb_tree_insert(&coroutine_tree, key, tnode) != RB_OK) {
            free(tnode);
            tnode = NULL;
            key = NULL;
        }
    }

    return key;
}

int lua_f_lua_uthread_create(lua_State *L)
{
    lua_State *co = NULL;
    rb_key_t *key = NULL;

    luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1, "Lua function expected");

    co = lua_newthread(L);
    key = get_thread_rbtree_key(co);

    if(key) {
        lua_pushthread(co);
        key->ref = luaL_ref(co, LUA_REGISTRYINDEX);
    }

    lua_pushvalue(L, 1);    /* copy entry function to top of L*/
    lua_xmove(L, co, 1);    /* move entry function from L to co */

    return 1;
}

int lua_f_lua_uthread_wait(lua_State *L)
{
    lua_State *co = NULL;
    rb_key_t *key = NULL;
    int i = 0, nargs = 0, waited = 0;

    if(lua_istable(L, 1)) {
        nargs = luaL_getn(L, 1);

        for(i = 0; i < nargs; i++) {
            lua_rawgeti(L, 1, i + 1);

            if(lua_isthread(L, -1)) {
                co = lua_tothread(L, -1);

                if(lua_status(co) == LUA_YIELD) {
                    key = get_thread_rbtree_key(co);

                    if(key && key->n < 6) {
                        key->waiting[key->n++] = L;

                        key = get_thread_rbtree_key(L);
                        key->w++;

                        if(key->w > 1) {
                            key->wd = 1;
                        }

                        waited++;
                    }
                }
            }

            lua_pop(L, 1);
        }

    } else if(lua_isthread(L, 1)) {
        nargs = lua_gettop(L);

        for(i = 0; i < nargs; i++) {
            if(!lua_isthread(L, i + 1)) {
                continue;
            }

            co = lua_tothread(L, i + 1);

            if(lua_status(co) == LUA_YIELD) {
                key = get_thread_rbtree_key(co);

                if(key && key->n < 6) {
                    key->waiting[key->n++] = L;

                    key = get_thread_rbtree_key(L);
                    key->w++;

                    if(key->w > 1) {
                        key->wd = 1;
                    }

                    waited++;
                }

            }
        }
    }

    if(waited > 0) {
        return lua_yield(L, 0);
    }

    lua_pushnumber(L, waited);

    return 1;
}

int lua_f_lua_uthread_resume_in_c(lua_State *L, int nargs)
{
    rb_key_t *key = NULL;
    int status = 0, ref = 0, j = 0;
    status = lua_resume(L, nargs);

    if(status == LUA_YIELD) {
    } else if(status) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        LOGF(ERR, "Lua:error %s", lua_tostring(L, lua_gettop(L)));

        nargs = lua_gettop(L) - 2;

        for(j = 0; j < nargs; j++) {
            lua_remove(L, -3);
        }

        lua_pushnil(L);
        lua_replace(L, 1);

        key = get_thread_rbtree_key(L);

        if(key) {
            ref = key->ref;
        }

        lua_f_lua_uthread_resume_waitings(L, 0);

        if(ref > 0) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }

    } else {
        key = get_thread_rbtree_key(L);

        if(key) {
            ref = key->ref;
        }

        lua_f_lua_uthread_resume_waitings(L, 1);

        if(ref > 0) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }

    lua_settop(L, lua_gettop(L));

    return status;
}

static int lua_f_lua_uthread_resume_waitings(lua_State *L, int deaded)
{
    lua_State *co = NULL;
    int i = 0, n = 0, status = 0, j = 0, ref = 0, nargs = 0;

    rb_key_t *key = NULL, _key, *key2 = NULL;
    rb_tree_node_t *tnode = NULL;

    _key.co = L;

    n = lua_gettop(L);

    if(rb_tree_find(&coroutine_tree, &_key, &tnode) == RB_OK) {
        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

        for(i = 0; i < key->n; i++) {
            co = key->waiting[i];

            key2 = get_thread_rbtree_key(co);

            if(lua_status(co) == LUA_YIELD && (--key2->w) == 0) {
                if(n > 0 && key2->wd == 0) {
                    key2->w = 0;

                    for(j = 0; j < n; j++) {
                        lua_pushvalue(L, j + 1);
                    }

                    lua_xmove(L, co, n);
                    lua_f_lua_uthread_resume_in_c(co, n);

                } else {
                    lua_pushboolean(co, key2->wd || deaded);
                    key2->w = 0;
                    key2->wd = 0;
                    lua_f_lua_uthread_resume_in_c(co, 1);
                }
            }
        }

        if(rb_tree_remove(&coroutine_tree, tnode) == RB_OK) {
            free(tnode);
        }
    }

    return 0;
}

int lua_f_lua_uthread_resume(lua_State *L)
{
    lua_State *co = NULL;
    rb_key_t *key = NULL;
    int nargs = 0, status = 0, res = 0, i = 0, ref = 0;

    luaL_argcheck(L, lua_isthread(L, 1), 1, "coroutine expected");

    co = lua_tothread(L, 1);

    if(lua_status(co) != LUA_YIELD && lua_gettop(co) == 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "cannot resume dead coroutine");
        return 2;
    }

    if(lua_gettop(L) > 1) {
        nargs = lua_gettop(L) - 1;
        lua_xmove(L, co, nargs);
    }

    //do {
    status = lua_resume(co, nargs);
    /*
    if(status == LUA_YIELD){
        lua_settop(co, 0);
        nargs = 0;
    }*/
    //} while(1);

    if(status == LUA_YIELD) {
        lua_pushboolean(L, 1);
        res = 1 + lua_gettop(co);
        lua_xmove(co, L, lua_gettop(co));

    } else if(status) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        const char *err = lua_tostring(co, lua_gettop(co));

        LOGF(ERR, "Lua:error %s", lua_tostring(co, lua_gettop(co)));

        nargs = lua_gettop(co) - 2;

        for(i = 0; i < nargs; i++) {
            lua_remove(co, -3);
        }

        lua_pushnil(co);
        lua_replace(co, 1);

        key = get_thread_rbtree_key(co);

        if(key) {
            ref = key->ref;
        }

        lua_f_lua_uthread_resume_waitings(co, 0);


        if(ref > 0) {
            luaL_unref(co, LUA_REGISTRYINDEX, ref);
        }

        //lua_pushboolean(L, 0);
        lua_pushnil(L);
        lua_pushstring(L, err);
        res = 2;

    } else {
        key = get_thread_rbtree_key(co);

        if(key) {
            ref = key->ref;
        }

        lua_f_lua_uthread_resume_waitings(co, 1);

        if(ref > 0) {
            luaL_unref(co, LUA_REGISTRYINDEX, ref);
        }

        lua_pushboolean(L, 1);
        res = 1 + lua_gettop(co);
        lua_xmove(co, L, lua_gettop(co));
    }

    return res;
}

int lua_f_lua_uthread_spawn(lua_State *L)
{
    int nargs = 0, status = 0;
    lua_State *co = NULL;
    rb_key_t *key = NULL;

    luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1, "Lua function expected");

    co = lua_newthread(L);
    key = get_thread_rbtree_key(co);

    if(key) {
        lua_pushthread(co);
        key->ref = luaL_ref(co, LUA_REGISTRYINDEX);
    }

    lua_pushvalue(L, 1);    /* copy entry function to top of L*/
    lua_xmove(L, co, 1);    /* move entry function from L to co */

    nargs = lua_gettop(L) - 2;
    lua_xmove(L, co, nargs + 1);
    lua_settop(co, nargs + 1);

    status = lua_f_lua_uthread_resume_in_c(co, nargs);

    lua_settop(L, lua_gettop(L));

    if(status != LUA_YIELD && status) {
        //error
        const char *err = lua_tostring(co, lua_gettop(co));
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }

    lua_pushthread(co);
    lua_xmove(co, L, 1);

    return 1;
}


LUALIB_API int luaopen_uthread(lua_State *L)
{
    rb_tree_new(&coroutine_tree, corotinue_rbtree_compare);
    vm = L;

    /* get old coroutine table */
    lua_getglobal(L, "coroutine");

    lua_getfield(L, -1, "create");
    lua_setfield(L, -2, "_create");

    lua_getfield(L, -1, "resume");
    lua_setfield(L, -2, "_resume");

    lua_pushcfunction(L, lua_f_lua_uthread_create);
    lua_setfield(L, -2, "create");

    lua_pushcfunction(L, lua_f_lua_uthread_resume);
    lua_setfield(L, -2, "resume");

    lua_pushcfunction(L, lua_f_lua_uthread_wait);
    lua_setfield(L, -2, "wait");

    lua_pushcfunction(L, lua_f_lua_uthread_spawn);
    lua_setfield(L, -2, "spawn");

    /* pop the old coroutine */
    lua_pop(L, 1);

    return 0;
}
