/* fd_read_edge_probe.c -- standalone WinSock probe: is WSAEventSelect's
 * FD_READ edge-triggered or level-triggered on Windows?
 *
 * WHY THIS FILE EXISTS
 *   The repo's Windows socket backend
 *   (Drive/windows/Device/Network/XDeviceNetwork_win32_select.c)
 *   associates ONE event object per socket with WSAEventSelect exactly
 *   once, then every poll round does
 *       WSAWaitForMultipleEvents(..., 0, ...)  +  WSAEnumNetworkEvents(...)
 *   and caches the returned bits.  The semantics of the FD_READ bit
 *   ("does AFD report it again while unread data is still queued?") are
 *   recorded there as an UNVERIFIED ASSUMPTION.  This probe measures the
 *   real behaviour on this machine with plain WinSock only (no framework,
 *   no repo headers) so the answer can be re-checked independently.
 *
 * WHAT IS MEASURED (3 cases + 1 control, each run in 2 variants)
 *   A: server pushes 1 frame, client NEVER reads it.
 *      -> do rounds 2..N still report FD_READ?
 *   B: server pushes 1 frame, client reads exactly that frame in round 1
 *      (kernel receive buffer is empty afterwards, no new data arrives).
 *      -> do rounds 2..N still report FD_READ?
 *   C: server pushes 2 frames, client reads exactly frame 1 in round 1
 *      (frame 2 is still queued in the kernel).
 *      -> do rounds 2..N still report FD_READ?
 *   D: control -- server never sends anything.
 *      -> any FD_READ here would be a false positive and would invalidate
 *         the whole run.
 *
 * THE ONLY KNOBS THAT SEPARATE THE CASES
 *   (a) how many frames the server pushes (1 / 1 / 2 / 0), and
 *   (b) how many bytes the client reads in round 1 (0 / 16 / 16 / 0).
 *   Everything else (rounds, waits, masks, variants) is identical.
 *
 * WHY TWO VARIANTS (read-before-enum vs enum-before-read)
 *   WSAEnumNetworkEvents resets the event object, and AFD then re-arms its
 *   poll.  WHEN the read happens relative to the Enum therefore decides
 *   whether the reported FD_READ bit describes the buffer state BEFORE or
 *   AFTER the read.  The repo's current order is Enum-then-read; the
 *   "pure" semantics are observed with read-then-enum (the buffer state
 *   after the read is what gets reported).  Measuring both makes the
 *   difference visible instead of hiding it.
 *
 * METHOD NOTES (each step says what it is there to prove)
 *   - The "settle" phase waits with ioctlsocket(FIONREAD) until the
 *     expected bytes are really in the kernel receive buffer.  FIONREAD is
 *     a pure query: it neither consumes the FD_READ state nor touches the
 *     event object, so it can be used to establish the precondition
 *     without disturbing the measurement.
 *   - Each measurement round first does the zero-timeout wait (exactly
 *     what the repo's poll loop does).  If that times out, a bounded wait
 *     (default 100 ms) is performed so that an event which AFD re-arms a
 *     moment later is not mistaken for "no event".  Both stages are
 *     reported separately (IMMED / WAIT / NONE) so the reader can tell
 *     "already pending" from "arrived during the round".
 *   - There is NO sleep between rounds: the only waits are inside
 *     WSAWaitForMultipleEvents.
 *   - Every round also records FIONREAD before/after its read, so
 *     "FD_READ reported" can be correlated with "bytes actually pending".
 *
 * BUILD (any MSVC x64 developer prompt):
 *   cl /nologo /W3 /O2 /MD /utf-8 /Fe:fd_read_edge_probe.exe ^
 *      fd_read_edge_probe.c ws2_32.lib
 * RUN:
 *   fd_read_edge_probe.exe [rounds] [boundedWaitMs]     (defaults 8 / 100)
 * EXIT CODE
 *   0 = measurement completed (read the printed table)
 *   2 = setup failed (socket / event / connect error)
 *
 * NOTE: this program prints the conclusion it DERIVED from the measurement.
 * It does not assume one.
 */
