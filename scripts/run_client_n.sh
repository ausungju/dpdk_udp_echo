#!/usr/bin/env bash
set -eu

if [ $# -lt 1 ]; then
    echo "Usage: $0 N"
    echo "  N              Number of times to run the command"
    exit 1
fi


WORKSPACE=$HOME/workspace/dpdk/echo

N=$1
LCORES=1
PCI_ADDR=0000:01:00.0
PORT=0
DST_MAC=00:02:ca:44:20:74
BURST=64
PAYLOAD_LEN=1400
DURATION=60
THREADS=1

cd $WORKSPACE
make >/dev/null 2>&1
./scripts/init.sh >/dev/null 2>&1
mkdir -p logs

for i in $(seq 1 "$N"); do
    echo "[run_client_n] Run $i/$N}"
    sudo $WORKSPACE/build/client -l $LCORES -a $PCI_ADDR -- --port $PORT --dst-mac $DST_MAC --burst $BURST --payload-len $PAYLOAD_LEN --duration $DURATION -T $THREADS >/dev/null 2>&1
    rc=$?
    echo "[run_client_n] Exit code: $rc"
done
