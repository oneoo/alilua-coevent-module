#include "coevent.h"
#include "connection-pool.h"

static int _loop_fd = 0;
static lua_State *LM = NULL;
static unsigned char temp_buf[4096];
static int _process_count = 1;

static void timeout_handle(void *ptr)
{
    cosocket_t *cok = ptr;
    delete_timeout(cok->timeout_ptr);
    cok->timeout_ptr = NULL;

    if(cok->pool_wait) {
        delete_in_waiting_get_connection(cok->pool_wait);
        cok->pool_wait = NULL;
    }

    lua_pushnil(cok->L);

    if(cok->ptr) {
        if(cok->status == 3) {
            lua_pushstring(cok->L, "Connect error!(wait pool timeout)");

        } else if(((se_ptr_t *) cok->ptr)->wfunc == cosocket_be_ssl_connected) {
            lua_pushstring(cok->L, "SSL Connect timeout!");

        } else if(((se_ptr_t *) cok->ptr)->wfunc == cosocket_be_write) {
            lua_pushstring(cok->L, "Send timeout!");

        } else if(((se_ptr_t *) cok->ptr)->rfunc == cosocket_be_read) {
            lua_pushstring(cok->L, "Read timeout!");

        } else {
            lua_pushstring(cok->L, "Timeout!");
        }

    } else {
        if(cok->status == 3) {
            lua_pushstring(cok->L, "Connect error!(wait pool timeout)");

        } else {
            lua_pushstring(cok->L, "Timeout!");
        }
    }

    {
        se_delete(cok->ptr);
        cok->ptr = NULL;

        if(cok->fd > -1) {
            connection_pool_counter_operate(cok->pool_key, -1);
            close(cok->fd);
            cok->fd = -1;
        }

        cok->status = 0;
    }

    if(cok->ssl) {
        SSL_shutdown(cok->ssl);
        SSL_free(cok->ssl);
        cok->ssl = NULL;
        SSL_CTX_free(cok->ctx);
        cok->ctx = NULL;
    }

    if(cok->read_buf) {
        cosocket_link_buf_t *fr = cok->read_buf;
        cosocket_link_buf_t *nb = NULL;

        while(fr) {
            nb = fr->next;
            free(fr->buf);
            free(fr);
            fr = nb;
        }

        cok->read_buf = NULL;
    }

    if(cok->send_buf_need_free) {
        free(cok->send_buf_need_free);
        cok->send_buf_need_free = NULL;
    }


    cok->inuse = 0;

    lua_f_lua_uthread_resume_in_c(cok->L, 2);
}

int cosocket_be_ssl_connected(se_ptr_t *ptr)
{
    cosocket_t *cok = ptr->data;

    if(SSL_connect(cok->ssl)) {
        delete_timeout(cok->timeout_ptr);
        cok->timeout_ptr = NULL;
        se_be_pri(cok->ptr, NULL);
        lua_pushboolean(cok->L, 1);
        cok->inuse = 0;
        lua_f_lua_uthread_resume_in_c(cok->L, 1);
        return 1;
    }

    return 0;
}

static int ssl_password_cb(char *buf, int num, int rwflag, void *userdata)
{
    char *pemPasswd;
    static char empty_pw[] = "\0";

    pemPasswd = (char *)userdata;

    if(pemPasswd == NULL) {
        pemPasswd = empty_pw;
    }

    if(num < strlen(pemPasswd) + 1) {
        return 0;
    }

    strcpy(buf, pemPasswd);
    return(strlen(pemPasswd));
}

static int _be_connect(cosocket_t *cok, int fd, int yielded)
{
    int flag = 1;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    cok->fd = fd;
    cok->ptr = se_add(_loop_fd, fd, cok);

    cok->status = 2;
    cok->in_read_action = 0;

    if(cok->use_ssl) {
        if(cok->ctx == NULL) {
            cok->ctx = SSL_CTX_new(SSLv23_client_method());

            if(cok->ctx == NULL) {
                connection_pool_counter_operate(cok->pool_key, -1);
                se_delete(cok->ptr);
                close(cok->fd);

                cok->ptr = NULL;
                cok->fd = -1;
                cok->status = 0;

                lua_pushnil(cok->L);
                lua_pushstring(cok->L, "SSL_CTX_new Error");
                return 2;
            }
        }

        cok->ssl = SSL_new(cok->ctx);

        if(cok->ssl == NULL) {
            connection_pool_counter_operate(cok->pool_key, -1);
            se_delete(cok->ptr);
            close(cok->fd);

            cok->ptr = NULL;
            cok->fd = -1;
            cok->status = 0;

            SSL_CTX_free(cok->ctx);
            cok->ctx = NULL;

            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "SSL_new Error");
            return 2;
        }

        SSL_set_fd(cok->ssl, cok->fd);

        if(se_be_read(cok->ptr, cosocket_be_ssl_connected) == -1) {
            se_delete(cok->ptr);
            close(cok->fd);
            cok->ptr = NULL;
            cok->fd = -1;
            cok->status = 0;

            SSL_shutdown(cok->ssl);
            SSL_free(cok->ssl);
            cok->ssl = NULL;
            SSL_CTX_free(cok->ctx);
            cok->ctx = NULL;

            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "Network Error");
            return 2;

        } else {

            if(SSL_connect(cok->ssl) == 1) {
                se_be_pri(cok->ptr, NULL);
                lua_pushboolean(cok->L, 1);
                cok->inuse = 0;
                return 1;
            }

            cok->timeout_ptr = add_timeout(cok, cok->timeout, timeout_handle);
        }

        return -2;
    }

    se_be_pri(cok->ptr, NULL);
    lua_pushboolean(cok->L, 1);
    cok->inuse = 0;

    return 1;
}

static void be_connect(void *data, int fd)
{
    cosocket_t *cok = data;

    if(fd < 0) {
        connection_pool_counter_operate(cok->pool_key, -1);

        lua_pushnil(cok->L);

        if(se_errno == SE_DNS_QUERY_TIMEOUT) {
            lua_pushstring(cok->L, "Connect error!(dns query timeout)");

        } else if(se_errno == SE_CONNECT_TIMEOUT) {
            lua_pushstring(cok->L, "Connect error!(timeout)");

        } else {
            lua_pushstring(cok->L, "Connect error!(unknow)");
        }

        cok->status = 0;
        cok->inuse = 0;

        lua_f_lua_uthread_resume_in_c(cok->L, 2);
        return;
    }

    int ret = _be_connect(cok, fd, 1);

    if(ret == -2) {
        return;
    }

    lua_f_lua_uthread_resume_in_c(cok->L, ret);
}

