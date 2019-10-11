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

static void _init_recycle_queue(abqueue_t *abqueue);
static int _recycle_node(abqueue_node_t* node);
static void _restore_node(abqueue_node_t *node);
static void _free_recycle_node(void);
static abqueue_node_t* _get_usable_node(abqueue_t *abqueue);
static abqueue_node_t* _init_node(abqueue_t *abqueue);

static int _enqueue(abqueue_t *abqueue, abqueue_node_t *node);
static abqueue_node_t* _dequeue(abqueue_t *abqueue);

static inline void* _abqueue_malloc(void* mpl, size_t sz){
  return malloc(sz);
}
static inline void _abqueue_free(void* mpl, void* ptr){
  free(ptr);
}

size_t abqueue_size(abqueue_t *abqueue){
  return __ABQ_ADD_AND_FETCH(&abqueue->size, 0);
}

int abqueue_simple_init(abqueue_t *abqueue){
  return abqueue_init(abqueue, NULL, _abqueue_malloc, _abqueue_free);
}

int abqueue_init(abqueue_t *abqueue, void* mpl, abqueue_malloc_fn abqueue_malloc, abqueue_free_fn abqueue_free){
  
  abqueue->_malloc = abqueue_malloc;
  abqueue->_free = abqueue_free;
  abqueue->mpl = mpl;
  
  abqueue->head = abqueue->tail = NULL;
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
  
  return 0;
}

void* abqueue_deq(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  node = _dequeue(abqueue);
  if(NULL != node){
    if(NULL == _recycle_queue){
      _init_recycle_queue(abqueue);
    }
    _recycle_node(node);
    
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
        
        if(NULL == abqueue->head && NULL == abqueue->tail && 0 == abqueue_size(abqueue)){
          abqueue->head = abqueue->tail = node;
          __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
          return 0;
        }
        
        if(__ABQ_BOOL_COMPARE_AND_SWAP(&tail->next, NULL, node)){
          __ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, node);
          __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
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
      
      if(NULL == abqueue->head && NULL == abqueue->tail && 0 == abqueue_size(abqueue)){
        return NULL;
      }
      
      if(abqueue->head == abqueue->tail && 1 == abqueue_size(abqueue)){
        abqueue->head = NULL;
        abqueue->tail = NULL;
        __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
        return head;
      }
        
      next = head->next;
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, next)){
        __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
        return head;
      }
    }
  }
  
  return NULL;
}

static abqueue_node_t* _get_usable_node(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  
  if(abqueue_size(_recycle_queue) > 0){
    node = abqueue_deq(_recycle_queue);
    _restore_node(node);
  } else {
    node = _init_node(abqueue);
  }
  
  return node;
}

static int _recycle_node(abqueue_node_t* node){
  _free_recycle_node();
  if(0 == _enqueue(_recycle_queue, node)){
    return 0;
  }
  
  return -1;
}

static void _free_recycle_node(void){
  abqueue_node_t *node = NULL;
  while(abqueue_size(_recycle_queue) >= _RECYCLE_QUEUE_MAX_SIZE){
    node = _dequeue(_recycle_queue);
    _recycle_queue->_free(_recycle_queue->mpl, node);
  }
}

static void _restore_node(abqueue_node_t *node){
  node->value = NULL;
  node->next = NULL;
}

static abqueue_node_t* _init_node(abqueue_t *abqueue){
  abqueue_node_t *node = abqueue->_malloc(abqueue->mpl, sizeof(abqueue_node_t));
  if(NULL != node){
    _restore_node(node);
  }
  
  return node;
}

static void _init_recycle_queue(abqueue_t *abqueue){
  _recycle_queue = malloc(sizeof(abqueue_t));
  abqueue_init(_recycle_queue, abqueue->mpl, abqueue->_malloc, abqueue->_free);
}
