#include "coevent.h"
#include "connection-pool.h"

void *cosocket_connection_pool_counters[64] = {0};
void *connect_pool_p[2][64] = {{0}, {0}};
int connect_pool_ttl = 60; /// cache times

static int cosocket_be_close(se_ptr_t *ptr)
{
    //printf("cosocket_be_close %d\n", ptr->fd);
    int fd = ptr->fd;

    cosocket_connection_pool_t *n = ptr->data;
    int k = n->pool_key % 64;

    if(n == connect_pool_p[0][k]) {
        connect_pool_p[0][k] = n->next;

        if(n->next) {
            ((cosocket_connection_pool_t *) n->next)->uper = NULL;
        }

    } else if(n == connect_pool_p[1][k]) {
        connect_pool_p[1][k] = n->next;

        if(n->next) {
            ((cosocket_connection_pool_t *) n->next)->uper = NULL;
        }

    } else {
        ((cosocket_connection_pool_t *) n->uper)->next = n->next;

        if(n->next) {
            ((cosocket_connection_pool_t *) n->next)->uper = n->uper;
        }
    }

    se_delete(n->ptr);
    n->ptr = NULL;

    if(n->ssl) {
        SSL_free(n->ssl);
        n->ssl = NULL;
        SSL_CTX_free(n->ctx);
        n->ctx = NULL;
    }

    connection_pool_counter_operate(n->pool_key, -1);
    close(fd);

    free(n);

    return 1;
}

cosocket_connection_pool_counter_t *get_connection_pool_counter(unsigned long pool_key)
{
    int k = pool_key % 64;
    cosocket_connection_pool_counter_t *n = cosocket_connection_pool_counters[k];
    cosocket_connection_pool_counter_t *u = NULL;

    while(n && n->pool_key != pool_key) {
        u = n;
        n = n->next;
    }

    if(!n) {
        n = malloc(sizeof(cosocket_connection_pool_counter_t));
        memset(n, 0, sizeof(cosocket_connection_pool_counter_t));
        n->pool_key = pool_key;

        if(cosocket_connection_pool_counters[k] == NULL) {    // at top
            cosocket_connection_pool_counters[k] = n;

        } else if(u) {
            n->uper = u;
            u->next = n;
        }
    }

    return n;
}

void connection_pool_counter_operate(unsigned long pool_key, int a)
{
    /// add: connection_pool_counter_operate(key, 1);
    /// remove: connection_pool_counter_operate(key, -1);
    if(pool_key < 1) {
        return;
    }

    cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter(pool_key);
    pool_counter->count += a;
}

void *waiting_get_connections[64] = {0};
void *waiting_get_connections_end[64] = {0};

void *add_waiting_get_connection(cosocket_t *cok)
{
    if(cok->pool_key < 1) {
        return NULL;
    }

    int k = cok->pool_key % 64;
    cosocket_waiting_get_connection_t *n = malloc(sizeof(cosocket_waiting_get_connection_t));

    if(!n) {
        return NULL;
    }

    n->cok = cok;
    n->next = NULL;
    n->uper = NULL;
    n->k = k;

    if(waiting_get_connections[k] == NULL) {
        waiting_get_connections[k] = n;
        waiting_get_connections_end[k] = n;
        return n;

    } else {
        ((cosocket_waiting_get_connection_t *) waiting_get_connections_end[k])->next = n;
        n->uper = waiting_get_connections_end[k];
        waiting_get_connections_end[k] = n;
        return n;
    }
}

void delete_in_waiting_get_connection(void *_n)
{
    cosocket_waiting_get_connection_t *n = _n;
    int k = n->k;

    if(waiting_get_connections[k] == n) {
        waiting_get_connections[k] = n->next;
    }

    if(waiting_get_connections_end[k] == n) {
        waiting_get_connections_end[k] = n->uper;
    }

    if(n->uper) {
        ((cosocket_waiting_get_connection_t *) n->uper)->next = n->next;
    }

    if(n->next) {
        ((cosocket_waiting_get_connection_t *) n->next)->uper = n->uper;
    }

    free(n);
}

void resume_in_waiting_get_connection(int loop_fd)
{
    int k = 0;

    for(k = 0; k < 64; k++) {
        if(waiting_get_connections[k]) {
            cosocket_t *cok = ((cosocket_waiting_get_connection_t *)waiting_get_connections[k])->cok;

            cok->ptr = get_connection_in_pool(loop_fd, cok->pool_key, cok);

            if(cok->ptr) {
                cok->pool_wait = NULL;
                delete_in_waiting_get_connection(waiting_get_connections[k]);
                ((se_ptr_t *) cok->ptr)->data = cok;
                cok->status = 2;
                cok->inuse = 0;
                cok->reusedtimes = 1;
                cok->in_read_action = 0;
                cok->fd = ((se_ptr_t *) cok->ptr)->fd;
                //printf("reuse %d\n", cok->fd);
                delete_timeout(cok->timeout_ptr);
                cok->timeout_ptr = NULL;
                se_be_pri(cok->ptr, NULL);
                lua_pushboolean(cok->L, 1);

                lua_f_lua_uthread_resume_in_c(cok->L, 1);

                return;
            }

        }
    }
}

