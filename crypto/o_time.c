/*
 * Copyright 2001-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/e_os2.h>
#include <string.h>
#include <openssl/crypto.h>

#ifdef OPENSSL_SYS_VMS
# if __CRTL_VER >= 70000000 && \
     (defined _POSIX_C_SOURCE || !defined _ANSI_C_SOURCE)
#  define VMS_GMTIME_OK
# endif
# ifndef VMS_GMTIME_OK
#  include <libdtdef.h>
#  include <lib$routines.h>
#  include <lnmdef.h>
#  include <starlet.h>
#  include <descrip.h>
#  include <stdlib.h>
# endif                         /* ndef VMS_GMTIME_OK */


/*
 * Needed to pick up the correct definitions and declarations in some of the
 * DEC C Header Files (*.H).
 */
# define __NEW_STARLET 1

# if (defined(__alpha) || defined(__ia64))
#  include <iledef.h>
# else

/* VAX */
typedef struct _ile3 {          /* Copied from ILEDEF.H for Alpha   */
#  pragma __nomember_alignment
    unsigned short int ile3$w_length;        /* Length of buffer in bytes */
    unsigned short int ile3$w_code;          /* Item code value */
    void *ile3$ps_bufaddr;                   /* Buffer address */
    unsigned short int *ile3$ps_retlen_addr; /* Address of word for returned length */
} ILE3;
# endif   /* alpha || ia64    */
#endif    /* OPENSSL_SYS_VMS  */

struct tm *OPENSSL_gmtime(const time_t *timer, struct tm *result)
{
    struct tm *ts = NULL;

#if defined(OPENSSL_THREADS) && !defined(OPENSSL_SYS_WIN32) && (!defined(OPENSSL_SYS_VMS) || defined(gmtime_r)) && !defined(OPENSSL_SYS_MACOSX)
    /*
     * should return &data, but doesn't on some systems, so we don't even
     * look at the return value
     */
    if (gmtime_r(timer, result) == NULL)
        return NULL;
    ts = result;
#elif !defined(OPENSSL_SYS_VMS) || defined(VMS_GMTIME_OK)
    ts = gmtime(timer);
    if (ts == NULL)
        return NULL;

