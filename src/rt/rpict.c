/* Copyright (c) 1992 Regents of the University of California */

#ifndef lint
static char SCCSid[] = "$SunId$ LBL";
#endif

/*
 *  rpict.c - routines and variables for picture generation.
 *
 *     8/14/85
 */

#include  "ray.h"

#ifdef BSD
#include  <sys/time.h>
#include  <sys/resource.h>
#endif

#include  <signal.h>

#include  "view.h"

#include  "resolu.h"

#include  "random.h"

#include  "paths.h"

int  dimlist[MAXDIM];			/* sampling dimensions */
int  ndims = 0;				/* number of sampling dimensions */
int  samplendx;				/* sample index number */

VIEW  ourview = STDVIEW;		/* view parameters */
int  hresolu = 512;			/* horizontal resolution */
int  vresolu = 512;			/* vertical resolution */
double	pixaspect = 1.0;		/* pixel aspect ratio */

int  psample = 4;			/* pixel sample size */
double	maxdiff = .05;			/* max. difference for interpolation */
double	dstrpix = 0.67;			/* square pixel distribution */

double	dstrsrc = 0.0;			/* square source distribution */
double	shadthresh = .05;		/* shadow threshold */
double	shadcert = .5;			/* shadow certainty */
int  directrelay = 1;			/* number of source relays */
int  vspretest = 512;			/* virtual source pretest density */
int  directinvis = 0;			/* sources invisible? */
double	srcsizerat = .25;		/* maximum ratio source size/dist. */

double	specthresh = .15;		/* specular sampling threshold */
double	specjitter = 1.;		/* specular sampling jitter */

int  maxdepth = 6;			/* maximum recursion depth */
double	minweight = 5e-3;		/* minimum ray weight */

COLOR  ambval = BLKCOLOR;		/* ambient value */
double	ambacc = 0.2;			/* ambient accuracy */
int  ambres = 32;			/* ambient resolution */
int  ambdiv = 128;			/* ambient divisions */
int  ambssamp = 0;			/* ambient super-samples */
int  ambounce = 0;			/* ambient bounces */
char  *amblist[128];			/* ambient include/exclude list */
int  ambincl = -1;			/* include == 1, exclude == 0 */

#ifdef MSDOS
int  ralrm = 60;			/* seconds between reports */
#else
int  ralrm = 0;				/* seconds between reports */
#endif

double	pctdone = 0.0;			/* percentage done */

long  tlastrept = 0L;			/* time at last report */

extern long  time();
extern long  tstart;			/* starting time */

extern long  nrays;			/* number of rays traced */

#define	 MAXDIV		16		/* maximum sample size */

#define	 pixjitter()	(.5+dstrpix*(.5-frandom()))

#define	 RFTEMPLATE	"rfXXXXXX"
#define	 HFTEMPLATE	TEMPLATE

static char  *hfname = NULL;		/* header file name */
static FILE  *hfp = NULL;		/* header file pointer */

static int  hres, vres;			/* resolution for this frame */

extern char  *mktemp();

double	pixvalue();


quit(code)			/* quit program */
int  code;
{
	if (code)			/* report status */
		report();
	if (hfname != NULL) {		/* delete header file */
		if (hfp != NULL)
			fclose(hfp);
		unlink(hfname);
	}
	exit(code);
}


#ifdef BSD
report()		/* report progress */
{
	struct rusage  rubuf;
	double	t;

	getrusage(RUSAGE_SELF, &rubuf);
	t = (rubuf.ru_utime.tv_usec + rubuf.ru_stime.tv_usec) / 1e6;
	t += rubuf.ru_utime.tv_sec + rubuf.ru_stime.tv_sec;
	getrusage(RUSAGE_CHILDREN, &rubuf);
	t += (rubuf.ru_utime.tv_usec + rubuf.ru_stime.tv_usec) / 1e6;
	t += rubuf.ru_utime.tv_sec + rubuf.ru_stime.tv_sec;

	sprintf(errmsg, "%ld rays, %4.2f%% done after %5.4f CPU hours\n",
			nrays, pctdone, t/3600.0);
	eputs(errmsg);
	tlastrept = time((long *)0);
}
#else
report()		/* report progress */
{
	tlastrept = time((long *)0);
	sprintf(errmsg, "%ld rays, %4.2f%% done after %5.4f hours\n",
			nrays, pctdone, (tlastrept-tstart)/3600.0);
	eputs(errmsg);
	signal(SIGALRM, report);
}
#endif


