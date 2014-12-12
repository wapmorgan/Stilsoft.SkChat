#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int     opterr = 1,             /* if error message should be printed */
        optind = 1,             /* index into parent argv vector */
        optopt,                 /* character checked for validity */
        optreset;               /* reset getopt */
extern _TCHAR  *optarg;                /* argument associated with option */
extern _TCHAR *__progname;

#define BADCH   (int)'?'
#define BADARG  (int)':'
#define EMSG    _T("")

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int
getopt(
        int nargc,
        _TCHAR * const *nargv,
        const _TCHAR* ostr)
{
        
        static _TCHAR *place = EMSG;              /* option letter processing */
        _TCHAR *oli;                              /* option letter list index */

        if (optreset || !*place) {              /* update scanning pointer */
                optreset = 0;
                if (optind >= nargc || *(place = nargv[optind]) != '-') {
                        place = EMSG;
                        return (EOF);
                }
                if (place[1] && *++place == '-') {      /* found "--" */
                        ++optind;
                        place = EMSG;
                        return (EOF);
                }
        }                                       /* option letter okay? */
        if ((optopt = (int)*place++) == (int)':' ||
	            !(oli = (TCHAR*)_tcschr(ostr, optopt))) {
                /*
                 * if the user didn't specify '-' as an option,
                 * assume it means EOF.
                 */
                if (optopt == (int)'-')
                        return (EOF);
                if (!*place)
                        ++optind;
                if (opterr && *ostr != ':')
                        (void)_ftprintf(stderr,
                          _T("%s: illegal option -- %c\n"), __progname, optopt);
                return (BADCH);
        }
        if (*++oli != ':') {                    /* don't need argument */
                optarg = NULL;
                if (!*place)
                        ++optind;
        }
        else {                                  /* need an argument */
                if (*place)                     /* no white space */
                        optarg = place;
                else if (nargc <= ++optind) {   /* no arg */
                        place = EMSG;
                        if (*ostr == ':')
                                return (BADARG);
                        if (opterr)
                                (void)_ftprintf(stderr,
                                  _T("%s: option requires an argument -- %c\n"),
                                  __progname, optopt);
                        return (BADCH);
                }
                else                            /* white space */
                        optarg = nargv[optind];
                place = EMSG;
                ++optind;
        }
        return (optopt);                        /* dump back option letter */
}
