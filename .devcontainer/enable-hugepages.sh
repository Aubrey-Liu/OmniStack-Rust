#!/usr/bin/env bash

echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
mkdir -p /dev/hugepages
mountpoint -q /dev/hugepages
mount -t hugetlbfs nodev /dev/hugepages