openheader()			/* save standard output to header file */
{
	hfname = mktemp(HFTEMPLATE);
	if (freopen(hfname, "w", stdout) == NULL) {
		sprintf(errmsg, "cannot open header file \"%s\"", hfname);
		error(SYSTEM, errmsg);
	}
}


closeheader()			/* done with header output */
{
	if (hfname == NULL)
		return;
	if (fflush(stdout) == EOF || (hfp = fopen(hfname, "r")) == NULL)
		error(SYSTEM, "error reopening header file");
#ifdef MSDOS
	setmode(fileno(hfp), O_BINARY);
#endif
}


dupheader()			/* repeat header on standard output */
{
	register int  c;

	if (fseek(hfp, 0L, 0) < 0)
		error(SYSTEM, "seek error on header file");
	while ((c = getc(hfp)) != EOF)
		putchar(c);
}


rpict(seq, pout, zout, prvr)			/* generate image(s) */
int  seq;
char  *pout, *zout, *prvr;
/*
 * If seq is greater than zero, then we will render a sequence of
 * images based on view parameter strings read from the standard input.
 * If pout is NULL, then all images will be sent to the standard ouput.
 * If seq is greater than zero and prvr is an integer, then it is the
 * frame number at which rendering should begin.  Preceeding view parameter
 * strings will be skipped in the input.
 * If pout and prvr are the same, prvr is renamed to avoid overwriting.
 * Note that pout and zout should contain %d format specifications for
 * sequenced file naming.
 */
{
	extern char  *rindex(), *strncpy(), *strcat();
	char  fbuf[128], fbuf2[128];
	register char  *cp;
	RESOLU	rs;
	double	pa;
					/* finished writing header */
	closeheader();
					/* check sampling */
	if (psample < 1)
		psample = 1;
	else if (psample > MAXDIV) {
		sprintf(errmsg, "pixel sampling reduced from %d to %d",
				psample, MAXDIV);
		error(WARNING, errmsg);
		psample = MAXDIV;
	}
					/* get starting frame */
	if (seq <= 0)
		seq = 0;
	else if (prvr != NULL && isint(prvr)) {
		register int  rn;		/* skip to specified view */
		if ((rn = atoi(prvr)) < seq)
			error(USER, "recover frame less than start frame");
		if (pout == NULL)
			error(USER, "missing output file specification");
		for ( ; seq < rn; seq++)
			if (nextview(stdin) == EOF)
				error(USER, "unexpected EOF on view input");
		prvr = fbuf;			/* mark for renaming */
	}
	if (pout != NULL) {
		sprintf(fbuf, pout, seq);
		if (prvr != NULL && !strcmp(prvr, fbuf)) {	/* rename */
			fbuf2[0] = '\0';
			if ((cp = rindex(fbuf, '/')) != NULL)
				strncpy(fbuf2, fbuf, cp-fbuf+1);
			strcat(fbuf2, RFTEMPLATE);
			prvr = mktemp(fbuf2);
			if (rename(fbuf, prvr) < 0 && errno != ENOENT) {
				sprintf(errmsg,
					"cannot rename \"%s\" to \"%s\"",
						fbuf, prvr);
				error(SYSTEM, errmsg);
			}
		}
	}
					/* render sequence */
	do {
		if (seq && nextview(stdin) == EOF)
			break;
		if (pout != NULL) {
			sprintf(fbuf, pout, seq);
			if (freopen(fbuf, "w", stdout) == NULL) {
				sprintf(errmsg,
					"cannot open output file \"%s\"", fbuf);
				error(SYSTEM, errmsg);
			}
#ifdef MSDOS
			setmode(fileno(stdout), O_BINARY);
#endif
			dupheader();
		}
		hres = hresolu; vres = vresolu; pa = pixaspect;
		if (prvr != NULL)
			if (viewfile(prvr, &ourview, &rs) <= 0
					|| rs.or != PIXSTANDARD) {
				sprintf(errmsg,
			"cannot recover view parameters from \"%s\"", prvr);
				error(WARNING, errmsg);
			} else {
				pa = 0.0;
				hres = scanlen(&rs);
				vres = numscans(&rs);
			}
		if ((cp = setview(&ourview)) != NULL)
			error(USER, cp);
		normaspect(viewaspect(&ourview), &pa, &hres, &vres);
		if (seq) {
			if (ralrm > 0) {
				fprintf(stderr, "FRAME %d:", seq);
				fprintview(&ourview, stderr);
				putc('\n', stderr);
				fflush(stderr);
			}
			printf("FRAME=%d\n", seq);
		}
		fputs(VIEWSTR, stdout);
		fprintview(&ourview, stdout);
		putchar('\n');
		if (pa < .99 || pa > 1.01)
			fputaspect(pa, stdout);
		fputformat(COLRFMT, stdout);
		putchar('\n');
		if (zout != NULL)
			sprintf(cp=fbuf, zout, seq);
		else
			cp = NULL;
		render(cp, prvr);
		prvr = NULL;
	} while (seq++);
}


