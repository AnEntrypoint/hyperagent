#!/bin/sh
set -eu
log=/config/logs/prod-health.csv
mkdir -p "$(dirname "$log")"
ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
for p in / /app /privacy /terms; do
  code=$(timeout 25 curl -s -m 20 -o /dev/null -w '%{http_code}' "https://ubuntudashboard.co.za$p" 2>/dev/null || true)
  if [ -z "$code" ]; then code=000; fi
  printf '%s,%s,%s\n' "$ts" "$p" "$code" >> "$log"
done
