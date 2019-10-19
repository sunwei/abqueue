#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include "abqueue.h"


typedef void (*test_function)(pthread_t*);

void _sleep(unsigned int milisec);
void*  worker_c(void *);
void*  worker_s(void *);
void*  worker_sc(void *);
void one_enq_and_multi_deq(pthread_t *threads);
void one_deq_and_multi_enq(pthread_t *threads);
void multi_enq_deq(pthread_t *threads);

void running_test(test_function testfn);

struct timeval  tv1, tv2;
#define total_put 50
#define total_running_loop 100
int nthreads = 4;
int one_thread = 1;
int nthreads_exited = 0;
abqueue_t *myq;


void _sleep(unsigned int milisec) {
#if defined __GNUC__ || defined __CYGWIN__ || defined __MINGW32__ || defined __APPLE__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
	usleep(milisec * 1000);
#pragma GCC diagnostic pop
#else
	Sleep(milisec);
#endif
}

void*  worker_c(void *arg) {
	int i = 0;
	int *int_data;
	int total_loop = total_put * (*(int*)arg);
	while (i++ < total_loop) {
		while ((int_data = abqueue_deq(myq)) == NULL) {
		  _sleep(1);
		}

		free(int_data);
	}
	__sync_add_and_fetch(&nthreads_exited, 1);
	return 0;
}

void*  worker_s(void *arg)
{
	int i = 0, *int_data;
	int total_loop = total_put * (*(int*)arg);
	while (i++ < total_loop) {
		int_data = (int*)malloc(sizeof(int));
		assert(int_data != NULL);
		*int_data = i;
		while (abqueue_enq(myq, int_data)) {
			 printf("ENQ FULL?\n");
		}
	}
	return 0;
}

void*  worker_sc(void *arg)
{
	int i = 0;
	int *int_data;
	while (i < total_put) {
		int_data = (int*)malloc(sizeof(int));
		assert(int_data != NULL);
		*int_data = i++;
		while (abqueue_enq(myq, int_data)) {
			printf("ENQ FULL?\n");
		}

		while ((int_data = abqueue_deq(myq)) == NULL) {
			_sleep(1);
		}
		free(int_data);
	}
	__sync_add_and_fetch(&nthreads_exited, 1);
	return 0;
}

#define join_threads \
for (i = 0; i < nthreads; i++) {\
pthread_join(threads[i], NULL); \
}

#define detach_thread_and_loop \
for (i = 0; i < nthreads; i++)\
  pthread_detach(threads[i]);\
while ( nthreads_exited < nthreads ) \
	_sleep(10);\
if(abqueue_size(myq) != 0){\
_sleep(10);\
}

void multi_enq_deq(pthread_t *threads) {
	printf("-----------%s---------------\n", "multi_enq_deq");
	int i;
	for (i = 0; i < nthreads; i++) {
		pthread_create(threads + i, NULL, worker_sc, NULL);
	}

	join_threads;
	// detach_thread_and_loop;
}

void one_deq_and_multi_enq(pthread_t *threads) {
	printf("-----------%s---------------\n", "one_deq_and_multi_enq");
	int i;
	for (i = 0; i < nthreads; i++)
		pthread_create(threads + i, NULL, worker_s, &one_thread);

	worker_c(&nthreads);

	join_threads;
	// detach_thread_and_loop;
}

void one_enq_and_multi_deq(pthread_t *threads) {
	printf("-----------%s---------------\n", "one_enq_and_multi_deq");
	int i;
	for (i = 0; i < nthreads; i++)
		pthread_create(threads + i, NULL, worker_c, &one_thread);
  
  worker_s(&nthreads);
  
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
	detach_thread_and_loop;
#pragma GCC diagnostic pop

}

void running_test(test_function testfn) {
	int n;
	for (n = 0; n < total_running_loop; n++) {
		printf("Current running at =%d, ", n);
		nthreads_exited = 0;
		/* Spawn threads. */
		pthread_t threads[nthreads];
		printf("Using %d thread%s.\n", nthreads, nthreads == 1 ? "" : "s");
		printf("Total requests %d \n", total_put);
		gettimeofday(&tv1, NULL);

		testfn(threads);
//    printf("test end ...");
//		 one_enq_and_multi_deq(threads);
//		one_deq_and_multi_enq(threads);
		// multi_enq_deq(threads);
		// worker_s(&ri);
		// worker_c(&ri);

		gettimeofday(&tv2, NULL);
		printf ("Total time = %f seconds\n",
		        (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
		        (double) (tv2.tv_sec - tv1.tv_sec));

		_sleep(10);
		assert ( 0 == abqueue_size(myq) && "Error, all queue should be consumed but not");
	}
}

int main(void) {
	myq = malloc(sizeof	(abqueue_t));
	if (abqueue_simple_init(myq) == -1)
		return -1;

	running_test(one_enq_and_multi_deq);
	running_test(one_deq_and_multi_enq);
	running_test(multi_enq_deq);

	abqueue_destroy(myq);
	free(myq);

	printf("Test Pass!\n");

	return 0;
}