#define _CRT_SECURE_NO_WARNINGS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")

#define FRAME_BYTES       16        /* payload size of one "frame"        */
#define DEFAULT_ROUNDS    8         /* measurement rounds per case        */
#define DEFAULT_WAIT_MS   100       /* bounded wait when Wait(0) timed out*/
#define SETTLE_TIMEOUT_MS 3000      /* max time to wait for the payload  */
#define MAX_ROUNDS        64        /* upper bound on the round count     */
#define VARIANT_COUNT     2

static int g_rounds = DEFAULT_ROUNDS;
static int g_waitMs  = DEFAULT_WAIT_MS;

/* ------------------------------------------------------------------------ */
/* small string helpers                                                      */
/* ------------------------------------------------------------------------ */
static void appendStr(char* buf, size_t cap, const char* s)
{
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    strncat(buf, s, cap - len - 1);
}

static void appendMask(char* buf, size_t cap, long m)
{
    struct { long bit; const char* name; } tbl[] = {
        { FD_READ,   "FD_READ"   },
        { FD_WRITE,  "FD_WRITE"  },
        { FD_OOB,    "FD_OOB"    },
        { FD_ACCEPT, "FD_ACCEPT" },
        { FD_CONNECT,"FD_CONNECT"},
        { FD_CLOSE,  "FD_CLOSE"  }
    };
    size_t i;
    for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (m & tbl[i].bit) {
            if (buf[0]) appendStr(buf, cap, "|");
            appendStr(buf, cap, tbl[i].name);
        }
    }
    if (!buf[0]) appendStr(buf, cap, "-");
}

/* ------------------------------------------------------------------------ */
/* socket pair + event object                                               */
/* ------------------------------------------------------------------------ */
typedef struct {
    SOCKET  listenSock;
    SOCKET  serverSock;   /* accepted side, stays BLOCKING on purpose    */
    SOCKET  clientSock;   /* the socket under test (non-blocking after
                           * WSAEventSelect)                            */
    WSAEVENT event;
    unsigned short port;
} Pair;

static void closePair(Pair* pr)
{
    if (!pr) return;
    if (pr->clientSock != INVALID_SOCKET) { closesocket(pr->clientSock); pr->clientSock = INVALID_SOCKET; }
    if (pr->serverSock != INVALID_SOCKET) { closesocket(pr->serverSock); pr->serverSock = INVALID_SOCKET; }
    if (pr->listenSock != INVALID_SOCKET) { closesocket(pr->listenSock); pr->listenSock = INVALID_SOCKET; }
    if (pr->event != WSA_INVALID_EVENT)    { WSACloseEvent(pr->event);   pr->event = WSA_INVALID_EVENT; }
}

/* FIONREAD: how many bytes the kernel still holds for us.
 * Returns -1 on error. Pure query: does not consume the FD_READ state. */
static int pendingBytes(SOCKET s)
{
    u_long n = 0;
    if (ioctlsocket(s, FIONREAD, &n) == SOCKET_ERROR) return -1;
    return (int)n;
}

/* Bounded copy into a fixed buffer (keeps the error strings ASCII and short). */
static void setErr(char* err, size_t cap, const char* msg)
{
    if (!err || cap == 0) return;
    strncpy(err, msg, cap - 1);
    err[cap - 1] = '\0';
}

/* Create a loopback TCP pair: client connected, server accepted.
 * Returns 0 on success, -1 on failure (err filled with an ASCII reason). */
