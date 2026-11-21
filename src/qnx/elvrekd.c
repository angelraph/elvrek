/*
 * elvrekd: the Elvrek safety authority (QNX 8.0).
 * SPDX-License-Identifier: Apache-2.0
 *
 * The only process that writes the actuators. Two threads:
 *
 *  control  SCHED_FIFO ELV_PRIO_CTRL. Woken by a 1 ms timer pulse on a
 *           private channel. Each tick: read sensors (seqlock, never blocks),
 *           take the latest advice and operator commands from lock-free
 *           mailboxes, run elv_core_step() and the independent invariant
 *           monitor, publish actuators and a status snapshot. A monitor
 *           violation forces a controlled retract.
 *
 *  resmgr   serves /dev/elvrek/{status,advice,operator,piston,shock}.
 *           Handlers run at the client's priority (QNX message-passing
 *           priority inheritance), so a flooding advisor at priority 10
 *           cannot delay the control thread at 60.
 *
 * After start-up the process denies and locks the abilities it no longer
 * needs (spawn, fork, setuid, physical memory, I/O privileges, interrupts).
 *
 * Usage: elvrekd [-v]
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/procmgr.h>
#include <sys/syspage.h>
#include <sys/slog2.h>

#include "elv_core.h"
#include "elv_inv.h"
#include "elv_ipc.h"

#define PULSE_TICK (_PULSE_CODE_MINAVAIL + 1)

/* shared state between the two threads */
static elv_core_t   g_core;          /* control thread only */
static elv_inv_t    g_inv;           /* control thread only */
static elv_hw_shm_t *g_hw;

static elv_seq_t          g_adv_seq;
static elv_advisor_msg_t  g_adv_box;
static _Atomic uint64_t   g_adv_count;
static _Atomic uint32_t   g_adv_rejected;

/* operator one-shot commands */
#define OP_START  0x01u
#define OP_STOP   0x02u
#define OP_SHOCK  0x04u
#define OP_EPI    0x08u
static _Atomic uint32_t g_op_once;
static _Atomic int      g_op_30_2;
static _Atomic int      g_op_transport;

static elv_seq_t    g_st_seq;
static elv_status_t g_status;

static int g_verbose;
static int g_emulator;      /* -e: wider sensor timeout for virtual machines */
static slog2_buffer_t g_log;

#define LOG(sev, ...) do { if (g_log) slog2f(g_log, 0, (sev), __VA_ARGS__); \
                           if (g_verbose) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void die(const char *what)
{
    fprintf(stderr, "elvrekd: %s: %s\n", what, strerror(errno));
    exit(EXIT_FAILURE);
}

/* resource manager */
static resmgr_connect_funcs_t g_cfuncs;
static resmgr_io_funcs_t      g_iofuncs;
static iofunc_attr_t a_status, a_advice, a_operator, a_piston, a_shock;

static int format_status(char *buf, size_t len)
{
    elv_status_t s;
    if (ELV_SEQ_READ(&g_st_seq, &s, &g_status) != 0) return -1;
    return snprintf(buf, len,
        "tick=%llu state=%s sw=%d fault=%s ccf=%.4f shocks=%u windows=%u "
        "max_window=%u last_window=%u machine=%u max_machine=%u human=%u max_human=%u "
        "retries=%u preauth=%u compressions=%u breaths=%u depth_cmd_mm=%.1f "
        "force_n=%.0f pos_mm=%.1f etco2=%.1f epi_prompt=%d epi_prompts=%u epi_given=%u "
        "inv=%u missed=%llu late=%llu overrun=%llu adv_age=%llu adv_msgs=%u "
        "adv_rejected=%u adv_ignored=%u adv_skips=%u shock_line=%d vent=%d charge=%d "
        "ready=%d transport=%d mode_30_2=%d force_limits=%u emulator=%d "
        "zp_checks=%u zp_skipped=%u zp_sure=%u zp_unsure=%u zp_score=%.2f\n",
        (unsigned long long)s.tick, elv_state_name((elv_state_t)s.state), s.sw_phase,
        elv_fault_name((elv_fault_t)s.fault), s.ccf, s.shocks, s.windows,
        s.max_window, s.last_window, s.last_machine, s.max_machine, s.last_human, s.max_human,
        s.press_retries, s.preauth_shocks, s.compressions, s.breaths, s.depth_cmd_m * 1000,
        s.force_n, s.pos_m * 1000, s.etco2, s.epi_prompt, s.epi_prompts, s.epi_given,
        s.inv_total, (unsigned long long)s.missed_ticks, (unsigned long long)s.late_wakeups,
        (unsigned long long)s.work_overruns, (unsigned long long)s.adv_age_ticks,
        s.adv_msgs, s.adv_rejected, s.adv_ignored, s.adv_skips, s.shock_line, s.vent_on,
        s.charge_on, s.charger_ready, s.transport_comp, s.mode_30_2, s.force_limit_events,
        g_emulator, s.zp_checks, s.zp_skipped, s.zp_sure_shock, s.zp_unsure, s.zp_mean);
}

