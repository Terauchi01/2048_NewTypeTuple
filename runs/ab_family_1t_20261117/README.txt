Current campaign documentation — verified 2026-11-17 (Europe/Amsterdam)
======================================================================
The canonical local code/config/SHA map is ../../docs/PUBLICATION_CODE_MAP.md
(from this directory). These workspace paths are not published GitHub links.
The startup controller now accepts an omitted external archive manifest.
Existing jobs and protected historical inputs are preserved.

Actual launched allocation (manifest.json; runs/<task>/config.json):
 A x3, seeds0/1/2, i02/i03/i10: float32, paper TC-UCB, normal starts.
 B x3, seeds0/1/2, i05/i04/i11: float32, greedy, archive request50%, no
   extra source-afterstate update.
 Bprime (B′) x3, seeds0/1/2, i06/i08/i12: B plus source-afterstate update.
 Bdoubleprime (B″) x1, seed0, i07: greedy float32, targets archive-origin
   cumulative learning steps50%; no extra source update. This is not a
   fixed50% request probability.
 D x1, seed0, i09: float64, source TC-UCB, normal starts.
No C task exists here. ../nt9_1t_20261117/README.md is earlier preparation /
dry-run material containing C, not this campaign's launched conditions.
A versus D changes both precision and UCB formula. D is not a normal-start
greedy control and this is not a precision-only comparison.

All11: fresh,15 workers/CPUs0-14,32MiB feature cache per worker, async model,
greedy-max teacher, legacy TC0.5 post-update coherence, restart limit10,
1T target, first save50B then every50B, retention disabled, TC reset50B.
A/D: c=.0065, warmup=0, decay_end=0 (no decay). B family is greedy even
though formula/c fields are present. Exact UCB formulas are in the code map.

Pinned releases (do not substitute working-tree C++ files):
 A/B family: ../fullfloat_ttas_saveable_20261116/source/
 source-manifest SHA256:
  76428313e6c6728559db14ee84225dca15b2c00cf29bec8103ca3b811e2db11c
 binary build/bin/learn9_ttas SHA256:
  d04836898c652a7eb3213298aec399ea5b43c76befeff39b3213ecc80231eabd
 All10 A/B-family tasks use this same binary with method flags.
 D: ../d64_speed_20261117/source/
 source-manifest SHA256:
  932a3c64d9f43038758cd263f9dd3780e099f7111a695625828f55bbe9ac79d2
 binary build/bin/learn9_ttas64 SHA256:
  c95dc08e86e410c95590462f3b7ab8155da8ccc168e02c0852832ee6e7501ba0
Each release has source_manifest.sha256 and BUILD9.json. Source SHA is the
hash of that source manifest, not a Git commit ID. Manifest/config hashes,
all86/87 listed source files and both binaries were checked for agreement.
runtime/controller.py:command generates argv; pinned C++ CLI resolves the
archive-condition flags. manifest.runtime_hashes includes3 external helpers;
the code map lists these small publication dependencies separately from
large models/logs. During this audit, an external process changed manifest
and controller: controller SHA changed from
 c8a73987979764422f5fad622c1edcb2ef54aa5068eeef818e38dceb99f6a0ed
to the rechecked manifest/file SHA
 5363cf7a64f598deffe3970e487ab38e8d074c20c768a9b468442bfe4840864b.
Training source/binary/task conditions still agree. The relation to the
runtime loaded at launch is unverified; these files were not edited here. No add/commit/push or publication was performed.

Archive conditions: random128, initial donor train_manifest as pinned in
manifest.json; first normal-original16k/32k/64k afterstates supply subsequent
sources, latest pending source per stage.128 uses per source, each including
original+restarts; independent9:1 spawn each start. Complete128 claims before
switching; starvation/busy batch falls back to normal. Promotion uses the
complete1000-normal-original window's32k count: >=200 for16k->32k, >=501
for32k->64k (not64k reach rate). Details/fallbacks are in the code map.
Request probability, realized archive starts and archive-origin step share
are distinct. Inspect experience_allocation.csv and episode/source logs;
no latest realized percentage is asserted by this documentation audit.
Bprime source update is separately counted, not added to global steps.

Current graph definitions: GRAPHING.txt and graphs/sampling_report.json.
Score/reach use ALL complete logged normal-start original games, including
normal fallback: exact append-only counts/sums in100M-step cache bins.
Archive/restart-only outcomes and donor score are excluded. The old2000-game
cap and byte sampling do NOT apply to score/reach. Progress/speed still use
bounded byte-offset observations plus64KiB tail, default max-points2000.
Training observations are not independent fixed-weight evaluation. Missing
or unreached time points remain missing, not zero; observed reached=0 with
positive normal-game denominator is a measured0%. See the code map for
smoothing, thresholds, historical guides, output/cache and log locations.

