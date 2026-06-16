#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=${WORKSPACE:-$HOME/workspace/dpdk/echo}
LCORES=${LCORES:-1,2}
PCI_ADDR=${PCI_ADDR:-0000:01:00.0}
PORT=${PORT:-1}
BURST=${BURST:-64}
TIMER_PERIOD=${TIMER_PERIOD:-1}
SERVER_MODE=${SERVER_MODE:-auto}

cd "$WORKSPACE"
make
./scripts/init.sh "$PCI_ADDR"
mkdir -p logs

echo "[run_server] Starting server"
echo "[run_server] LCORES=$LCORES PCI_ADDR=$PCI_ADDR PORT=$PORT BURST=$BURST TIMER_PERIOD=$TIMER_PERIOD SERVER_MODE=$SERVER_MODE"
exec sudo env ECHO_SW_FANOUT="$SERVER_MODE" "$WORKSPACE/build/server" -l "$LCORES" -a "$PCI_ADDR" -- \
    --port "$PORT" --burst "$BURST" -T "$TIMER_PERIOD"