nextview(fp)				/* get next view from fp */
FILE  *fp;
{
	char  linebuf[256];

	while (fgets(linebuf, sizeof(linebuf), fp) != NULL)
		if (isview(linebuf) && sscanview(&ourview, linebuf) > 0)
			return(0);
	return(EOF);
}	


render(zfile, oldfile)				/* render the scene */
char  *zfile, *oldfile;
{
	extern long  lseek();
	COLOR  *scanbar[MAXDIV+1];	/* scanline arrays of pixel values */
	float  *zbar[MAXDIV+1];		/* z values */
	char  *sampdens;		/* previous sample density */
	int  ypos;			/* current scanline */
	int  ystep;			/* current y step size */
	int  hstep;			/* h step size */
	int  zfd;
	COLOR  *colptr;
	float  *zptr;
	register int  i;
					/* allocate scanlines */
	for (i = 0; i <= psample; i++) {
		scanbar[i] = (COLOR *)malloc(hres*sizeof(COLOR));
		if (scanbar[i] == NULL)
			goto memerr;
	}
	hstep = (psample*140+49)/99;		/* quincunx sampling */
	ystep = (psample*99+70)/140;
	if (hstep > 2) {
		i = hres/hstep + 2;
		if ((sampdens = malloc(i)) == NULL)
			goto memerr;
		while (i--)
			sampdens[i] = hstep;
	} else
		sampdens = NULL;
					/* open z file */
	if (zfile != NULL) {
		if ((zfd = open(zfile, O_WRONLY|O_CREAT, 0666)) == -1) {
			sprintf(errmsg, "cannot open z file \"%s\"", zfile);
			error(SYSTEM, errmsg);
		}
#ifdef MSDOS
		setmode(zfd, O_BINARY);
#endif
		for (i = 0; i <= psample; i++) {
			zbar[i] = (float *)malloc(hres*sizeof(float));
			if (zbar[i] == NULL)
				goto memerr;
		}
	} else {
		zfd = -1;
		for (i = 0; i <= psample; i++)
			zbar[i] = NULL;
	}
					/* write out boundaries */
	fprtresolu(hres, vres, stdout);
					/* recover file and compute first */
	i = salvage(oldfile);
	if (zfd != -1 && i > 0 &&
			lseek(zfd, (long)i*hres*sizeof(float), 0) == -1)
		error(SYSTEM, "z file seek error in render");
	pctdone = 100.0*i/vres;
	if (ralrm > 0)			/* report init stats */
		report();
#ifndef	 BSD
	else
#endif
	signal(SIGALRM, report);
	ypos = vres-1 - i;
	fillscanline(scanbar[0], zbar[0], sampdens, hres, ypos, hstep);
						/* compute scanlines */
	for (ypos -= ystep; ypos > -ystep; ypos -= ystep) {
							/* bottom adjust? */
		if (ypos < 0) {
			ystep += ypos;
			ypos = 0;
		}
		colptr = scanbar[ystep];		/* move base to top */
		scanbar[ystep] = scanbar[0];
		scanbar[0] = colptr;
		zptr = zbar[ystep];
		zbar[ystep] = zbar[0];
		zbar[0] = zptr;
							/* fill base line */
		fillscanline(scanbar[0], zbar[0], sampdens,
				hres, ypos, hstep);
							/* fill bar */
		fillscanbar(scanbar, zbar, hres, ypos, ystep);
							/* write it out */
#ifndef	 BSD
		signal(SIGALRM, SIG_IGN);	/* don't interrupt writes */
#endif
		for (i = ystep; i > 0; i--) {
			if (zfd != -1 && write(zfd, (char *)zbar[i],
					hres*sizeof(float))
					< hres*sizeof(float))
				goto writerr;
			if (fwritescan(scanbar[i], hres, stdout) < 0)
				goto writerr;
		}
		if (fflush(stdout) == EOF)
			goto writerr;
							/* record progress */
		pctdone = 100.0*(vres-1-ypos)/vres;
		if (ralrm > 0 && time((long *)0) >= tlastrept+ralrm)
			report();
#ifndef	 BSD
		else
			signal(SIGALRM, report);
#endif
	}
						/* clean up */
	signal(SIGALRM, SIG_IGN);
	if (zfd != -1) {
		if (write(zfd, (char *)zbar[0], hres*sizeof(float))
				< hres*sizeof(float))
			goto writerr;
		if (close(zfd) == -1)
			goto writerr;
		for (i = 0; i <= psample; i++)
			free((char *)zbar[i]);
	}
	fwritescan(scanbar[0], hres, stdout);
	if (fflush(stdout) == EOF)
		goto writerr;
	for (i = 0; i <= psample; i++)
		free((char *)scanbar[i]);
	if (sampdens != NULL)
		free(sampdens);
	pctdone = 100.0;
	if (ralrm > 0)
		report();
	return;
writerr:
	error(SYSTEM, "write error in render");
memerr:
	error(SYSTEM, "out of memory in render");
}