static int lua_co_connect(lua_State *L)
{
    cosocket_t *cok = NULL;
    {
        if(!lua_isuserdata(L, 1) || !lua_isstring(L, 2)) {
            lua_pushnil(L);
            lua_pushstring(L, "Error params!");
            return 2;
        }

        cok = (cosocket_t *) lua_touserdata(L, 1);

        if(cok->status > 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Aleady connected!");
            return 2;
        }

        if(cok->inuse == 1) {
            lua_pushnil(L);
            lua_pushstring(L, "socket busy!");
            return 2;
        }

        //printf(" 0x%p connect to %s\n", L, lua_tostring(L, 2));
        size_t host_len = 0;
        const char *host = lua_tolstring(L, 2, &host_len);

        if(host_len > (host[0] == '/' ? 108 : 60)) {
            lua_pushnil(L);
            lua_pushstring(L, "hostname length must be <= 60!");
            return 2;
        }

        int port = 0;
        int pn = 3;

        if(host[0] != '/') {
            port = lua_tonumber(L, 3);

            if(port < 1) {
                lua_pushnil(L);
                lua_pushstring(L, "port must be > 0");
                return 2;
            }

            pn = 4;
        }

        if(lua_gettop(L) >= pn) { /// set keepalive options
            if(lua_isnumber(L, pn)) {
                cok->pool_size = lua_tonumber(L, pn);

                if(cok->pool_size < 0) {
                    cok->pool_size = 0;

                } else if(cok->pool_size > 4096) {
                    cok->pool_size = 4096;
                }

                pn++;
            }

            if(cok->pool_size > 0) {
                size_t len = 0;

                if(lua_gettop(L) == pn && lua_isstring(L, pn)) {
                    const char *key = lua_tolstring(L, pn, &len);
                    cok->pool_key = fnv1a_32(key, len);
                }
            }
        }

        if(cok->pool_key == 0) { /// create a normal key
            int len = snprintf(temp_buf, 4096, "%s%s:%d:%ld", port > 0 ? "tcp://" : "unix://", host, port, cok->ssl_sign);
            cok->pool_key = fnv1a_32(temp_buf, len);
        }

        cok->status = 1;
        cok->L = L;
        cok->read_buf = NULL;
        cok->last_buf = NULL;
        cok->total_buf_len = 0;
        cok->buf_read_len = 0;

        /// check pool count
        cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter(cok->pool_key);

        if(pool_counter->size > cok->pool_size) {
            cok->pool_size = pool_counter->size;
        }

        if(cok->pool_size > 0) {
            cok->ptr = get_connection_in_pool(_loop_fd, cok->pool_key, cok);

            if(cok->ptr) {
                ((se_ptr_t *) cok->ptr)->data = cok;
                cok->status = 2;
                cok->reusedtimes = 1;
                cok->fd = ((se_ptr_t *) cok->ptr)->fd;
                //printf("reuse %d\n", cok->fd);
                se_be_pri(cok->ptr, NULL);
                lua_pushboolean(L, 1);

                return 1;
            }

            if(pool_counter->count > 0 && pool_counter->count >= cok->pool_size / _process_count) {
                /// pool full
                if((cok->pool_wait = add_waiting_get_connection(cok))) {
                    cok->status = 3;
                    cok->timeout_ptr = add_timeout(cok, cok->timeout, timeout_handle);
                    //printf("wait %d\n", cok->fd);
                    cok->inuse = 1;
                    return lua_yield(L, 0);

                } else {
                    lua_pushnil(L);
                    lua_pushstring(L, "pool error");
                    return 2;
                }
            }
        }

        int fd = se_connect(_loop_fd, host, port, cok->timeout > 0 ? cok->timeout : 30000, be_connect, cok);

        if(fd != -2) {
            if(fd > -1) {
                connection_pool_counter_operate(cok->pool_key, 1);
                int ret = _be_connect(cok, fd, 0);

                if(ret == -2) {
                    return lua_yield(L, 0);
                }

                return ret;

            } else {
                lua_pushnil(L);
                lua_pushstring(L, strerror(errno));
                return 2;
            }
        }

        connection_pool_counter_operate(cok->pool_key, 1);
    }

    return lua_yield(L, 0);
}

int cosocket_be_write(se_ptr_t *ptr)
{
    cosocket_t *cok = ptr->data;
    int n = 0, ret = 0;
    cok->in_read_action = 0;

    if(!cok->ssl) {
        while((n = send(cok->fd, cok->send_buf + cok->send_buf_ed, cok->send_buf_len - cok->send_buf_ed, MSG_DONTWAIT)) > 0) {
            cok->send_buf_ed += n;
        }

    } else {
        while((n = SSL_write(cok->ssl, cok->send_buf + cok->send_buf_ed, cok->send_buf_len - cok->send_buf_ed)) > 0) {
            cok->send_buf_ed += n;
        }
    }

    if(cok->send_buf_ed == cok->send_buf_len || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            se_delete(cok->ptr);
            cok->ptr = NULL;
            connection_pool_counter_operate(cok->pool_key, -1);
            close(cok->fd);
            cok->fd = -1;
            cok->status = 0;
            cok->send_buf_ed = 0;

        }

        /* else {
            se_be_pri(cok->ptr, NULL);
        }*/

        if(cok->send_buf_need_free) {
            free(cok->send_buf_need_free);
            cok->send_buf_need_free = NULL;
        }

        delete_timeout(cok->timeout_ptr);
        cok->timeout_ptr = NULL;

        int rc = 1;

        if(cok->send_buf_ed >= cok->send_buf_len) {
            lua_pushnumber(cok->L, cok->send_buf_ed);

        } else if(cok->fd == -1) {
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "connection closed!");
            rc = 2;

        } else {
            lua_pushboolean(cok->L, 0);
        }

        if(cok->inuse == 1) {
            se_be_pri(cok->ptr, NULL);
            cok->inuse = 0;
            lua_f_lua_uthread_resume_in_c(cok->L, rc);
            return 0;

        } else {
            return 0 - rc;
        }
    }

    return cok->send_buf_len - cok->send_buf_ed;
}

