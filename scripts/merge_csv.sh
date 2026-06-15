#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [output_file]"
    echo "  output_file  Optional. Default: logs/merged.csv"
    exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
fi

OUT=${1:-logs/merged.csv}

TMP=$(mktemp --tmpdir merge_csv.XXXXXX)
trap 'rm -f "$TMP"' EXIT

shopt -s nullglob

for f in logs/*.csv; do
    [ -e "$f" ] || continue
    lines=$(wc -l < "$f" || echo 0)
    if [ "$lines" -le 4 ]; then
        echo "Skipping $f (<=4 lines)" >&2
        continue
    fi
    keep=$((lines - 4))
    tail -n +4 "$f" | head -n "$keep" >> "$TMP"
done

mv "$TMP" "$OUT"
trap - EXIT

echo "Merged CSVs into $OUT" >&2