static int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    char buf[1024];
    int n;
    if (ocb->attr == &a_status) {
        n = format_status(buf, sizeof(buf));
    } else if (ocb->attr == &a_piston) {
        elv_actuators_t a;
        if (ELV_SEQ_READ(&g_hw->aseq, &a, &g_hw->act) != 0) return EAGAIN;
        n = snprintf(buf, sizeof(buf), "target_mm=%.2f vmax=%.2f\n",
                     a.piston_target_m * 1000, a.piston_vmax_ms);
    } else if (ocb->attr == &a_shock) {
        elv_actuators_t a;
        if (ELV_SEQ_READ(&g_hw->aseq, &a, &g_hw->act) != 0) return EAGAIN;
        n = snprintf(buf, sizeof(buf), "shock_line=%d charge=%d dump=%d\n",
                     a.shock_line, a.charge_on, a.charge_dump);
    } else {
        return EBADF;               /* write-only devices */
    }
    if (n < 0) return EAGAIN;

    /* one snapshot per open: offset 0 returns it, anything later is EOF */
    size_t nbytes = 0;
    if (ocb->offset < n) {
        nbytes = (size_t)(n - ocb->offset);
        if (nbytes > msg->i.nbytes) nbytes = msg->i.nbytes;
    }
    _IO_SET_READ_NBYTES(ctp, nbytes);
    if (nbytes) {
        SETIOV(ctp->iov, buf + ocb->offset, nbytes);
        ocb->offset += nbytes;
        return _RESMGR_NPARTS(1);
    }
    return _RESMGR_NPARTS(0);
}

static void handle_operator(const char *cmd)
{
    if      (!strncmp(cmd, "start", 5))         atomic_fetch_or(&g_op_once, OP_START);
    else if (!strncmp(cmd, "stop", 4))          atomic_fetch_or(&g_op_once, OP_STOP);
    else if (!strncmp(cmd, "shock", 5))         atomic_fetch_or(&g_op_once, OP_SHOCK);
    else if (!strncmp(cmd, "epi", 3))           atomic_fetch_or(&g_op_once, OP_EPI);
    else if (!strncmp(cmd, "mode 30:2", 9))     atomic_store(&g_op_30_2, 1);
    else if (!strncmp(cmd, "mode cont", 9))     atomic_store(&g_op_30_2, 0);
    else if (!strncmp(cmd, "transport on", 12)) atomic_store(&g_op_transport, 1);
    else if (!strncmp(cmd, "transport off", 13))atomic_store(&g_op_transport, 0);
}

static int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    size_t nbytes = _IO_WRITE_GET_NBYTES(msg);

    if (ocb->attr == &a_advice) {
        elv_advisor_msg_t m;
        if (nbytes != sizeof(m)) { atomic_fetch_add(&g_adv_rejected, 1); return EINVAL; }
        if (resmgr_msgread(ctp, &m, sizeof(m), sizeof(msg->i)) != (ssize_t)sizeof(m))
            return EIO;
        /* structural checks only; the core decides what advice means */
        if (m.magic != ELV_ADVISOR_MAGIC) { atomic_fetch_add(&g_adv_rejected, 1); return EINVAL; }
        elv_seq_wbegin(&g_adv_seq);
        g_adv_box = m;
        elv_seq_wend(&g_adv_seq);
        atomic_fetch_add(&g_adv_count, 1);
    } else if (ocb->attr == &a_operator) {
        char cmd[64];
        size_t n = nbytes < sizeof(cmd) - 1 ? nbytes : sizeof(cmd) - 1;
        if (resmgr_msgread(ctp, cmd, n, sizeof(msg->i)) < 0) return EIO;
        cmd[n] = '\0';
        handle_operator(cmd);
    } else {
        return EPERM;               /* piston / shock / status are not writable */
    }
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    return _RESMGR_NPARTS(0);
}

static void attach(dispatch_t *dpp, resmgr_attr_t *ra, const char *path,
                   iofunc_attr_t *attr, mode_t mode)
{
    iofunc_attr_init(attr, S_IFCHR | mode, NULL, NULL);
    attr->uid = 0;
    attr->gid = 0;
    if (resmgr_attach(dpp, ra, path, _FTYPE_ANY, 0, &g_cfuncs, &g_iofuncs, attr) == -1)
        die(path);
}

