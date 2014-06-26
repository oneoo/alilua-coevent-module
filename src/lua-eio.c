#include "coevent.h"
#include "../merry/se/libeio/eio.h"

static int eio_res_cb(eio_req *req)
{
    lua_State *L = req->data;

    if(L) {
        if(req->result < 0) {
            lua_pushnil(L);
            lua_pushnumber(L, req->errorno);
            lua_pushstring(L, strerror(req->errorno));
            lua_resume(L, 3);

        } else {
            lua_pushboolean(L, 1);
            lua_resume(L, 1);
        }

    } else {
        //printf("gc close\n");
    }

    return 0;
}

static int eio_readdir_cb(eio_req *req)
{
    lua_State *L = req->data;

    if(EIO_RESULT(req) > -1) {
        lua_newtable(L);
        int i = 1;
        char *buf = (char *)EIO_BUF(req);

        if(EIO_RESULT(req) < 0) {
            lua_resume(L, 0);
            return 0;
        }

        while(EIO_RESULT(req)--) {
            lua_pushstring(L, buf);
            buf += strlen(buf) + 1;
            lua_rawseti(L, -2, i++);
        }

        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static const char *mode2string(mode_t mode)
{
    if(S_ISREG(mode)) {
        return "file";
    }

    else if(S_ISDIR(mode)) {
        return "directory";
    }

    else if(S_ISLNK(mode)) {
        return "link";
    }

#ifdef S_ISSOCK

    else if(S_ISSOCK(mode)) {
        return "socket";
    }

#endif

    else if(S_ISFIFO(mode)) {
        return "named pipe";
    }

    else if(S_ISCHR(mode)) {
        return "char device";
    }

    else if(S_ISBLK(mode)) {
        return "block device";
    }

    else {
        return "other";
    }
}

static int eio_stat_cb(eio_req *req)
{
    lua_State *L = req->data;
    struct stat *info = EIO_STAT_BUF(req);

    if(EIO_RESULT(req) == 0) {
        char mode_str[10] = {0};

        sprintf(mode_str, "%o", (0xfff & info->st_mode));

        lua_newtable(L);

        /* device inode resides on */
        lua_pushliteral(L, "dev");
        lua_pushnumber(L, (lua_Number)info->st_dev);
        lua_rawset(L, -3);

        /* inode's number */
        lua_pushliteral(L, "ino");
        lua_pushnumber(L, (lua_Number)info->st_ino);
        lua_rawset(L, -3);

        /* Type of entry */
        lua_pushliteral(L, "mode");
        lua_pushnumber(L,  atoi(mode_str));
        lua_rawset(L, -3);

        /* Type of entry */
        lua_pushliteral(L, "type");
        lua_pushstring(L, mode2string(info->st_mode));
        lua_rawset(L, -3);

        /* number of hard links to the file */
        lua_pushliteral(L, "nlink");
        lua_pushnumber(L, (lua_Number)info->st_nlink);
        lua_rawset(L, -3);

        /* user-id of owner */
        lua_pushliteral(L, "uid");
        lua_pushnumber(L, (lua_Number)info->st_uid);
        lua_rawset(L, -3);

        /* group-id of owner */
        lua_pushliteral(L, "gid");
        lua_pushnumber(L, (lua_Number)info->st_gid);
        lua_rawset(L, -3);

        /* device type, for special file inode */
        lua_pushliteral(L, "rdev");
        lua_pushnumber(L, (lua_Number)info->st_rdev);
        lua_rawset(L, -3);

        /* time of last access */
        lua_pushliteral(L, "access");
        lua_pushnumber(L, info->st_atime);
        lua_rawset(L, -3);

        /* time of last data modification */
        lua_pushliteral(L, "modification");
        lua_pushnumber(L, info->st_mtime);
        lua_rawset(L, -3);

        /* time of last file status change */
        lua_pushliteral(L, "change");
        lua_pushnumber(L, info->st_ctime);
        lua_rawset(L, -3);

        /* file size, in bytes */
        lua_pushliteral(L, "size");
        lua_pushnumber(L, (lua_Number)info->st_size);
        lua_rawset(L, -3);

        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int eio_stat_isdir_cb(eio_req *req)
{
    lua_State *L = req->data;
    struct stat *info = EIO_STAT_BUF(req);

    if(EIO_RESULT(req) == 0) {
        lua_pushboolean(L, S_ISDIR(info->st_mode));
        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int eio_stat_isfile_cb(eio_req *req)
{
    lua_State *L = req->data;
    struct stat *info = EIO_STAT_BUF(req);

    if(EIO_RESULT(req) == 0) {
        lua_pushboolean(L, S_ISREG(info->st_mode));
        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int eio_stat_exists_cb(eio_req *req)
{
    lua_State *L = req->data;
    struct stat *info = EIO_STAT_BUF(req);

    if(EIO_RESULT(req) == 0) {
        if(S_ISDIR(info->st_mode)) {
            lua_pushstring(L, "dir");

        } else {
            lua_pushstring(L, "file");
        }

        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int lua_f_eio_isdir(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_stat(lua_tostring(L, 1), 0, eio_stat_isdir_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_isfile(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_stat(lua_tostring(L, 1), 0, eio_stat_isfile_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_exists(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_stat(lua_tostring(L, 1), 0, eio_stat_exists_cb, L);

    return lua_yield(L, 0);
}

static int oct2decimal(int octal)
{
    char val [21] = {0}; /* large enough for any 64-bit integer */
    int decval = 0;
    int i = 0;

    sprintf(val, "%d", octal);

    for(i = 0; i < strlen(val); i ++) {
        int octval = val [i];

        decval *= 8;
        octval = octval - '0';

        if((octval > 7) || (octval < 0)) {
            return -1;
        }

        decval += octval;
    }

    return decval;
}

static int lua_f_eio_mkdir(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int mode = 0700;

    if(lua_isnumber(L, 2)) {
        mode = oct2decimal(lua_tonumber(L, 2));

        if(mode < 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Invalid mode for mkdir()");
            return 2;
        }
    }

    eio_mkdir(lua_tostring(L, 1), mode, 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_readdir(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_readdir(lua_tostring(L, 1), 0, 0, eio_readdir_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_rename(lua_State *L)
{
    if(!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_rename(lua_tostring(L, 1), lua_tostring(L, 2), 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_chmod(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int mode = 0600;

    if(lua_isnumber(L, 2)) {
        mode = oct2decimal(lua_tonumber(L, 2));

        if(mode < 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Invalid mode for chmod()");
            return 2;
        }
    }

    eio_chmod(lua_tostring(L, 1), mode, 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_unlink(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_unlink(lua_tostring(L, 1), 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_rmdir(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_rmdir(lua_tostring(L, 1), 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_stat(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    eio_stat(lua_tostring(L, 1), 0, eio_stat_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_chown(lua_State *L)
{
    const char *path = NULL;
    uid_t owner = getuid();
    gid_t group = getgid();


    if(lua_isstring(L, 1)) {
        path = lua_tostring(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushstring(L, "Error params! chown(string, string|int, [string|int])");
        return 2;
    }

    /* Owner */
    if(lua_isnumber(L, 2)) {
        owner = (uid_t)lua_tonumber(L, 2);

    } else if(lua_isstring(L, 2)) {
        struct passwd *p = getpwnam(lua_tostring(L, 2));

        if(p != NULL) {
            owner = p->pw_uid;

        } else {
            lua_pushnil(L);
            lua_pushstring(L, "User not found in passwd");
            return 2;
        }

    } else {
        lua_pushnil(L);
        lua_pushstring(L, "Error params! chown(string, string|int, [string|int])");
        return 2;
    }

    /* Group */
    if(lua_isnumber(L, 3)) {
        group = (gid_t)lua_tonumber(L, 2);

    } else if(lua_isstring(L, 3)) {
        struct passwd *p = getpwnam(lua_tostring(L, 3));

        if(p != NULL) {
            group = p->pw_gid;

        } else {
            lua_pushnil(L);
            lua_pushstring(L, "User not found in passwd");
            return 2;
        }
    }

    eio_chown(path, owner, group, 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_gc(lua_State *L)
{
    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd > -1) {
        eio_close(*efd, 0, eio_res_cb, NULL);
    }

    return 0;
}

static int lua_f_eio_close(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd > -1) {
        eio_close(*efd, 0, eio_res_cb, L);
    }

    return lua_yield(L, 0);
}

static int eio_read_cb(eio_req *req)
{
    lua_State *L = req->data;

    int rc = EIO_RESULT(req);

    if(rc >= 0) {
        lua_pushlstring(L, EIO_BUF(req), rc);
        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int lua_f_eio_read(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "File handle closed!");
        return 2;
    }

    int length = 0, offset = -1;

    if(lua_isnumber(L, 2)) {
        length = lua_tonumber(L, 2);
    }

    if(lua_isnumber(L, 3)) {
        offset = lua_tonumber(L, 3);
    }

    if(length < 1) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params! read(length, [offset])");
        return 2;
    }

    eio_read(*efd, 0, length, offset, EIO_PRI_DEFAULT, eio_read_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_write(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "File handle closed!");
        return 2;
    }

    size_t length = 0, offset = -1;
    const char *data = lua_tolstring(L, 2, &length);

    if(lua_isnumber(L, 3)) {
        offset = lua_tonumber(L, 3);
    }

    if(length < 1) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params! read(length, [offset])");
        return 2;
    }

    eio_write(*efd, (void *)data, length, offset, EIO_PRI_DEFAULT, eio_res_cb, L);

    return lua_yield(L, 0);
}

static int eio_seek_cb(eio_req *req)
{
    lua_State *L = req->data;

    if(req->result < 0) {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);

    } else {
        lua_pushnumber(L, req->offs);
        lua_resume(L, 1);
    }

    return 0;
}

static int lua_f_eio_seek(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isnumber(L, 2) || !lua_isstring(L, 3)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "File handle closed!");
        return 2;
    }

    size_t offset = 0;

    if(lua_isnumber(L, 2)) {
        offset = lua_tonumber(L, 2);
    }

    if(offset < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params! read(length, [offset])");
        return 2;
    }

    int whence = EIO_SEEK_CUR;
    const char *_whence = lua_tostring(L, 3);

    if(strcmp(_whence, "set") == 0) {
        whence = EIO_SEEK_SET;

    } else if(strcmp(_whence, "end") == 0) {
        whence = EIO_SEEK_END;
    }

    eio_seek(*efd, offset, whence, EIO_PRI_DEFAULT, eio_seek_cb, L);

    return lua_yield(L, 0);
}

static int lua_f_eio_sync(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int *efd = (int *) lua_touserdata(L, 1);

    if(*efd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "File handle closed!");
        return 2;
    }

    eio_fsync(*efd, 0, eio_res_cb, L);

    return lua_yield(L, 0);
}

static const luaL_reg EIO_M[] = {
    {"write",   lua_f_eio_write},
    {"read",    lua_f_eio_read},
    {"seek",    lua_f_eio_seek},
    {"sync",    lua_f_eio_sync},
    {"close",   lua_f_eio_close},
    {"__gc",    lua_f_eio_gc},

    {NULL, NULL}
};

static int eio_open_cb(eio_req *req)
{
    lua_State *L = req->data;
    int fd = EIO_RESULT(req);

    if(fd > -1) {
        int *efd = (int *) lua_newuserdata(L, sizeof(int));

        if(!efd) {
            lua_pushnil(L);
            lua_pushstring(L, "stack error!");
            lua_resume(L, 2);
        }

        *efd = fd;

        luaL_getmetatable(L, "eio:fh");
        lua_setmetatable(L, -2);
        lua_resume(L, 1);

    } else {
        lua_pushnil(L);
        lua_pushnumber(L, req->errorno);
        lua_pushstring(L, strerror(req->errorno));
        lua_resume(L, 3);
    }

    return 0;
}

static int lua_f_eio_open(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    int om = 1;

    if(lua_isstring(L, 2)) {
        const char *p = lua_tostring(L, 2);

        if(p[0] == 'w') {
            om = 3;

        } else if(p[0] == 'a') {
            om = 2;
        }
    }

    int mode = 0644;

    if(lua_isnumber(L, 3)) {
        mode = oct2decimal(lua_tonumber(L, 3));

        if(mode < 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Invalid mode for open()");
            return 2;
        }
    }

    eio_open(lua_tostring(L, 1), (om == 1 ? O_RDONLY :
                                  (om == 2 ? (O_APPEND | O_CREAT) : (O_RDWR | O_CREAT)))
             , mode, 0, eio_open_cb, L);

    return lua_yield(L, 0);
}

static const luaL_reg F[] = {
    {"mkdir",               lua_f_eio_mkdir},
    {"stat",                lua_f_eio_stat},
    {"chown",               lua_f_eio_chown},
    {"chmod",               lua_f_eio_chmod},
    {"unlink",              lua_f_eio_unlink},
    {"rmdir",               lua_f_eio_rmdir},
    {"rename",              lua_f_eio_rename},
    {"readdir",             lua_f_eio_readdir},
    {"isdir",               lua_f_eio_isdir},
    {"isfile",              lua_f_eio_isfile},
    {"exists",              lua_f_eio_exists},

    {"open",                lua_f_eio_open},

    {NULL,                  NULL}
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

LUALIB_API int luaopen_eio(lua_State *L)
{
#ifdef PRE_LUA51
    luaL_openlib(L, "eio", F, 0);
#else
    luaL_register(L, "eio", F);
#endif

    luaL_newmetatable(L, "eio:fh");
    lua_pushvalue(L, lua_upvalueindex(1));
    setfuncs(L, EIO_M, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    return 1;
}