static int lua_co_send(lua_State *L)
{
    cosocket_t *cok = NULL;
    {
        if(lua_gettop(L) < 2) {
            return 0;
        }

        int t = lua_type(L, 2);

        if(!lua_isuserdata(L, 1) || (t != LUA_TSTRING && t != LUA_TTABLE)) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "Error params!");
            return 2;
        }

        cok = (cosocket_t *) lua_touserdata(L, 1);

        if(cok->status != 2 || cok->fd == -1 || !cok->ptr) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "Not connected!");
            return 2;
        }

        if(cok->inuse == 1) {
            lua_pushnil(L);
            lua_pushstring(L, "socket busy!");
            return 2;
        }

        cok->L = L;
        cok->send_buf_ed = 0;

        if(t == LUA_TTABLE) {
            cok->send_buf_len = lua_calc_strlen_in_table(L, 2, 2, 1 /* strict */);

            if(cok->send_buf_len > 0) {
                if(cok->send_buf_len < _SENDBUF_SIZE) {
                    cok->send_buf_need_free = NULL;
                    lua_copy_str_in_table(L, 2, cok->_send_buf);
                    cok->send_buf = cok->_send_buf;

                } else {
                    cok->send_buf_need_free = large_malloc(cok->send_buf_len);

                    if(!cok->send_buf_need_free) {
                        LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
                        exit(1);
                    }

                    lua_copy_str_in_table(L, 2, cok->send_buf_need_free);
                    cok->send_buf = cok->send_buf_need_free;
                }
            }

        } else {
            const char *p = lua_tolstring(L, 2, &cok->send_buf_len);
            cok->send_buf_need_free = NULL;

            if(cok->send_buf_len < _SENDBUF_SIZE) {
                memcpy(cok->_send_buf, p, cok->send_buf_len);
                cok->send_buf = cok->_send_buf;

            } else {
                cok->send_buf_need_free = large_malloc(cok->send_buf_len);

                if(!cok->send_buf_need_free) {
                    LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
                    exit(1);
                }

                memcpy(cok->send_buf_need_free, p, cok->send_buf_len);
                cok->send_buf = cok->send_buf_need_free;
            }
        }

        if(cok->send_buf_len < 1) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "content empty!");
            return 2;
        }

        cok->inuse = 0;
        int ret = cosocket_be_write(cok->ptr);

        if(ret > 0) {
            if(
                se_be_write(cok->ptr, cosocket_be_write) == -1) {
                se_delete(cok->ptr);
                close(cok->fd);
                cok->ptr = NULL;
                cok->fd = -1;
                cok->status = 0;

                lua_pushnil(cok->L);
                lua_pushstring(cok->L, "Network Error");
                return 2;

            } else {

                cok->timeout_ptr = add_timeout(cok, cok->timeout, timeout_handle);
                cok->inuse = 1;
                return lua_yield(L, 0);
            }

        } else {
            return 0 - ret;
        }
    }
}

int lua_co_read_(cosocket_t *cok)
{
    if(cok->total_buf_len < 1) {
        if(cok->status == 0) {
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "Not connected!");
            return 2;
        }

        return 0;
    }

    size_t be_copy = cok->buf_read_len;

    if(cok->buf_read_len == -1) { // read line
        int i = 0;
        int oi = 0;
        int has = 0;
        cosocket_link_buf_t *nbuf = cok->read_buf;

        while(nbuf) {
            for(i = 0; i < nbuf->buf_len; i++) {
                if(nbuf->buf[i] == '\n') {
                    has = 1;
                    break;
                }
            }

            if(has == 1) {
                break;
            }

            oi += i;
            nbuf = nbuf->next;
        }

        i += oi;

        if(has == 1) {
            i += 1;
            be_copy = i;

        } else {
            return 0;
        }

    } else if(cok->buf_read_len == -2) {
        be_copy = cok->total_buf_len;
    }

    if(cok->status == 0) {
        if(be_copy > cok->total_buf_len) {
            be_copy = cok->total_buf_len;
        }
    }

    int kk = 0;

    if(be_copy > 0 && cok->total_buf_len >= be_copy) {
        char *buf2lua = large_malloc(be_copy);

        if(!buf2lua) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        size_t copy_len = be_copy;
        size_t copy_ed = 0;
        int this_copy_len = 0;
        cosocket_link_buf_t *bf = NULL;
        while(cok->read_buf) {
            this_copy_len = (cok->read_buf->buf_len + copy_ed > copy_len ? copy_len - copy_ed :
                             cok->read_buf->buf_len);
            if(this_copy_len > 0) {
                memcpy(buf2lua + copy_ed, cok->read_buf->buf, this_copy_len);
                copy_ed += this_copy_len;
                memmove(cok->read_buf->buf, cok->read_buf->buf + this_copy_len,
                        cok->read_buf->buf_len - this_copy_len);
                cok->read_buf->buf_len -= this_copy_len;
            }

            if(copy_ed >= be_copy) { /// not empty
                cok->total_buf_len -= copy_ed;

                if(cok->buf_read_len == -1) { /// read line , cut the \r \n
                    if(buf2lua[be_copy - 1] == '\n') {
                        be_copy -= 1;
                    }

                    if(buf2lua[be_copy - 1] == '\r') {
                        be_copy -= 1;
                    }
                }

                lua_pushlstring(cok->L, buf2lua, be_copy);
                free(buf2lua);
                return 1;

            } else {
                bf = cok->read_buf;
                cok->read_buf = cok->read_buf->next;

                if(cok->last_buf == bf) {
                    cok->last_buf = NULL;
                    cok->read_buf = NULL;
                }

                free(bf->buf);
                free(bf);
            }
        }
        free(buf2lua);
    }

    return 0;
}

