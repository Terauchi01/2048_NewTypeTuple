#!/usr/bin/env bash
set -euo pipefail

root=${RESULT_ROOT:-results_probability_128}
printf 'condition\taliases\tstates\tcomplete\tpossible\tprobability\tlower\tupper\tpercent\tinterval_width\twall_seconds\tpeak_rss_kb\n'

while IFS=$'\t' read -r _group condition _cells aliases; do
  result="$root/$condition/result.txt"
  resources="$root/$condition/resources.txt"
  [[ -f "$result" ]] || continue
  field() {
    awk -F= -v key="$1" '$1==key {print $2}' "$result"
  }
  resource() {
    if [[ -f "$resources" ]]; then
      awk -F= -v key="$1" '$1==key {print $2}' "$resources"
    fi
  }
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$condition" "$aliases" "$(field states)" "$(field complete)" \
    "$(field possible)" "$(field optimal_success_probability)" \
    "$(field optimal_success_probability_lower)" \
    "$(field optimal_success_probability_upper)" \
    "$(field optimal_success_percent)" "$(field probability_interval_width)" \
    "$(resource wall_seconds)" "$(resource peak_rss_kb)"
done < exact_layout_groups.tsv
