/* Copyright (c) 1988 Regents of the University of California */

/* SCCSid "$SunId$ LBL" */

/*
 *	Miscellaneous definitions required by many routines.
 */

#include  <stdio.h>

#include  <math.h>

#include  <errno.h>

#include  "fvect.h"

#define  FHUGE		(1e10)		/* large real number */
#define  FTINY		(1e-6)		/* small real number */

#ifdef  M_PI
#define  PI		M_PI
#else
#define  PI		3.14159265358979323846
#endif

#ifndef  F_OK			/* mode bits for access(2) call */
#define  R_OK		4		/* readable */
#define  W_OK		2		/* writable */
#define  X_OK		1		/* executable */
#define  F_OK		0		/* exists */
#endif
				/* error codes */
#define  WARNING	1		/* non-fatal error */
#define  USER		2		/* fatal user-caused error */
#define  SYSTEM		3		/* fatal system-related error */
#define  INTERNAL	4		/* fatal program-related error */
#define  CONSISTENCY	5		/* bad consistency check, abort */
#define  COMMAND	6		/* interactive error */

extern char  errmsg[];			/* global buffer for error messages */

extern int  errno;			/* system error number */

extern char  *sskip();
extern char  *getpath();
extern char  *malloc(), *calloc(), *realloc();
extern char  *bmalloc(), *savestr(), *savqstr();
