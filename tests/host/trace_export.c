/*
 * Elvrek: export a recorded run of the real controller for the website.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs the same safety core, invariant monitor and advisor used everywhere
 * else against the physics plant (VF, one shock converts, perfusion returns),
 * and writes a compact JSON trace at 100 Hz from 105 s to 150 s. The site
 * plays it back; nothing on the page is invented.
 *
 * Usage: trace_export out.json
 */
#include <stdio.h>
#include <string.h>
#include "elv_core.h"
#include "elv_inv.h"
#include "elv_plant.h"
#include "elv_advisor.h"

static elv_core_t C;
static elv_plant_t P;
static elv_inv_t V;
static elv_advisor_t A;

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "trace.json";
    FILE *f = fopen(out, "w");
    if (!f) { perror(out); return 1; }

    elv_patient_cfg_t pc;
    memset(&pc, 0, sizeof(pc));
    pc.shock_converts = true;
    pc.rosc_delay_ticks = 20000;
    elv_core_init(&C);
    elv_inv_init(&V);
    elv_plant_init(&P, &pc, ELV_RHYTHM_SHOCKABLE);
    elv_advisor_init(&A, ELV_RATE_DEFAULT_CPM / 60.0f);

    elv_actuators_t act;
    memset(&act, 0, sizeof(act));
    uint64_t last_ecg = 0, seq = 0, press_since = 0;
    const uint64_t from = 105000, to = 150000;
    int first = 1;

    fprintf(f, "{\"source\":\"Elvrek controller, host build, physics plant (not a patient)\","
               "\"hz\":100,\"start_s\":105,\"fields\":[\"depth_mm\",\"force_n\",\"ecg_mv\","
               "\"state\",\"shock\",\"vent\",\"etco2\"],\"states\":[");
    for (int s = 0; s <= ELV_ST_STOPPED; s++)
        fprintf(f, "%s\"%s\"", s ? "," : "", elv_state_name((elv_state_t)s));
    fprintf(f, "],\"rows\":[");

    for (uint64_t t = 1; t <= to; t++) {
        elv_plant_step(&P, &act);
        const elv_sensors_t *s = &P.s;
        elv_advisor_msg_t m, *pm = NULL;
        if (s->ecg_seq != last_ecg) {
            last_ecg = s->ecg_seq;
            elv_advisor_push(&A, s->ecg_mv);
            if (s->ecg_seq % ELV_ECG_HZ == 0 && elv_advisor_classify(&A, &m)) {
                m.seq = ++seq;
                pm = &m;
            }
        }
        elv_operator_t op;
        memset(&op, 0, sizeof(op));
        op.start = t == 1000;
        if (C.state == ELV_ST_SHOCK_WINDOW && C.sw_phase == ELV_SW_AWAIT_PRESS) {
            if (!press_since) press_since = t;
            op.shock_press = t - press_since >= 300;
        } else press_since = 0;
        elv_core_step(&C, s, pm, &op, &act);
        (void)elv_inv_check(&V, &C, s, &act);

        if (t >= from && t % 10 == 0) {
            double depth = (P.x - P.surf) * 1000.0;
            if (depth < 0) depth = 0;
            fprintf(f, "%s[%.1f,%.0f,%.3f,%d,%d,%d,%.1f]", first ? "" : ",",
                    depth, s->force_n, s->ecg_mv, (int)C.state, act.shock_line ? 1 : 0,
                    act.vent_on ? 1 : 0, s->etco2_mmhg);
            first = 0;
        }
    }
    fprintf(f, "],\"summary\":{\"shocks\":%u,\"hands_off_ticks\":%u,\"machine_ticks\":%u,"
               "\"human_ticks\":%u,\"ccf\":%.3f,\"invariant_violations\":%u}}\n",
            C.m.shocks, C.m.max_window_ticks, C.m.max_machine_ticks, C.m.max_human_ticks,
            elv_core_ccf(&C), elv_inv_total(&V));
    fclose(f);
    printf("wrote %s: shocks %u, hands-off %u ticks (machine %u, human %u), CCF %.3f, violations %u\n",
           out, C.m.shocks, C.m.max_window_ticks, C.m.max_machine_ticks, C.m.max_human_ticks,
           elv_core_ccf(&C), elv_inv_total(&V));
    return 0;
}