int cosocket_be_read(se_ptr_t *ptr)
{
    cosocket_t *cok = ptr->data;
    int n = 0, ret = 0;

init_read_buf:

    if(!cok->read_buf
       || (cok->last_buf->buf_len >= cok->last_buf->buf_size)) {    /// init read buf
        cosocket_link_buf_t *nbuf = NULL;
        nbuf = malloc(sizeof(cosocket_link_buf_t));

        if(nbuf == NULL) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        nbuf->buf = large_malloc(4096);

        if(!nbuf->buf) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        nbuf->buf_size = 4096;
        nbuf->buf_len = 0;
        nbuf->next = NULL;

        if(cok->read_buf) {
            cok->last_buf->next = nbuf;

        } else {
            cok->read_buf = nbuf;
        }

        cok->last_buf = nbuf;
    }

    if(!cok->ssl) {
        while((n = recv(cok->fd, cok->last_buf->buf + cok->last_buf->buf_len, cok->last_buf->buf_size - cok->last_buf->buf_len, 0)) > 0) {
            cok->last_buf->buf_len += n;
            cok->total_buf_len += n;

            if(cok->last_buf->buf_len >= cok->last_buf->buf_size) {
                goto init_read_buf;
            }
        }

    } else {
        while((n = SSL_read(cok->ssl, cok->last_buf->buf + cok->last_buf->buf_len,
                            cok->last_buf->buf_size - cok->last_buf->buf_len)) > 0) {
            cok->last_buf->buf_len += n;
            cok->total_buf_len += n;

            if(cok->last_buf->buf_len >= cok->last_buf->buf_size) {
                goto init_read_buf;
            }
        }

    }
    if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int rfd = cok->fd;
        /// socket closed
        delete_timeout(cok->timeout_ptr);
        cok->timeout_ptr = NULL;
        {
            cok->status = 0;

            se_delete(cok->ptr);
            cok->ptr = NULL;
            connection_pool_counter_operate(cok->pool_key, -1);
            close(cok->fd);
            cok->fd = -1;
            cok->status = 0;
        }

        if(cok->in_read_action == 1) {
            cok->in_read_action = 0;
            int rt = lua_co_read_(cok);
            cok->inuse = 0;

            if(rt > 0) {
                ret = lua_f_lua_uthread_resume_in_c(cok->L, rt);

            } else if(n == 0) {
                lua_pushnil(cok->L);
                ret = lua_f_lua_uthread_resume_in_c(cok->L, 1);
            }

            if(ret == LUA_ERRRUN) {
                se_delete(cok->ptr);
                cok->ptr = NULL;
                connection_pool_counter_operate(cok->pool_key, -1);
                close(cok->fd);
                cok->fd = -1;
                cok->status = 0;
            }
        }

    } else {
        if(cok->in_read_action == 1) {
            int rt = lua_co_read_(cok);

            if(rt > 0) {
                cok->in_read_action = 0;
                delete_timeout(cok->timeout_ptr);
                cok->timeout_ptr = NULL;
                cok->inuse = 0;

                ret = lua_f_lua_uthread_resume_in_c(cok->L, rt);

                if(ret == LUA_ERRRUN) {
                    se_delete(cok->ptr);
                    cok->ptr = NULL;
                    connection_pool_counter_operate(cok->pool_key, -1);
                    close(cok->fd);
                    cok->fd = -1;
                    cok->status = 0;
                }
            }
        }
    }

    return 0;
}

static int lua_co_read(lua_State *L)
{
    cosocket_t *cok = NULL;
    {
        if(!lua_isuserdata(L, 1)) {
            lua_pushnil(L);
            lua_pushstring(L, "Error params!");
            return 2;
        }

        cok = (cosocket_t *) lua_touserdata(L, 1);

        if((cok->status != 2 || cok->fd == -1 || !cok->ptr) && cok->total_buf_len < 1) {
            lua_pushnil(L);
            lua_pushfstring(L, "Not connected!");
            return 2;
        }

        if(cok->inuse == 1) {
            lua_pushnil(L);
            lua_pushstring(L, "socket busy!");
            return 2;
        }

        cok->L = L;
        cok->buf_read_len = -1; /// read line

        if(lua_isnumber(L, 2)) {
            cok->buf_read_len = lua_tonumber(L, 2);

            if(cok->buf_read_len < 0) {
                cok->buf_read_len = 0;
                lua_pushnil(L);
                lua_pushstring(L, "Error params!");
                return 2;
            }

        } else {
            if(lua_isstring(L, 2)) {
                if(strcmp("*a", lua_tostring(L, 2)) == 0) {
                    cok->buf_read_len = -2;    /// read all
                }
            }
        }
        int rt = lua_co_read_(cok);
        if(rt > 0) {
            return rt; // has buf
        }
        if(cok->fd == -1) {
            lua_pushnil(L);
            lua_pushstring(L, "Not connected!");
            return 2;
        }
        if(cok->in_read_action != 1) {
            cok->in_read_action = 1;

            if(se_be_read(cok->ptr, cosocket_be_read) == -1) {
                close(cok->fd);
                cok->fd = -1;

                lua_pushnil(L);
                lua_pushstring(L, "Network Error!");
                return 2;
            }

        }

        cok->timeout_ptr = add_timeout(cok, cok->timeout, timeout_handle);
    }
    cok->inuse = 1;
    return lua_yield(L, 0);
}

static int _lua_co_close(lua_State *L, cosocket_t *cok)
{
    if(cok->read_buf) {
        cosocket_link_buf_t *fr = cok->read_buf;
        cosocket_link_buf_t *nb = NULL;

        while(fr) {
            nb = fr->next;
            free(fr->buf);
            free(fr);
            fr = nb;
        }

        cok->read_buf = NULL;
    }

    if(cok->send_buf_need_free) {
        free(cok->send_buf_need_free);
        cok->send_buf_need_free = NULL;
    }

    if(cok->pool_wait) {
        delete_in_waiting_get_connection(cok->pool_wait);
        cok->pool_wait = NULL;
    }

    delete_timeout(cok->timeout_ptr);
    cok->timeout_ptr = NULL;

    cok->status = 0;

    if(cok->fd > -1) {
        ((se_ptr_t *) cok->ptr)->fd = cok->fd;

        if(cok->pool_size < 1
           || add_connection_to_pool(_loop_fd, cok->pool_key, cok->pool_size, cok->ptr, cok->ssl,
                                     cok->ctx, cok->ssl_pw) == 0) {
            se_delete(cok->ptr);
            cok->ptr = NULL;

            connection_pool_counter_operate(cok->pool_key, -1);
            close(cok->fd);

            if(cok->ssl) {
                SSL_free(cok->ssl);
                cok->ssl = NULL;
            }

            if(cok->ctx) {
                SSL_CTX_free(cok->ctx);
                cok->ctx = NULL;
            }

            if(cok->ssl_pw) {
                free(cok->ssl_pw);
                cok->ssl_pw = NULL;
            }

        }

        cok->ssl = NULL;
        cok->ctx = NULL;
        cok->ssl_pw = NULL;

        cok->ptr = NULL;
        cok->fd = -1;
    }
}

static int lua_co_close(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);

    if(cok->status != 2) {
        lua_pushnil(L);
        lua_pushstring(L, "Not connected!");
        return 2;
    }

    if(cok->inuse == 1) {
        lua_pushnil(L);
        lua_pushstring(L, "socket busy!");
        return 2;
    }

    _lua_co_close(L, cok);
    return 0;
}

static int lua_co_gc(lua_State *L)
{
    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);
    _lua_co_close(L, cok);

    return 0;
}

int lua_co_getreusedtimes(lua_State *L)
{
    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);
    lua_pushnumber(L, cok->reusedtimes);
    return 1;
}

int lua_co_settimeout(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);
    cok->timeout = lua_tonumber(L, 2);
    return 0;
}