se_ptr_t *get_connection_in_pool(int loop_fd, unsigned long pool_key, cosocket_t *cok)
{
    int k = pool_key % 64;
    int p = (now / connect_pool_ttl) % 2;
    cosocket_connection_pool_t  *n = NULL,
                                 *m = NULL,
                                  *nn = NULL;
    se_ptr_t *ptr = NULL;

    /// clear old caches
    int q = (p + 1) % 2;
    int i = 0;

    for(i = 0; i < 64; i++) {
        n = connect_pool_p[q][i];

        while(n) {
            m = n;
            n = n->next;

            ptr = m->ptr;

            if(m->z == 0) {
                m->z = 1;
                nn = connect_pool_p[p][m->pool_key % 64];

                if(nn == NULL) {
                    connect_pool_p[p][m->pool_key % 64] = m;
                    m->next = NULL;
                    m->uper = NULL;

                } else {
                    m->uper = NULL;
                    m->next = nn;
                    nn->uper = m;
                    connect_pool_p[p][m->pool_key % 64] = m;
                }

            } else {
                int fd = ptr->fd;
                se_delete(ptr);
                connection_pool_counter_operate(m->pool_key, -1);
                close(fd);
                free(m);
            }
        }

        connect_pool_p[q][i] = NULL;
    }

    /// end
    if(pool_key == 0) {
        resume_in_waiting_get_connection(loop_fd);
        return NULL; /// only do clear job
    }

regetfd:
    n = connect_pool_p[p][k];

    while(n != NULL) {
        if(n->pool_key == pool_key) {
            break;
        }

        n = (cosocket_connection_pool_t *) n->next;
    }

    if(n) {
        if(n == connect_pool_p[p][k]) {    /// at top
            m = n->next;

            if(m) {
                m->uper = NULL;
                connect_pool_p[p][k] = m;

            } else {
                connect_pool_p[p][k] = NULL;
            }

        } else {
            ((cosocket_connection_pool_t *) n->uper)->next = n->next;

            if(n->next) {
                ((cosocket_connection_pool_t *) n->next)->uper = n->uper;
            }
        }

        ptr = n->ptr;

        if(cok) {
            if(cok->ctx) {
                SSL_CTX_free(cok->ctx);
            }

            if(cok->ssl_pw) {
                free(cok->ssl_pw);
            }

            cok->ctx = n->ctx;
            cok->ssl = n->ssl;
            cok->ssl_pw = n->ssl_pw;
        }

        free(n);
        //printf ( "get fd in pool%d %d key:%d\n", p, ptr->fd, k );
        return ptr;
    }

    if(p != q) {
        p = q;
        goto regetfd;
    }

    return NULL;
}

int add_connection_to_pool(int loop_fd, unsigned long pool_key, int pool_size, se_ptr_t *ptr, void *ssl, void *ctx,
                           char *ssl_pw)
{
    if(pool_key < 0 || !ptr || ptr->fd < 0) {
        return 0;
    }

    int k = pool_key % 64;

    int p = (now / connect_pool_ttl) % 2;

    cosocket_connection_pool_t  *n = NULL, *m = NULL;
    n = connect_pool_p[p][k];

    int keepalive = 1;
    setsockopt(ptr->fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));
#ifdef linux
    int keepidle = 30;
    int keepinterval = 5;
    int keepcount = 3;
    socklen_t optlen = sizeof(keepidle);
    setsockopt(ptr->fd, SOL_TCP, TCP_KEEPIDLE, (void *)&keepidle , optlen);
    setsockopt(ptr->fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepinterval , optlen);
    setsockopt(ptr->fd, SOL_TCP, TCP_KEEPCNT, (void *)&keepcount , optlen);
#endif

    if(n == NULL) {
        m = malloc(sizeof(cosocket_connection_pool_t));

        if(m == NULL) {
            return 0;
        }

        m->z = 0; // recached
        m->ptr = ptr;
        m->pool_key = pool_key;
        m->ssl = ssl;
        m->ssl_pw = ssl_pw;
        m->ctx = ctx;
        m->next = NULL;
        m->uper = NULL;

        connect_pool_p[p][k] = m;

        ptr->data = m;
        n = connect_pool_p[p][k];

        se_be_read(ptr, cosocket_be_close);

        return 1;

    } else {
        int in_pool = 0;

        while(n != NULL) {
            if(n->pool_key == pool_key) {
                if(in_pool++ >= pool_size) {    /// pool full
                    return 0;
                }
            }

            if(n->next == NULL) {    /// last
                m = malloc(sizeof(cosocket_connection_pool_t));

                if(m == NULL) {
                    return 0;
                }

                m->z = 0; // recached
                m->ptr = ptr;
                m->pool_key = pool_key;
                m->ssl = ssl;
                m->ctx = ctx;
                m->next = NULL;
                m->uper = n;
                //m->fd = fd;
                n->next = m;

                ptr->data = m;
                se_be_read(ptr, cosocket_be_close);
                return 1;
            }

            n = (cosocket_connection_pool_t *) n->next;
        }
    }

    return 0;
}
