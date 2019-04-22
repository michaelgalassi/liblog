/*
 * liblog support
 *
 * Copyright 2012-2019 Michael Galassi
 */

#ifndef __liblog_h__
#define __liblog_h__

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*
 * the "api", not a heavy one :-)
 */

/*
 * logging levels, messages less important than the currently set
 * level reduce to a test and a return
 */
#define LL_LVL_OFF 0
#define LL_LVL_PANIC 1
#define LL_LVL_ERR 2
#define LL_LVL_WARN 3
#define LL_LVL_INFO 4
#define LL_LVL_NOTE 5
#define LL_LVL_DBG 6
#define LL_LVL_NOISE 7
#define LL_LVL_BOGUS -1

/* set up shared memory segment, daemon, etc */
int ll_open(int level, size_t len);
/* tear down what was set up and flush buffer */
void ll_close(void);
/* add an entry to the log */
void ll_log(int level, const char *fmt, ...);
/* force a flush to disk */
void ll_flush(void);
/* set logging level */
int ll_setlevel(int l);

/*
 * this lives at the start of the shared memory segment
 */
typedef struct {
   size_t len;          /* usable buffer */
   size_t head;         /* next write goes here */
   int dlevel;          /* current level we log at */
   pid_t dpid;          /* daemon's PID */
   char buf[0];
} lls;

/*
 * provide more information in logged message
 *
 * note that you can't do:
 *      LLOG(lvl, "string");
 * you instead need
 *      LLOG(lvl, "%s", "string");
 *
 * also note that even though ll_log() is fast by intent this is much
 * faster as it skips putting anything on the stack if we're below the
 * selected logging level
 */
extern lls *llsp;

#define LLOG(lvl, fmt, ...) { \
      if (llsp && lvl <= llsp->dlevel) { \
         ll_log(lvl, "%s:%d " fmt "\n", __FILE__, __LINE__, ## __VA_ARGS__); \
      } \
   }

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* __liblog_h__ */