fillscanline(scanline, zline, sd, xres, y, xstep)	/* fill scan at y */
register COLOR	*scanline;
register float	*zline;
register char  *sd;
int  xres, y, xstep;
{
	static int  nc = 0;		/* number of calls */
	int  bl = xstep, b = xstep;
	double	z;
	register int  i;
	
	z = pixvalue(scanline[0], 0, y);
	if (zline) zline[0] = z;
				/* zig-zag start for quincunx pattern */
	for (i = ++nc & 1 ? xstep : xstep/2; i < xres-1+xstep; i += xstep) {
		if (i >= xres) {
			xstep += xres-1-i;
			i = xres-1;
		}
		z = pixvalue(scanline[i], i, y);
		if (zline) zline[i] = z;
		if (sd) b = sd[0] > sd[1] ? sd[0] : sd[1];
		if (i <= xstep)
			b = fillsample(scanline, zline, 0, y, i, 0, b/2);
		else
			b = fillsample(scanline+i-xstep,
					zline ? zline+i-xstep : NULL,
					i-xstep, y, xstep, 0, b/2);
		if (sd) *sd++ = nc & 1 ? bl : b;
		bl = b;
	}
	if (sd && nc & 1) *sd = bl;
}


fillscanbar(scanbar, zbar, xres, y, ysize)	/* fill interior */
register COLOR	*scanbar[];
register float	*zbar[];
int  xres, y, ysize;
{
	COLOR  vline[MAXDIV+1];
	float  zline[MAXDIV+1];
	int  b = ysize;
	register int  i, j;
	
	for (i = 0; i < xres; i++) {
		
		copycolor(vline[0], scanbar[0][i]);
		copycolor(vline[ysize], scanbar[ysize][i]);
		if (zbar[0]) {
			zline[0] = zbar[0][i];
			zline[ysize] = zbar[ysize][i];
		}
		
		b = fillsample(vline, zbar[0] ? zline : NULL,
				i, y, 0, ysize, b/2);
		
		for (j = 1; j < ysize; j++)
			copycolor(scanbar[j][i], vline[j]);
		if (zbar[0])
			for (j = 1; j < ysize; j++)
				zbar[j][i] = zline[j];
	}
}