Current host authority: latest ../../AGENTS.md and SERVER_USAGE_POLICY.json;
i02..i12 only, graph refresh i02 CPU15/nice19. An old temporary i14/i15 entry
is not current permission. No graph redraw or SSH monitoring was needed.
../../tools/experiment_status is legacy-only; do not run its old config.

Verified here: stored configuration, source/binary provenance and aggregation
definitions. Not verified here: current process liveness, latest progress or
realized archive ratios,1T completion, current fixed-weight independent
results, clean-checkout build or full9 D resume. Existing evidence below
reports D first-save verification, but full9 resume remains incomplete.

Historical operations and evolving graph notes (2026-11-17)
==========================================================
The unchanged text below records earlier instructions and observations.
In particular, sampled-score/2000-normal-game sections are superseded by
GRAPHING.txt and the current definitions above. Historical host placements
or commands are not authorization to execute them now. No commands below
were executed during this documentation update.

Current graph instructions: GRAPHING.txt. Score/reach now use exact append-only
normal-game aggregation; older sampling notes below are historical.

Campaign: A x3, B x3, Bprime x3, Bdoubleprime x1, D x1

User authorization: 15 workers/run, target 1T, checkpoint every 50B,
no old November return deadline. BEATRIS co-location explicitly authorized;
foreign processes and their affinity are not modified.

Placement: A i02/i03/i10; B i05/i04/i11; Bprime i06/i08/i12;
Bdoubleprime i07; D i09. Seeds 0/1/2 in the listed order.
CPU affinity: logical CPUs 0-14 (15 workers; hosts have 8 physical cores,
16 SMT logical CPUs). This does not guarantee exclusive physical cores.

A/B family float32 TTAS; D float64 TTAS64. Source/binary/runtime digests
are fixed in manifest.json. A uses paper UCB; D uses source UCB;
c=.0065, warmup=0, decay_end=0. B family is greedy with archive request
50%; Bdoubleprime targets cumulative learning steps. Early archive supply
may be insufficient. Start fresh; no old model or checkpoint is modified.

First scheduled checkpoint 50B, then every 50B through 1T (20 generations).
Retention disabled: all these checkpoints are preserved. Shared native I/O
lock serializes checkpoints. Estimated retained model data 4.38 TiB.

Control from project root:
  python3 runs/ab_family_1t_20261117/control.py status
  python3 runs/ab_family_1t_20261117/control.py pause --task A_seed00
Omit --task to request all runs to save and stop. Pause files are durable.
Do not run start twice. Claims/output/local-control directories reject reuse.

Controllers and independent watchdogs survive SSH disconnection. Operator
pause, low RAM/disk, sustained swap, and controller loss request native save
and stop via identity-checked local request files. Never TERM/KILL learners.
64 MiB global swap increase is recorded as a warning, not a forced stop.
Native save or worker exceptions can still exit without saving; wrappers
cannot guarantee recovery from native failure. Checkpoints commit with
model/driver SHA256; terminal save verification includes both checksums.
D full9 first save independently verified earlier; full9 resume validation
remains incomplete (previous validation resource guards interrupted it).

Old 1T data and ancestors remain protected. No deletion was performed.

Subsequent explicit user request: BEATRIS CPU placement changed on
 i03/i04/i08/i10/i11/i12. beatris_affinity.py pins all threads of the exact
 BEATRIS tta_duel_branch_match executable to logical CPU15, including new
 matches every five seconds. Before/after masks, PID/starttick are in
 affinity/<host>.jsonl. It does not signal, stop, or modify model data.
 Two BEATRIS processes on the same host share that one logical CPU.
 Stop the affinity monitor by creating affinity/<host>.stop; stopping
 monitoring alone does not restore already-applied affinity masks.
 New matching processes may briefly run on other CPUs before the next scan.

Progress graphs (manual refresh, no training restart needed)
-----------------------------------------------------------
From project root, run on the permitted compute host:

ssh i02 'cd /home2/matsu-lab2/terauchi/2048_NewTypeTuple && MPLCONFIGDIR=runs/ab_family_1t_20261117/plot_cache taskset -c 15 nice -n 19 runs/ab_family_1t_20261117/plot_venv/bin/python runs/ab_family_1t_20261117/plot_progress.py --output-dir runs/ab_family_1t_20261117/graphs'

Outputs:
 graphs/progress.svg: completed learning steps vs training elapsed hours.
 graphs/speed.svg: learning steps/s over wide time windows (at least60s).
 graphs/score.svg: smoothed normal-start training score vs learning steps.
 graphs/metrics.csv: plotted observations, score sample counts, speed windows.
 graphs/sampling_report.json: sampling details, input sizes, source/manifest
 hashes, missing/empty/unflushed run diagnostics and metric definitions.

