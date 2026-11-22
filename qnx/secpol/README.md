# Elvrek security policy

The policy in [`elvrek.pol`](elvrek.pol) is enforced on the QNX 8.0 image (`mkqnximage --secpol=secure`). Every Elvrek process runs under its own type and the kernel denies anything the policy does not allow.

## How it was made

This follows the workflow in the QNX System Security Guide.

1. Build the image in recording mode: `bash qnx/image-policy.sh open` (mkqnximage `--secpol=open`). `secpolgenerate` watches every process.
2. Run the whole fault suite with each process under its type: `TYPES=1 bash qnx/suite-only.sh`.
3. Read `/dev/secpolgenerate/policy` from the target. That is the base of `elvrek.pol`.
4. Tighten by hand. The only edit is the advisor: while recording, its attack test tried `setuid(0)`, priority 65 and connections to the safety process, so the recorder saw those too. They are removed. The advisor may drop to uid 2002, run at priority 10 or below, and connect to the advice gate. Nothing else.
5. Compile and check: `secpolcompile -o elvrek.bin elvrek.pol` (the compiler refuses a policy where a type could gain rights by switching type).
6. Build the enforcing image: `bash qnx/image-policy.sh secure qnx/secpol/elvrek.pol`, then `TYPES=1 bash qnx/suite-only.sh`.

## Why there is an advice gate

On QNX a channel takes the security type of the process that creates it. If the safety process served the advice path itself, the advisor would need permission to connect to the same channel that serves the piston and the shock line. So advice goes through [`elv-advice-gate`](../../src/qnx/elv_advice_gate.c), a small process with its own type (`elv_gate_t`). The advisor may connect to the gate and nothing else; only the gate may connect to the safety process, on a root-only path (`/dev/elvrek/advice_core`).

| Type | May connect to | Notable rights |
|---|---|---|
| `elv_plant_t` | nothing | runs the hardware stand-in |
| `elvrekd_t` | the system logger | attaches `/dev/elvrek/*`, keeps a public channel |
| `elv_gate_t` | `elvrekd_t` | attaches `/dev/elvrek/advice`, drops to uid 2003 |
| `elv_super_t` | `elvrekd_t` (status) | spawns the advisor straight into `elv_advisor_t`, may kill it |
| `elv_advisor_t` | `elv_gate_t` only | drops to uid 2002, priority 10 or below |

## Results on QNX 8.0 ([raw output](../../docs/results/qnx-secpol-attack-ab.txt))

| Advisor attack | Refused |
|---|---|
| As designed: under its type, drops to uid 2002 | 7 of 7 |
| Kept root, under its type: only the policy stands in the way | 5 of 7 |
| The same root process without the type | 0 of 7 |

As root under its type, the kernel refuses every connection to the piston, the shock line, the operator and status devices, and any priority raise. Two things still get through for a root process, and we state them plainly: it can open the hardware shared memory (this policy does not govern shared memory objects), and `setuid(0)` trivially succeeds for a process that is already root. The real advisor drops root before it runs, which closes both.

The full fault suite passes 12 of 12 with the policy enforced and every process under its type ([raw output](../../docs/results/qnx-fault-suite-secpol-enforced-1.txt)).
