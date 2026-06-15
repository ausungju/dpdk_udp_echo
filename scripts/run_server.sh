#!/usr/bin/env bash
set -eu


WORKSPACE=$HOME/workspace/dpdk/echo

LCORES=1
PCI_ADDR=0000:01:00.0
PORT=0
BURST=32
THREADS=1

cd $WORKSPACE
make >/dev/null 2>&1
./scripts/init.sh >/dev/null 2>&1
mkdir -p logs

echo "[run_server] Server started"
sudo $WORKSPACE/build/server -l $LCORES -a $PCI_ADDR -- --port $PORT --burst $BURST -T $THREADS >/dev/null 2>&1
echo "[run_server] Server stopped"
