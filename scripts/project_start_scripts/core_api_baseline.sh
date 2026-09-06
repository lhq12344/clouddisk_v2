#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:2024}"
DURATION="${DURATION:-10}"
QPS="${QPS:-20}"
CONCURRENCY="${CONCURRENCY:-5}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
RESULT_DIR="${RESULT_DIR:-${PROJECT_ROOT}/baseline-results/core-api-$(date +%Y%m%d-%H%M%S)}"
PERF_SCRIPT="${PERF_SCRIPT:-${PROJECT_ROOT}/test/perf_test.sh}"

if [[ ! -f "$PERF_SCRIPT" ]]; then
  echo "[baseline] perf script not found: ${PERF_SCRIPT}" >&2
  exit 2
fi

if ! curl -fsS -m 5 "${BASE_URL}/health" > /dev/null; then
  echo "[baseline] Core API health check failed at ${BASE_URL}/health" >&2
  echo "[baseline] start the intended OpenResty/gateway stack first, then rerun this script" >&2
  exit 3
fi

mkdir -p "$RESULT_DIR"

run_probe() {
  local name="$1"
  local method="$2"
  local path="$3"
  local payload="${4:-}"
  shift 4 || true

  local args=(-u "${BASE_URL}${path}" -m "$method" -d "$DURATION" -q "$QPS" -c "$CONCURRENCY")
  if [[ -n "$payload" ]]; then
    args+=(-p "$payload" -H 'Content-Type: application/json')
  fi
  for header in "$@"; do
    args+=(-H "$header")
  done

  echo "[baseline] ${name} -> ${method} ${path}"
  bash "$PERF_SCRIPT" "${args[@]}" | tee "${RESULT_DIR}/${name}.log"
}

run_probe health GET /health ""

if [[ -z "${JWT:-}" && -n "${SMOKE_SIGNIN_USERNAME:-}" && -n "${SMOKE_SIGNIN_PASSWORD:-}" ]]; then
  echo "[baseline] JWT not set; signing in with SMOKE_SIGNIN_USERNAME/SMOKE_SIGNIN_PASSWORD"
  SIGNIN_RESPONSE_FILE="${RESULT_DIR}/signin-response.json"
  SIGNIN_PAYLOAD="${RESULT_DIR}/signin-request.json"
  printf '{"username":"%s","password":"%s"}' "$SMOKE_SIGNIN_USERNAME" "$SMOKE_SIGNIN_PASSWORD" > "$SIGNIN_PAYLOAD"
  if curl -fsS -m 30 -X POST "${BASE_URL}/user/signin" -H 'Content-Type: application/json' --data-binary "@${SIGNIN_PAYLOAD}" > "$SIGNIN_RESPONSE_FILE"; then
    JWT="$(node -e "const fs=require('fs'); const p=process.argv[1]; const j=JSON.parse(fs.readFileSync(p,'utf8')); process.stdout.write(j.token || '')" "$SIGNIN_RESPONSE_FILE")"
    if [[ -n "$JWT" ]]; then
      echo '[baseline] signin token acquired' | tee "${RESULT_DIR}/signin.log"
    else
      echo '[baseline] signin succeeded but token was missing' | tee "${RESULT_DIR}/signin.log"
    fi
  else
    echo '[baseline] signin failed' | tee "${RESULT_DIR}/signin.log"
  fi
fi

if [[ -z "${JWT:-}" ]]; then
  echo "[baseline] JWT not set; skipping authenticated endpoint probes" | tee "${RESULT_DIR}/auth-skipped.log"
  exit 0
fi

AUTH_HEADER="Authorization: Bearer ${JWT}"
INIT_PAYLOAD="${RESULT_DIR}/initupload.json"
PRESIGN_PAYLOAD="${RESULT_DIR}/presign-parts.json"
COMPLETE_PAYLOAD="${RESULT_DIR}/complete-multipart.json"

cat > "$INIT_PAYLOAD" <<'JSON'
{"file_name":"baseline.bin","file_hash":"baseline-phase0-hash","file_size":1048576,"content_type":"application/octet-stream"}
JSON
cat > "$PRESIGN_PAYLOAD" <<'JSON'
{"upload_id":"baseline-upload-id","part_numbers":[1]}
JSON
cat > "$COMPLETE_PAYLOAD" <<'JSON'
{"upload_id":"baseline-upload-id"}
JSON

run_probe userinfo GET /user/info "" "$AUTH_HEADER"
run_probe file_query POST /file/query "" "$AUTH_HEADER"
run_probe initupload POST /file/initupload "$INIT_PAYLOAD" "$AUTH_HEADER"
run_probe presign_parts POST /file/PresignParts "$PRESIGN_PAYLOAD" "$AUTH_HEADER"
run_probe complete_multipart POST /file/CompleteMultipart "$COMPLETE_PAYLOAD" "$AUTH_HEADER"

echo "[baseline] results written to ${RESULT_DIR}"
