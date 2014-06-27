#ifndef _CONNECTION_POOL_H
#define _CONNECTION_POOL_H

typedef struct {
    unsigned long size;
    unsigned long count;
    unsigned long pool_key;
    void   *next;
    void   *uper;
} cosocket_connection_pool_counter_t;

typedef struct {
    void *cok;
    void *next;
    void *uper;
    unsigned long k;
    unsigned long z;
} cosocket_waiting_get_connection_t;

typedef struct {
    void *ptr;
    unsigned long pool_key;
    void *next;
    void *uper;
    void *ssl;
    void *ctx;
    int z;
} cosocket_connection_pool_t;

cosocket_connection_pool_counter_t *get_connection_pool_counter(unsigned long pool_key);

void connection_pool_counter_operate(unsigned long pool_key, int a);
void *add_waiting_get_connection(cosocket_t *cok);
void delete_in_waiting_get_connection(void *_n);

se_ptr_t *get_connection_in_pool(int loop_fd, unsigned long pool_key,
                                 cosocket_t *cok);

#endif // _CONNECTION_POOL_H
