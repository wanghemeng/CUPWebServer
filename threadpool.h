#ifndef THREADPOOL_H
#define THREADPOOL_H

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <signal.h>

static volatile int threads_keepalive;
static volatile int threads_on_hold;

/* #define bool int */

typedef int bool;

/* queue status and conditional variable */
typedef struct staconv
{
    pthread_mutex_t mutex; /* contorl the threads in the pool */
    pthread_cond_t cond; /* false:no task, ture:has task */
    int status;
}staconv;

/* task */
typedef struct task
{
    struct task *next; /* pointer to next task */
    void (*function)(void *arg); /* pointer of function */
    void *arg; /* pointer of function argument */
}task;

/* teak queue */
typedef struct taskqueue
{
    pthread_mutex_t mutex; /* for read/write command queue */
    task *front; /* point the head of the queue */
    task *rear; /* point the tail of the queue */
    staconv *has_jobs; /* block the thread based on status */
    int len; /* the number of task in the queue */
}taskqueue;

/* thread */
typedef struct thread
{
    int id; /* thread id */
    pthread_t pthread; /* POSIX thread */
    struct threadpool *pool; /* band with thread pool */
}thread;

/* thread pool */
typedef struct threadpool
{
    thread **threads; /* threads pointer array */
    volatile int num_threads; /* the number of threads in the pool */
    volatile int num_working; /* the working number of thread */
    pthread_mutex_t thcount_lock; /* threadpool mutex for the previous two variate */
    pthread_cond_t threads_all_idle; /* the conition variate for destory thread */
    taskqueue queue; /* task queue */
    volatile bool is_alive; /* the threadpool is alive or not */
}threadpool;

int init_taskqueue(taskqueue *queue);
int create_thread(threadpool *pool, thread **pthread, int id);
threadpool* initThreadPool(int num_threads);
void push_taskqueue(taskqueue *queue, task *curtask);
void addTask2ThreadPool(threadpool *pool, void (*function_p)(void*), void* arg_p);
void waitThreadPool(threadpool *pool);
static void bsem_init(staconv *bsem_p, int value);
static void bsem_reset(staconv *bsem_p);
void destory_taskqueue(taskqueue *queue);
static void bsem_post_all(staconv *bsem_p);
static void thread_destroy (thread* thread_p);
void distoryThreadPool(threadpool *pool);
int getNumofThreadWorking(threadpool *pool);
static void bsem_post(staconv *bsem_p);
task* take_taskqueue(taskqueue *queue);
void bsem_wait(staconv* bsem_p);
static void thread_hold(int sig_id);
void* thread_do(thread* pthread);

/* initialize the task queue */
int init_taskqueue(taskqueue *queue)
{
    pthread_mutex_init(&(queue->mutex), NULL);
    queue->front = NULL;
    queue->rear = NULL;
    queue->len = 0;
    queue->has_jobs = (struct staconv *)malloc(sizeof(struct staconv)); /* malloc for the initial */
    if (queue->has_jobs == NULL)
    {
		return 0;
	}
    bsem_init(queue->has_jobs, 0);
    // pthread_mutex_init(&(queue->has_jobs->mutex), NULL);
    // pthread_cond_init(&(queue->has_jobs->cond), NULL);
    // queue->has_jobs->status = 0; /* false means no task */
    return 1;
}

/* create thread */
int create_thread(threadpool *pool, thread **pthread, int id)
{
    /* malloc for thread */
    *pthread = (struct thread *)malloc(sizeof(struct thread));
    if (*pthread == NULL)
    {
        printf("create_thread(): Could not allocate memory for thread\n");
        return -1;
    }

    /* set the attribute of the thread */
    (*pthread)->pool = pool;
    (*pthread)->id = id;

    /* create thread */
    pthread_create(&(*pthread)->pthread, NULL, (void *)thread_do, (*pthread));
    pthread_detach((*pthread)->pthread);
    return 0;
}