static void *resmgr_thread(void *arg)
{
    dispatch_t *dpp = arg;
    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    if (!ctp) die("dispatch_context_alloc");
    for (;;) {
        dispatch_context_t *c = dispatch_block(ctp);
        if (c) dispatch_handler(c);
    }
    return NULL;
}

/* control thread */
static void publish_status(const elv_sensors_t *s, const elv_actuators_t *a,
                           uint64_t missed, uint64_t late, uint64_t overrun)
{
    const elv_core_t *c = &g_core;
    elv_seq_wbegin(&g_st_seq);
    g_status.tick = c->tick;
    g_status.state = c->state;
    g_status.sw_phase = c->sw_phase;
    g_status.fault = c->fault;
    g_status.ccf = elv_core_ccf(c);
    g_status.shocks = c->m.shocks;
    g_status.windows = c->m.shock_windows;
    g_status.max_window = c->m.max_window_ticks;
    g_status.last_window = c->m.last_window_ticks;
    g_status.max_machine = c->m.max_machine_ticks;
    g_status.last_machine = c->m.last_machine_ticks;
    g_status.max_human = c->m.max_human_ticks;
    g_status.last_human = c->m.last_human_ticks;
    g_status.press_retries = c->m.press_retries;
    g_status.preauth_shocks = c->m.preauth_shocks;
    g_status.zp_checks = c->m.zp_checks;
    g_status.zp_skipped = c->m.zp_skipped;
    g_status.zp_sure_shock = c->m.zp_sure_shock;
    g_status.zp_unsure = c->m.zp_unsure;
    g_status.zp_mean = c->zp_mean;
    g_status.compressions = c->m.compressions;
    g_status.breaths = c->m.breaths;
    g_status.depth_cmd_m = c->depth_cmd_m;
    g_status.force_n = s->force_n;
    g_status.pos_m = s->piston_pos_m;
    g_status.etco2 = s->etco2_mmhg;
    g_status.epi_prompt = c->epi_prompt;
    g_status.epi_prompts = c->m.epi_prompts;
    g_status.epi_given = c->m.epi_given;
    g_status.inv_total = elv_inv_total(&g_inv);
    g_status.missed_ticks = missed;
    g_status.late_wakeups = late;
    g_status.work_overruns = overrun;
    g_status.adv_age_ticks = c->adv_valid ? c->tick - c->last_adv_tick : c->tick;
    g_status.adv_msgs = (uint32_t)atomic_load(&g_adv_count);
    g_status.adv_rejected = atomic_load(&g_adv_rejected);
    g_status.adv_ignored = c->m.advisor_demands_ignored;
    g_status.adv_skips = c->m.advisor_skips;
    g_status.shock_line = a->shock_line;
    g_status.vent_on = a->vent_on;
    g_status.charge_on = a->charge_on;
    g_status.charger_ready = s->charger_ready;
    g_status.transport_comp = c->transport_comp;
    g_status.mode_30_2 = c->mode_30_2;
    g_status.force_limit_events = c->m.force_limit_events;
    elv_seq_wend(&g_st_seq);
}

static void lock_abilities(void)
{
    int r = procmgr_ability(0,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SPAWN,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_FORK,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SETUID,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SETGID,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_MEM_PHYS,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_IO,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_INTERRUPT,
        PROCMGR_AID_EOL);
    if (r != EOK) LOG(SLOG2_WARNING, "procmgr_ability lock failed: %d", r);
}