int
fillsample(colline, zline, x, y, xlen, ylen, b) /* fill interior points */
register COLOR	*colline;
register float	*zline;
int  x, y;
int  xlen, ylen;
int  b;
{
	double	ratio;
	double	z;
	COLOR  ctmp;
	int  ncut;
	register int  len;
	
	if (xlen > 0)			/* x or y length is zero */
		len = xlen;
	else
		len = ylen;
		
	if (len <= 1)			/* limit recursion */
		return(0);
	
	if (b > 0
	|| (zline && 2.*fabs(zline[0]-zline[len]) > maxdiff*(zline[0]+zline[len]))
			|| bigdiff(colline[0], colline[len], maxdiff)) {
	
		z = pixvalue(colline[len>>1], x + (xlen>>1), y + (ylen>>1));
		if (zline) zline[len>>1] = z;
		ncut = 1;
		
	} else {					/* interpolate */
	
		copycolor(colline[len>>1], colline[len]);
		ratio = (double)(len>>1) / len;
		scalecolor(colline[len>>1], ratio);
		if (zline) zline[len>>1] = zline[len] * ratio;
		ratio = 1.0 - ratio;
		copycolor(ctmp, colline[0]);
		scalecolor(ctmp, ratio);
		addcolor(colline[len>>1], ctmp);
		if (zline) zline[len>>1] += zline[0] * ratio;
		ncut = 0;
	}
							/* recurse */
	ncut += fillsample(colline, zline, x, y, xlen>>1, ylen>>1, (b-1)/2);
	
	ncut += fillsample(colline+(len>>1), zline ? zline+(len>>1) : NULL,
			x+(xlen>>1), y+(ylen>>1),
			xlen-(xlen>>1), ylen-(ylen>>1), b/2);

	return(ncut);
}


double
pixvalue(col, x, y)		/* compute pixel value */
COLOR  col;			/* returned color */
int  x, y;			/* pixel position */
{
	static RAY  thisray;

	if (viewray(thisray.rorg, thisray.rdir, &ourview,
			(x+pixjitter())/hres, (y+pixjitter())/vres) < 0) {
		setcolor(col, 0.0, 0.0, 0.0);
		return(0.0);
	}

	rayorigin(&thisray, NULL, PRIMARY, 1.0);

	samplendx = pixnumber(x,y,hres,vres);	/* set pixel index */

	rayvalue(&thisray);			/* trace ray */

	copycolor(col, thisray.rcol);		/* return color */
	
	return(thisray.rt);			/* return distance */
}


int
salvage(oldfile)		/* salvage scanlines from killed program */
char  *oldfile;
{
	COLR  *scanline;
	FILE  *fp;
	int  x, y;

	if (oldfile == NULL)
		return(0);
	
	if ((fp = fopen(oldfile, "r")) == NULL) {
		sprintf(errmsg, "cannot open recover file \"%s\"", oldfile);
		error(WARNING, errmsg);
		return(0);
	}
#ifdef MSDOS
	setmode(fileno(fp), O_BINARY);
#endif
				/* discard header */
	getheader(fp, NULL);
				/* get picture size */
	if (!fscnresolu(&x, &y, fp)) {
		sprintf(errmsg, "bad recover file \"%s\"", oldfile);
		error(WARNING, errmsg);
		fclose(fp);
		return(0);
	}

	if (x != hres || y != vres) {
		sprintf(errmsg, "resolution mismatch in recover file \"%s\"",
				oldfile);
		error(USER, errmsg);
	}

	scanline = (COLR *)malloc(hres*sizeof(COLR));
	if (scanline == NULL)
		error(SYSTEM, "out of memory in salvage");
	for (y = 0; y < vres; y++) {
		if (freadcolrs(scanline, hres, fp) < 0)
			break;
		if (fwritecolrs(scanline, hres, stdout) < 0)
			goto writerr;
	}
	if (fflush(stdout) == EOF)
		goto writerr;
	free((char *)scanline);
	fclose(fp);
	unlink(oldfile);
	return(y);
writerr:
	sprintf(errmsg, "write error during recovery of \"%s\"", oldfile);
	error(SYSTEM, errmsg);
}


int
pixnumber(x, y, xres, yres)		/* compute pixel index (brushed) */
register int  x, y;
int  xres, yres;
{
	x -= y;
	while (x < 0)
		x += xres;
	return((((x>>2)*yres + y) << 2) + (x & 3));
}
