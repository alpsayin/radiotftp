

#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "timers.h"

static struct sigaction sa;
static struct itimerval timer;
void (*mainTimerHandler)(int sig );

uint8_t timers_initialize( void(*handlerfptr)(int sig ))
{
    mainTimerHandler=handlerfptr;
    return 0;
}

uint8_t timers_create_timer( int expireS, int intervalS, int expireUS, int intervalUS)
{
/*
  struct sigevent         te;
  struct itimerspec       its;
  struct sigaction        sa;
  int                     sigNo = SIGRTMIN;

  // Set up signal handler.
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = mainTimerHandler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(sigNo, &sa, NULL) == -1)
    {
      fprintf(stderr, "Failed to setup signal handling for %s.\n", name);
      return -2;
    }

  // Set and enable alarm 
  te.sigev_notify = SIGEV_SIGNAL;
  te.sigev_signo = sigNo;
  te.sigev_value.sival_ptr = timerID;
  timer_create(CLOCK_REALTIME, &te, timerID);

  its.it_interval.tv_sec = intervalS;
  its.it_interval.tv_nsec = 0;
  its.it_value.tv_sec = expireS;
  its.it_value.tv_nsec = 0;
  timer_settime(*timerID, 0, &its, NULL);
*/

    /* Install timer_handler as the signal handler for SIGVTALRM.  */
    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = mainTimerHandler;
    sigaction (SIGALRM, &sa, NULL);

    /* Configure the timer to expire after 250 msec...  */
    timer.it_value.tv_sec = expireS;
    timer.it_value.tv_usec = expireUS;
    /* ... and every 250 msec after that.  */
    timer.it_interval.tv_sec = intervalUS;
    timer.it_interval.tv_usec = intervalS;
    /* Start a virtual timer. It counts down whenever this process is
    executing.  */
    setitimer (ITIMER_REAL, &timer, NULL);

  return 0;
}
uint8_t timers_cancel_timer(void)
{
    /* Install timer_handler as the signal handler for SIGVTALRM.  */
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = mainTimerHandler;
    sigaction (SIGALRM, NULL, NULL);

    /* Configure the timer to expire after 250 msec...  */
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    /* ... and every 250 msec after that.  */
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    /* Start a virtual timer. It counts down whenever this process is
    executing.  */
    setitimer (ITIMER_REAL, NULL, NULL);
    //alarm(0);

    return 0;
}
