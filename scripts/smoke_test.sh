#!/usr/bin/env bash
# End-to-end smoke test: starts a local HTTP server with healthy, slow, and
# failing endpoints, runs the real healthmon binary against it, and checks
# the /metrics, /status, and /readyz endpoints plus a clean SIGTERM shutdown.
set -euo pipefail

BIN=${1:-build/healthmon}
WORK=$(mktemp -d)
export NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost
cleanup() { kill "${SERVER_PID:-}" "${MON_PID:-}" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT

cat > "$WORK/server.py" <<'PY'
import http.server, time
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/slow":
            time.sleep(0.6)
        code = 503 if self.path == "/fail" else 200
        self.send_response(code)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")
    def log_message(self, *args):
        pass
http.server.ThreadingHTTPServer(("127.0.0.1", 18088), Handler).serve_forever()
PY

cat > "$WORK/config.json" <<'JSON'
{
  "metrics_port": 19100,
  "workers": 4,
  "failure_threshold": 2,
  "targets": [
    {"name": "healthy", "url": "http://127.0.0.1:18088/ok", "interval_ms": 500, "timeout_ms": 500},
    {"name": "slow", "url": "http://127.0.0.1:18088/slow", "interval_ms": 1000, "timeout_ms": 1000, "degraded_latency_ms": 300},
    {"name": "failing", "url": "http://127.0.0.1:18088/fail", "interval_ms": 500, "timeout_ms": 500,
     "retry": {"max_attempts": 2, "base_delay_ms": 50, "max_delay_ms": 100}}
  ]
}
JSON

python3 "$WORK/server.py" & SERVER_PID=$!
sleep 1

echo "== --once mode (expect exit code 1 because one target fails)"
set +e; "$BIN" --config "$WORK/config.json" --once; rc=$?; set -e
[[ $rc -eq 1 ]] || { echo "FAIL: expected exit 1 from --once, got $rc"; exit 1; }

echo "== daemon mode"
"$BIN" --config "$WORK/config.json" > "$WORK/run.log" & MON_PID=$!
sleep 4

metrics=$(curl -sf http://127.0.0.1:19100/metrics)
check() { grep -qF "$1" <<<"$metrics" || { echo "FAIL: metrics missing: $1"; exit 1; }; }
check 'healthmon_target_state{target="healthy",url="http://127.0.0.1:18088/ok",state="UP"} 1'
check 'healthmon_target_state{target="slow",url="http://127.0.0.1:18088/slow",state="DEGRADED"} 1'
check 'healthmon_target_state{target="failing",url="http://127.0.0.1:18088/fail",state="DOWN"} 1'
curl -sf http://127.0.0.1:19100/status | python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d["targets"]) == 3'
[[ $(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:19100/readyz) == 200 ]] || { echo "FAIL: not ready"; exit 1; }

kill -TERM "$MON_PID"
wait "$MON_PID"
MON_PID=
grep -q '"event":"stopped"' "$WORK/run.log" || { echo "FAIL: no clean shutdown"; exit 1; }
echo "PASS: smoke test"