int lua_co_setkeepalive(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);
    cok->pool_size = lua_tonumber(L, 2);

    if(cok->pool_size < 0) {
        cok->pool_size = 0;

    } else if(cok->pool_size > 4096) {
        cok->pool_size = 4096;
    }

    cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter(cok->pool_key);

    if(cok->pool_size > pool_counter->size) {
        pool_counter->size = cok->pool_size;
    }

    if(lua_gettop(L) == 3 && lua_isstring(L, 3)) {
        size_t len = 0;
        const char *key = lua_tolstring(L, 3, &len);
        cok->pool_key = fnv1a_32(key, len);
    }

    _lua_co_close(L, cok);

    lua_pushboolean(L, 1);
    return 1;
}


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

static const luaL_reg M[] = {
    {"connect", lua_co_connect},
    {"send", lua_co_send},
    {"read", lua_co_read},
    {"receive", lua_co_read},
    {"settimeout", lua_co_settimeout},
    {"setkeepalive", lua_co_setkeepalive},
    {"getreusedtimes", lua_co_getreusedtimes},

    {"close", lua_co_close},
    {"__gc", lua_co_gc},

    {NULL, NULL}
};

static int lua_co_tcp(lua_State *L)
{
    cosocket_t *cok = (cosocket_t *) lua_newuserdata(L, sizeof(cosocket_t));

    if(!cok) {
        lua_pushnil(L);
        lua_pushstring(L, "stack error!");
        return 2;
    }

    bzero(cok, sizeof(cosocket_t));

    if(lua_isstring(L, 1) && lua_isstring(L, 2)) {
        if(cok->ctx) {
            SSL_CTX_free(cok->ctx);
        }

        cok->ctx = SSL_CTX_new(SSLv23_client_method());

        if(cok->ctx == NULL) {
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "SSL_CTX_new Error");
            return 2;
        }

        if(lua_isstring(L, 3)) {
            if(cok->ssl_pw) {
                free(cok->ssl_pw);
            }

            size_t len = 0;
            const char *p1 = lua_tolstring(L, 3, &len);
            cok->ssl_pw = malloc(len + 1);

            if(cok->ssl_pw) {
                memcpy(cok->ssl_pw, p1, len);
                cok->ssl_pw[len] = '\0';
                SSL_CTX_set_default_passwd_cb_userdata(cok->ctx, (void *)cok->ssl_pw);
                SSL_CTX_set_default_passwd_cb(cok->ctx, ssl_password_cb);
            }

            int l = snprintf(temp_buf, 4096, "%s:%s:%s", lua_tostring(L, 1), lua_tostring(L, 2), p1);
            cok->ssl_sign = fnv1a_32(temp_buf, l);

        } else {
            int l = snprintf(temp_buf, 4096, "%s:%s:", lua_tostring(L, 1), lua_tostring(L, 2));
            cok->ssl_sign = fnv1a_32(temp_buf, l);
        }

        if(SSL_CTX_use_certificate_file(cok->ctx, lua_tostring(L, 1), SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(cok->ctx);
            cok->ctx = NULL;
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "SSL_CTX_use_certificate_file Error");
            return 2;
        }

        if(SSL_CTX_use_PrivateKey_file(cok->ctx, lua_tostring(L, 2), SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(cok->ctx);
            cok->ctx = NULL;
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "SSL_CTX_use_PrivateKey_file Error");
            return 2;
        }

        if(!SSL_CTX_check_private_key(cok->ctx)) {
            SSL_CTX_free(cok->ctx);
            cok->ctx = NULL;
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "SSL_CTX_check_private_key Error");
            return 2;
        }

        cok->use_ssl = 1;

    } else if(lua_isboolean(L, 1) && lua_toboolean(L, 1) == 1) {
        cok->use_ssl = 1;

    } else {
        cok->use_ssl = 0;
    }

    cok->L = L;
    cok->fd = -1;
    cok->timeout = 30000;
    /*
    if(luaL_newmetatable(L, "cosocket")) {
        //luaL_checkstack(L, 1, "not enough stack to register connection MT");
        lua_pushvalue(L, lua_upvalueindex(1));
        setfuncs(L, M, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }*/
    luaL_getmetatable(L, "cosocket:tcp");
    lua_setmetatable(L, -2);

    return 1;
}

static int lua_co_dup_setpeername(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);

    if(cok->fd > -1) {
        se_delete(cok->ptr);
        cok->ptr = NULL;
        close(cok->fd);
    }

    if(lua_isnumber(L, 3)) {
        int fd = 0, slen = sizeof(cok->addr);
        bzero(&cok->addr, slen);
        cok->addr.sin_family = AF_INET;
        cok->addr.sin_port = htons(lua_tonumber(L, 3));
        const char *host = lua_tostring(L, 2);

        if(inet_aton(host, &cok->addr.sin_addr) == 0) {
            struct hostent *hp = NULL;

            if((hp = gethostbyname(host)) == 0) {
                lua_pushnil(L);
                lua_pushstring(L, "Nslookup Error!");
                return 2;
            }

            memcpy(&cok->addr.sin_addr.s_addr, hp->h_addr, hp->h_length);
        }

        if((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
            lua_pushnil(L);
            lua_pushstring(L, "Connect Error!");
            return 2;
        }

        if(!se_set_nonblocking(fd , 1)) {
            close(fd);
            lua_pushnil(L);
            lua_pushstring(L, "Connect Error!");
            return 2;
        }

        cok->ptr = se_add(_loop_fd, fd, cok);
        cok->fd = fd;
        cok->status = 2;
        lua_pushboolean(L, 1);
        return 1;
    }

    return 0;
}

