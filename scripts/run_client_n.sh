#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 N"
    echo "  N              Number of times to run the command"
    exit 1
fi

WORKSPACE=${WORKSPACE:-$HOME/workspace/dpdk/echo}
N=$1
LCORES=${LCORES:-1,2,3,4}
PCI_ADDR=${PCI_ADDR:-0000:01:00.0}
PORT=${PORT:-0}
DST_MAC=${DST_MAC:-00:02:ca:44:20:74}
BURST=${BURST:-64}
PAYLOAD_LEN=${PAYLOAD_LEN:-1400}
DURATION=${DURATION:-60}
TIMER_PERIOD=${TIMER_PERIOD:-1}

cd "$WORKSPACE"
make
./scripts/init.sh "$PCI_ADDR"
mkdir -p logs

for i in $(seq 1 "$N"); do
    echo "[run_client_n] Run $i/$N"
    echo "[run_client_n] LCORES=$LCORES PCI_ADDR=$PCI_ADDR PORT=$PORT DST_MAC=$DST_MAC BURST=$BURST PAYLOAD_LEN=$PAYLOAD_LEN DURATION=$DURATION TIMER_PERIOD=$TIMER_PERIOD"
    sudo "$WORKSPACE/build/client" -l "$LCORES" -a "$PCI_ADDR" -- \
        --port "$PORT" --dst-mac "$DST_MAC" --burst "$BURST" \
        --payload-len "$PAYLOAD_LEN" --duration "$DURATION" -T "$TIMER_PERIOD"
    rc=$?
    echo "[run_client_n] Exit code: $rc"
done
