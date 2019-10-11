#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "abqueue.h"

#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__ || defined __APPLE__

#include <unistd.h>
#include <sched.h>

#define __ABQ_BOOL_COMPARE_AND_SWAP __sync_bool_compare_and_swap
#define __ABQ_FETCH_AND_ADD __sync_fetch_and_add
#define __ABQ_ADD_AND_FETCH __sync_add_and_fetch
#define __ABQ_SYNC_MEMORY __sync_synchronize

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

void abqueue_destroy(abqueue_t *abqueue){
  void* p;
  while((p = abqueue_deq(abqueue))){
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
  
  abqueue_node_t *base = _init_node(abqueue);
  if(NULL == base){
    perror("Node malloc error");
    return errno;
  }
  abqueue->head = abqueue->tail = base;
  abqueue->size = 0;
  
  return 0;
}

int abqueue_enq(abqueue_t* abqueue, void* value){
  abqueue_node_t *node = _get_usable_node(abqueue);
  
  if(NULL == node){
    printf("%s", "error...000");
    perror("Malloc error with ABQueue node");
    return errno;
  }
  node->value = value;
  printf("-- enq %p\n", (void*)node);
  
  if(-1 == _enqueue(abqueue, node)){
    printf("%s", "error...!!!");
    perror("Enqueue error");
    return errno;
  }
  printf("abqueue enq size: %zu\n", abqueue->size);
  
  return 0;
}

void* abqueue_deq(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  node = _dequeue(abqueue);
  if(NULL != node){
    if(NULL == _recycle_queue){
      _init_recycle_queue(abqueue);
    }
    printf("deq size: %zu ---\n", __ABQ_ADD_AND_FETCH(&abqueue->size, 0));
    _recycle_node(node);
    printf("-- deq %p\n", (void*)node);
    
    return node->value;
  }
  
  return NULL;
}

static int _enqueue(abqueue_t *abqueue, abqueue_node_t *node){
  abqueue_node_t *tail = NULL;
  for(;;){
    __ABQ_SYNC_MEMORY();
    tail = abqueue->tail;
      
    if(__ABQ_BOOL_COMPARE_AND_SWAP(&tail->next, NULL, node)){
      __ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, node);
      __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
//        printf("--%p--%s----\n", (void *)node->next, "_enqueue 522...");
      return 0;
    }
    
    printf("%zu\n", abqueue->size);
    printf("%p\n", (void *)abqueue->head);
    printf("%p\n", (void *)abqueue->tail);
    printf("%p\n", (void *)tail->next);
    printf("----%s----\n", "_enqueue 622...");
//      break;
    }
    
    return -1;
}

static abqueue_node_t* _dequeue(abqueue_t *abqueue){
  abqueue_node_t *head, *next;
  
  for(;;){
    head = abqueue->head;
    if (__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, head)) {
      
      next = head->next;
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, head, head)){
        if(NULL == next){
          __ABQ_SYNC_MEMORY();
          return NULL;
        }
      } else {
        if(next){
          if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, next)){
            __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
            return head;
          }
        } else {
          __ABQ_SYNC_MEMORY();
          return NULL;
        }
      }
    }
  }
  
  return NULL;
}

static abqueue_node_t* _get_usable_node(abqueue_t *abqueue){
  abqueue_node_t *node = NULL;
  
  if(NULL == _recycle_queue){
    _init_recycle_queue(abqueue);
  }
  
  if(abqueue_size(_recycle_queue) > 0){
    node = _dequeue(_recycle_queue);
  } else {
    node = _init_node(abqueue);
  }
  _restore_node(node);
  
  return node;
}

static int _recycle_node(abqueue_node_t* node){
  _free_recycle_node();
  _restore_node(node);
  printf("-- recycle %p\n", (void*)node);
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