static int lua_co_udp_send(lua_State *L)
{
    if(!lua_isuserdata(L, 1) || !lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);

    if(cok->fd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Not Connected!");
        return 2;
    }

    size_t len = 0;
    const char *buf = lua_tolstring(L, 2, &len);

    if(sendto(cok->fd, buf, len, 0, (struct sockaddr *)&cok->addr, sizeof(cok->addr)) == -1) {
        lua_pushnil(L);
        lua_pushstring(L, "Send Error!");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_reg M_UDP[] = {
    {"setpeername", lua_co_dup_setpeername},
    {"send", lua_co_udp_send},

    {"read", lua_co_read},
    {"receive", lua_co_read},
    {"settimeout", lua_co_settimeout},

    {"close", lua_co_close},
    {"__gc", lua_co_gc},

    {NULL, NULL}
};

static int lua_co_udp(lua_State *L)
{
    cosocket_t *cok = (cosocket_t *) lua_newuserdata(L, sizeof(cosocket_t));

    if(!cok) {
        lua_pushnil(L);
        lua_pushstring(L, "stack error!");
        return 2;
    }

    bzero(cok, sizeof(cosocket_t));

    if(lua_isboolean(L, 1) && lua_toboolean(L, 1) == 1) {
        cok->use_ssl = 1;

    } else {
        cok->use_ssl = 0;
    }

    cok->L = L;
    cok->fd = -1;
    cok->timeout = 30000;
    luaL_getmetatable(L, "cosocket:udp");
    lua_setmetatable(L, -2);

    return 1;
}


static void popen_timeout_handle(void *ptr)
{
    cosocket_t *cok = ptr;
    delete_timeout(cok->timeout_ptr);
    cok->timeout_ptr = NULL;

    if(cok->pool_wait) {
        delete_in_waiting_get_connection(cok->pool_wait);
        cok->pool_wait = NULL;
    }

    lua_pushnil(cok->L);

    if(cok->ptr) {
        if(cok->status == 3) {
            lua_pushstring(cok->L, "Connect error!(wait pool timeout)");

        } else if(((se_ptr_t *) cok->ptr)->wfunc == cosocket_be_ssl_connected) {
            lua_pushstring(cok->L, "SSL Connect timeout!");

        } else if(((se_ptr_t *) cok->ptr)->wfunc == cosocket_be_write) {
            lua_pushstring(cok->L, "Send timeout!");

        } else if(((se_ptr_t *) cok->ptr)->rfunc == cosocket_be_read) {
            lua_pushstring(cok->L, "Read timeout!");

        } else {
            lua_pushstring(cok->L, "Timeout!");
        }

    } else {
        if(cok->status == 3) {
            lua_pushstring(cok->L, "Connect error!(wait pool timeout)");

        } else {
            lua_pushstring(cok->L, "Timeout!");
        }
    }

    {
        se_delete(cok->ptr);
        cok->ptr = NULL;

        if(cok->fd > -1) {
            pclose(cok->fp);
            cok->fd = -1;
        }

        cok->status = 0;
    }

    if(cok->read_buf) {
        cosocket_link_buf_t *fr = cok->read_buf;
        cosocket_link_buf_t *nb = NULL;

        while(fr) {
            nb = fr->next;
            free(fr->buf);
            free(fr);
            fr = nb;
        }

        cok->read_buf = NULL;
    }

    if(cok->send_buf_need_free) {
        free(cok->send_buf_need_free);
        cok->send_buf_need_free = NULL;
    }


    cok->inuse = 0;

    lua_f_lua_uthread_resume_in_c(cok->L, 2);
}
int lua_co_popen_read_(cosocket_t *cok)
{
    if(cok->total_buf_len < 1) {
        if(cok->status == 0) {
            lua_pushnil(cok->L);
            lua_pushstring(cok->L, "Not connected!");
            return 2;
        }

        return 0;
    }

    size_t be_copy = cok->buf_read_len;

    if(cok->buf_read_len == -1) { // read line
        int i = 0;
        int oi = 0;
        int has = 0;
        cosocket_link_buf_t *nbuf = cok->read_buf;

        while(nbuf) {
            for(i = 0; i < nbuf->buf_len; i++) {
                if(nbuf->buf[i] == '\n') {
                    has = 1;
                    break;
                }
            }

            if(has == 1) {
                break;
            }

            oi += i;
            nbuf = nbuf->next;
        }

        i += oi;

        if(has == 1) {
            i += 1;
            be_copy = i;

        } else {
            return 0;
        }

    } else if(cok->buf_read_len == -2) {
        be_copy = cok->total_buf_len;
    }

    if(cok->status == 0) {
        if(be_copy > cok->total_buf_len) {
            be_copy = cok->total_buf_len;
        }
    }

    int kk = 0;

    if(be_copy > 0 && cok->total_buf_len >= be_copy) {
        char *buf2lua = large_malloc(be_copy);

        if(!buf2lua) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        size_t copy_len = be_copy;
        size_t copy_ed = 0;
        int this_copy_len = 0;
        cosocket_link_buf_t *bf = NULL;
        while(cok->read_buf) {
            this_copy_len = (cok->read_buf->buf_len + copy_ed > copy_len ? copy_len - copy_ed :
                             cok->read_buf->buf_len);
            if(this_copy_len > 0) {
                memcpy(buf2lua + copy_ed, cok->read_buf->buf, this_copy_len);
                copy_ed += this_copy_len;
                memmove(cok->read_buf->buf, cok->read_buf->buf + this_copy_len,
                        cok->read_buf->buf_len - this_copy_len);
                cok->read_buf->buf_len -= this_copy_len;
            }

            if(copy_ed >= be_copy) { /// not empty
                cok->total_buf_len -= copy_ed;

                if(cok->buf_read_len == -1) { /// read line , cut the \r \n
                    if(buf2lua[be_copy - 1] == '\n') {
                        be_copy -= 1;
                    }

                    if(buf2lua[be_copy - 1] == '\r') {
                        be_copy -= 1;
                    }
                }

                lua_pushlstring(cok->L, buf2lua, be_copy);
                free(buf2lua);
                return 1;

            } else {
                bf = cok->read_buf;
                cok->read_buf = cok->read_buf->next;

                if(cok->last_buf == bf) {
                    cok->last_buf = NULL;
                    cok->read_buf = NULL;
                }

                free(bf->buf);
                free(bf);
            }
        }
        free(buf2lua);
    }

    return 0;
}

int cosocket_be_popen_read(se_ptr_t *ptr)
{
    cosocket_t *cok = ptr->data;
    int n = 0, ret = 0;

init_read_buf:

    if(!cok->read_buf
       || (cok->last_buf->buf_len >= cok->last_buf->buf_size)) {    /// init read buf
        cosocket_link_buf_t *nbuf = NULL;
        nbuf = malloc(sizeof(cosocket_link_buf_t));

        if(nbuf == NULL) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        nbuf->buf = large_malloc(4096);

        if(!nbuf->buf) {
            LOGF(ERR, "malloc error @%s:%d\n", __FILE__, __LINE__);
            exit(1);
        }

        nbuf->buf_size = 4096;
        nbuf->buf_len = 0;
        nbuf->next = NULL;

        if(cok->read_buf) {
            cok->last_buf->next = nbuf;

        } else {
            cok->read_buf = nbuf;
        }

        cok->last_buf = nbuf;
    }

    while((n = read(cok->fd, cok->last_buf->buf + cok->last_buf->buf_len, cok->last_buf->buf_size - cok->last_buf->buf_len)) > 0) {
        cok->last_buf->buf_len += n;
        cok->total_buf_len += n;
        if(cok->last_buf->buf_len >= cok->last_buf->buf_size) {
            goto init_read_buf;
        }
    }

    if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int rfd = cok->fd;
        /// socket closed
        delete_timeout(cok->timeout_ptr);
        cok->timeout_ptr = NULL;
        {
            cok->status = 0;

            se_delete(cok->ptr);
            cok->ptr = NULL;
            pclose(cok->fp);
            cok->fd = -1;
            cok->status = 0;
        }

        if(cok->in_read_action == 1) {
            cok->in_read_action = 0;
            int rt = lua_co_popen_read_(cok);
            cok->inuse = 0;

            if(rt > 0) {
                ret = lua_f_lua_uthread_resume_in_c(cok->L, rt);

            } else if(n == 0) {
                lua_pushnil(cok->L);
                ret = lua_f_lua_uthread_resume_in_c(cok->L, 1);
            }

            if(ret == LUA_ERRRUN) {
                se_delete(cok->ptr);
                cok->ptr = NULL;
                pclose(cok->fp);
                cok->fd = -1;
                cok->status = 0;
            }
        }

    } else {
        if(cok->in_read_action == 1) {
            int rt = lua_co_popen_read_(cok);

            if(rt > 0) {
                cok->in_read_action = 0;
                delete_timeout(cok->timeout_ptr);
                cok->timeout_ptr = NULL;
                cok->inuse = 0;

                ret = lua_f_lua_uthread_resume_in_c(cok->L, rt);

                if(ret == LUA_ERRRUN) {
                    se_delete(cok->ptr);
                    cok->ptr = NULL;
                    pclose(cok->fp);
                    cok->fd = -1;
                    cok->status = 0;
                }
            }
        }
    }

    return 0;
}

static int lua_co_popen_read(lua_State *L)
{
    cosocket_t *cok = NULL;
    {
        if(!lua_isuserdata(L, 1)) {
            lua_pushnil(L);
            lua_pushstring(L, "Error params!");
            return 2;
        }

        cok = (cosocket_t *) lua_touserdata(L, 1);

        if((cok->status != 2 || cok->fd == -1 || !cok->ptr) && cok->total_buf_len < 1) {
            lua_pushnil(L);
            lua_pushfstring(L, "Not connected!");
            return 2;
        }

        if(cok->inuse == 1) {
            lua_pushnil(L);
            lua_pushstring(L, "socket busy!");
            return 2;
        }

        cok->L = L;
        cok->buf_read_len = -1; /// read line

        if(lua_isnumber(L, 2)) {
            cok->buf_read_len = lua_tonumber(L, 2);

            if(cok->buf_read_len < 0) {
                cok->buf_read_len = 0;
                lua_pushnil(L);
                lua_pushstring(L, "Error params!");
                return 2;
            }

        } else {
            if(lua_isstring(L, 2)) {
                if(strcmp("*a", lua_tostring(L, 2)) == 0) {
                    cok->buf_read_len = -2;    /// read all
                }
            }
        }
        int rt = lua_co_popen_read_(cok);
        if(rt > 0) {
            return rt; // has buf
        }
        if(cok->fd == -1) {
            lua_pushnil(L);
            lua_pushstring(L, "Not connected!");
            return 2;
        }
        if(cok->in_read_action != 1) {
            cok->in_read_action = 1;

            if(se_be_read(cok->ptr, cosocket_be_popen_read) == -1) {
                pclose(cok->fp);
                cok->fd = -1;

                lua_pushnil(L);
                lua_pushstring(L, "Network Error!");
                return 2;
            }

        }

        cok->timeout_ptr = add_timeout(cok, cok->timeout, popen_timeout_handle);
    }
    cok->inuse = 1;
    return lua_yield(L, 0);
}

static int lua_co_popen(lua_State *L)
{
    if(lua_gettop(L) < 1){
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    const char* cmd = lua_tostring(L, 1);
    FILE * fp = popen(cmd, "r");
    if(!fp){
        lua_pushnil(L);
        lua_pushstring(L, "popen error!");
        return 2;
    }

    cosocket_t *cok = (cosocket_t *) lua_newuserdata(L, sizeof(cosocket_t));
    if(!cok) {
        lua_pushnil(L);
        lua_pushstring(L, "stack error!");
        return 2;
    }
    bzero(cok, sizeof(cosocket_t));

    cok->fp = fp;

    cok->L = L;

    cok->fd = fileno(fp);
    se_set_nonblocking(cok->fd , 1);
    cok->ptr = se_add(_loop_fd, cok->fd, cok);
    se_be_pri(cok->ptr, NULL);
    cok->status = 2;
    cok->ssl = 0;
    cok->in_read_action = 0;
    cok->inuse = 0;

    cok->timeout = 30000;
    cok->read_buf = NULL;
    cok->last_buf = NULL;
    cok->total_buf_len = 0;
    cok->buf_read_len = 0;
    luaL_getmetatable(L, "cosocket:popen");
    lua_setmetatable(L, -2);

    return 1;
}
static int lua_co_popen_close(lua_State *L)
{
    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }
    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);

    if(cok->status != 2) {
        lua_pushnil(L);
        lua_pushstring(L, "Not connected!");
        return 2;
    }

    if(cok->inuse == 1) {
        lua_pushnil(L);
        lua_pushstring(L, "socket busy!");
        return 2;
    }
    if(cok->fd > -1){
        se_delete(cok->ptr);
        cok->ptr = NULL;

        pclose(cok->fp);
        cok->fd = -1;
    }

    _lua_co_close(L, cok);
    return 0;
}

