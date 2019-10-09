#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "abqueue.h"

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__ || defined __APPLE__

#include <unistd.h>
#include <sched.h>

#define __ABQ_BOOL_COMPARE_AND_SWAP __sync_bool_compare_and_swap
#define __ABQ_VAL_COMPARE_AND_SWAP __sync_val_compare_and_swap
#define __ABQ_FETCH_AND_ADD __sync_fetch_and_add
#define __ABQ_ADD_AND_FETCH __sync_add_and_fetch
#define __ABQ_SYNC_MEMORY __sync_synchronize
#define __ABQ_YIELD_THREAD sched_yield

#define abq_bool_t int

#else
// TBD for others
#endif

#define _RECYCLE_QUEUE_MAX_SIZE 10
abqueue_t *_recycle_queue = NULL;

struct abqueue_node_s {
  void * value;
  struct abqueue_node_s *next;
};

static void _init_recycle_queue();
static abqueue_node_t* _init_abqueue_node(abqueue_t *abqueue);

static inline void* _abqueue_malloc(void* mpl, size_t sz){
  return malloc(sz);
}
static inline void _abqueue_free(void* mpl, void* ptr){
  free(ptr);
}

int abqueue_simple_init(abqueue_t *abqueue){
  return abqueue_init(abqueue, NULL, _abqueue_malloc, _abqueue_free);
}

int abqueue_init(abqueue_t *abqueue, void* mpl, abqueue_malloc_fn abqueue_malloc, abqueue_free_fn abqueue_free){
  
  abqueue->_malloc = abqueue_malloc;
  abqueue->_free = abqueue_free;
  abqueue->mpl = mpl;
  
  abqueue_node_t *base = _init_abqueue_node(abqueue);
  abqueue->head = abqueue->tail = base;
  abqueue->size = 0;
  
  _init_recycle_queue();
  
  return 0;
}

static void _init_recycle_queue(){
  _recycle_queue = malloc(sizeof(abqueue_t));
  _recycle_queue->head = _recycle_queue->tail = NULL;
  _recycle_queue->size = 0;
}

static abqueue_node_t* _init_abqueue_node(abqueue_t *abqueue){
  abqueue_node_t *node = abqueue->_malloc(abqueue->mpl, sizeof(abqueue_node_t));
  if(NULL == node){
    perror("Malloc error with ABQueue node");
    return errno;
  }
  node->value = NULL;
  node->next = NULL;
  
  return node;
}