static void control_loop(void)
{
    struct sched_param sp = { .sched_priority = ELV_PRIO_CTRL };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != EOK)
        die("SCHED_FIFO (run as root or grant the priority ability)");

    int chid = ChannelCreate(_NTO_CHF_PRIVATE);
    if (chid == -1) die("ChannelCreate");
    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) die("ConnectAttach");

    struct sigevent ev;
    SIGEV_PULSE_INIT(&ev, coid, ELV_PRIO_CTRL, PULSE_TICK, 0);
    timer_t tid;
    if (timer_create(CLOCK_MONOTONIC, &ev, &tid) == -1) die("timer_create");
    struct itimerspec its = {
        .it_value    = { 0, 1000000 },
        .it_interval = { 0, 1000000 },
    };
    if (timer_settime(tid, 0, &its, NULL) == -1) die("timer_settime");

    const uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    const uint64_t period_cyc = cps / ELV_TICK_HZ;
    uint64_t last_wake = 0, missed = 0, late = 0, overrun = 0;
    uint64_t last_sseq = 0, last_adv = 0, last_sensor_cyc = 0, last_accel_n = 0;

    elv_actuators_t act;
    memset(&act, 0, sizeof(act));
    elv_sensors_t s;
    memset(&s, 0, sizeof(s));

    LOG(SLOG2_INFO, "elvrekd: control loop running at %d Hz, priority %d",
        ELV_TICK_HZ, ELV_PRIO_CTRL);

    for (;;) {
        struct _pulse p;
        if (MsgReceivePulse(chid, &p, sizeof(p), NULL) == -1) continue;
        if (p.code != PULSE_TICK) continue;

        uint64_t now = ClockCycles();
        uint32_t skipped = 0;
        if (last_wake) {
            uint64_t gap = now - last_wake;
            if (gap > period_cyc + period_cyc / 2) {
                late++;
                skipped = (uint32_t)(gap / period_cyc - 1);   /* whole periods */
                missed += skipped;
                /* The controller itself was asleep for this gap, so it is
                   no evidence that the hardware went quiet: when a whole
                   virtual machine freezes, the plant has not had its turn
                   yet when this loop wakes. Silence is counted only over
                   time the controller was awake to see it. */
                if (last_sensor_cyc) last_sensor_cyc += gap - period_cyc;
            }
        }
        last_wake = now;

        /* sensors: if the read races the writer 8 times, reuse last sample */
        elv_sensors_t ns;
        if (ELV_SEQ_READ(&g_hw->sseq, &ns, &g_hw->sensors) == 0) s = ns;

        /* Hardware silence is judged on real time, not on core ticks, so
           catch-up steps below cannot hide or fake a stale sensor. */
        if (s.seq != last_sseq) { last_sseq = s.seq; last_sensor_cyc = now; }
        else if (last_sensor_cyc &&
                 now - last_sensor_cyc > (uint64_t)g_core.sensor_stale_ticks * period_cyc)
            elv_core_force_fault(&g_core, ELV_FAULT_SENSOR_STALE);
        if (!last_sensor_cyc) last_sensor_cyc = now;

        /* latest advice, if any arrived since last tick */
        elv_advisor_msg_t am, *pam = NULL;
        uint64_t ac = atomic_load(&g_adv_count);
        if (ac != last_adv && ELV_SEQ_READ(&g_adv_seq, &am, &g_adv_box) == 0) {
            last_adv = ac;
            pam = &am;
        }

        uint32_t once = atomic_exchange(&g_op_once, 0);
        elv_operator_t op = {
            .start = once & OP_START,
            .stop = once & OP_STOP,
            .shock_press = once & OP_SHOCK,
            .epi_given = once & OP_EPI,
            .mode_30_2 = atomic_load(&g_op_30_2),
            .transport_comp = atomic_load(&g_op_transport),
        };
        /* A press while charging is a pre-authorisation, held by the core;
           a press in the window is the classic one. Elsewhere it is ignored. */

        /* Catch up: if the loop woke late, run the ticks it missed (at most
           200) so the state machine's clocks, the compression rate and the
           road motion estimate stay in step with real time. Replayed ticks
           get fresh sequence numbers because staleness is checked above on
           real time. Advice and crew input are applied once, on the last. */
        if (skipped > 200) skipped = 200;
        /* the motion samples that arrived since the last tick, oldest first */
        uint64_t an = atomic_load(&g_hw->accel_n);
        uint64_t fresh = an - last_accel_n;
        if (fresh > 500) fresh = 500;
        last_accel_n = an;
        for (uint32_t k = 0; k <= skipped; k++) {
            const int last = k == skipped;
            /* never replay inside the shock window: every tick there must
               reach the hardware, so the shock line is never only imagined */
            if (!last && g_core.state == ELV_ST_SHOCK_WINDOW) continue;
            elv_sensors_t sk = s;
            sk.seq = s.seq * 64 + k;
            if (fresh) {   /* spread the buffered samples across the replayed ticks */
                uint64_t idx = an - fresh + (fresh * (k + 1)) / (skipped + 1) - 1;
                sk.frame_accel_ms2 = g_hw->accel_ring[idx % 512];
            }
            static const elv_operator_t idle_op;
            elv_operator_t ok = op;
            if (!last) { ok = idle_op; ok.mode_30_2 = op.mode_30_2; ok.transport_comp = op.transport_comp; }
            elv_core_step(&g_core, &sk, last ? pam : NULL, &ok, &act);
            if (!elv_inv_check(&g_inv, &g_core, &sk, &act)) {
                elv_core_force_fault(&g_core, ELV_FAULT_INVARIANT);
                act.piston_target_m = 0.0;
                act.piston_vmax_ms = ELV_RETRACT_V_MS;
                act.shock_line = false;
                act.charge_on = false;
                act.charge_dump = true;
                LOG(SLOG2_ERROR, "invariant violation at tick %llu: forced retract",
                    (unsigned long long)g_core.tick);
            }
        }

        elv_seq_wbegin(&g_hw->aseq);
        g_hw->act = act;
        elv_seq_wend(&g_hw->aseq);
        atomic_store(&g_hw->act_tick, g_core.tick);

        if (ClockCycles() - now > period_cyc) overrun++;
        publish_status(&s, &act, missed, late, overrun);
    }
}