static int lua_co_popen_gc(lua_State *L)
{
    cosocket_t *cok = (cosocket_t *) lua_touserdata(L, 1);

    if(!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Error params!");
        return 2;
    }

    if(cok->status != 2) {
        lua_pushnil(L);
        lua_pushstring(L, "Not connected!");
        return 2;
    }

    if(cok->inuse == 1) {
        lua_pushnil(L);
        lua_pushstring(L, "socket busy!");
        return 2;
    }

    if(cok->fd > -1){
        se_delete(cok->ptr);
        cok->ptr = NULL;
        pclose(cok->fp);
        cok->fd = -1;
    }

    _lua_co_close(L, cok);

    return 0;
}
static const luaL_reg M_POPEN[] = {
    {"read", lua_co_popen_read},
    {"settimeout", lua_co_settimeout},

    {"close", lua_co_popen_close},
    {"__gc", lua_co_popen_gc},

    {NULL, NULL}
};

int swop_counter = 0;
cosocket_swop_t *swop_top = NULL;
cosocket_swop_t *swop_lat = NULL;

int lua_f_coroutine_swop(lua_State *L)
{
    if(swop_counter++ < 800) {
        lua_pushboolean(L, 0);
        return 1;
    }

    cosocket_swop_t *swop = malloc(sizeof(cosocket_swop_t));

    if(swop == NULL) {
        lua_pushboolean(L, 0);
        return 1;
    }

    swop_counter = 0;
    swop->L = L;
    swop->next = NULL;

    if(swop_lat != NULL) {
        swop_lat->next = swop;

    } else {
        swop_top->next = swop;
    }

    swop_lat = swop;
    return lua_yield(L, 0);
}

