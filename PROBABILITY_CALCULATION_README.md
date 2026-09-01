# 128 Creation Probability Calculation for Seed Layouts

## Overview

This script calculates the probability of creating a 128-tile starting from each unique layout (8 protected tiles + 8 free cells) observed at turn 14500 across all 10 seeds.

The calculation is parallelized across 16 jobs to efficiently compute ~1,440 unique layouts.

## Files

1. **calc_layout_probabilities.py** - Single-job probability calculator
   - Reads `seed_top8_layout_frequencies.tsv`
   - Extracts unique layouts
   - Calls `exact_second_32768` for each layout
   - Outputs `job_N.tsv` with probabilities

2. **run_parallel_probability.sh** - Parallel execution orchestrator
   - Launches 16 parallel jobs
   - Waits for all to complete
   - Triggers result merging

3. **merge_probability_results.sh** - Result aggregator and weighted average calculator
   - Merges job results into single file
   - Computes seed-wise weighted averages
   - Computes classification-wise averages (straight vs block2x2)
   - Generates summary statistics

## Usage on 172.21.52.93

### Step 1: Prepare Environment

```bash
cd /path/to/2048_NewTypeTuple
git pull  # Get latest scripts

# Ensure exact_second_32768 binary is available
ls -l exact_second_32768
chmod +x exact_second_32768
```

### Step 2: Run Parallel Calculation

```bash
# Option 1: Using bash script (recommended)
bash run_parallel_probability.sh

# Option 2: Manual control with more options
mkdir -p probability_results
for job_id in {0..15}; do
    python3 calc_layout_probabilities.py \
        --input seed_top8_layout_frequencies.tsv \
        --output probability_results/job_${job_id}.tsv \
        --exact-bin ./exact_second_32768 \
        --job-id ${job_id} \
        --total-jobs 16 &
done
wait

# Merge results
bash merge_probability_results.sh probability_results
```

### Step 3: Monitor Progress

```bash
# Check job progress
watch -n 10 "ls -lh probability_results/job_*.tsv | wc -l"

# Check file sizes
ls -lh probability_results/

# Tail latest logs
tail -20 probability_results/job_*.tsv | head -50
```

### Step 4: Retrieve Results

After completion, the following files are generated:

- **probability_results/layout_probabilities_merged.tsv**
  - All unique layouts with their 128 creation probabilities
  - Columns: classification | layout_id | rank_cells | probability_128

- **probability_results/seed_weighted_average_128_probability.tsv**
  - Seed-wise weighted averages
  - Classification averages and statistics
  - Summary with difference and ratio

## Expected Output Format

### Seed-wise Results

```
seed    classification  probability_128
0       straight        0.7946596974148
1       block2x2        0.1993228222860
2       straight        0.7946596974148
...
```

### Classification Summary

```
Straight  (seeds 0,2,4,7,8,9): 79.47%
Block2x2  (seeds 1,3,5,6):     19.93%
Difference: 59.53 points
Ratio: 3.9868x
```

## Push Results to Git

Once calculations are complete:

```bash
# Copy results back to main machine
scp -r probability_results user@mainmachine:/path/to/2048_NewTypeTuple/

# Or on the main machine
cd /path/to/2048_NewTypeTuple
git add probability_results/
git add REMAINING_8_CELLS_RESULTS_FOR_SLIDES.md
git commit -m "Calculate 128 creation probability for seed layouts"
git push origin main
```

## Performance Notes

- **Compute Time**: ~30-60 minutes for full 16-parallel run
- **Memory Usage**: Each job uses <1GB; total <16GB peak
- **Storage**: Output files ~10MB total

## Troubleshooting

### Job Hangs or Times Out
- Check system load: `top -b -n 1 | head -20`
- Check disk space: `df -h`
- Re-run individual job manually for debugging

### exact_second_32768 Exits with Error
- Some layouts may be infeasible; 0.0 probability is recorded
- Check stderr output for specific layout failures

### Network Issues
- If running on remote machine, use `tmux` or `screen` to prevent interruption
- Or run inside a detached process manager

## Architecture

```
seed_top8_layout_frequencies.tsv (11.8K layouts)
    ↓
calc_layout_probabilities.py × 16 (parallel)
    ↓ (each writes job_N.tsv)
merge_probability_results.sh
    ↓
Seed-wise weighted averages
    ↓
seed_weighted_average_128_probability.tsv
```

Each job processes ~740 unique layouts independently, then results are merged and aggregated.
