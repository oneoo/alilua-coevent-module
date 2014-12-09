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

#define VERSION "0.1"
#define AUTORELOAD

static int lb_reverse(int a)
{
    union {
        int i;
        char c[4];
    } u, r;

    u.i = a;
    r.c[0] = u.c[3];
    r.c[1] = u.c[2];
    r.c[2] = u.c[1];
    r.c[3] = u.c[0];

    return r.i;
}

static char *lb_strsplit(const void *string_org, int org_len, const char *demial, char **last, int *len)
{
    unsigned char *str;
    unsigned char *p;

    if(org_len < 1 || !string_org || !demial) {
        return NULL;
    }

    if(*last) {
        if(*last == string_org) {
            *last = NULL;
            return NULL;
        }

        str = (unsigned char *)*last;

    } else {
        str = (unsigned char *)string_org;
    }

    if(!str) {
        return (char *)str;
    }

    p = str;

    while(p < (unsigned char *)string_org + org_len && *p != demial[0]) {
        p++;
    }

    if(p == (unsigned char *)string_org + org_len) {
        *last = (char *)string_org;

    } else {
        *last = (char *)p + 1;
    }

    if(str) {
        if(*last != string_org) {
            *len = (*last - (char *)str) - 1;

        } else {
            *len = (unsigned char *)(string_org + org_len) - str;
        }
    }

    return (char *)str;
}

static char buf1024[1024] = {0};
static char monipdata_file[1024] = {0};
static char machine_little_endian;
static char *monipdata = NULL;
static uint32_t monipdata_offset = 0;
static uint32_t monipdata_len = 0;
static uint32_t check_per = 0;
static uint32_t monipdata_file_mtime = 0;

static int monipdata_init(const char *file)
{
    int machine_endian_check = 1;
    machine_little_endian = ((char *)&machine_endian_check)[0];

    uint32_t nlen;

    if(monipdata_file != file){
        strcpy(monipdata_file, file);
    }
    int fd = open(file, O_RDONLY, 0);

    if(fd > -1) {
        off_t len = lseek(fd, 0, SEEK_END);
        char *_p = malloc(len);

        if(_p) {
            lseek(fd, 0, SEEK_SET);

            if(read(fd, _p, len) != len) {
                close(fd);
                return 0;

            } else {
                memcpy(&monipdata_offset, _p, 4);
                monipdata_len = len;

                if(machine_little_endian) {
                    monipdata_offset = lb_reverse(monipdata_offset);
                }

                if(_p) {
                    if(monipdata) {
                        free(monipdata);
                    }

                    monipdata = _p;
                }

                close(fd);
                return 1;
            }

        } else {
            close(fd);
            return 0;
        }
    }

    return 0;
}

static const char *getposbyip(const char *ip_str, int *len)
{
#ifdef AUTORELOAD

    if(check_per++ > 1000000) {
        check_per = 0;
        struct stat sb;

        if(stat(monipdata_file, &sb) != -1) {
            if(sb.st_mtime != monipdata_file_mtime) {
                monipdata_file_mtime = sb.st_mtime;
                monipdata_init(monipdata_file);
            }
        }
    }

#endif
    unsigned int ip_str_len;

    unsigned long lgip;
#ifdef HAVE_INET_PTON
    struct in_addr uip;
#else
    unsigned long int uip;
#endif
    char *ip_str_tok, *ipdot, *ip, *nip;
    uint tmp_offset;
    uint start, max_comp_len, index_offset = 0;
    unsigned char pos_len;
    char *ip_addr;

    ip_str_len = strlen(ip_str);

#ifdef HAVE_INET_PTON

    if(ip_str_len == 0 || inet_pton(AF_INET, ip_str, &uip) != 1) {
        *len = 28;
        return "未知\t未知\t未知\t\t未知";
    }

    lgip = ntohl(uip.s_addr);
#else

    if(ip_str_len == 0 || (uip = inet_addr(ip_str)) == INADDR_NONE) {
        *len = 28;
        return "未知\t未知\t未知\t\t未知";
    }

    lgip = ntohl(uip);
#endif

    char b4[4] = {0};

    if(ip_str[3] == '.') {
        memcpy(&b4, ip_str, 3);

    } else if(ip_str[2] == '.') {
        memcpy(&b4, ip_str, 2);

    } else {
        memcpy(&b4, ip_str, 1);
    }

    tmp_offset = atoi(b4) * 4;

    if(machine_little_endian) {
        lgip = lb_reverse(lgip);
    }

    nip = (char *)&lgip;

    start = 0;
    memcpy(&start, (monipdata + 4) + tmp_offset, 4);

    if(!machine_little_endian) {
        start = lb_reverse(start);
    }

    max_comp_len = monipdata_offset - 1024;

    for(start = start * 8 + 1024; start < max_comp_len; start += 8) {

        if(memcmp((monipdata + 4) + start, nip, 4) >= 0) {
            memcpy(&index_offset, (monipdata + 4) + start + 4, 3);
            //memcpy(&index_offset + 3, "\x0", 1);

            if(!machine_little_endian) {
                index_offset = lb_reverse(index_offset);
            }

            pos_len = *((monipdata + 4) + start + 7);
            break;
        }
    }

    if(index_offset < 1 || (monipdata_offset + index_offset - 1024 + pos_len) > monipdata_len) {
        *len = 28;
        return "未知\t未知\t未知\t\t未知";
    }

    *len = pos_len;
    return monipdata + (monipdata_offset + index_offset - 1024);
}

static int init(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need 17monipdb.dat path");
        return 2;
    }

    lua_pushboolean(L, monipdata_init(lua_tostring(L, 1)));
    return 1;
}

static int find(lua_State *L)
{
    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "need IP");
        return 2;
    }

    if(!monipdata) {
        lua_pushnil(L);
        lua_pushstring(L, "monip not inited");
        return 2;
    }

    int c = 0;
    int len = 0;

    const char *pos = getposbyip(lua_tostring(L, 1), &len);
    char *last = NULL;
    int plen = 0;
    char *p = lb_strsplit(pos, len, "\t", &last, &plen);

    while(p) {
        c++;

        lua_pushlstring(L, p, plen);

        p = lb_strsplit(pos, len, "\t", &last, &plen);
    }

    return c;
}

static const struct luaL_reg thislib[] = {
    {"init", init},
    {"find", find},
    {NULL, NULL}
};

LUALIB_API int luaopen_monip(lua_State *L)
{
    luaL_register(L, "monip", thislib);

    lua_pushstring(L, VERSION);
    lua_setfield(L, -2, "_VERSION");

    return 1;
}
