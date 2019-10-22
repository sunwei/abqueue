#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__ || defined __APPLE__

#include <sys/time.h>
#include <unistd.h>
#include <sched.h>

#define __ABQ_BOOL_COMPARE_AND_SWAP __sync_bool_compare_and_swap
#define __ABQ_FETCH_AND_ADD __sync_fetch_and_add
#define __ABQ_ADD_AND_FETCH __sync_add_and_fetch
#define __ABQ_SYNC_MEMORY __sync_synchronize

#else

#include <Windows.h>
#include <time.h>
#ifdef _WIN64
inline BOOL __SYNC_BOOL_CAS(LONG64 volatile *dest, LONG64 input, LONG64 comparand) {
  return InterlockedCompareExchangeNoFence64(dest, input, comparand) == comparand;
}
#define __ABQ_VAL_COMPARE_AND_SWAP(dest, comparand, input) \
    InterlockedCompareExchangeNoFence64((LONG64 volatile *)dest, (LONG64)input, (LONG64)comparand)
#define __ABQ_BOOL_COMPARE_AND_SWAP(dest, comparand, input) \
    __SYNC_BOOL_CAS((LONG64 volatile *)dest, (LONG64)input, (LONG64)comparand)
#define __ABQ_FETCH_AND_ADD InterlockedExchangeAddNoFence64
#define __ABQ_ADD_AND_FETCH InterlockedAddNoFence64
#define __ABQ_SYNC_MEMORY MemoryBarrier

#else
#ifndef asm
#define asm __asm
#endif
inline BOOL __SYNC_BOOL_CAS(LONG volatile *dest, LONG input, LONG comparand) {
  return InterlockedCompareExchangeNoFence(dest, input, comparand) == comparand;
}
#define __ABQ_VAL_COMPARE_AND_SWAP(dest, comparand, input) \
    InterlockedCompareExchangeNoFence((LONG volatile *)dest, (LONG)input, (LONG)comparand)
#define __ABQ_BOOL_COMPARE_AND_SWAP(dest, comparand, input) \
    __SYNC_BOOL_CAS((LONG volatile *)dest, (LONG)input, (LONG)comparand)
#define __ABQ_FETCH_AND_ADD InterlockedExchangeAddNoFence
#define __ABQ_ADD_AND_FETCH InterlockedAddNoFence
#define __ABQ_SYNC_MEMORY() asm mfence

#endif
#include <windows.h>
#define __ABQ_YIELD_THREAD SwitchToThread

#endif

#include "abqueue.h"
#define _RECYCLE_QUEUE_MAX_SIZE 200
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

static int _enqueue(abqueue_t *abqueue, void *value);
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

void abqueue_destroy(abqueue_t *abqueue){
  void* p;
  while((p = _dequeue(abqueue))){
    abqueue->_free(abqueue->mpl, p);
  }
  if(NULL != _recycle_queue){
    while((p = _dequeue(_recycle_queue))){
      _recycle_queue->_free(_recycle_queue->mpl, p);
    }
  }
}

int abqueue_simple_init(abqueue_t *abqueue){
  return abqueue_init(abqueue, NULL, _abqueue_malloc, _abqueue_free);
}

int abqueue_init(abqueue_t *abqueue, void* mpl, abqueue_malloc_fn abqueue_malloc, abqueue_free_fn abqueue_free){
  abqueue->_malloc = abqueue_malloc;
  abqueue->_free = abqueue_free;
  abqueue->mpl = mpl;
  abqueue->size = 0;
  abqueue_node_t *node = _get_usable_node(abqueue);
  abqueue->head = abqueue->tail = node;
  return 0;
}

int abqueue_enq(abqueue_t* abqueue, void* value){
  if(-1 == _enqueue(abqueue, value)){
    printf("Enqueue error!");
    perror("Enqueue error");
    return errno;
  }
  
  return 0;
}

void* abqueue_deq(abqueue_t *abqueue) {
  void *val = NULL;
    
  abqueue_node_t *node = NULL;
  node = _dequeue(abqueue);
  if(node){
    val = node->value;
    if(NULL == _recycle_queue){
      _init_recycle_queue(abqueue);
    }
    _recycle_node(node);
  }
  return val;
}

static int _enqueue(abqueue_t *abqueue, void *value){
  abqueue_node_t *tail = NULL;
  abqueue_node_t *node = _get_usable_node(abqueue);
  node->value = value;
    
  for(;;){
    __ABQ_SYNC_MEMORY();
    tail = abqueue->tail;
    if (__ABQ_BOOL_COMPARE_AND_SWAP(&tail->next, NULL, node)) {
      __ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, node);
      return 0;
    }
  }
  return -1;
}

static abqueue_node_t* _dequeue(abqueue_t *abqueue){
  abqueue_node_t *head, *next;
  
  for (;;) {
      head = abqueue->head;
      if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, head)) {
        next = head->next;
        if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, head, head)) {
          if (next == NULL) {
            __ABQ_SYNC_MEMORY();
            return NULL;
          }
        }
        else {
          if (next) {
            if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, next)) {
              return next;
            }
          } else {
            __ABQ_SYNC_MEMORY();
            return NULL;
          }
        }
      }
    }
  
  __ABQ_SYNC_MEMORY();
  return NULL;
}

static abqueue_node_t* _get_usable_node(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  
  if(NULL == _recycle_queue){
    _init_recycle_queue(abqueue);
  }
  
  if(abqueue_size(_recycle_queue) > 0){
    node = _dequeue(_recycle_queue);
    if(node){
      _restore_node(node);
    } else {
      node = _init_node(abqueue);
    }
  } else {
    node = _init_node(abqueue);
  }
  
  return node;
}

static int _recycle_node(abqueue_node_t* node){
  if(node){
    if(0 == _enqueue(_recycle_queue, node->value)){
      return 0;
    }
    _free_recycle_node();
  }
  return -1;
}

static void _free_recycle_node(void){
  abqueue_node_t *node = NULL;
  while(abqueue_size(_recycle_queue) > _RECYCLE_QUEUE_MAX_SIZE){
    node = _dequeue(_recycle_queue);
    if(node && __ABQ_BOOL_COMPARE_AND_SWAP(&node, node, node)){
      _recycle_queue->_free(_recycle_queue->mpl, node);
    }
  }
}

static void _restore_node(abqueue_node_t *node){
  if(node){
//    __ABQ_BOOL_COMPARE_AND_SWAP(&node->next, node->next, NULL);
//    __ABQ_BOOL_COMPARE_AND_SWAP(&node->value, node->value, NULL);
    node->next = NULL;
    node->value = NULL;
  }
}

static abqueue_node_t* _init_node(abqueue_t *abqueue){
  abqueue_node_t *node = abqueue->_malloc(abqueue->mpl, sizeof(abqueue_node_t));
  if(NULL == node){
    printf("Node malloc error!");
    perror("Node malloc error");
  } else {
    _restore_node(node);
  }
  
  return node;
}

static void _init_recycle_queue(abqueue_t *abqueue){
  _recycle_queue = malloc(sizeof(abqueue_t));
  abqueue_init(_recycle_queue, abqueue->mpl, abqueue->_malloc, abqueue->_free);
}