/* main */
int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "ve")) != -1) {
        if (opt == 'v') g_verbose = 1;
        if (opt == 'e') g_emulator = 1;
    }

    slog2_buffer_set_config_t cfg = {
        .num_buffers = 1, .buffer_set_name = "elvrekd",
        .verbosity_level = SLOG2_INFO,
        .buffer_config = { { .buffer_name = "main", .num_pages = 4 } },
    };
    if (slog2_register(&cfg, &g_log, 0) == -1) g_log = NULL;

    /* hardware boundary, created by elv-plant or the hardware driver */
    int fd = -1;
    for (int i = 0; i < 50 && fd == -1; i++) {
        fd = shm_open(ELV_SHM_HW, O_RDWR, 0);
        if (fd == -1) usleep(100000);
    }
    if (fd == -1) die("shm_open " ELV_SHM_HW " (start elv-plant first)");
    g_hw = mmap(NULL, sizeof(*g_hw), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_hw == MAP_FAILED) die("mmap hw");
    close(fd);
    if (g_hw->magic != ELV_HW_MAGIC) { errno = EINVAL; die("hw magic"); }

    elv_core_init(&g_core);
    if (g_emulator) {
        g_core.sensor_stale_ticks = ELV_EMU_SENSOR_STALE_TICKS;
        LOG(SLOG2_WARNING, "elvrekd: EMULATOR MODE, sensor timeout %d ms", ELV_EMU_SENSOR_STALE_TICKS);
    }
    elv_inv_init(&g_inv);

    /* resource manager */
    dispatch_t *dpp = dispatch_create();
    if (!dpp) die("dispatch_create");
    resmgr_attr_t ra;
    memset(&ra, 0, sizeof(ra));
    ra.nparts_max = 1;
    ra.msg_max_size = 2048;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &g_cfuncs, _RESMGR_IO_NFUNCS, &g_iofuncs);
    g_iofuncs.read = io_read;
    g_iofuncs.write = io_write;

    attach(dpp, &ra, ELV_DEV_STATUS,   &a_status,   0444);
    attach(dpp, &ra, ELV_DEV_OPERATOR, &a_operator, 0600);
    attach(dpp, &ra, ELV_DEV_PISTON,   &a_piston,   0400);
    attach(dpp, &ra, ELV_DEV_SHOCK,    &a_shock,    0400);

    /* The advice device lives on its own dispatch, so its own channel. Under
       the security policy that channel gets its own type
       (allow_attach elvrekd_t /dev/elvrek/advice elv_advice_t), and the
       advisor's type may connect to that channel and nothing else. */
    dispatch_t *dpp_adv = dispatch_create();
    if (!dpp_adv) die("dispatch_create advice");
    attach(dpp_adv, &ra, ELV_DEV_ADVICE, &a_advice, 0622);

    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setinheritsched(&ta, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&ta, SCHED_RR);
    struct sched_param rp = { .sched_priority = ELV_PRIO_RESMGR };
    pthread_attr_setschedparam(&ta, &rp);
    pthread_t rt, rt_adv;
    if (pthread_create(&rt, &ta, resmgr_thread, dpp) != EOK) die("pthread_create resmgr");
    if (pthread_create(&rt_adv, &ta, resmgr_thread, dpp_adv) != EOK) die("pthread_create advice");

    /* QNX timers expire on system clock ticks, so log the tick the 1 ms
       control timer runs on. On QNX 8.0 in QEMU it is a fixed 1 ms and
       ClockPeriod cannot change it (it returns ENOTSUP), so we read it
       rather than assume it. Missed ticks are measured in the loop. */
    struct _clockperiod cp;
    if (ClockPeriod(CLOCK_REALTIME, NULL, &cp, 0) == 0)
        LOG(SLOG2_INFO, "system clock period %u ns", cp.nsec);

    /* nothing below needs these abilities again */
    lock_abilities();

    LOG(SLOG2_INFO, "elvrekd: up; devices under %s", ELV_DEV_DIR);
    control_loop();
    return 0;
}
