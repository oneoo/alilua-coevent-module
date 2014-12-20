#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/timeb.h>

#include <lua.h>
#include <lauxlib.h>

#include "../merry/merry.h"
#include "../merry/common/rbtree.h"

#define VERSION "0.1"


#ifndef _UTHREAD_H
#define _UTHREAD_H

typedef struct {
    lua_State *co;
    lua_State *waiting[6];
    uint8_t n;
    uint8_t wd;
    uint16_t w;
    uint32_t ref;
} rb_key_t;

LUALIB_API int luaopen_uthread(lua_State *L);
int lua_f_lua_uthread_resume_in_c(lua_State *L, int nargs);

#endif