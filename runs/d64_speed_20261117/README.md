# D double source-UCB: TTAS64 implementation and verification

2026-11-17: all3/full9 learner and benchmark builds and the double15-writer
probe builds completed on i06 CPUs0-3. `BUILD_COMPLETE.json` pins11 binaries
and confirms unchanged source. The subsequent i06 fixture1/15worker,
same-feature15-writer and shared-stripe/paused-owner probes all passed;
`fixture_verification/RESULT.json` records commands, hashes and resource samples.
Minimum observed host MemAvailable was123.199GiB and swapout did not increase.
These are reduced-fixture checks. Full9 performance/save-resume and long-term
learning quality are separate checks and are not established by this result.

## Completed full9 speed measurements

`benchmark/RESULT.json` records i06, 15 workers, fresh seed7, source UCB,
180B decay, no archive, cache16, sample100000, and15M steps per trial.
The futex8/TTAS64 pair uses the same source with reversed trial order.

| variant | steps/s, trial1 | steps/s, trial2 | median |
|---|---:|---:|---:|
| futex8 | 449260 | 458490 | 453875 |
| TTAS64 | 482136 | 478346 | 480241 |

TTAS64 improved this short paired comparison by5.81%. The old D native
driver measured268366steps/s once, with final checkpoint commit and independent
SHA verification. That old/new comparison also changes layout, indexing, THP
and driver instrumentation, so it does not isolate the lock effect. Initial
THP coverage varied between trials. These are short learning-wave speeds,
excluding initialization/save; they do not establish mature-model performance
or long-term score quality. Additional optimization experiments were stopped
at the user's request; the retained D implementation is TTAS64.

## Full9 persistence status

The i09 attempt stopped during its first save under the resource guard;
the old runner did not record the triggering snapshot, so the exact resource
condition is unknown. `native_validation/FAILED.json` preserves that failure.

The separate i02 attempt reached1,006,112steps, but stopped during the final
save when host swapout increased65.21MiB, above the unchanged64MiB guard.
Effective RAM headroom was76.91GiB and free disk208.35GiB at that point.
See `native_validation_attempt2/FAILED.json` and per-second resource telemetry.
The earlier11,495step checkpoint was committed and subsequently passed an
independent model/driver SHA and file-stability audit in
`native_validation_attempt2/FIRST_CHECKPOINT_READONLY_AUDIT.json`.

Full9 same-binary resume was not reached and remains unverified. The final
uncommitted save is not a resumable checkpoint. Both failures and their
artifacts remain preserved; no further retry was performed. These persistence
checks used the historical180B decay setting, not the new constant coefficient.

This is a new isolated source and binary identity, not a migration of any
previous checkpoint. D here means float64 throughout W/E/A, TC, evaluation,
TD and UCB; source UCB, coefficient0.0065, no archive, greedy-max
teacher, TC0.5 post-update,10 restarts, async, cache16MiB/worker. No float32
storage macro is set. Integer counters and indices retain their types.

Latest user instruction: future fresh D commands keep c=0.0065 constant with
`--warmup-steps 0 --decay-end-steps 0`. Here0 disables the step decay; it does
not set c=0. `plan_trial.py` now generates this fixed-c setting. All completed
fixture, benchmark and native-validation records used180B decay and remain
unchanged historical evidence. Fixed-c speed and long-term quality are not
established by those measurements. Changing the schedule changes checkpoint
configuration identity; do not relabel an old decaying run for full resume.
The fixed-c launcher and float32/float64 coefficient-function checks passed;
see `../ucb_fixed_c_20261117/RESULT.json`. This was configuration/unit
verification, not a new learning run.

`prepare.py` starts from the already verified precision50 generic source.
Only `head_double/exploration_concurrency.h` changes: it adds the existing
double-A/TTAS lock branch and lock identity, preserving the baseline's futex
spin8 branch. The numerical update/atomic tail and all other source files
are byte-identical to precision50. See `PREPARATION.json`,
`source_manifest.sha256`, and `concurrency.patch`.

