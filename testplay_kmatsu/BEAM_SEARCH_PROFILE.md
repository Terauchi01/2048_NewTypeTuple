# Beam Search bottleneck profile

Run a same-process profile of the baseline and separate fast implementation:

```sh
bash testplay_kmatsu/profile_beam_search.sh tuples6-seed0-VSE-count004.dat
```

Defaults are depth 5, fixed width 256 and five measured repetitions after a
warm-up pair. Set `DEPTH`, `WIDTH`, `REPEATS` or `RESULT_DIR` to change them.
The evaluator callback is timed once per 256 calls to limit measurement
overhead; its estimated time is subtracted from total search time to estimate
the remaining search work. The final `selection` row separately compares full
stable sorting with `nth_element` plus sorting the retained prefix. That row
uses synthetic candidates sized from the actual search's average generated
candidates per layer, so it isolates selection cost rather than claiming an
exact whole-search percentage.

The result file is saved under `testplay_kmatsu/results/profile_beam_<timestamp>/profile.csv`.
