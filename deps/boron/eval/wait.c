/*
  Copyright 2010,2023,2025 Karl Robillard

  This file is part of the Boron programming language.

  Boron is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Boron is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Boron.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <poll.h>
#include <errno.h>
#define INFINITE -1
typedef int DWORD;
#endif

#include "boron.h"
#include "boron_internal.h"
#include "urlan_atoms.h"


#define MAX_PORTS   16      // LIMIT: Maximum ports wait can handle.


typedef struct
{
#ifdef _WIN32
    DWORD portCount;
    DWORD timeout;
    HANDLE handles[ MAX_PORTS ];
    UPortDevice* devices[ MAX_PORTS ];
#else
    int portCount;
    int timeout;
    struct pollfd readfds[ MAX_PORTS ];
#endif
    UIndex ports[ MAX_PORTS ];
#ifdef CONFIG_FIBRE
    UThread* fibre[ MAX_PORTS ];
#endif
}
WaitInfo;


static void _waitOnPort(UThread* ut, WaitInfo* wi, UIndex bufN)
{
    int fd;
    UBuffer* pbuf = ur_buffer(bufN);
    PORT_DEVICE(dev, pbuf);
    if( dev )
    {
        assert(wi->portCount < MAX_PORTS);
#ifdef _WIN32
        fd = dev->waitFD(pbuf, wi->handles + wi->portCount);
        if (fd > -1) {
            int i = wi->portCount++;
            wi->ports[i] = bufN;
            wi->devices[i] = dev;
#ifdef CONFIG_FIBRE
            wi->fibre[i] = ut;
#endif
        }
#else
        fd = dev->waitFD(pbuf);
        if (fd > -1) {
            int i = wi->portCount++;
            wi->ports[i] = bufN;
            wi->readfds[i].fd = fd;
            wi->readfds[i].events = POLLIN;
#ifdef CONFIG_FIBRE
            wi->fibre[i] = ut;
#endif
        }
#endif
    }
}


#ifdef CONFIG_FIBRE
#ifdef _WIN32
#include <sys/timeb.h>
#else
#include <time.h>
#endif

/**
  Get monotonic clock time.

  \return Millisecond value.
*/
int64_t boron_msec()
{
#ifdef _WIN32
    struct _timeb tb;
    _ftime(&tb);
    return (int64_t) tb.time*1000 + tb.millitm;
#elif defined(CLOCK_MONOTONIC)
    // Android, Linux, macOS 10.12
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec*1000 + ts.tv_nsec/1000000;
#else
    // Older POSIX systems.  POSIX.1-2008 marks gettimeofday() as obsolete.
    struct timeval ts;
    gettimeofday(&ts, NULL);
    return (int64_t) ts.tv_sec*1000 + ts.tv_usec/1000;
#endif
}


/*
  Throw a yield exception from inside a BoronCFunc.

  \param portsBlkN	Index of block containing ports or UR_INVALID_BUF.
  \param timeout	Milliseconds to wait, or zero to wait forever.
*/
UStatus boron_yield(UThread* ut, UCell* res, UIndex portsBlkN, int timeout)
{
    BT->yieldResult = res;
    BT->waitPorts = portsBlkN;
    BT->timeout   = (timeout > 0) ? boron_msec() + timeout : 0;
    //boron_throwWord(ut, UR_ATOM_YIELD, 0);
    return CFUNC_YIELD;
}


#define ur_bufferT(thr,n)   (thr->dataStore.ptr.buf + (n))

#if 0
#define REPORT(msg,...)       printf(msg,__VA_ARGS__)
#else
#define REPORT(msg,...)
#endif

