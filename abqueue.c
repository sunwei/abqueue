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
  abqueue->head = abqueue->tail = NULL;
  abqueue->size = 0;
  
  return 0;
}

int abqueue_enq(abqueue_t* abqueue, void* value){
  abqueue_node_t *node = _get_usable_node(abqueue);
  
  printf("%s", "==================================enq======================\n");
//  printf("-- enq000000 head %p\n", (void*)abqueue->head);
//  printf("-- enq000000 tail %p\n", (void*)abqueue->tail);
//  printf("-- enq000001 head %p\n", (void*)_recycle_queue->head);
//  printf("-- enq000001 tail %p\n", (void*)_recycle_queue->tail);
  
  if(NULL == node){
    perror("Malloc error with ABQueue node");
    return errno;
  }
  node->value = value;
//  printf("-- enq0 node %p\n", (void*)node);
//  printf("-- enq0 next %p\n", (void*)node->next);
  
  if(-1 == _enqueue(abqueue, node)){
    perror("Enqueue error");
    return errno;
  }
//  printf("abqueue enq size: %zu\n", abqueue->size);
  
  return 0;
}

void* abqueue_deq(abqueue_t *abqueue){
  void *val = NULL;
  abqueue_node_t *node = NULL;
  printf("%s", "==================================deq======================\n");
//  printf("-- deq999999 head %p\n", (void*)abqueue->head);
//  printf("-- deq999999 tail %p\n", (void*)abqueue->tail);
//  printf("-- deq999990 head %p\n", (void*)_recycle_queue->head);
//  printf("-- deq999990 tail %p\n", (void*)_recycle_queue->tail);
  node = _dequeue(abqueue);
  if(NULL != node){
    if(NULL == _recycle_queue){
      _init_recycle_queue(abqueue);
    }
    printf("deq size: %zu ---\n", __ABQ_ADD_AND_FETCH(&abqueue->size, 0));
    val = node->value; 
    
    _recycle_node(node);
    printf("-- deq %p\n", (void*)node);
  }
  
  return val;
}

static int _enqueue(abqueue_t *abqueue, abqueue_node_t *node){
  abqueue_node_t *tail = NULL;
  for(;;){
    __ABQ_SYNC_MEMORY();
    tail = abqueue->tail;
//    printf("-- enq000 abqueue %p\n", (void*)abqueue);
//    printf("-- enq1 tail %p\n", (void*)tail);
//    if(NULL != tail){
//      printf("-- enq1 next %p\n", (void*)tail->next);
//    }
//    printf("-- enq2 node %p\n", (void*)node);
//    printf("-- enq2 next %p\n", (void*)node->next);
    
    if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, NULL, node)){
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, NULL, node)){
        printf("%s", "-- enq init base\n");
        __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
//        printf("-- enq001122 head %p\n", (void*)abqueue->head);
//        printf("-- enq001122 tail %p\n", (void*)abqueue->tail);
        return 0;
      }
    } else {
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&tail->next, NULL, node)){
        __ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, tail, node);
        __ABQ_ADD_AND_FETCH(&abqueue->size, 1);
        
//        printf("-- enq0011 head %p\n", (void*)abqueue->head);
//        printf("-- enq0011 tail %p\n", (void*)abqueue->tail);
//        printf("-- enq0011 tail next %p\n", (void*)abqueue->tail->next);
        
        return 0;
      }
    }
    
    printf("----%s----\n", "_enqueue 622...");
//    printf("-- _enqueue size %zu\n", abqueue->size);
//    printf("-- head %p\n", (void *)abqueue->head);
//    printf("-- tail %p\n", (void *)abqueue->tail);
//    if(NULL != tail->next){
//      printf("-- tail next %p\n", (void *)tail->next);
//    }
    exit(1);
    }
    
    return -1;
}

static abqueue_node_t* _dequeue(abqueue_t *abqueue){
  abqueue_node_t *head, *next;
  
  for(;;){
    head = abqueue->head;
    
    if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, head)){
    
      if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, NULL, NULL)){
      
        return NULL;
      } else {
        next = head->next;
        
        if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->tail, head, NULL)){
          if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, NULL)){
            __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
          }
          __ABQ_BOOL_COMPARE_AND_SWAP(&head->next, head->next, NULL);
          return head;
        } else {
          if(next){
            if(__ABQ_BOOL_COMPARE_AND_SWAP(&abqueue->head, head, next)){
              __ABQ_FETCH_AND_ADD(&abqueue->size, -1);
              __ABQ_BOOL_COMPARE_AND_SWAP(&head->next, head->next, NULL);
              return head;
            }
          } else {
            printf("%s", "what????...");
            __ABQ_SYNC_MEMORY();
            return NULL;
          }
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
//  _restore_node(node);
//  printf("-- recycle %p\n", (void*)node);
  if(0 == _enqueue(_recycle_queue, node)){
    return 0;
  }
  
  return -1;
}

static void _free_recycle_node(void){
  abqueue_node_t *node = NULL;
  while(abqueue_size(_recycle_queue) >= _RECYCLE_QUEUE_MAX_SIZE){
    printf("%s", "free...");
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
  if(NULL == node){
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
