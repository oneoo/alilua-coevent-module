#include "coevent.h"

void *cosocket_connection_pool_counters[64] = {NULL32 NULL32};

cosocket_connection_pool_counter_t *
get_connection_pool_counter ( unsigned long pool_key )
{
    int k = pool_key % 64;
    cosocket_connection_pool_counter_t *n = cosocket_connection_pool_counters[k];
    cosocket_connection_pool_counter_t *u = NULL;

    while ( n && n->pool_key != pool_key ) {
        u = n;
        n = n->next;
    }

    if ( !n ) {
        n = malloc ( sizeof ( cosocket_connection_pool_counter_t ) );
        n->pool_key = pool_key;
        n->count = 0;
        n->next = NULL;
        n->uper = NULL;

        if ( cosocket_connection_pool_counters[k] == NULL ) { // at top
            cosocket_connection_pool_counters[k] = n;

        } else if ( u ) {
            n->uper = u;
            u->next = n;
        }
    }

    return n;
}

void
connection_pool_counter_operate ( unsigned long pool_key, int a )
{
    /// add: connection_pool_counter_operate(key, 1);
    /// remove: connection_pool_counter_operate(key, -1);
    if ( pool_key < 1 ) {
        return;
    }

    cosocket_connection_pool_counter_t *pool_counter = get_connection_pool_counter (
                pool_key );
    pool_counter->count += a;
}

void *waiting_get_connections[64] = {NULL32 NULL32};

int
add_waiting_get_connection ( cosocket_t *cok )
{
    if ( cok->pool_key < 1 ) {
        return 0;
    }

    int k = cok->pool_key % 64;
    cosocket_waiting_get_connection_t *n = malloc ( sizeof (
            cosocket_waiting_get_connection_t ) );

    if ( !n ) {
        return 0;
    }

    n->cok = cok;
    n->next = NULL;
    n->uper = NULL;

    if ( waiting_get_connections[k] == NULL ) {
        waiting_get_connections[k] = n;
        return 1;

    } else {
        cosocket_waiting_get_connection_t *m = waiting_get_connections[k];

        while ( m ) {
            if ( !m->next ) {
                m->next = n;
                n->uper = m;
                return 1;
            }

            m = m->next;
        }
    }
}

void *connect_pool_p[2][64] = {{NULL32 NULL32}, {NULL32 NULL32}};
int connect_pool_ttl = 30; /// cache times

int
get_connection_in_pool ( int epoll_fd, unsigned long pool_key )
{
    int k = pool_key % 64;
    int p = ( timer / connect_pool_ttl ) % 2;
    cosocket_connection_pool_t  *n = NULL,
                                 *m = NULL,
                                  *nn = NULL;
    /// clear old caches
    int q = ( p + 1 ) % 2;
    int i = 0;
    struct epoll_event ev;

    for ( i = 0; i < 64; i++ ) {
        n = connect_pool_p[q][i];

        while ( n ) {
            m = n;
            n = n->next;

            if ( m->recached == 0 ) {
                m->recached = 1;
                nn = connect_pool_p[p][m->pool_key % 64];

                if ( nn == NULL ) {
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
                epoll_ctl ( epoll_fd, EPOLL_CTL_DEL, m->fd, &ev );
                close ( m->fd );
                connection_pool_counter_operate ( m->pool_key, -1 );
                free ( m );
            }
        }

        connect_pool_p[q][i] = NULL;
    }

    /// end
    if ( pool_key == 0 ) {
        return -1; /// only do clear job
    }

regetfd:
    n = connect_pool_p[p][k];

    while ( n != NULL ) {
        if ( n->pool_key == pool_key ) {
            break;
        }

        n = ( cosocket_connection_pool_t * ) n->next;
    }

    if ( n ) {
        if ( n == connect_pool_p[p][k] ) { /// at top
            m = n->next;

            if ( m ) {
                m->uper = NULL;
                connect_pool_p[p][k] = m;

            } else {
                connect_pool_p[p][k] = NULL;
            }

        } else {
            ( ( cosocket_connection_pool_t * ) n->uper )->next = n->next;

            if ( n->next ) {
                ( ( cosocket_connection_pool_t * ) n->next )->uper = n->uper;
            }
        }

        int fd = n->fd;
        free ( n );
        //printf("get fd in pool%d %d key:%d\n",p, fd, k);
        return fd;
    }

    if ( p != q ) {
        p = q;
        goto regetfd;
    }

    return -1;
}

void
del_connection_in_pool ( int epoll_fd, cosocket_connection_pool_t *n )
{
    int k = n->pool_key % 64;

    if ( n == connect_pool_p[0][k] ) {
        connect_pool_p[0][k] = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = NULL;
        }

    } else if ( n == connect_pool_p[1][k] ) {
        connect_pool_p[1][k] = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = NULL;
        }

    } else {
        ( ( cosocket_connection_pool_t * ) n->uper )->next = n->next;

        if ( n->next ) {
            ( ( cosocket_connection_pool_t * ) n->next )->uper = n->uper;
        }
    }

    struct epoll_event ev;

    epoll_ctl ( epoll_fd, EPOLL_CTL_DEL, n->fd, &ev );

    close ( n->fd );

    connection_pool_counter_operate ( n->pool_key, -1 );

    free ( n );
}