Same method uses same color; seeds use different line styles. The default
normal-start sample target is2000 per run; --max-points changes that target.
Byte-offset sampling increases its total draw budget when normal starts are
insufficient. CSV files are never modified; large files use bounded reads.
Incomplete live trailing rows are excluded. Progress is a buffered lower bound.
Score uses only start_kind=empty original_score, including normal fallbacks;
archive continuation scores and donor-carried scores are excluded. These are
sampled training scores, not independent evaluation or exact episode averages.
Byte sampling and changing episode lengths can bias score means. Elapsed
excludes model initialization and downtime/reload when resumed, but includes
checkpoint waits/save after learning starts. No PNG is generated.

Score curves use a centered moving average over up to 11 nonempty step bins
(five neighbors on either side), weighted by each bin's sampled episode count.
At the edges only available neighbors are used. metrics.csv keeps the raw
score_mean alongside score_moving_average, smoothing_sampled_episodes and
smoothing_start/end_steps; sampling_report.json records the smoothing method.
This is retrospective smoothing: later samples can revise earlier curve values.

The isolated plot_venv contains matplotlib; requirements_plot.txt records
installed dependency versions. Plot execution checks compute/graph-refresh hosts
and requires CPU15 affinity so it does not occupy learner CPUs0-14.

Automatic refresh (15 minutes after each completed refresh)
---------------------------------------------------------
refresh_graphs.py runs on i02, CPU15, nice19, detached from SSH. It updates
the same graphs immediately and then every 15 minutes, with a shared lock
preventing duplicate refresher loops. It does not restart training or read
model/checkpoint files. A plotting error stops the loop and records failure.
Reboot does not automatically restart it.
The latest user instruction on 2026-11-17 moved graph refresh back from
i15 to the available CPU15 on i02. General compute hosts remain i02..i12;
SERVER_USAGE_POLICY.json assigns automatic graph refresh to i02 only.

Status: graphs/refresh.status.json (host/PID, last duration, child peak RSS,
next refresh epoch); output/errors: graphs/refresh.log.
Child peak RSS is the maximum across refreshes since this loop started.
Initial automatic refresh on i02 before relocation: 3.34 seconds, 96.2 MiB
child peak RSS.
Stop gracefully from project root (current refresh finishes first):
  touch runs/ab_family_1t_20261117/graphs/refresh.stop

To start after checking host policy, CPU15 usage and free resources on i02:
  ssh i02 'cd /home2/matsu-lab2/terauchi/2048_NewTypeTuple && nohup taskset -c 15 nice -n 19 python3 -u runs/ab_family_1t_20261117/refresh_graphs.py >> runs/ab_family_1t_20261117/graphs/refresh.log 2>&1 < /dev/null &'
For a deliberate restart after stopping, remove only graphs/refresh.stop
before starting. Do not launch the manual refresh while the loop is updating.

Post-start load observation for B seed1 (i04)
-------------------------------------------
observe_load.py is an independent read-only observer, pinned to CPU15 with
nice19. Every 10 seconds after the previous sample it records learner and
BEATRIS CPU counters, worker scheduler runtime/runqueue wait and wchan,
CPU frequency snapshots, temperatures and thermal-throttle counters, host
CPU/memory/IO pressure, memory/swap counters, and the last complete episode
row (bounded 64KiB tail). No learner/controller or BEATRIS settings change.
It verifies host policy, learner executable/arguments and PID/starttick.
An exclusive lock prevents duplicate observers. Worker exits during a sample
are reported in vanished_threads; other read failures stop with failed status.

Data: load_observation/B_seed01.jsonl (append only).
Status: load_observation/B_seed01.status.json.
Errors: load_observation/B_seed01.observer.log.
Stop only this observer by creating load_observation/B_seed01.stop.
Reboot does not restart it. Manual launch after checking i04 resources:
  ssh i04 'cd /home2/matsu-lab2/terauchi/2048_NewTypeTuple && nohup taskset -c 15 python3 -u runs/ab_family_1t_20261117/observe_load.py >> runs/ab_family_1t_20261117/load_observation/B_seed01.observer.log 2>&1 < /dev/null &'

The start event records SC_CLK_TCK. CPU percentage is delta ticks divided
by SC_CLK_TCK and monotonic sample interval, times 100 (100% = one logical
CPU). schedstat_ns contains runtime, runqueue wait (nanoseconds), dispatch
count. Compare deltas only for the same PID/TID and starttick. Frequency
is a snapshot, not an interval average. Throttle counters are cumulative;
use their increments, not absolute values, to identify new throttling.
Epoch/sample_start_epoch bound the read interval. Episode data is buffered:
its training elapsed is not exactly the sample epoch. Controller elapsed
also starts before native initialization finishes and has a different origin.
Use paired steps/episode elapsed for speed over at least 60 seconds and
sample epoch for load; allow for CSV buffering when comparing transitions.
observer_cpu_seconds/read_duration_seconds record observer overhead.
The shared-file mtime was ahead of i04 sample epoch during verification;
do not use mtime minus sample epoch as a staleness measurement. Compare
successive observed episode steps/elapsed instead.