/**
  Wait for ports and timeouts of all fibres.

  \return A thread that has finished waiting.
*/
UThread* boron_waitFibre(UThread* ut)
{
    WaitInfo wi;
    UThread* fib = ut;
    UThread* mtThread = NULL;
    int64_t sysClock;
    int64_t minTimeout = INFINITE;
    int64_t ftime;
    int isWaiting;
    UIndex waitBufN;
    DWORD n;

    REPORT("KR wait %d %p\n", ut->flags, (void*) ut);
    wi.portCount = 0;

    sysClock = boron_msec();

    do {
        ftime = ((BoronThread*) fib)->timeout;
        if (ftime) {
            if (ftime <= sysClock) {
                ((BoronThread*) fib)->timeout = 0;
                return fib;
            }
            if (minTimeout == INFINITE || ftime < minTimeout) {
                minTimeout = ftime;
                mtThread = fib;
            }
            isWaiting = 1;
        } else
            isWaiting = 0;

        waitBufN = ((BoronThread*) fib)->waitPorts;
        if (waitBufN) {
            UBuffer* buf = ur_bufferT(fib, waitBufN);
            if (buf->type == UT_PORT) {
                _waitOnPort(fib, &wi, waitBufN);
                isWaiting = 1;
            } else {
                const UCell* cell;
                UBlockIt bi;

                bi.it  = buf->ptr.cell;
                bi.end = bi.it + buf->used;
                ur_foreach(bi) {
                    if (ur_is(bi.it, UT_WORD)) {
                        cell = ur_wordCell(fib, bi.it);
                        // NOTE: Silently ignoring any word binding error here.
                        if (! cell)
                            continue;
                    } else {
                        cell = bi.it;
                    }
                    if (ur_is(cell, UT_PORT)) {
                        _waitOnPort(fib, &wi, cell->port.buf);
                        isWaiting = 1;
                    }
                }
            }
        }

        if (! isWaiting)
            return fib;

        fib = (UThread*) ((BoronThread*) fib)->nextFibre;
    } while (fib != ut);

    if (minTimeout != INFINITE)
        minTimeout -= sysClock;

#ifdef _WIN32
    n = WaitForMultipleObjects(wi.portCount, wi.handles, FALSE, minTimeout);
    if (n == WAIT_FAILED) {
        ur_error(ut, UR_ERR_INTERNAL,
                 "WaitForMultipleObjects - %d\n", GetLastError());
        return NULL;
    } else if (n != WAIT_TIMEOUT) {
        n -= WAIT_OBJECT_0;
        if (n < wi.portCount) {
            UIndex bufN = wi.ports[ n ];
            ut = wi.fibre[i];

            // Windows polling is a mess; each type of object requires
            // special handling.
            wi.devices[ n ]->waitFD(ur_buffer(bufN), NULL);

            ur_setId(res, UT_PORT);
            res->port.buf = bufN;
            return ut;
        }
    }
#else
    n = poll(wi.readfds, wi.portCount, (int) minTimeout);
    if (n == -1) {
        ur_error(ut, UR_ERR_INTERNAL, "poll - %s\n", strerror(errno));
        return NULL;
    }
    if (n) {
        int i;
        for (i = 0; i < wi.portCount; ++i) {
            if (wi.readfds[i].revents & POLLIN) {
                UCell* res;
                ut = wi.fibre[i];
                REPORT("KR port-ready %d %p\n", ut->flags, (void*) ut);
                res = BT->yieldResult;
                ur_setId(res, UT_PORT);
                res->port.buf = wi.ports[i];
                return ut;
            }
        }
    }
#endif

    if (mtThread) {
        ut = mtThread;
        BT->timeout = 0;
        // Result is set prior to when boron_yield gets called.
        REPORT("KR timeout %d %p\n", ut->flags, (void*) ut);
    }
    return ut;
}

#else

static int _fillWaitInfo(UThread* ut, WaitInfo* wi,
                         const UCell* it, const UCell* end)
{
    while (it != end) {
        if (ur_is(it, UT_INT))
        {
            wi->timeout = ur_int(it) * 1000;
        }
        else if(ur_is(it, UT_WORD))
        {
            const UCell* cell;
            if (! (cell = ur_wordCell(ut, it)))
                return UR_THROW;
            if( ur_is(cell, UT_PORT) )
                _waitOnPort(ut, wi, cell->port.buf);
        }
        else if(ur_is(it, UT_PORT))
        {
            _waitOnPort(ut, wi, it->port.buf);
        }
        else if(ur_is(it, UT_DOUBLE) || ur_is(it, UT_TIME))
        {
            wi->timeout = (DWORD) (ur_double(it) * 1000.0);
        }

        ++it;
    }
    return UR_OK;
}
#endif