/* the initialization fuction of threadpool */
threadpool* initThreadPool(int num_threads)
{
    threads_on_hold   = 0;
	threads_keepalive = 1;

    /* create the space of threadpool */
    threadpool *pool;
    pool = (threadpool *)malloc(sizeof(threadpool));
    if (pool == NULL)
    {
		printf("thpool_init(): Could not allocate memory for thread pool\n");
		return NULL;
	}

    pool->num_threads = 0;
    pool->num_working = 0;
    pthread_mutex_init(&(pool->thcount_lock), NULL);
    pthread_cond_init(&pool->threads_all_idle, NULL);

    /* initialize the task queue */
    init_taskqueue(&pool->queue);

    /* create thread array */
    pool->threads = (struct thread **)malloc(sizeof(struct thread *) * num_threads);

    /* create thread */
    for (int i = 0; i < num_threads; i++)
    {
        create_thread(pool, &(pool->threads[i]), i); /* i:thread id */
    }

    // /* Thread init */
	// for (int n = 0; n < num_threads; n++)
    // {
	// 	thread_init(pool, &pool->threads[n], n);
	// }

    /* waiting all the threads been created, oprate "pool->num_threads++" in each thread function */
    /* so that here is a busy wait, waiting all the threads been created, run the block code to return */
    while (pool->num_threads != num_threads);

    return pool;
}

/* add task to queue */
void push_taskqueue(taskqueue *queue, task *curtask)
{
    pthread_mutex_lock(&queue->mutex);
    curtask->next = NULL;

    if (queue->len == 0)
    {
        queue->front = curtask;
        queue->rear = curtask;
    }
    else
    {
        queue->rear->next = curtask;
        queue->rear = curtask;
    }
    queue->len++;
    bsem_post(queue->has_jobs);
    // queue->has_jobs->status = 1; /* ture means have job */
	pthread_mutex_unlock(&queue->mutex);
}

/* add task to threadpool */
void addTask2ThreadPool(threadpool *pool, void (*function_p)(void*), void* arg_p)
{
    task* newjob;
	newjob = (struct task*)malloc(sizeof(struct task));

	/* add function and argument */
	newjob->function = function_p;
	newjob->arg = arg_p;
    /* add task to queue */
    push_taskqueue(&pool->queue, newjob);
}

/* waiting al the task finished */
void waitThreadPool(threadpool *pool)
{
    pthread_mutex_lock(&pool->thcount_lock);
    while (pool->queue.len || pool->num_working)
    {
        pthread_cond_wait(&pool->threads_all_idle, &pool->thcount_lock);
    }
    pthread_mutex_unlock(&pool->thcount_lock);
}

/* Init semaphore to 1 or 0 copied*/
static void bsem_init(staconv *bsem_p, int value)
{
	if (value < 0 || value > 1) {
		// err("bsem_init(): Binary semaphore can take only values 1 or 0");
		exit(1);
	}
	pthread_mutex_init(&(bsem_p->mutex), NULL);
	pthread_cond_init(&(bsem_p->cond), NULL);
	bsem_p->status = value;
}


/* Reset semaphore to 0 copied*/
static void bsem_reset(staconv *bsem_p)
{
	bsem_init(bsem_p, 0);
}

void destory_taskqueue(taskqueue *queue)
{
    while (queue->len)
    {
        free(take_taskqueue(queue));
    }
    queue->front = NULL;
    queue->rear = NULL;
    bsem_reset(queue->has_jobs);
    
    free(queue->has_jobs);
}

/* Post to all threads, copied*/
static void bsem_post_all(staconv *bsem_p)
{
	pthread_mutex_lock(&bsem_p->mutex);
	bsem_p->status = 1;
	pthread_cond_broadcast(&bsem_p->cond);
	pthread_mutex_unlock(&bsem_p->mutex);
}
 /* copied */
static void thread_destroy (thread* thread_p)
{
	free(thread_p);
}

/* distory the threadpool */
void distoryThreadPool(threadpool *pool)
{
    /* No need to destory if it's NULL */
	if (pool == NULL) return ;

	volatile int threads_total = pool->num_threads;

	/* End each thread 's infinite loop */
	threads_keepalive = 0;

	/* Give one second to kill idle threads */
	double TIMEOUT = 1.0;
	time_t start, end;
	double tpassed = 0.0;
	time (&start);
	while (tpassed < TIMEOUT && pool->num_threads){
		bsem_post_all(pool->queue.has_jobs);
		time (&end);
		tpassed = difftime(end,start);
	}

	/* Poll remaining threads */
	while (pool->num_threads){
		bsem_post_all(pool->queue.has_jobs);
		sleep(1);
	}

	/* Job queue cleanup */
	destory_taskqueue(&pool->queue);
	/* Deallocs */
	int n;
	for (n=0; n < threads_total; n++){
		thread_destroy(pool->threads[n]);
	}
	free(pool->threads);
	free(pool);
    // /* if the task queue is not empty, waiting */
    // while (pool->queue.len != 0 && pool->queue.has_jobs->status == 0);

    // /* distory the task queue */
    // destory_taskqueue(&pool->queue);

    // /* distory the thread pointer array, and free the memory of threadpool */
    // free(pool->threads);
    // free(pool);
}

