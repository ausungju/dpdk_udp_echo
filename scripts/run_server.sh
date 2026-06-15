#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=${WORKSPACE:-$HOME/workspace/dpdk/echo}
LCORES=${LCORES:-1,2,3,4}
PCI_ADDR=${PCI_ADDR:-0000:01:00.0}
PORT=${PORT:-0}
BURST=${BURST:-32}
TIMER_PERIOD=${TIMER_PERIOD:-1}

cd "$WORKSPACE"
make
./scripts/init.sh "$PCI_ADDR"
mkdir -p logs

echo "[run_server] Starting server"
echo "[run_server] LCORES=$LCORES PCI_ADDR=$PCI_ADDR PORT=$PORT BURST=$BURST TIMER_PERIOD=$TIMER_PERIOD"
exec sudo "$WORKSPACE/build/server" -l "$LCORES" -a "$PCI_ADDR" -- \
    --port "$PORT" --burst "$BURST" -T "$TIMER_PERIOD"
