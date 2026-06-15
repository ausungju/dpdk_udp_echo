#!/usr/bin/env bash
set -euo pipefail

PCI_ADDR=${1:-0000:01:00.0}
HUGEPAGES=${HUGEPAGES:-512}
HUGEPAGE_DIR=/dev/hugepages
HUGEPAGE_NR=/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

echo "$HUGEPAGES" | sudo tee "$HUGEPAGE_NR" >/dev/null
mountpoint -q "$HUGEPAGE_DIR" || sudo mount -t hugetlbfs nodev "$HUGEPAGE_DIR"

sudo modprobe mlx4_core
sudo modprobe mlx4_en
sudo modprobe mlx4_ib
sudo modprobe ib_uverbs
sudo dpdk-devbind.py -b mlx4_core "$PCI_ADDR"

echo "Ready. Example:"
echo "  sudo ./build/l2fwd -a ${PCI_ADDR},port=1 -- -p 0x1"