/* get the number of working thread in the pool */
int getNumofThreadWorking(threadpool *pool)
{
    return pool->num_working;
}

/* Post to at least one thread copied */
static void bsem_post(staconv *bsem_p)
{
	pthread_mutex_lock(&bsem_p->mutex);
	bsem_p->status = 1;
	pthread_cond_signal(&bsem_p->cond);
	pthread_mutex_unlock(&bsem_p->mutex);
}

task* take_taskqueue(taskqueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
	task* job_p = queue->front;

	switch(queue->len){

		case 0:  /* if no jobs in queue */
		  			break;

		case 1:  /* if one job in queue */
					queue->front = NULL;
					queue->rear  = NULL;
					queue->len = 0;
					break;

		default: /* if >1 jobs in queue */
					queue->front = job_p->next;
					queue->len--;
					/* more than one job in queue -> post it */
					bsem_post(queue->has_jobs);

	}

	pthread_mutex_unlock(&queue->mutex);
	return job_p;
    // nwejob = queue->front;
    // queue->front = first->next;
    // len--;
    // return first;
}

/* Wait on semaphore until semaphore has value 0 */
void bsem_wait(staconv* bsem_p)
{
	pthread_mutex_lock(&bsem_p->mutex);
	while (bsem_p->status != 1) {
		pthread_cond_wait(&bsem_p->cond, &bsem_p->mutex);
	}
	bsem_p->status = 0;
	pthread_mutex_unlock(&bsem_p->mutex);
}

/* Sets the calling thread on hold copied*/
static void thread_hold(int sig_id)
{
    (void)sig_id;
	threads_on_hold = 1;
	while (threads_on_hold){
		sleep(1);
	}
}

/* the logical fuction of running thread */
void* thread_do(thread* pthread)
{
    /* set the name of thread */
    char thread_name[128] = {0};
    sprintf(thread_name, "thread-pool-%d", pthread->id); /* not sprint */

    #if defined(__linux__)
        /* Use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration */
        prctl(PR_SET_NAME, thread_name);
    #elif defined(__APPLE__) && defined(__MACH__)
        pthread_setname_np(thread_name);
    #else
        err("thread_do(): pthread_setname_np is not supported on this system");
    #endif
    // prctl(PR_SET_NAME, thread_name);

    /* obtain the threadpool */
    threadpool* pool = pthread->pool;

    /* Register signal handler copied */ 
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = thread_hold;
	if (sigaction(SIGUSR1, &act, NULL) == -1) {
		// err("thread_do(): cannot handle SIGUSR1");
	}

    /* while initialize the threadpool, count the number of finishing creation, do "pool->num_thread++" */
    pthread_mutex_lock(&pool->thcount_lock);
    (pool->num_threads)++;
    pthread_mutex_unlock(&pool->thcount_lock);

    /* the thread can be repeat, until pool->is_alive became false */
    while (threads_keepalive)
    {
        /* if the task queue still have task, keep runing, else block */
        /* if (pool->queue->len == 0)
        {
            waitThreadPool(pool);
        } */
        bsem_wait(pool->queue.has_jobs);
        
        if (threads_keepalive)
        {
            /* the thread is working, need count the working number */
			pthread_mutex_lock(&(pool->thcount_lock));
            (pool->num_working)++;
			pthread_mutex_unlock(&(pool->thcount_lock));

            /* get the first task and run */
            void (*func)(void*);
            void* arg;
            
            /* take_taskqueue get the task from the head of the queue, and delete the task in the queue */
            task* curtask = take_taskqueue(&pool->queue);
            if (curtask)
            {
                func = curtask->function;
                arg = curtask->arg;
                /* do the task */
                func(arg);
                /* release the task */
                free(curtask);
            }
            /* thread is finished, need to change the num of working threads */
			pthread_mutex_lock(&pool->thcount_lock);
            pool->num_working--;
            if (!pool->num_working)
            {
				pthread_cond_signal(&pool->threads_all_idle);
			}
			pthread_mutex_unlock(&pool->thcount_lock);
            /* if the num of working thread is 0, should run the thread which is block in the waitThreadPool() */
        }
    }
    /* the thread is going to exit, need to change the num of thread in the pool */
	pthread_mutex_lock(&pool->thcount_lock);
    pool->num_threads--;
	pthread_mutex_unlock(&pool->thcount_lock);

    return NULL;
}

#endif