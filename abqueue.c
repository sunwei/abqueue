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

static void _init_recycle_queue(void);
static abqueue_node_t* _get_usable_node(abqueue_t *abqueue);
static abqueue_node_t* _init_abqueue_node(abqueue_t *abqueue);
static int _enqueue(abqueue_t *abqueue, abqueue_node_t *node);
static abqueue_node_t* _dequeue(abqueue_t *abqueue);

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
  
  return 0;
}

int abqueue_enq(abqueue_t *abqueue, void *value){
  abqueue_node_t *node = _get_usable_node(abqueue);
  
  if(NULL == node){
    perror("Malloc error with ABQueue node");
    return errno;
  }
  node->value = value;
  
  if(-1 == _enqueue(abqueue, node)){
      perror("Enqueue error");
      return errno;
  }
  
  __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
  return 0;
}

void* abqueue_deq(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  node = _dequeue(abqueue);
  if(NULL != node){
    __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
    if(NULL == _recycle_queue){
      _init_recycle_queue();
    }
    _enqueue(_recycle_queue, node);
    
    return node->value;
  }
  
  return NULL;
}

static int _enqueue(abqueue_t *abqueue, abqueue_node_t *node){
  abqueue_node_t *tail = NULL;
  
  for(;;){
      tail = abqueue->tail;
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, tail)){
        __ABQ_SYNC_MEMORY();
        if(__ABQ_BOOL_COMPARE_AND_SWAP(&tail->next, NULL, node)){
          __ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, node);
          return 0;
        }
      }
    }
    
    return -1;
}

static abqueue_node_t* _dequeue(abqueue_t *abqueue){
  abqueue_node_t *head, *next;
  
  for(;;){
    head = abqueue->head;
    if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, head)) {
      __ABQ_SYNC_MEMORY();
      next = head->next;
      if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, head, head) && NULL == next) {
        return NULL;
      } else {
        if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, next)){
          return head;
        }
      }
    }
  }
  
  return NULL;
}

static abqueue_node_t* _get_usable_node(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  node = _init_abqueue_node(abqueue);
  
  return node;
}

static abqueue_node_t* _init_abqueue_node(abqueue_t *abqueue){
  abqueue_node_t *node = abqueue->_malloc(abqueue->mpl, sizeof(abqueue_node_t));
  if(NULL != node){
    node->value = NULL;
    node->next = NULL;
  }
  
  return node;
}

static void _init_recycle_queue(void){
  _recycle_queue = malloc(sizeof(abqueue_t));
  abqueue_simple_init(_recycle_queue);
}