The existing double-A source was compared first. Its numeric implementation
is the same but storage is double-only and its futex branch is fixed-spin32.
Starting from precision50 therefore needs one changed header, preserves the
requested futex8 control, and avoids copying any float-only layout.
Both new variants use the same generic AoS32 layout, delta indexing,
read-ahead32, existing update-ahead16, compiler flags and source identity.
Only lock-selection defines differ. Every translation unit uses its variant's
same flags. TTAS64 is nonfair; it does not guarantee bounded waiting or owner
progress. The short speed improvement above does not establish long-term
performance or quality equivalence.

## Build procedure (completed)

Parent-assigned build host is i06, CPUs0-3. Recheck host identity, other jobs,
CPU load, RAM and disk immediately before using it. No compilation on gw.
`build.py` defaults to printing commands and verifying the source manifest.
Actual compilation additionally requires `--run` and rejects other hosts or
CPU affinities. All outputs are isolated and existing object trees are refused.

```
python3 runs/d64_speed_20261117/build.py --digits 3
taskset -c 0-3 python3 runs/d64_speed_20261117/build.py --digits 3 --run
taskset -c 0-3 python3 runs/d64_speed_20261117/build.py --digits 9 --run
taskset -c 0-3 python3 runs/d64_speed_20261117/build_checks.py
```

Required outputs: learn3/bench3/learn9/bench9, each futex8 and ttas64.
BUILD3/BUILD9.json will pin actual compiler, source, complete commands and
binary hashes after successful builds. No binary hash exists before building.
The optional bench reuses the unmodified existing bench_driver.cpp; its
20M cap and no-resume/no-checkpoint semantics remain in effect.

## Verification before performance

`verify_fixture.py` exercises source UCB and no archive with real 3-digit
learners. At1worker it compares all four logical checkpoint fields while
masking only the two known header identity hashes; episodes and decisions
must match. It checks same-binary save/resume, unchanged original checkpoint,
finite CSV values, exact double/config identity and cross-lock resume rejection.
At15workers it checks complete save/resume and hashes, without trajectory
equality. The script is restricted to assigned i06 until explicitly revised
following a new host allocation. Outputs must be new directories.

```
taskset -c 0 python3 runs/d64_speed_20261117/verify_fixture.py --output /tmp/d64_fixture_1w
taskset -c 0-14 python3 runs/d64_speed_20261117/verify_fixture.py --threads 15 --output /tmp/d64_fixture_15w
timeout 120s taskset -c 0-14 runs/d64_speed_20261117/build/bin/hot_writers_futex8
timeout 120s taskset -c 0-14 runs/d64_speed_20261117/build/bin/hot_writers_ttas64
timeout 120s taskset -c 0-14 runs/d64_speed_20261117/build/bin/lock_fixture_ttas64
```

The existing15-writer tests were adapted only to double values, double
increments and sizeof(double). Provenance is in tests/PROVENANCE.json.
They cover repeated same-feature updates, distinct-feature/shared-stripe
mutual exclusion and a paused owner/yield. They do not prove all schedules.
Further full9 save/resume and checkpoint SHA/config audits are required before
calling the candidate ready for long training. Actual50B reset,1T boundaries
and score quality remain separate tests.

## Same-host speed comparison

`plan_trial.py` prints pinned paths/hashes and D commands; it never launches.
Use `old_d` for precision10 native double (`learn_float64`), `precision50`
for existing saved futex8 AoS32 double, and `futex8`/`ttas64` for the new pair.
For a fair three-way comparison use real learners, equal seed/steps/15workers,
fresh no-archive starts, first checkpoint at the target, and reversed trial
order. Their wave_complete compute timing excludes initialization and save
and includes in-wave CSV. Optional --bench is available only for the new pair.

Execute only through a parent-reviewed guarded runner, after host/CPU/RAM/
cgroup/disk/swap checks and with own-process watchdog and timeout. Double AoS
is about46.3GiB live and compact model36.17GiB; staged restore plus margin needs
about94.5GiB. A full checkpoint requires sufficient separate disk space.
Existing source/binary/config identities must never be relabeled for resume.

Historical double profiles identify TC update/lock as a candidate bottleneck;
those were not this exact D15worker condition. A new15worker source-UCB
diagnostic measured55.04% of exclusive compute thread-wall time in TC updates
including locks,19.19% in forward W/A reads, and13.73% in backward W reads.
See `../d64_profile_source_20261117/RESULT.json`. Its source is a separate
instrumented old-D-like derivative, not the exact old release; its times
include waits/descheduling and do not measure pure lock contention.