During the speed investigation, observation was extended to include all
process CPU counters (including root kernel threads), individual process
read timestamps, and ancestor cgroup CPU quotas/throttle counters. The
observer was restarted independently; earlier samples remain in the same
append-only log. analyze_load.py summarizes each observer session into
>=60-second windows without crossing observer restart boundaries:
  ssh i04 'cd /home2/matsu-lab2/terauchi/2048_NewTypeTuple && taskset -c 15 nice -n 19 python3 runs/ab_family_1t_20261117/analyze_load.py'
It writes load_observation/B_seed01.windows.csv and prints the same data.
Other-process percentages use each process's own read-to-read interval.

16k/32k reach-rate graphs
------------------------
 graphs/reach16k.svg: sampled normal-start probability of reaching >=16384.
 graphs/reach32k.svg: sampled normal-start probability of reaching >=32768.
Both use the first game's original_max_tile_exponent, before any restart,
with start_kind=empty only (includes normal fallbacks, excludes archive starts).
32k/64k outcomes also count as16k;64k also counts as32k. Denominators are
sampled completed normal-start games, not all episodes or decision samples.
Metrics store threshold_exponent, reached_episodes, sampled_episodes and
reach_rate (0..1); graph y axes show0..100%. Fixed step bins match score bins.
Reach-rate curves use raw bin rates, without score's centered smoothing;
points are at each bin's last observed learning step. Small samples can be
noisy and byte sampling can bias estimates. These are training observations,
not independent evaluation results. The existing15-minute refresh loop picks
up both additional SVGs automatically; its restart is unnecessary.

16k / 32k reach-rate graphs
--------------------------
The same refresh command now also generates graphs/reach16k.svg and
 graphs/reach32k.svg. Existing 15-minute refresh picks up these additions
 automatically; no learner or refresh-process restart is needed.

Definition: among sampled normal-start episodes (start_kind=empty,
 including normal fallbacks), the original game's maximum tile exponent
 is >=14 for16k and >=15 for32k.32k and higher also count toward16k.
 Archive starts and restart-only maxima are excluded. These are sampled
 training-game reach rates, not independent evaluation probabilities.

X: completed learning steps; Y:0–100%. Each point is the raw rate in
 the same fixed step bin used by score, placed at its last sampled step.
 No smoothing or later-bin results are mixed into these reach-rate plots.
 Small sample bins can fluctuate strongly, especially Bdoubleprime.
 metrics.csv records threshold_exponent, reached_episodes,
 sampled_episodes and reach_rate (0–1).sampling_report.json records the
 input sampling policy and limitations. Rerunning updates all5 SVGs.

Reach plots updated:16k/32k/64k
------------------------------
Reach plots now use5x the score bin width (roughly10 intervals over the
current history), pooling raw reached/total sample counts in each bin.
Reach markers are removed and lines thickened. The score plot is unchanged.
This replaces the earlier statement that reach and score share bin widths.

 graphs/reach64k.svg is also generated: normal-start original maximum
 tile exponent>=16 (65536 or higher). These games also count toward32k
 and16k. metrics.csv includes normal_training_reach64k_sample_rate.
 The existing15-minute refresh updates all6 SVGs without a restart.

Adaptive normal-start graph sampling
------------------------------------
All methods now target2000 normal-start(start_kind=empty) games per refresh.
The initial total byte-offset budget equals the target, then grows1.5x per
round until enough normal starts are found. The previous Bdoubleprime-only
10x override is removed. If enough are found, choose exactly the target
number of distinct normal games evenly across the sampled chronology,
including its final normal game. Score and16k/32k/64k rates use this selection;
progress and speed retain the broader final-round sample.

Do not duplicate games to fill an insufficient target. Missing/unflushed
inputs, all available complete games exhausted, or safety limits produce
an explicit shortfall with the real found/used count. Per-run bounds are
64000 draw positions and256MiB cumulative measured reads per refresh.
The sampling report records rounds, actual budgets, found/used normal
counts, target attainment and shortfall reasons. The existing15-minute
refresher reads this implementation automatically. Learners are unchanged.

外部archiveデータは新規起動設定では任意。archive_manifest / archive_manifest_sha256 を両方省略すると、random128で通常ゲームからオンライン取得する。明示時は絶対パスとSHA256を両方指定する。省略起動には runs/online_archive_optional_20261117 の対応binary/sourceをtaskに指定する必要がある。現在のmanifestにある既存runの外部データ参照・binaryは保持し、稼働jobの再起動や変更は行わない。
