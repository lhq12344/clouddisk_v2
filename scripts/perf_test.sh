#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./scripts/perf_test.sh -u <url> [-m METHOD] [-d SECONDS] [-q QPS] [-c CONCURRENCY] [-H HEADER] [-p DATA_FILE]

Options:
  -u URL            Target URL (required)
  -m METHOD         HTTP method (default: GET)
  -d SECONDS        Duration in seconds (default: 10)
  -q QPS            Target QPS (default: 50)
  -c CONCURRENCY    Number of parallel workers (default: 10)
  -H HEADER         Extra header, can be repeated (e.g., -H 'Authorization: Bearer xxx')
  -p DATA_FILE      Payload file (for POST/PUT/PATCH)

Example:
  ./scripts/perf_test.sh -u http://localhost:8080/health -d 15 -q 200 -c 20
  ./scripts/perf_test.sh -u http://localhost:8080/api/upload -m POST -p payload.json -H 'Content-Type: application/json'
USAGE
}

URL=""
METHOD="GET"
DURATION=10
QPS=50
CONCURRENCY=10
PAYLOAD=""
HEADERS=()

while getopts ":u:m:d:q:c:H:p:h" opt; do
  case "$opt" in
    u) URL="$OPTARG";;
    m) METHOD="$OPTARG";;
    d) DURATION="$OPTARG";;
    q) QPS="$OPTARG";;
    c) CONCURRENCY="$OPTARG";;
    H) HEADERS+=("$OPTARG");;
    p) PAYLOAD="$OPTARG";;
    h) usage; exit 0;;
    *) usage; exit 1;;
  esac
done

if [[ -z "$URL" ]]; then
  echo "Error: URL is required."
  usage
  exit 1
fi

TOTAL=$((QPS * DURATION))
if [[ $TOTAL -le 0 ]]; then
  echo "Error: QPS * duration must be > 0"
  exit 1
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
LAT_FILE="$TMP_DIR/latency_ms.txt"
STATUS_FILE="$TMP_DIR/status.txt"

build_curl_cmd() {
  local cmd=(curl -s -o /dev/null -w "%{http_code} %{time_total}\n" -X "$METHOD")
  for h in "${HEADERS[@]}"; do
    cmd+=(-H "$h")
  done
  if [[ -n "$PAYLOAD" ]]; then
    cmd+=(--data-binary "@$PAYLOAD")
  fi
  cmd+=("$URL")
  printf '%q ' "${cmd[@]}"
}

CURL_CMD=$(build_curl_cmd)

worker() {
  local count="$1"
  for ((i=0; i<count; i++)); do
    eval "$CURL_CMD" >> "$STATUS_FILE"
  done
}

REQ_PER_WORKER=$((TOTAL / CONCURRENCY))
REMAINDER=$((TOTAL % CONCURRENCY))

START_TS=$(date +%s%N)

pids=()
for ((w=0; w<CONCURRENCY; w++)); do
  reqs=$REQ_PER_WORKER
  if [[ $w -lt $REMAINDER ]]; then
    reqs=$((reqs + 1))
  fi
  worker "$reqs" &
  pids+=("$!")
  # 简单节流以接近目标 QPS
  sleep "$(awk -v qps="$QPS" 'BEGIN{printf "%.6f", 1/qps}')"
done

for pid in "${pids[@]}"; do
  wait "$pid"
done

END_TS=$(date +%s%N)
ELAPSED_MS=$(( (END_TS - START_TS) / 1000000 ))

awk '{print $1}' "$STATUS_FILE" | sort | uniq -c > "$TMP_DIR/status_summary.txt"
awk '{print $2*1000}' "$STATUS_FILE" > "$LAT_FILE"

python3 - <<'PY'
import os, statistics

lat_file = os.environ['LAT_FILE']
status_file = os.environ['STATUS_FILE']

def percentile(sorted_vals, pct):
    if not sorted_vals:
        return 0
    k = (len(sorted_vals)-1) * (pct/100.0)
    f = int(k)
    c = min(f+1, len(sorted_vals)-1)
    if f == c:
        return sorted_vals[f]
    return sorted_vals[f] + (sorted_vals[c]-sorted_vals[f]) * (k - f)

with open(lat_file) as f:
    vals = [float(x.strip()) for x in f if x.strip()]

with open(status_file) as f:
    statuses = [line.split()[0] for line in f if line.strip()]

vals.sort()

print("\n=== Performance Summary ===")
print(f"Requests: {len(vals)}")
print(f"Latency ms: avg={statistics.mean(vals):.2f} p50={percentile(vals,50):.2f} p90={percentile(vals,90):.2f} p99={percentile(vals,99):.2f}")
from collections import Counter
counts = Counter(statuses)
print("Status codes:")
for code, cnt in sorted(counts.items()):
    print(f"  {code}: {cnt}")
PY

TOTAL_REQ=$(wc -l < "$STATUS_FILE")
if [[ $ELAPSED_MS -gt 0 ]]; then
  ACTUAL_QPS=$(awk -v total="$TOTAL_REQ" -v ms="$ELAPSED_MS" 'BEGIN{printf "%.2f", total/(ms/1000)}')
else
  ACTUAL_QPS="0"
fi

TOTAL_MB=$(awk -v total="$TOTAL_REQ" 'BEGIN{printf "%.2f", total}')

echo "Elapsed: ${ELAPSED_MS} ms"
echo "Target QPS: ${QPS}"
echo "Actual QPS: ${ACTUAL_QPS}"
echo "Data volume (requests): ${TOTAL_MB}"