int
add_connection_to_pool ( int epoll_fd, unsigned long pool_key, int pool_size, int fd )
{
    if ( pool_key < 0 ) {
        return 0;
    }

    int k = pool_key % 64;
    int i = 0;
    /// check waiting list
    {
        cosocket_waiting_get_connection_t *m = waiting_get_connections[k];

        if ( m != NULL ) {
            while ( m && ( ( cosocket_t * ) m->cok )->pool_key != pool_key ) {
                m = m->next;
            }

            if ( m ) {
                if ( m->uper ) {
                    ( ( cosocket_waiting_get_connection_t * ) m->uper )->next = m->next;

                    if ( m->next ) {
                        ( ( cosocket_waiting_get_connection_t * ) m->next )->uper = m->uper;
                    }

                } else {
                    waiting_get_connections[k] = m->next;

                    if ( m->next ) {
                        ( ( cosocket_waiting_get_connection_t * ) m->next )->uper = NULL;
                    }
                }

                cosocket_t *_cok = m->cok;
                free ( m );
                _cok->fd = fd;
                _cok->status = 2;
                _cok->reusedtimes = 1;
                _cok->inuse = 0;
                lua_pushboolean ( _cok->L, 1 );

                if ( lua_resume ( _cok->L, 1 ) == LUA_ERRRUN ) {
                    if ( lua_isstring ( _cok->L, -1 ) ) {
                        printf ( "%s:%d isstring: %s\n", __FILE__, __LINE__, lua_tostring ( _cok->L, -1 ) );
                        lua_pop ( _cok->L, -1 );
                    }
                }

                return 1;
            }
        }
    }
    /// end
    time ( &timer );
    int p = ( timer / connect_pool_ttl ) % 2;
    struct epoll_event ev;
    cosocket_connection_pool_t  *n = NULL,
                                 *m = NULL;
    n = connect_pool_p[p][k];

    if ( n == NULL ) {
        m = malloc ( sizeof ( cosocket_connection_pool_t ) );

        if ( m == NULL ) {
            return 0;
        }

        m->type = EPOLL_PTR_TYPE_COSOCKET_WAIT;
        m->recached = 0;
        m->pool_key = pool_key;
        m->next = NULL;
        m->uper = NULL;
        m->fd = fd;
        connect_pool_p[p][k] = m;
        ev.data.ptr = m;
        ev.events = EPOLLIN;

        if ( epoll_ctl ( epoll_fd, EPOLL_CTL_MOD, m->fd, &ev ) == -1 ) {
            printf ( "EPOLL_CTL_MOD error: %d %s", __LINE__, strerror ( errno ) );
        }

        return 1;

    } else {
        int in_pool = 0;

        while ( n != NULL ) {
            if ( n->pool_key == pool_key ) {
                if ( in_pool++ >= pool_size ) { /// pool full
                    return 0;
                }
            }

            if ( n->next == NULL ) { /// last
                m = malloc ( sizeof ( cosocket_connection_pool_t ) );

                if ( m == NULL ) {
                    return 0;
                }

                m->type = EPOLL_PTR_TYPE_COSOCKET_WAIT;
                m->recached = 0;
                m->pool_key = pool_key;
                m->next = NULL;
                m->uper = n;
                m->fd = fd;
                n->next = m;
                ev.data.ptr = m;
                ev.events = EPOLLIN;

                if ( epoll_ctl ( epoll_fd, EPOLL_CTL_MOD, m->fd, &ev ) == -1 ) {
                    printf ( "EPOLL_CTL_MOD error: %d %s", __LINE__, strerror ( errno ) );
                }

                return 1;
            }

            n = ( cosocket_connection_pool_t * ) n->next;
        }
    }

    return 0;
}