extern void ur_sleep(int);

/*-cf-
    sleep
        time    int!/double!/time!
    return: unset!
    Delay for a number of seconds.
*/
CFUNC_PUB( cfunc_sleep )
{
    int msec;
#ifndef CONFIG_FIBRE
    (void) ut;
#endif

    if (ur_is(a1, UT_INT))
        msec = (int) (ur_int(a1) * 1000);
    else
        msec = (int) (ur_double(a1) * 1000.0);

    ur_setId(res, UT_UNSET);

    if (msec > 0) {
#ifdef CONFIG_FIBRE
        if (BT->nextFibre != BT)
            return boron_yield(ut, res, UR_INVALID_BUF, msec);
#endif
        ur_sleep(msec);
    }
    return UR_OK;
}


/*-cf-
    wait
        target  int!/double!/time!/block!/port!
    return: Port ready for reading or none.
    group: io

    Wait for data on ports.
*/
// (target -- port)
CFUNC_PUB( cfunc_wait )
{
#ifdef CONFIG_FIBRE
    UIndex bufN = UR_INVALID_BUF;
    int timeout = 0;
    int type = ur_type(a1);

    if (type == UT_DOUBLE)
        timeout = (int) (ur_double(a1) * 1000.0);
    else if (type == UT_INT)
        timeout = ur_int(a1) * 1000;
    else if (type == UT_PORT)
        bufN = a1->port.buf;
    else if (type == UT_BLOCK) {
        UBlockIt bi;
        bufN = a1->series.buf;
        ur_blockIt(ut, &bi, a1);
        ur_foreach(bi) {
            if (ur_is(bi.it, UT_DOUBLE))
                timeout = (int) (ur_double(bi.it) * 1000.0);
            else if (ur_is(bi.it, UT_INT))
                timeout = ur_int(bi.it) * 1000;
        }
    }

    ur_setId(res, UT_NONE);
    return boron_yield(ut, res, bufN, timeout);
#else
    WaitInfo wi;
    DWORD n;

    wi.timeout = INFINITE;
    wi.portCount = 0;

    if (ur_is(a1, UT_BLOCK))
    {
        UBlockIt bi;
        ur_blockIt(ut, &bi, a1);
        if (! _fillWaitInfo(ut, &wi, bi.it, bi.end))
            return UR_THROW;
    }
    else
    {
        if (! _fillWaitInfo(ut, &wi, a1, a1 + 1))
            return UR_THROW;
    }

#ifdef _WIN32
    n = WaitForMultipleObjects(wi.portCount, wi.handles, FALSE, wi.timeout);
    if (n == WAIT_FAILED)
    {
        return ur_error(ut, UR_ERR_INTERNAL,
                        "WaitForMultipleObjects - %d\n", GetLastError());
    }
    else if (n != WAIT_TIMEOUT)
    {
        n -= WAIT_OBJECT_0;
        if (n < wi.portCount) {
            UIndex bufN = wi.ports[ n ];

            // Windows polling is a mess; each type of object requires
            // special handling.
            wi.devices[ n ]->waitFD(ur_buffer(bufN), NULL);

            ur_setId(res, UT_PORT);
            res->port.buf = bufN;
            return UR_OK;
        }
    }
#else
    n = poll(wi.readfds, wi.portCount, wi.timeout);
    if (n == -1)
        return ur_error(ut, UR_ERR_INTERNAL, "poll - %s\n", strerror(errno));
    if (n) {
        int i;
        for (i = 0; i < wi.portCount; ++i) {
            if (wi.readfds[i].revents & POLLIN) {
                ur_setId(res, UT_PORT);
                res->port.buf = wi.ports[i];
                return UR_OK;
            }
        }
    }
#endif

    ur_setId(res, UT_NONE);
    return UR_OK;
#endif
}