static lua_State *job_L = NULL;

void set_loop_fd(int fd, int __process_count)    /// for alilua-serv
{
    _loop_fd = fd;

    if(__process_count > 1) {
        _process_count = __process_count;
    }
}

int coevnet_module_do_other_jobs()
{
    swop_counter = swop_counter / 2;

    get_connection_in_pool(_loop_fd, 0, NULL);
    check_lua_sleep_timeouts();

    /// resume swops
    cosocket_swop_t *swop = NULL;

    if(swop_top->next != NULL) {
        swop = swop_top->next;
        swop_top->next = swop->next;

        if(swop_top->next == NULL) {
            swop_lat = NULL;
        }

        lua_State *L = swop->L;
        free(swop);
        swop = NULL;

        if(lua_status(L) == LUA_YIELD) {
            //lua_pushboolean ( L, 1 );
            lua_f_lua_uthread_resume_in_c(L, 0);
        }
    }
}

static int _do_other_jobs()
{
    coevnet_module_do_other_jobs();

    if(lua_status(job_L) != LUA_YIELD) {
        return 0;
    }

    return 1;
}

static const struct luaL_reg cosocket_methods[] = {
    { "tcp", lua_co_tcp },
    { "udp", lua_co_udp },
    { "popen", lua_co_popen },
    { NULL, NULL }
};

static void on_exit_handler()
{
    //some things
    if(LOGF_T != NULL) {
        log_destory(LOGF_T);
        LOGF_T = NULL;
    }
}

int lua_f_startloop(lua_State *L)
{
    if(_loop_fd == -1) {
        _loop_fd = se_create(4096);
        attach_on_exit(on_exit_handler);
        atexit(on_exit_handler);
    }

    luaL_argcheck(L, lua_isfunction(L, 1)
                  && !lua_iscfunction(L, 1), 1, "Lua function expected");
    job_L = lua_newthread(L);
    /*
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_xmove(L, job_L, 1);
        lua_replace(job_L, LUA_GLOBALSINDEX);
        lua_xmove(LM, L, 1);*/

    lua_pushvalue(L, 1);    /* copy entry function to top of L*/
    lua_xmove(L, job_L, 1);    /* move entry function from L to co */

    lua_pushvalue(L, 1);    /* move function to top */
    lua_xmove(L, job_L, 1);    /* move function from L to job_L */

    if(lua_resume(job_L, 0) == LUA_ERRRUN) {
        lua_gc(job_L, LUA_GCCOLLECT, 0);

        if(lua_isstring(job_L, -1)) {
            luaL_error(L, lua_tostring(job_L, -1));
        }

        lua_pop(job_L, 1);
    }

    LM = job_L;

    se_loop(_loop_fd, 10, _do_other_jobs);

    return 0;
}

int luaopen_coevent(lua_State *L)
{
    luaopen_uthread(L);

    LM = L;
    _loop_fd = -1;

    swop_top = malloc(sizeof(cosocket_swop_t));
    swop_top->next = NULL;

    pid = getpid();
    update_time();

    SSL_library_init();
    OpenSSL_add_all_algorithms();

    lua_pushlightuserdata(L, NULL);
    lua_setglobal(L, "null");

    lua_register(L, "startloop", lua_f_startloop);
    lua_register(L, "swop", lua_f_coroutine_swop);
    lua_register(L, "sleep", lua_f_sleep);
    lua_register(L, "md5", lua_f_md5);
    lua_register(L, "sha1bin", lua_f_sha1bin);
    lua_register(L, "hmac_sha1", lua_f_hmac_sha1);
    lua_register(L, "base64_encode", lua_f_base64_encode);
    lua_register(L, "base64_decode", lua_f_base64_decode);
    lua_register(L, "base64_encode_url", lua_f_base64_encode_url);
    lua_register(L, "base64_decode_url", lua_f_base64_decode_url);
    lua_register(L, "escape", cosocket_lua_f_escape);
    lua_register(L, "escape_uri", lua_f_escape_uri);
    lua_register(L, "unescape_uri", lua_f_unescape_uri);
    lua_register(L, "time", lua_f_time);
    lua_register(L, "longtime", lua_f_longtime);
    lua_register(L, "build_dns_query_packet", lua_co_build_dns_query_packet);
    lua_register(L, "parse_dns_result", lua_co_parse_dns_result);
    lua_register(L, "LOG", lua_f_log);
    lua_register(L, "open_log", lua_f_open_log);
    luaopen_eio(L);

    luaL_loadstring(L, " \
DEBUG,INFO,NOTICE,WARN,ALERT,ERR = 1,2,3,4,5,6 \
coroutine_resume=coroutine.resume \
coroutine_create=coroutine.create \
coroutine_wait=coroutine.wait \
wait=coroutine.wait \
newthread=coroutine.spawn \
newco = newthread ");

    lua_pcall(L, 0, 0, 0);

    luaL_newmetatable(L, "cosocket:tcp");
    lua_pushvalue(L, lua_upvalueindex(1));
    setfuncs(L, M, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newmetatable(L, "cosocket:udp");
    lua_pushvalue(L, lua_upvalueindex(1));
    setfuncs(L, M_UDP, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newmetatable(L, "cosocket:popen");
    lua_pushvalue(L, lua_upvalueindex(1));
    setfuncs(L, M_POPEN, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    static const struct luaL_reg _MT[] = {{NULL, NULL}};
    luaL_openlib(L, "cosocket", cosocket_methods, 0);

    luaL_newmetatable(L, "cosocket*");
    luaL_register(L, NULL, _MT);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);               /* dup methods table*/
    lua_rawset(L, -3);                  /* metatable.__index = methods */
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);               /* dup methods table*/
    lua_rawset(L, -3);                  /* hide metatable:
                                         metatable.__metatable = methods */
    lua_pop(L, 1);                      /* drop metatable */

    lua_pushcfunction(L, lua_f_startloop);
    return 1;
}