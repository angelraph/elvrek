/*
 * elv-advice-gate: the only door from the AI classifier to the safety process.
 * SPDX-License-Identifier: Apache-2.0
 *
 * On QNX a channel takes the security type of the process that creates it.
 * If the safety process served the advice path itself, the advisor would have
 * to be allowed to connect to the same channel that serves the piston and the
 * shock line. So advice gets its own small process, with its own type:
 *
 *   elv-advisor  --(may connect to elv_gate_t only)-->  elv-advice-gate
 *   elv-advice-gate  --(may connect to elvrekd_t)-->     elvrekd (advice_core)
 *
 * The security policy then lets the kernel refuse the advisor any connection
 * to the safety process at all, even when the advisor runs as root.
 *
 * The gate serves /dev/elvrek/advice (mode 0622), accepts only whole
 * elv_advisor_msg_t messages with the right magic, and forwards them to
 * /dev/elvrek/advice_core, which only root may open. It opens that path
 * before dropping to an unprivileged user, then locks its abilities.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/procmgr.h>
#include <unistd.h>
#include "elv_ipc.h"
#include "elv_proto.h"
#include "elv_rt.h"

#define ELV_GATE_UID 2003

static int g_core_fd = -1;
static resmgr_connect_funcs_t g_cfuncs;
static resmgr_io_funcs_t g_iofuncs;
static iofunc_attr_t g_attr;
static _Atomic unsigned g_forwarded, g_rejected;

static void die(const char *what) { perror(what); exit(1); }

static int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    size_t nbytes = _IO_WRITE_GET_NBYTES(msg);
    elv_advisor_msg_t m;
    if (nbytes != sizeof(m)) { atomic_fetch_add(&g_rejected, 1); return EINVAL; }
    if (resmgr_msgread(ctp, &m, sizeof(m), sizeof(msg->i)) != (ssize_t)sizeof(m)) return EIO;
    if (m.magic != ELV_ADVISOR_MAGIC) { atomic_fetch_add(&g_rejected, 1); return EINVAL; }
    if (write(g_core_fd, &m, sizeof(m)) != (ssize_t)sizeof(m)) return EIO;
    atomic_fetch_add(&g_forwarded, 1);
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    return _RESMGR_NPARTS(0);
}

static void drop_privileges(void)
{
    if (setgid(ELV_GATE_UID) == -1 || setuid(ELV_GATE_UID) == -1) die("elv-advice-gate: drop root");
    procmgr_ability(0,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SPAWN,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_FORK,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SETUID,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SETGID,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_PRIORITY,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_MEM_PHYS,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_IO,
        PROCMGR_AID_EOL);
}

int main(void)
{
    if (elv_rt_set_prio(ELV_PRIO_ADVISOR) != 0)
        fprintf(stderr, "elv-advice-gate: warning: could not set priority %d\n", ELV_PRIO_ADVISOR);

    /* the controller's private advice door, opened while still root */
    for (int i = 0; i < 50 && g_core_fd == -1; i++) {
        g_core_fd = open(ELV_DEV_ADVICE_CORE, O_WRONLY);
        if (g_core_fd == -1) usleep(100000);
    }
    if (g_core_fd == -1) die(ELV_DEV_ADVICE_CORE);

    dispatch_t *dpp = dispatch_create();
    if (!dpp) die("dispatch_create");
    resmgr_attr_t ra;
    memset(&ra, 0, sizeof(ra));
    ra.nparts_max = 1;
    ra.msg_max_size = 2048;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &g_cfuncs, _RESMGR_IO_NFUNCS, &g_iofuncs);
    g_iofuncs.write = io_write;
    iofunc_attr_init(&g_attr, S_IFCHR | 0622, NULL, NULL);
    g_attr.uid = 0;
    g_attr.gid = 0;
    if (resmgr_attach(dpp, &ra, ELV_DEV_ADVICE, _FTYPE_ANY, 0, &g_cfuncs, &g_iofuncs, &g_attr) == -1)
        die(ELV_DEV_ADVICE);

    drop_privileges();
    fprintf(stderr, "elv-advice-gate: serving %s, forwarding to %s as uid %d\n",
            ELV_DEV_ADVICE, ELV_DEV_ADVICE_CORE, ELV_GATE_UID);

    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    if (!ctp) die("dispatch_context_alloc");
    for (;;) {
        dispatch_context_t *c = dispatch_block(ctp);
        if (c) dispatch_handler(c);
    }
}
