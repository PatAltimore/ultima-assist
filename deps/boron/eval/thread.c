/* Boron Thread */


#include <math.h>
#include <time.h>

extern void boron_installThreadPort(UThread*, const UCell*, UThread*);
extern void boron_setJoinThread(UThread*, const UCell*, OSThread);


/*
    Make new thread and copy task code (as UTF-8 string) to temporary binary.

    \param flags    UThread::flags
    \param fname    Calling function name.
    \param portC    Create a thread port! if this is not NULL.

    \return NULL if error thrown.
*/
static UThread* _makeTaskThread(UThread* ut, uint32_t flags, const char* fname,
                                const UCell* body, UCell* portC)
{
    UBuffer code;
    UThread* child;

    ur_strInit(&code, UR_ENC_UTF8, 0);

    if (ur_is(body, UT_STRING)) {
        USeriesIter si;
        ur_seriesSlice(ut, &si, body);
        ur_strAppend(&code, si.buf, si.it, si.end);
    } else if (ur_is(body, UT_BLOCK)) {
        ur_toStr(ut, body, &code, -1);
    } else {
        ur_error(ut, UR_ERR_TYPE, "%s expected string!/block!", fname);
        return NULL;
    }

    child = ur_makeThread(ut, flags);
    if (! child) {
        ur_strFree(&code);
        ur_error(ut, UR_ERR_INTERNAL, "No memory for thread");
        return NULL;
    }

    // Copy code to child temp. binary.
    {
    UBuffer* bin = &((BoronThread*) child)->tbin;
    bin->used = 0;
    ur_binAppendData(bin, code.ptr.b, code.used);
    ur_strFree(&code);
    }

    if (portC) {
        if (! port_thread.open(ut, &port_thread, NULL, flags, portC))
            return NULL;
        boron_installThreadPort(ut, portC, child);
    }

    return child;
}


#ifdef _WIN32
static DWORD WINAPI threadRoutine(LPVOID arg)
#else
static void* threadRoutine(void* arg)
#endif
{
    UThread* ut = (UThread*) arg;
    UBuffer* bin = &BT->tbin;
    if (! boron_evalUtf8(ut, bin->ptr.c, bin->used)) {
        UBuffer str;
        UCell* ex = ur_exception(ut);
        if (! ur_is(ex, UT_WORD) || ur_atom(ex) != UR_ATOM_QUIT) {
            ur_strInit(&str, UR_ENC_UTF8, 0);
            ur_toText(ut, ex, &str);
            ur_strTermNull(&str);
            puts(str.ptr.c);
            ur_strFree(&str);
        }
    }
    ur_destroyThread(ut);
    return 0;
}


/*-cf-
    thread
        routine string!/block!
        /port   Create thread port.
    return: Thread port or unset!
    see: fibre

    Create a new thread to run a routine.

    A string! routine argument is preferred as a block! will be converted to
    a string! before it can be passed to the thread.

    If the /port option is used the new thread will have a port! bound to the
    'thread-port word that is connected to the one returned from the function.
    This can be used for bi-directional communication.

    Each thread has it's own data store, so series values passed through the
    port will become empty on write as ownership is transferred.
    Only one series can be sent through the port on each write, so writing a
    block! that contains series values will throw an error.
    Words not bound to the shared environment will become unbound in the
    reading thread.
*/
CFUNC( cfunc_thread )
{
#define OPT_THREAD_PORT 0x01
    OSThread osThr;
    UThread* child;
    int makePort = CFUNC_OPTIONS & OPT_THREAD_PORT;
#ifdef _WIN32
    DWORD winId;
#endif

    ur_setId(res, UT_UNSET);

    child = _makeTaskThread(ut, 0, "thread", a1, makePort ? res : NULL);
    if (! child)
        return UR_THROW;

#ifdef _WIN32
    osThr = CreateThread(NULL, 0, threadRoutine, child, 0, &winId);
    if (osThr == NULL)
#else
    if (pthread_create(&osThr, 0, threadRoutine, child) != 0)
#endif
    {
        return ur_error(ut, UR_ERR_INTERNAL, "Could not create thread");
    }

    if (makePort) {
        // Will pthread_join() when port closed.
        boron_setJoinThread(ut, res, osThr);
    }
#ifndef _WIN32
    else {
        // Detach to automatically release thread memory when it exits.
        pthread_detach(osThr);
    }
#endif

    return UR_OK;
}


#ifdef CONFIG_FIBRE
static UStatus _bootFibre(UThread* ut, const char* script, int len)
{
    UCell* res = ur_stackTop(ut);
    UIndex bufN = ur_tokenize(ut, script, script + len, res);   // gc!
    if (! bufN)
        return UR_THROW;

    boron_bindDefault(ut, bufN);
    boron_startFibre(ut, bufN, ur_push(ut, UT_UNSET));
    return UR_OK;
}


/*-cf-
    fibre
        data        If not none! then data is sent to the fibre thread-port.
        routine string!/block!
    return: Thread port!
    see: thread

    Create a new fibre to run a routine.
*/
CFUNC( cfunc_fibre )
{
    BoronThread* child;
    UStatus ok;

    ur_setId(res, UT_UNSET);

    child = (BoronThread*) _makeTaskThread(ut, UR_TFLAG_FIBRE, "fibre",
                                           a1+1, res);
    if (! child)
        return UR_THROW;

    // Prepare child for evaluation.
    ok = _bootFibre(&child->thread, child->tbin.ptr.c, child->tbin.used);
    if (ok == UR_OK) {
        // Link to fibre list.
        child->nextFibre = BT->nextFibre;
        BT->nextFibre = child;

        // Send initial data to child thread-port.
        if (! ur_is(a1, UT_NONE))
            return port_thread.write(ut, ur_buffer(res->port.buf), a1);
    }
    else
    {
        // TODO: Copy tokenize error! from child.
        ur_error(ut, UR_ERR_SYNTAX, "Invalid fibre routine");
        ur_destroyThread(&child->thread);
    }
    return ok;
}
#endif