static int setupPair(Pair* pr, char* err, size_t errLen)
{
    struct sockaddr_in sa;
    int addrLen = sizeof(sa);
    SOCKET ls = INVALID_SOCKET, ss = INVALID_SOCKET, cs = INVALID_SOCKET;
    WSAEVENT ev = WSA_INVALID_EVENT;

    memset(pr, 0, sizeof(*pr));
    pr->listenSock = INVALID_SOCKET;
    pr->serverSock = INVALID_SOCKET;
    pr->clientSock = INVALID_SOCKET;
    pr->event      = WSA_INVALID_EVENT;

    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { setErr(err, errLen, "socket(listen) failed"); goto fail; }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;                       /* ephemeral port */
    if (bind(ls, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        setErr(err, errLen, "bind failed"); goto fail;
    }
    if (listen(ls, 1) == SOCKET_ERROR) { setErr(err, errLen, "listen failed"); goto fail; }
    addrLen = sizeof(sa);
    if (getsockname(ls, (struct sockaddr*)&sa, &addrLen) == SOCKET_ERROR) {
        setErr(err, errLen, "getsockname failed"); goto fail;
    }

    cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cs == INVALID_SOCKET) { setErr(err, errLen, "socket(client) failed"); goto fail; }
    /* Blocking connect: the backend under test also associates the event
     * object only after the connection is up (xw32_probeArm). */
    if (connect(cs, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        setErr(err, errLen, "connect failed"); goto fail;
    }
    ss = accept(ls, NULL, NULL);
    if (ss == INVALID_SOCKET) { setErr(err, errLen, "accept failed"); goto fail; }

    ev = WSACreateEvent();
    if (ev == WSA_INVALID_EVENT) { setErr(err, errLen, "WSACreateEvent failed"); goto fail; }

    pr->listenSock = ls; pr->serverSock = ss; pr->clientSock = cs;
    pr->event = ev; pr->port = ntohs(sa.sin_port);
    return 0;

fail:
    if (cs != INVALID_SOCKET) closesocket(cs);
    if (ss != INVALID_SOCKET) closesocket(ss);
    if (ls != INVALID_SOCKET) closesocket(ls);
    if (ev != WSA_INVALID_EVENT) WSACloseEvent(ev);
    return -1;
}

/* Associate the event object with the client socket.
 * The full mask is FD_READ|FD_CLOSE|FD_WRITE|FD_CONNECT|FD_ACCEPT|FD_OOB.
 * Some Winsock builds reject FD_ACCEPT (and possibly FD_CONNECT) on a
 * non-listening socket, so we fall back through a ladder of masks and
 * REPORT which mask was actually accepted -- a silently reduced mask would
 * make the whole measurement untrustworthy.
 * Returns 0 on success; -1 on failure. */
static int armEventSelect(SOCKET s, WSAEVENT ev, long* outMask,
                           char* note, size_t noteLen)
{
    static const long masks[] = {
        FD_READ | FD_CLOSE | FD_WRITE | FD_CONNECT | FD_ACCEPT | FD_OOB,
        FD_READ | FD_CLOSE | FD_WRITE | FD_CONNECT | FD_OOB,
        FD_READ | FD_CLOSE | FD_WRITE | FD_OOB,
        FD_READ | FD_CLOSE | FD_OOB
    };
    size_t i;
    note[0] = '\0';
    for (i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        if (WSAEventSelect(s, ev, masks[i]) != SOCKET_ERROR) {
            *outMask = masks[i];
            if (i != 0) {
                char tmp[160];
                appendStr(tmp, sizeof(tmp), "note: full mask rejected, used ");
                appendMask(tmp, sizeof(tmp), masks[i]);
                strncpy(note, tmp, noteLen - 1);
                note[noteLen - 1] = '\0';
            }
            return 0;
        }
    }
    {
        int e = WSAGetLastError();
        char tmp[128];
        sprintf(tmp, "WSAEventSelect failed (WSA error %d)", e);
        strncpy(note, tmp, noteLen - 1);
        note[noteLen - 1] = '\0';
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* one measurement round                                                     */
/* ------------------------------------------------------------------------ */
typedef struct {
    int  immediate;    /* Wait(0) returned the event object        */
    int  waited;       /* bounded wait returned the event object   */
    int  timedOut;     /* neither wait returned the event object   */
    int  waitFailed;   /* WSA_WAIT_FAILED                          */
    int  enumerated;   /* WSAEnumNetworkEvents actually ran        */
    long events;       /* ne.lNetworkEvents bits                   */
    int  enumErr;      /* WSAGetLastError of a failed Enum         */
    int  errCode0;     /* ne.iErrorCode[0] (FD_ERROR)              */
    int  errCode1;     /* ne.iErrorCode[1] (FD_CONNECT)            */
    int  pendBefore;   /* FIONREAD before the read                 */
    int  pendAfter;    /* FIONREAD after the read                  */
    int  bytesRead;    /* bytes actually read this round           */
    int  readErr;      /* WSAGetLastError of the read              */
    int  peerClosed;   /* recv() returned 0 (FIN seen)              */
} RoundRec;

/* The read budget: the ONLY behavioural knob that differs between cases. */
static void doRead(SOCKET s, int budget, RoundRec* o)
{
    char buf[256];
    int total = 0;

    if (budget <= 0) return;
    while (total < budget) {
        int n = recv(s, buf, budget - total, 0);
        if (n > 0) { total += n; continue; }
        if (n == 0) { o->peerClosed = 1; break; }
        o->readErr = WSAGetLastError();
        break;
    }
    o->bytesRead = total;
}

/* readFirst != 0 : wait -> read -> Enum   ("read then enum")
 * readFirst == 0 : wait -> Enum -> read   ("enum then read", repo order) */
static void runRound(SOCKET s, WSAEVENT ev, int readBudget, int readFirst,
                     RoundRec* o)
{
    WSANETWORKEVENTS ne;
    DWORD w;

    memset(o, 0, sizeof(*o));
    o->pendBefore = pendingBytes(s);

    /* Stage 1 -- zero timeout wait: this is exactly what the repo's poll
     * loop does every round. */
    w = WSAWaitForMultipleEvents(1, &ev, FALSE, 0, FALSE);
    if (w == WAIT_OBJECT_0) {
        o->immediate = 1;
    } else if (w == WSA_WAIT_FAILED) {
        o->waitFailed = 1;
    } else {
        /* Stage 2 -- bounded wait.  If AFD re-arms the condition it may
         * need a moment to signal again; without this stage a re-armed
         * event could be mistaken for "nothing happened". */
        w = WSAWaitForMultipleEvents(1, &ev, FALSE, (DWORD)g_waitMs, FALSE);
        if (w == WAIT_OBJECT_0)       o->waited = 1;
        else if (w == WSA_WAIT_FAILED)     o->waitFailed = 1;
        else                               o->timedOut = 1;
    }

    if (readFirst) doRead(s, readBudget, o);

    if (o->immediate || o->waited) {
        if (WSAEnumNetworkEvents(s, ev, &ne) == SOCKET_ERROR) {
            o->enumErr = WSAGetLastError();
        } else {
            o->enumerated = 1;
            o->events  = (long)ne.lNetworkEvents;
            o->errCode0 = (int)ne.iErrorCode[0];
            o->errCode1 = (int)ne.iErrorCode[1];
        }
    }

    if (!readFirst) doRead(s, readBudget, o);

    o->pendAfter = pendingBytes(s);
}

static const char* waitLabel(const RoundRec* r)
{
    if (r->waitFailed)   return "FAIL";
    if (r->immediate)    return "IMMED";
    if (r->waited)       return "WAIT";
    if (r->timedOut)     return "NONE";
    return "?";
}

static void printRound(int round, const RoundRec* r)
{
    char evbuf[128];
    appendMask(evbuf, sizeof(evbuf), r->events);
    printf("  r%02d | %-5s | %-32s | FD_READ=%-3s | pend %d->%d | read %d\n",
           round, waitLabel(r), evbuf,
           (r->events & FD_READ) ? "YES" : "no",
           r->pendBefore, r->pendAfter, r->bytesRead);
    if (r->enumErr)    printf("        ^ Enum failed, WSA error %d\n", r->enumErr);
    if (r->readErr)    printf("        ^ recv error %d%s\n", r->readErr,
                              (r->readErr == WSAEWOULDBLOCK) ? " (WOULDBLOCK)" : "");
    if (r->peerClosed)  printf("        ^ recv returned 0 (peer FIN)\n");
    /* FD_ERROR is NOT a bit constant: it is the *return value* of
     * WSAEnumNetworkEvents on failure (already shown via r->enumErr). */
    if (r->events & FD_CONNECT)
        printf("        ^ FD_CONNECT code=%d\n", r->errCode1);
}

/* ------------------------------------------------------------------------ */
/* one case                                                                  */
/* ------------------------------------------------------------------------ */
typedef struct {
    const char* tag;
    const char* desc;
    int  frames;        /* frames the server pushes                        */
    int  readBytesR1;   /* bytes the client reads in round 1               */
    char validKind;     /* 'A'/'B'/'C'/'D' -- which precondition to check */
} CaseSpec;

typedef struct {
    int  fdReadRounds;      /* rounds in which FD_READ was reported        */
    int  fdReadAfterFirst;  /* rounds 2..N reported FD_READ                */
    int  fdReadInRound2;    /* round 2 reported FD_READ                    */
    int  fdReadLater;       /* rounds 3..N reported FD_READ                */
    int  firstFdReadRound;  /* 1-based index of the first FD_READ round    */
    int  pendBeforeR1;
    int  pendAfterR1;
    int  bytesReadR1;
    int  valid;
    char note[112];
    char pattern[MAX_ROUNDS + 2]; /* '#' per round where FD_READ was seen */
} CaseResult;

static void checkValid(const CaseSpec* cs, CaseResult* res)
{
    res->valid = 1;
    res->note[0] = '\0';
    switch (cs->validKind) {
    case 'A':   /* data present and NOT read */
        if (res->pendBeforeR1 != FRAME_BYTES || res->bytesReadR1 != 0 ||
            res->pendAfterR1 != FRAME_BYTES) {
            res->valid = 0;
            sprintf(res->note, "precondition broken (pend %d->%d, read %d)",
                    res->pendBeforeR1, res->pendAfterR1, res->bytesReadR1);
        }
        break;
    case 'B':   /* data read away, kernel empty */
        if (res->pendBeforeR1 != FRAME_BYTES || res->bytesReadR1 != FRAME_BYTES ||
            res->pendAfterR1 != 0) {
            res->valid = 0;
            sprintf(res->note, "precondition broken (pend %d->%d, read %d)",
                    res->pendBeforeR1, res->pendAfterR1, res->bytesReadR1);
        }
        break;
    case 'C':   /* frame1 read, frame2 still queued */
        if (res->pendBeforeR1 != 2 * FRAME_BYTES ||
            res->bytesReadR1 != FRAME_BYTES ||
            res->pendAfterR1 != FRAME_BYTES) {
            res->valid = 0;
            sprintf(res->note, "precondition broken (pend %d->%d, read %d)",
                    res->pendBeforeR1, res->pendAfterR1, res->bytesReadR1);
        }
        break;
    case 'D':   /* control: nothing was ever sent */
        if (res->pendBeforeR1 != 0 || res->bytesReadR1 != 0) {
            res->valid = 0;
            sprintf(res->note, "precondition broken (pend %d->%d, read %d)",
                    res->pendBeforeR1, res->pendAfterR1, res->bytesReadR1);
        }
        break;
    default:
        break;
    }
}

static void runCase(const CaseSpec* cs, const char* variantName,
                     int readFirst, CaseResult* res)
{
    Pair pr;
    char err[128];
    long mask = 0;
    char note[192];
    int expect, waited, r;

    memset(res, 0, sizeof(*res));

    printf("--------------------------------------------------------------------\n");
    printf("CASE %s [%s] : %s\n", cs->tag, variantName, cs->desc);
    printf("  server pushes %d frame(s) of %d bytes; client reads %d byte(s) in round 1\n",
           cs->frames, FRAME_BYTES, cs->readBytesR1);

    if (setupPair(&pr, err, sizeof(err)) != 0) {
        printf("  setup failed: %s\n", err);
        res->valid = 0;
        setErr(res->note, sizeof(res->note), "setup failed");
        return;
    }
    printf("  loopback pair established on 127.0.0.1:%u\n", (unsigned)pr.port);

    note[0] = '\0';
    if (armEventSelect(pr.clientSock, pr.event, &mask, note, sizeof(note)) != 0) {
        printf("  %s\n", note);
        res->valid = 0;
        setErr(res->note, sizeof(res->note), "WSAEventSelect failed");
        closePair(&pr);
        return;
    }
    {
        char mb[128];
        appendMask(mb, sizeof(mb), mask);
        printf("  WSAEventSelect mask = %s%s%s\n", mb, note[0] ? "  " : "", note);
    }

    /* Step 1 -- the server pushes its frames.  The server socket is
     * blocking, so send() returns once the bytes are in the kernel. */
    {
        int i;
        for (i = 0; i < cs->frames; i++) {
            char payload[FRAME_BYTES];
            int sent = 0;
            memset(payload, 'A' + (i % 26), sizeof(payload));
            while (sent < FRAME_BYTES) {
                int n = send(pr.serverSock, payload + sent, FRAME_BYTES - sent, 0);
                if (n <= 0) break;
                sent += n;
            }
        }
        printf("  server sent %d frame(s)\n", cs->frames);
    }

    /* Step 2 -- settle: wait until the bytes are really in the client's
     * kernel receive buffer.  FIONREAD only queries, it does not consume
     * any event state, so the measurement stays undisturbed. */
    expect = cs->frames * FRAME_BYTES;
    waited = 0;
    while (waited < SETTLE_TIMEOUT_MS) {
        int p = pendingBytes(pr.clientSock);
        if (p < 0) break;
        if (p >= expect) break;
        Sleep(5);
        waited += 5;
    }
    printf("  settle: %d byte(s) pending in kernel after %d ms\n",
           pendingBytes(pr.clientSock), waited);

    /* Step 3 -- the measurement rounds. */
    for (r = 1; r <= g_rounds; r++) {
        RoundRec rec;
        int budget = (r == 1) ? cs->readBytesR1 : 0;
        runRound(pr.clientSock, pr.event, budget, readFirst, &rec);
        printRound(r, &rec);
        if (rec.events & FD_READ) {
            res->fdReadRounds++;
            if (res->firstFdReadRound == 0) res->firstFdReadRound = r;
            if (r == 2) res->fdReadInRound2 = 1;
            if (r >= 2) res->fdReadAfterFirst = 1;
            if (r >= 3) res->fdReadLater = 1;
            res->pattern[r - 1] = '#';
        } else {
            res->pattern[r - 1] = '.';
        }
        if (r == 1) {
            res->pendBeforeR1 = rec.pendBefore;
            res->pendAfterR1  = rec.pendAfter;
            res->bytesReadR1  = rec.bytesRead;
        }
    }
    res->pattern[g_rounds] = '\0';

    checkValid(cs, res);
    printf("  case %s precondition: %s%s%s\n", cs->tag,
           res->valid ? "OK" : "BROKEN", res->note[0] ? " - " : "",
           res->note[0] ? res->note : "");
    printf("  FD_READ per round: %s\n", res->pattern);

    closePair(&pr);
}

/* ------------------------------------------------------------------------ */
/* summary                                                                   */
/* ------------------------------------------------------------------------ */
static void printVariantTable(const char* title, const CaseSpec* specs,
                               const CaseResult* results, int n)
{
    int i;
    printf("\n");
    printf("================================================================\n");
    printf(" %s\n", title);
    printf("================================================================\n");
    printf(" case | setup                                    | FD_READ per round | valid\n");
    printf(" -----+------------------------------------------+------------------+------\n");
    for (i = 0; i < n; i++) {
        printf("  %-4s | %-40s | %-16s | %s\n",
               specs[i].tag, specs[i].desc, results[i].pattern,
               results[i].valid ? "ok" : "BROKEN");
    }
    printf(" -----+------------------------------------------+------------------+------\n");
}

static const char* classify(const CaseResult* r, int n)
{
    int allValid = 1, i;
    for (i = 0; i < n; i++) if (!r[i].valid) allValid = 0;

    if (!allValid)               return "UNKNOWN (a case precondition was broken - do not trust)";
    if (r[1].fdReadAfterFirst)   return "STICKY (FD_READ reported even after the data was read -> the bit must NOT be consumed)";
    if (r[0].fdReadAfterFirst && !r[3].fdReadRounds)
                                  return "LEVEL (FD_READ is re-reported while unread data is queued -> the bit may be consumed after the read)";
    if (!r[0].fdReadAfterFirst)  return "EDGE (FD_READ reported once per arrival, not re-armed while data is queued -> the bit must stay resident)";
    return "MIXED (inconsistent measurements - do not trust)";
}

int main(int argc, char** argv)
{
    WSADATA wsaData;
    static const CaseSpec specs[] = {
        { "A", "data present, client never reads",      1, 0,             'A' },
        { "B", "data read away, no new data arrives",   1, FRAME_BYTES,   'B' },
        { "C", "frame1 read, frame2 still queued",       2, FRAME_BYTES,   'C' },
        { "D", "control: server never sent anything",    0, 0,             'D' }
    };
    const int nCases = (int)(sizeof(specs) / sizeof(specs[0]));
    static const char* variantNames[VARIANT_COUNT] = {
        "read-then-enum", "enum-then-read"
    };
    static const char* variantTitles[VARIANT_COUNT] = {
        "SUMMARY - variant \"read then enum\"  (recv BEFORE WSAEnumNetworkEvents)",
        "SUMMARY - variant \"enum then read\"  (WSAEnumNetworkEvents BEFORE recv = repo order)"
    };
    CaseResult results[VARIANT_COUNT][4];
    const char* cls;
    char clsb[256];
    int v, i;

    if (argc > 1) { int x = atoi(argv[1]); if (x > 0) g_rounds = x; }
    if (argc > 2) { int x = atoi(argv[2]); if (x >= 0) g_waitMs = x; }
    if (g_rounds > MAX_ROUNDS) g_rounds = MAX_ROUNDS;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 2;
    }

    printf("================================================================\n");
    printf(" FD_READ edge-vs-level probe  (WSAEventSelect, plain WinSock)\n");
    printf("================================================================\n");
    printf(" rounds per case      : %d\n", g_rounds);
    printf(" bounded wait fallback: %d ms (only used when the zero-timeout wait timed out)\n", g_waitMs);
    printf(" frame size           : %d bytes\n", FRAME_BYTES);
    printf(" transport            : loopback TCP 127.0.0.1 (fresh pair per case AND per variant)\n");
    printf(" wait label  : IMMED = Wait(0) returned the event, WAIT = bounded wait returned it,\n");
    printf("               NONE = no event within the bounded wait\n");
    printf(" pend        : FIONREAD bytes pending in the kernel, before -> after the round's read\n");
    printf(" pattern     : '#' = FD_READ reported in that round, '.' = not reported\n");
    printf(" variants    : \"read-then-enum\" reports the buffer state AFTER the read,\n");
    printf("               \"enum-then-read\" is the order the repo uses today\n");

    for (v = 0; v < VARIANT_COUNT; v++) {
        for (i = 0; i < nCases; i++) {
            runCase(&specs[i], variantNames[v], (v == 0) ? 1 : 0, &results[v][i]);
            printf("\n");
        }
    }

    for (v = 0; v < VARIANT_COUNT; v++) {
        printVariantTable(variantTitles[v], specs, results[v], nCases);
    }

    /* ---- facts, primary variant -------------------------------------- */
    printf("\n");
    printf("================================================================\n");
    printf(" MEASURED FACTS  (primary variant: read-then-enum)\n");
    printf("================================================================\n");
    printf("   case A  data present, client never reads it        -> FD_READ in rounds 2..N: %s\n",
           results[0][0].fdReadAfterFirst ? "YES" : "no");
    printf("   case B  data read away, no new data arrives       -> FD_READ in rounds 2..N: %s\n",
           results[0][1].fdReadAfterFirst ? "YES" : "no");
    printf("   case C  frame1 read, frame2 still queued in kernel -> FD_READ in rounds 2..N: %s\n",
           results[0][2].fdReadAfterFirst ? "YES" : "no");
    printf("   case D  control, server never sent anything        -> FD_READ in ANY round   : %s\n",
           results[0][3].fdReadRounds ? "YES" : "no");

    cls = classify(results[0], nCases);
    strncpy(clsb, cls, sizeof(clsb) - 1);
    clsb[sizeof(clsb) - 1] = '\0';

    printf("\n");
    printf("================================================================\n");
    printf(" CONCLUSION (derived from the measurements above)\n");
    printf("================================================================\n");
    printf("   FD_READ still visible while data is UNREAD    : %s\n",
           results[0][0].fdReadAfterFirst ? "YES" : "no");
    printf("   FD_READ reported after the buffer was DRAINED  : %s\n",
           results[0][1].fdReadAfterFirst ? "YES" : "no");
    printf("   FD_READ reported while a 2nd frame is queued  : %s\n",
           results[0][2].fdReadAfterFirst ? "YES" : "no");
    printf("   FD_READ reported although NOTHING was sent     : %s\n",
           results[0][3].fdReadRounds ? "YES" : "no");
    printf("   => FD_READ semantics : %s\n", clsb);

    printf("\n");
    printf("================================================================\n");
    printf(" WHAT THE REPO'S CURRENT ORDER SHOWS (enum-then-read)\n");
    printf("================================================================\n");
    for (i = 0; i < nCases; i++) {
        printf("   case %s  FD_READ per round : %s\n", specs[i].tag, results[1][i].pattern);
    }
    printf("   FD_READ reported in the round right after a drain : %s\n",
           results[1][1].fdReadInRound2 ? "YES" : "no");
    printf("   FD_READ still reported in rounds 3..N after drain : %s\n",
           results[1][1].fdReadLater ? "YES" : "no");
    printf("   => enum-then-read semantics : %s\n", classify(results[1], nCases));

    /* One machine-readable line per variant for scripted consumers. */
    printf("\n PROBE-SUMMARY read-then-enum A_REPEAT=%s B_AFTER_DRAIN=%s C_SECOND_FRAME=%s D_CONTROL=%s\n",
           results[0][0].fdReadAfterFirst ? "YES" : "NO",
           results[0][1].fdReadAfterFirst ? "YES" : "NO",
           results[0][2].fdReadAfterFirst ? "YES" : "NO",
           results[0][3].fdReadRounds ? "YES" : "NO");
    printf(" PROBE-SUMMARY enum-then-read A_REPEAT=%s B_AFTER_DRAIN=%s B_ROUND2=%s B_LATER=%s C_SECOND_FRAME=%s D_CONTROL=%s\n",
           results[1][0].fdReadAfterFirst ? "YES" : "NO",
           results[1][1].fdReadAfterFirst ? "YES" : "NO",
           results[1][1].fdReadInRound2 ? "YES" : "NO",
           results[1][1].fdReadLater ? "YES" : "NO",
           results[1][2].fdReadAfterFirst ? "YES" : "NO",
           results[1][3].fdReadRounds ? "YES" : "NO");

    WSACleanup();
    return 0;
}