    memcpy(result, ts, sizeof(struct tm));
    ts = result;
#endif
#if defined( OPENSSL_SYS_VMS) && !defined( VMS_GMTIME_OK)
    if (ts == NULL) {
        static $DESCRIPTOR(tabnam, "LNM$DCL_LOGICAL");
        static $DESCRIPTOR(lognam, "SYS$TIMEZONE_DIFFERENTIAL");
        char logvalue[256];
        unsigned int reslen = 0;
# if __INITIAL_POINTER_SIZE == 64
        ILEB_64 itemlist[2], *pitem;
# else
        ILE3 itemlist[2], *pitem;
# endif
        int status;
        time_t t;


        /*
         * Setup an itemlist for the call to $TRNLNM - Translate Logical Name.
         */
        pitem = itemlist;

# if __INITIAL_POINTER_SIZE == 64
        pitem->ileb_64$w_mbo = 1;
        pitem->ileb_64$w_code = LNM$_STRING;
        pitem->ileb_64$l_mbmo = -1;
        pitem->ileb_64$q_length = sizeof (logvalue);
        pitem->ileb_64$pq_bufaddr = logvalue;
        pitem->ileb_64$pq_retlen_addr = (unsigned __int64 *) &reslen;
        pitem++;
        /* Last item of the item list is null terminated */
        pitem->ileb_64$q_length = pitem->ileb_64$w_code = 0;
# else
        pitem->ile3$w_length = sizeof (logvalue);
        pitem->ile3$w_code = LNM$_STRING;
        pitem->ile3$ps_bufaddr = logvalue;
        pitem->ile3$ps_retlen_addr = (unsigned short int *) &reslen;
        pitem++;
        /* Last item of the item list is null terminated */
        pitem->ile3$w_length = pitem->ile3$w_code = 0;
# endif


        /* Get the value for SYS$TIMEZONE_DIFFERENTIAL */
        status = sys$trnlnm(0, &tabnam, &lognam, 0, itemlist);
        if (!(status & 1))
            return NULL;
        logvalue[reslen] = '\0';

        t = *timer;

        /* The following is extracted from the DEC C header time.h */
        /*
         **  Beginning in OpenVMS Version 7.0 mktime, time, ctime, strftime
         **  have two implementations.  One implementation is provided
         **  for compatibility and deals with time in terms of local time,
         **  the other __utc_* deals with time in terms of UTC.
         */
        /*
         * We use the same conditions as in said time.h to check if we should
         * assume that t contains local time (and should therefore be
         * adjusted) or UTC (and should therefore be left untouched).
         */
# if __CRTL_VER < 70000000 || defined _VMS_V6_SOURCE
        /* Get the numerical value of the equivalence string */
        status = atoi(logvalue);

        /* and use it to move time to GMT */
        t -= status;
# endif

        /* then convert the result to the time structure */

        /*
         * Since there was no gmtime_r() to do this stuff for us, we have to
         * do it the hard way.
         */
        {
            /*-
             * The VMS epoch is the astronomical Smithsonian date,
               if I remember correctly, which is November 17, 1858.
               Furthermore, time is measure in tenths of microseconds
               and stored in quadwords (64 bit integers).  unix_epoch
               below is January 1st 1970 expressed as a VMS time.  The
               following code was used to get this number:

               #include <stdio.h>
               #include <stdlib.h>
               #include <lib$routines.h>
               #include <starlet.h>

               main()
               {
                 unsigned long systime[2];
                 unsigned short epoch_values[7] =
                   { 1970, 1, 1, 0, 0, 0, 0 };

                 lib$cvt_vectim(epoch_values, systime);

                 printf("%u %u", systime[0], systime[1]);
               }
            */
            unsigned long unix_epoch[2] = { 1273708544, 8164711 };
            unsigned long deltatime[2];
            unsigned long systime[2];
            struct vms_vectime {
                short year, month, day, hour, minute, second, centi_second;
            } time_values;
            long operation;

            /*
             * Turn the number of seconds since January 1st 1970 to an
             * internal delta time. Note that lib$cvt_to_internal_time() will
             * assume that t is signed, and will therefore break on 32-bit
             * systems some time in 2038.
             */
            operation = LIB$K_DELTA_SECONDS;
            status = lib$cvt_to_internal_time(&operation, &t, deltatime);

            /*
             * Add the delta time with the Unix epoch and we have the current
             * UTC time in internal format
             */
            status = lib$add_times(unix_epoch, deltatime, systime);

            /* Turn the internal time into a time vector */
            status = sys$numtim(&time_values, systime);

            /* Fill in the struct tm with the result */
            result->tm_sec = time_values.second;
            result->tm_min = time_values.minute;
            result->tm_hour = time_values.hour;
            result->tm_mday = time_values.day;
            result->tm_mon = time_values.month - 1;
            result->tm_year = time_values.year - 1900;

            operation = LIB$K_DAY_OF_WEEK;
            status = lib$cvt_from_internal_time(&operation,
                                                &result->tm_wday, systime);
            result->tm_wday %= 7;

            operation = LIB$K_DAY_OF_YEAR;
            status = lib$cvt_from_internal_time(&operation,
                                                &result->tm_yday, systime);
            result->tm_yday--;

            result->tm_isdst = 0; /* There's no way to know... */

            ts = result;
        }
    }
#endif
    return ts;
}

/*
 * Take a tm structure and add an offset to it. This avoids any OS issues
 * with restricted date types and overflows which cause the year 2038
 * problem.
 */

#define SECS_PER_DAY (24 * 60 * 60)

static long date_to_julian(int y, int m, int d);
static void julian_to_date(long jd, int *y, int *m, int *d);
static int julian_adj(const struct tm *tm, int off_day, long offset_sec,
                      long *pday, int *psec);

