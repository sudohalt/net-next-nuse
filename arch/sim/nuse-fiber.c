#include <stdio.h>
#define __USE_GNU 1
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/resource.h>
//#include "nuse-hostcalls.h"
#include "sim-assert.h"

/* FIXME */
extern int (*host_pthread_create)(pthread_t *, const pthread_attr_t *,
                                  void *(*) (void *), void *);

typedef unsigned int uint32_t;
enum {
	false	= 0,
	true	= 1
};
void *sim_malloc (unsigned long size);
void sim_free (void *);

struct NuseFiber
{
  pthread_t pthread;
  pthread_mutex_t mutex;
  pthread_cond_t condvar;
  void* (*func)(void *);
  void *context;
  const char *name;
  int canceled;
  timer_t timerid;
};

#include <sched.h>
/* FIXME: more precise affinity shoudl be required. */
void
nuse_set_affinity ()
{
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  sched_setaffinity (getpid (), sizeof(cpu_set_t), &cpuset);
}

void *nuse_fiber_new_from_caller (uint32_t stackSize, const char *name)
{
  struct NuseFiber *fiber = sim_malloc (sizeof (struct NuseFiber));
  fiber->func = NULL;
  fiber->context = NULL;
  fiber->pthread = pthread_self (); 
  fiber->name = name;
  fiber->canceled = 0;
  fiber->timerid = NULL;
  return fiber;
}

void *nuse_fiber_new (void* (*callback)(void *),
                     void *context,
                     uint32_t stackSize,
                     const char *name)
{
  struct NuseFiber *fiber = nuse_fiber_new_from_caller (stackSize, name);
  fiber->func = callback;
  fiber->context = context;
  return fiber;
}

void nuse_fiber_start (void * handler)
{
  struct NuseFiber *fiber = handler;
  int error;

  error = pthread_mutex_init (&fiber->mutex, NULL);
  sim_assert (error == 0);
  error = pthread_cond_init (&fiber->condvar, NULL);
  sim_assert (error == 0);

  error = host_pthread_create (&fiber->pthread, NULL, 
                              fiber->func, fiber->context);
  sim_assert (error == 0);
  pthread_setname_np (fiber->pthread, fiber->name);

  return;
}

int
nuse_fiber_isself (void *handler)
{
  struct NuseFiber *fiber = handler;
  return fiber->pthread == pthread_self ();
}

void
nuse_fiber_wait (void *handler)
{
  struct NuseFiber *fiber = handler;
  pthread_mutex_lock (&fiber->mutex);
  pthread_cond_wait (&fiber->condvar, &fiber->mutex);
  pthread_mutex_unlock (&fiber->mutex);
}

int
nuse_fiber_wakeup (void *handler)
{
  struct NuseFiber *fiber = handler;
  int ret = pthread_cond_signal (&fiber->condvar);

  return (ret == 0 ? 1 : 0);
}

void nuse_fiber_stop (void * handler)
{
  struct NuseFiber *fiber = handler;
  fiber->canceled = 0;
  if (fiber->timerid)
    {
      timer_delete (fiber->timerid);
    }
  fiber->timerid = NULL;
  return;
}

int nuse_fiber_is_stopped (void * handler)
{
  struct NuseFiber *fiber = handler;
  return fiber->canceled;
}

void nuse_fiber_free (void * handler)
{
  struct NuseFiber *fiber = handler;
  pthread_mutex_destroy (&fiber->mutex);
  pthread_cond_destroy (&fiber->condvar);
  //  pthread_join (fiber->pthread, 0);
  sim_free (fiber);
}


/* hijacked functions */
extern struct list_head g_task_lists;
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg)
{
  int ret = host_pthread_create (thread, attr, start_routine, arg);
  if (ret)
    {
      return ret;
    }

  int error;
  struct NuseFiber *fiber = sim_malloc (sizeof (struct NuseFiber));
  fiber->func = NULL;
  fiber->context = NULL;
  fiber->pthread = *thread; 
  fiber->name = "app_thread";
  fiber->canceled = 0;
  pthread_setname_np (fiber->pthread, fiber->name);


  error = pthread_mutex_init (&fiber->mutex, NULL);
  sim_assert (error == 0);
  error = pthread_cond_init (&fiber->condvar, NULL);
  sim_assert (error == 0);

  nuse_task_add (fiber);
  return ret;
}


struct NuseTimerTrampolineContext
{
  void (*callback) (void *arg);
  void *context;
  timer_t timerid;
};

void
nuse_timer_trampoline (sigval_t context)
{
  struct NuseTimerTrampolineContext *ctx = context.sival_ptr;
  ctx->callback (ctx->context);
  if (ctx->timerid)
    {
      timer_delete (ctx->timerid);
    }
  ctx->timerid = NULL;
  sim_free (ctx);
  return;
}

void
nuse_add_timer (unsigned long ns, 
               void (*func) (void *arg),
               void *arg,
               void *handler)
{
  struct sigevent se;
  struct itimerspec new_value;
  struct NuseFiber *fiber = handler;

  new_value.it_value.tv_sec = ns / (1000*1000*1000);
  new_value.it_value.tv_nsec = ns % (1000*1000*1000);
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_nsec = 0;

  //  printf ("to %d sec %d nsec\n", new_value.it_value.tv_sec, new_value.it_value.tv_nsec);

  struct NuseTimerTrampolineContext *ctx = sim_malloc (sizeof (struct NuseTimerTrampolineContext));
  if (!ctx)
    {
      return;
    }
  ctx->callback = func;
  ctx->context = arg;

  memset (&se, 0, sizeof(se));
  se.sigev_value.sival_ptr = ctx;
  se.sigev_notify = SIGEV_THREAD;
  se.sigev_notify_function = nuse_timer_trampoline;
  se.sigev_notify_attributes = NULL;
  int ret = timer_create (CLOCK_REALTIME, &se, &ctx->timerid);
  if (ret)
    perror ("timer_create");
  ret = timer_settime (ctx->timerid, 0, &new_value, NULL);
  if (ret)
    perror ("timer_settime");

  fiber->timerid = ctx->timerid;
  return;
}
