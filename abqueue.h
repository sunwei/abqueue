#ifndef ABQUEUE_H
#define ABQUEUE_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abqueue_node_s abqueue_node_t;
typedef void* (*abqueue_malloc_fn)(void*, size_t);
typedef void (*abqueue_free_fn)(void*, void*);

typedef struct {
  volatile size_t size;
  void *mpl;
  abqueue_node_t *head, *tail;
  abqueue_malloc_fn _malloc;
  abqueue_free_fn _free;
} abqueue_t;

extern int    abqueue_init(abqueue_t *abqueue, void* mpl, abqueue_malloc_fn abqueue_malloc, abqueue_free_fn abqueue_free);
extern int    abqueue_simple_init(abqueue_t *abqueue);
extern int    abqueue_enq(abqueue_t *abqueue, void* value);
extern void*  abqueue_deq(abqueue_t *abqueue);
extern void   abqueue_destroy(abqueue_t *abqueue);
extern size_t abqueue_size(abqueue_t *abqueue);

#ifdef __cplusplus
}
#endif

#endif
