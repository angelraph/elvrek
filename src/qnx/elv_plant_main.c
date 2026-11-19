/*
 * elv-plant: the physics plant as a separate process, standing in for the
 * piston, load cell, recoil switch, capnograph, ECG front end and charger.
 * SPDX-License-Identifier: Apache-2.0
 *
 * It owns the hardware boundary (/elvrek_hw) exactly like the future Pi 5
 * actuator driver will, including a HARDWARE WATCHDOG: if elvrekd stops
 * publishing for ELV_HW_WD_TICKS, the "motor driver" retracts the piston to
 * recoil on its own and dumps the charger. Losing the safety process
 * therefore never leaves the cup at the bottom of the chest.
 *
 * Usage: elv-plant [-r vf|sinus|asystole] [-c] [-t] [-s]
 *   -c  a shock converts VF (and perfusion returns 20 s later)
 *   -t  transport (vehicle vibration and bumps)
 *   -s  stiff chest
 */
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "elv_ipc.h"
#include "elv_limits.h"
#include "elv_plant.h"
#include "elv_rt.h"

#define ELV_HW_WD_TICKS 20

static void *map_shm(const char *name, size_t len, mode_t mode)
{
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, mode);
    if (fd == -1) { perror(name); exit(1); }
    fchmod(fd, mode);
    if (ftruncate(fd, (off_t)len) == -1) { perror("ftruncate"); exit(1); }
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);
    memset(p, 0, len);
    return p;
}

int main(int argc, char **argv)
{
    elv_rhythm_t r = ELV_RHYTHM_SHOCKABLE;
    elv_patient_cfg_t pc;
    memset(&pc, 0, sizeof(pc));
    int opt;
    uint64_t wd_limit = ELV_HW_WD_TICKS;
    while ((opt = getopt(argc, argv, "r:ctse")) != -1) {
        switch (opt) {
        case 'e': wd_limit = ELV_EMU_HW_WD_TICKS; break;   /* emulator mode */
        case 'r':
            r = !strcmp(optarg, "sinus") ? ELV_RHYTHM_ORGANIZED :
                !strcmp(optarg, "asystole") ? ELV_RHYTHM_ASYSTOLE : ELV_RHYTHM_SHOCKABLE;
            break;
        case 'c': pc.shock_converts = true; pc.rosc_delay_ticks = 20000; break;
        case 't': pc.transport = true; break;
        case 's': pc.k1 = 11000; pc.k3 = 2.8e6; break;
        default:
            fprintf(stderr, "usage: elv-plant [-r vf|sinus|asystole] [-c] [-t] [-s] [-e]\n");
            return 1;
        }
    }

    elv_hw_shm_t  *hw  = map_shm(ELV_SHM_HW,  sizeof(*hw),  0600);
    elv_ecg_shm_t *ecg = map_shm(ELV_SHM_ECG, sizeof(*ecg), 0644);
    elv_flt_shm_t *flt = map_shm(ELV_SHM_FLT, sizeof(*flt), 0600);
    atomic_store(&flt->transport, pc.transport);
    atomic_store(&flt->shock_converts, pc.shock_converts);
    hw->magic = ELV_HW_MAGIC;

    static elv_plant_t plant;
    elv_plant_init(&plant, &pc, r);

    elv_rt_t rt;
    if (elv_rt_init(&rt, 1000000L, ELV_PRIO_PLANT) != 0) { perror("timer"); return 1; }
    if (elv_rt_set_prio(ELV_PRIO_PLANT) != 0)
        fprintf(stderr, "elv-plant: warning: could not set SCHED_FIFO %d\n", ELV_PRIO_PLANT);

    uint64_t last_act_tick = 0, wd_age = 0, last_ecg_seq = 0;
    bool wd_tripped = false;
    fprintf(stderr, "elv-plant: running (physics model, not a patient)\n");

    for (;;) {
        elv_rt_wait(&rt);

        /* fault-injection knobs */
        plant.cfg.transport = atomic_load(&flt->transport);
        plant.cfg.shock_converts = atomic_load(&flt->shock_converts);
        plant.cfg.drop_sensors_at = atomic_load(&flt->drop_sensors) ? 1 : 0;
        plant.cfg.estop_at = atomic_load(&flt->estop) ? 1 : 0;
        if (atomic_exchange(&flt->rosc_now, 0)) { plant.cfg.pulse_sensor = true; plant.cfg.rosc_at_tick = 1; }
        if (atomic_exchange(&flt->stiff_chest, 0)) { plant.cfg.k1 = 11000; plant.cfg.k3 = 2.8e6; }

        elv_actuators_t a;
        if (ELV_SEQ_READ(&hw->aseq, &a, &hw->act) != 0) a = (elv_actuators_t){ 0 };

        /* hardware watchdog on the safety process heartbeat */
        uint64_t at = atomic_load(&hw->act_tick);
        if (at != last_act_tick) { last_act_tick = at; wd_age = 0; wd_tripped = false; }
        else if (++wd_age > wd_limit && last_act_tick) {
            if (!wd_tripped) { hw->hw_watchdog_trips++; wd_tripped = true;
                fprintf(stderr, "elv-plant: HW WATCHDOG: safety process silent, retracting\n"); }
            a = (elv_actuators_t){ .piston_target_m = 0.0, .piston_vmax_ms = ELV_RETRACT_V_MS,
                                   .charge_dump = true };
        }

        elv_plant_step(&plant, &a);

        elv_seq_wbegin(&hw->sseq);
        hw->sensors = plant.s;
        elv_seq_wend(&hw->sseq);

        if (plant.s.ecg_seq != last_ecg_seq) {
            last_ecg_seq = plant.s.ecg_seq;
            uint64_t n = atomic_load(&ecg->n);
            ecg->mv[n % ELV_ECG_RING] = plant.s.ecg_mv;
            atomic_store(&ecg->n, n + 1);
        }

        hw->true_depth_m = plant.x - plant.surf;
        hw->max_true_depth_m = plant.max_depth;
        hw->max_force_n = plant.max_force;
        hw->strokes = plant.strokes;
        hw->strokes_in_range = plant.strokes_in_range;
        hw->shocks_received = plant.shocks_received;
        hw->shocks_off_recoil = plant.shocks_while_not_parked;
    }
}
