#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/timeb.h>
#include <time.h>
#include <math.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <inttypes.h>
#include <errno.h>
//#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <lua.h>
#include <lauxlib.h>

#include "../merry/merry.h"

//#define free(p) do { if (p) { free(p); p = NULL; } } while (0)
//#define close(fd) do { if (fd >= 0) { close(fd); fd = -1; } } while (0)

#include "lua-uthread.h"

#ifndef _COEVENT_H
#define _COEVENT_H

#define large_malloc(s) (malloc(((int)(s/4096)+1)*4096))

#ifndef u_char
#define u_char unsigned char
#endif

typedef struct {
    lua_State *L;
    void *next;
    char z[12]; /// size align
} cosocket_swop_t;

typedef struct {
    char *buf;
    void *next;
    int buf_size;
    int buf_len;
    char z[2]; /// size align
} cosocket_link_buf_t;

#define _SENDBUF_SIZE 3908
typedef struct {
    int fd;
    uint8_t use_ssl;
    uint8_t in_read_action;

    FILE *fp;

    void *pool_wait;

    SSL_CTX *ctx;
    char *ssl_pw;
    unsigned long ssl_sign;
    SSL *ssl;
    void *ptr;
    lua_State *L;
    const u_char *send_buf;
    size_t send_buf_len;
    size_t send_buf_ed;
    u_char *send_buf_need_free;

    cosocket_link_buf_t *read_buf;
    cosocket_link_buf_t *last_buf;
    size_t total_buf_len;
    size_t buf_read_len;

    size_t readed;
    int status;

    int timeout;
    timeout_t *timeout_ptr;
    struct sockaddr_in addr;
    int pool_size;
    int reusedtimes;
    unsigned long pool_key;

    int inuse;
    u_char _send_buf[_SENDBUF_SIZE];// with size align / 60
} cosocket_t;

int cosocket_be_ssl_connected(se_ptr_t *ptr);

int cosocket_be_write(se_ptr_t *ptr);
int cosocket_be_read(se_ptr_t *ptr);

int tcp_connect(const char *host, int port, cosocket_t *cok, int loop_fd, int *ret);

int coevnet_module_do_other_jobs();

int lua_f_coroutine_swop(lua_State *L);
int check_lua_sleep_timeouts();
int _lua_sleep(lua_State *L, int msec);
int lua_f_sleep(lua_State *L);

int lua_f_time(lua_State *L);
int lua_f_longtime(lua_State *L);
size_t lua_calc_strlen_in_table(lua_State *L, int index, int arg_i, unsigned strict);
unsigned char *lua_copy_str_in_table(lua_State *L, int index, u_char *dst);

int lua_f_md5(lua_State *L);
int lua_f_sha1bin(lua_State *L);
int lua_f_hmac_sha1(lua_State *L);

int lua_f_base64_encode(lua_State *L);
int lua_f_base64_decode(lua_State *L);
int lua_f_base64_encode_url(lua_State *L);
int lua_f_base64_decode_url(lua_State *L);

int cosocket_lua_f_escape(lua_State *L);
int lua_f_escape_uri(lua_State *L);
int lua_f_unescape_uri(lua_State *L);

int lua_f_log(lua_State *L);
int lua_f_open_log(lua_State *L);

LUALIB_API int luaopen_eio(lua_State *L);

int lua_co_build_dns_query_packet(lua_State *L);
int lua_co_parse_dns_result(lua_State *L);

#endif