int OPENSSL_gmtime_adj(struct tm *tm, int off_day, long offset_sec)
{
    int time_sec, time_year, time_month, time_day;
    long time_jd;

    /* Convert time and offset into Julian day and seconds */
    if (!julian_adj(tm, off_day, offset_sec, &time_jd, &time_sec))
        return 0;

    /* Convert Julian day back to date */

    julian_to_date(time_jd, &time_year, &time_month, &time_day);

    if (time_year < 1900 || time_year > 9999)
        return 0;

    /* Update tm structure */

    tm->tm_year = time_year - 1900;
    tm->tm_mon = time_month - 1;
    tm->tm_mday = time_day;

    tm->tm_hour = time_sec / 3600;
    tm->tm_min = (time_sec / 60) % 60;
    tm->tm_sec = time_sec % 60;

    return 1;

}

int OPENSSL_gmtime_diff(int *pday, int *psec,
                        const struct tm *from, const struct tm *to)
{
    int from_sec, to_sec, diff_sec;
    long from_jd, to_jd, diff_day;
    if (!julian_adj(from, 0, 0, &from_jd, &from_sec))
        return 0;
    if (!julian_adj(to, 0, 0, &to_jd, &to_sec))
        return 0;
    diff_day = to_jd - from_jd;
    diff_sec = to_sec - from_sec;
    /* Adjust differences so both positive or both negative */
    if (diff_day > 0 && diff_sec < 0) {
        diff_day--;
        diff_sec += SECS_PER_DAY;
    }
    if (diff_day < 0 && diff_sec > 0) {
        diff_day++;
        diff_sec -= SECS_PER_DAY;
    }

    if (pday)
        *pday = (int)diff_day;
    if (psec)
        *psec = diff_sec;

    return 1;

}

/* Convert tm structure and offset into julian day and seconds */
static int julian_adj(const struct tm *tm, int off_day, long offset_sec,
                      long *pday, int *psec)
{
    int offset_hms, offset_day;
    long time_jd;
    int time_year, time_month, time_day;
    /* split offset into days and day seconds */
    offset_day = offset_sec / SECS_PER_DAY;
    /* Avoid sign issues with % operator */
    offset_hms = offset_sec - (offset_day * SECS_PER_DAY);
    offset_day += off_day;
    /* Add current time seconds to offset */
    offset_hms += tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    /* Adjust day seconds if overflow */
    if (offset_hms >= SECS_PER_DAY) {
        offset_day++;
        offset_hms -= SECS_PER_DAY;
    } else if (offset_hms < 0) {
        offset_day--;
        offset_hms += SECS_PER_DAY;
    }

    /*
     * Convert date of time structure into a Julian day number.
     */

    time_year = tm->tm_year + 1900;
    time_month = tm->tm_mon + 1;
    time_day = tm->tm_mday;

    time_jd = date_to_julian(time_year, time_month, time_day);

    /* Work out Julian day of new date */
    time_jd += offset_day;

    if (time_jd < 0)
        return 0;

    *pday = time_jd;
    *psec = offset_hms;
    return 1;
}

/*
 * Convert date to and from julian day Uses Fliegel & Van Flandern algorithm
 */
static long date_to_julian(int y, int m, int d)
{
    return (1461 * (y + 4800 + (m - 14) / 12)) / 4 +
        (367 * (m - 2 - 12 * ((m - 14) / 12))) / 12 -
        (3 * ((y + 4900 + (m - 14) / 12) / 100)) / 4 + d - 32075;
}

static void julian_to_date(long jd, int *y, int *m, int *d)
{
    long L = jd + 68569;
    long n = (4 * L) / 146097;
    long i, j;

    L = L - (146097 * n + 3) / 4;
    i = (4000 * (L + 1)) / 1461001;
    L = L - (1461 * i) / 4 + 31;
    j = (80 * L) / 2447;
    *d = L - (2447 * j) / 80;
    L = j / 11;
    *m = j + 2 - (12 * L);
    *y = 100 * (n - 49) + i + L;
}
