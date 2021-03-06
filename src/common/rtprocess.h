/* RCSid $Id: rtprocess.h,v 3.17 2018/03/20 17:45:07 greg Exp $ */
/*
 *   rtprocess.h 
 *   Routines to communicate with separate process via dual pipes
 *
 *   WARNING: On Windows, there's a system header named <process.h>.
 */
#ifndef _RAD_PROCESS_H_
#define _RAD_PROCESS_H_

#include  <errno.h>
#include <stdio.h>
#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h> /* DWORD etc. */
  typedef DWORD RT_PID;
  #include <process.h> /* getpid() and others */
  #define getpid _getpid
  #define execv _execv
  #define execvp _execvp
#else
  #include <sys/param.h>
  #include <sys/types.h>
  typedef pid_t RT_PID;
#endif

#include "paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On Windows, a process ID is a DWORD. That might actually be the
   same thing as an int, but it's better not to assume anything.

   This means that we shouldn't rely on PIDs and file descriptors
   being the same type, so we have to describe processes with a struct,
   instead of the original int[3]. For that purpose, we typedef a
   platform independent RT_PID.
*/


#ifndef PIPE_BUF
  #ifdef PIPSIZ
    #define PIPE_BUF	PIPSIZ
  #else
    #ifdef PIPE_MAX
      #define PIPE_BUF	PIPE_MAX
    #else
      #define PIPE_BUF	512		/* hyperconservative */
    #endif
  #endif
#endif

typedef struct {
	int r; /* read handle */
	int w; /* write handle */
	int running; /* doing something */
	RT_PID pid; /* process ID */
} SUBPROC;

#define SP_INACTIVE {-1,-1,0,0} /* for static initializations */

#define close_process(pd)	close_processes(pd,1)

extern int open_process(SUBPROC *pd, char *av[]);
extern int close_processes(SUBPROC pd[], int nproc);
extern int process(SUBPROC *pd, char *recvbuf, char *sendbuf, int nbr, int nbs);
extern int readbuf(int fd, char *bpos, int siz);
extern int writebuf(int fd, char *bpos, int siz);

#if defined(_WIN32) || defined(_WIN64)
/* any non-negative increment will send the process to IDLE_PRIORITY_CLASS. */
extern int win_kill(RT_PID pid, int sig /* ignored */);
extern int win_nice(int inc);
#endif


#ifdef __cplusplus
}
#endif
#endif /* _RAD_PROCESS_H_ */

