/*
 * liblog -- lightweight logging library
 *
 * Copyright 2012-2019 Michael Galassi
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef LINUX
#include <sys/prctl.h>
#endif /*LINUX*/

#include "liblog.h"

#ifndef LOGDIR
#define LOGDIR "/var/log"
#endif /*LOGDIR*/

#define SHM_FLAGS IPC_CREAT|IPC_EXCL|S_IRUSR|S_IWUSR|S_IRGRP
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MINLOG 256

lls *llsp = (lls *)0;

static int llid = -1;

static int gotsigterm;
static int gotsigusr1;
static int gotsigusr2;

#define DEBUG

static void
catchsig(int sig)
{
   switch (sig) {
   case SIGTERM:
      gotsigterm = 1;
      break;
   case SIGUSR1:
      gotsigusr1 = 1;
      break;
   case SIGUSR2:
      gotsigusr2 = 1;
      break;
   }
   return;
}

void
loglvlup(void)
{
   if (++llsp->dlevel > LL_LVL_NOISE) {
      llsp->dlevel = LL_LVL_OFF;
   }
   fprintf(stderr, "loglvlup: level %d", llsp->dlevel);
}

static void
writelog(int fd)
{
   if (pwrite(fd, llsp, llsp->len + sizeof(lls), 0) == -1) {
      fprintf(stderr, "writelog: pwrite (%m)");
   }
   fdatasync(fd);
}

static void
lld(void)
{
   char fname[64];
   unsigned l;
   int fd;
   sigset_t mask;
   struct sigaction sa;

   (void)setsid();
   if (chdir(LOGDIR) == -1) {
      fprintf(stderr, "failed to cd to " LOGDIR "\n");
      if (chdir("/tmp") == -1) {
         fprintf(stderr, "failed to cd to /tmp\n");
         /* on the node we don't know where else we can write */
         _exit(EXIT_FAILURE);
      }
   }
   sa.sa_flags = 0;
   sa.sa_handler = catchsig;
   (void)sigfillset(&sa.sa_mask);                        /* no interupt nesting */
   (void)sigaction(SIGTERM, &sa, (struct sigaction *)0); /* allow SIGTERM to exit cleanly */
   (void)sigaction(SIGUSR1, &sa, (struct sigaction *)0); /* allow SIGUSR1 to flush */
   (void)sigaction(SIGUSR2, &sa, (struct sigaction *)0); /* allow SIGUSR2 to change debug level */
   sigfillset(&mask);
   sigdelset(&mask, SIGUSR1);
   sigdelset(&mask, SIGUSR2);
   sigdelset(&mask, SIGTERM);
   (void)sigprocmask(SIG_SETMASK, &mask, (sigset_t *)0);

   while (!llsp->dpid) {
      /* a you-first way of ensuring the lls struct is initialized */
      (void)sched_yield();
   }
   l = snprintf(fname, sizeof(fname), "lldump.%d", llsp->dpid);
   if (l >= sizeof(fname)) {
      fprintf(stderr, "lld: filename too long");
      return;
   }
   if ((fd = open(fname, O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1) {
      fprintf(stderr, "lld: open (%m)");
      return;
   }
#ifdef LINUX
   (void)prctl(PR_SET_NAME, (unsigned long *)"lldump", 0, 0);
#endif /*LINUX*/
   gotsigterm = gotsigusr1 = gotsigusr2 = 0;
   while (1) {
      (void)sigsuspend(&mask);  /* just wait around */
      if (gotsigterm) {
         writelog(fd);          /* flush to disk */
         (void)close(fd);
         (void)shmdt(llsp);
         (void)shmctl(llid, IPC_RMID, 0);
         _exit(EXIT_SUCCESS);
      }
      if (gotsigusr1) {
         writelog(fd);
         gotsigusr1 = 0;
      }
      if (gotsigusr2) {
         loglvlup();
         gotsigusr2 = 0;
      }
   }
   /*NOTREACHED*/
}

int
ll_open(int level, size_t len)
{
   pid_t pid;

   if (llsp) {
      return 0;
   }
   if (level < LL_LVL_OFF || level > LL_LVL_NOISE) {
      fprintf(stderr, "ll_open: level out of range");
      return -1;
   }
   if (len <= MINLOG + sizeof(lls)) {
      fprintf(stderr, "ll_open: len too small");
      return -1;
   }
   /* create shared memory segment */
   if ((llid = shmget(IPC_PRIVATE, len, SHM_FLAGS)) == -1) {
      fprintf(stderr, "ll_open: shmget (%m)");
      return -1;
   }
   if ((llsp = (lls *)shmat(llid, (void *)0, 0)) == (void *)-1) {
      fprintf(stderr, "ll_open: shmat (%m)");
      return -1;
   }
   (void)memset(llsp, 0, len);
   llsp->dlevel = level;
   llsp->head = 1;
   llsp->len = len - sizeof(lls);
   *llsp->buf = '\n';
   if ((pid = fork()) > 0) {            /* parent */
      llsp->dpid = pid;
      (void)signal(SIGCHLD, SIG_IGN);   /* no zombies please */
   } else if (pid == 0) {               /* child */
      lld();
      /*NOTREACHED*/
   } else {                             /* sigh */
      fprintf(stderr, "ll_open: fork (%m)");
      return -1;
   }
   return 0;
}

void
ll_log(int level, const char *fmt, ...)
{
   va_list aptr;
   struct timespec ts;
   size_t len;
   char s[MINLOG];

   if (!llsp) {
      return;           /* uninitialized */
   }
   if (level > llsp->dlevel) {
      return;
   }
   if ((clock_gettime(CLOCK_REALTIME_FAST, &ts)) == -1) {
      fprintf(stderr, "ll_log: clock_gettime (%m)");
      (void)memset(&ts, 0, sizeof(ts));
   }
   /* prefix microseconds to the format string */
   len = sprintf(s, "%lu: ", (unsigned long)ts.tv_sec * 1000 * 1000 + ts.tv_nsec / 1000);
   va_start(aptr, fmt);
   len += vsnprintf(&s[len], sizeof(s) - len - 1, fmt, aptr);
   va_end(aptr);
   /* silently truncate */
   len = MIN(len, sizeof(s)-1) + 1;     /* +1 is the null */

   if (len <= llsp->len - llsp->head) {
      /* the easy case */
      memcpy(&llsp->buf[llsp->head], s, len);
      llsp->head += (len - 1);
      if (llsp->head == llsp->len) {
         llsp->head = 0;     /* wrap */
      }
   } else {
      /* copy some to the end, then wrap to the start and copy the remainder */
      memcpy(&llsp->buf[llsp->head], s, llsp->len - llsp->head);
      memcpy(&llsp->buf[0], &s[llsp->len - llsp->head], len - (llsp->len - llsp->head));
      llsp->head = len - (llsp->len - llsp->head) - 1;
   }
   return;
}

void
ll_close(void)
{
   if (!llsp) {
      return;
   }
   (void)kill(llsp->dpid, SIGTERM);
   (void)shmdt(llsp);
   (void)shmctl(llid, IPC_RMID, 0);
}

void
ll_flush(void)
{
   (void)kill(llsp->dpid, SIGUSR1);
}

int
ll_setlevel(int l)
{
   int tmp;

   if (!llsp || l < LL_LVL_OFF || l > LL_LVL_NOISE) {
      return LL_LVL_BOGUS;      /* uninitialized or out of range */
   }
   tmp = llsp->dlevel;
   llsp->dlevel = l;
   fprintf(stderr, "ll_setlevel: level %d -> %d", tmp, l);
   return tmp;
}
