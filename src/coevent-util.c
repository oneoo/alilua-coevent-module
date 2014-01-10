#include "coevent.h"
#include "connection-pool.h"

static char tbuf_4096[4096] = {};

#define COEVENT_TIMEOUT_LINK_SIZE 256
static void *timeout_links[COEVENT_TIMEOUT_LINK_SIZE] = {0};

int add_to_timeout_link(cosocket_t *cok, int timeout)
{
    int p = ((long) cok) % COEVENT_TIMEOUT_LINK_SIZE;
    timeout_link_t  *_tl = NULL,
                     *_tll = NULL,
                      *_ntl = NULL;

    if(timeout < 10) {
        timeout = 1000;
    }

    if(timeout_links[p] == NULL) {
        _ntl = malloc(sizeof(timeout_link_t));

        if(_ntl == NULL) {
            return 0;
        }

        _ntl->cok = cok;
        _ntl->uper = NULL;
        _ntl->next = NULL;
        _ntl->timeout = longtime() + timeout;
        timeout_links[p] = _ntl;
        return 1;

    } else {
        _tl = timeout_links[p];

        while(_tl) {
            _tll = _tl; /// get last item

            if(_tl->cok == cok) {
                break;
            }

            _tl = _tl->next;
        }

        if(_tll != NULL) {
            _ntl = malloc(sizeof(timeout_link_t));

            if(_ntl == NULL) {
                return 0;
            }

            _ntl->cok = cok;
            _ntl->uper = _tll;
            _ntl->next = NULL;
            _ntl->timeout = longtime() + timeout;
            _tll->next = _ntl;
            return 1;
        }
    }

    return 0;
}

int del_in_timeout_link(cosocket_t *cok)
{
    int p = ((long) cok) % COEVENT_TIMEOUT_LINK_SIZE;
    timeout_link_t  *_tl = NULL,
                     *_utl = NULL,
                      *_ntl = NULL;

    if(timeout_links[p] == NULL) {
        return 0;

    } else {
        _tl = timeout_links[p];

        while(_tl) {
            if(_tl->cok == cok) {
                _utl = _tl->uper;
                _ntl = _tl->next;

                if(_utl == NULL) {
                    timeout_links[p] = _ntl;

                    if(_ntl != NULL) {
                        _ntl->uper = NULL;
                    }

                } else {
                    _utl->next = _tl->next;

                    if(_ntl != NULL) {
                        _ntl->uper = _utl;
                    }
                }

                free(_tl);
                return 1;
            }

            _tl = _tl->next;
        }
    }

    return 0;
}

int chk_do_timeout_link(int loop_fd)
{
    long nt = longtime();
    timeout_link_t  *_tl = NULL,
                     *_ttl = NULL,
                      *_utl = NULL,
                       *_ntl = NULL;
    int i = 0;

    for(i = 0; i < COEVENT_TIMEOUT_LINK_SIZE; i++) {
        if(timeout_links[i] == NULL) {
            continue;
        }

        _tl = timeout_links[i];

        while(_tl) {
            _ttl = _tl->next;

            if(nt >= _tl->timeout) {
                _utl = _tl->uper;
                _ntl = _tl->next;

                if(_utl == NULL) {
                    timeout_links[i] = _ntl;

                    if(_ntl != NULL) {
                        _ntl->uper = NULL;
                    }

                } else {
                    _utl->next = _tl->next;

                    if(_ntl != NULL) {
                        _ntl->uper = _utl;
                    }
                }

                cosocket_t *cok = _tl->cok;
                free(_tl);

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
                    lua_pushstring(cok->L, "Timeout!");
                }

                {
                    se_delete(cok->ptr);
                    cok->ptr = NULL;
                    close(cok->fd);
                    connection_pool_counter_operate(cok->pool_key, -1);
                    cok->fd = -1;
                    cok->status = 0;
                }

                if(cok->ssl) {
                    SSL_shutdown(cok->ssl);
                    SSL_CTX_free(cok->ctx);
                    cok->ctx = NULL;
                    SSL_free(cok->ssl);
                    cok->ssl = NULL;
                }

                cok->inuse = 0;

                lua_co_resume(cok->L, 2);
            }

            _tl = _ttl;
        }
    }

    return 0;
}
