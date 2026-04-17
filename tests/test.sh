#!/usr/bin/env bash
set -euo pipefail

IP_ADDR="${1:-192.168.50.1}"

log() {
    printf '[test] %s\n' "$1"
}

cleanup() {
    ip route del "${IP_ADDR}/32" dev vni0 2>/dev/null || true
    rmmod vni 2>/dev/null || true
}

trap cleanup EXIT

log "building module"
make clean
make

log "loading module"
insmod vni.ko

log "checking interface and procfs"
ip link show vni0 >/dev/null
test -e /proc/vni

log "configuring IPv4 ${IP_ADDR}"
printf '%s\n' "${IP_ADDR}" > /proc/vni
grep -qx "ip=${IP_ADDR}" /proc/vni

log "checking ping reply"
ip link set vni0 up
ip route add "${IP_ADDR}/32" dev vni0
ping -c 1 "${IP_ADDR}" >/dev/null

log "unloading module"
rmmod vni
if ip link show vni0 >/dev/null 2>&1; then
    echo "vni0 still exists after rmmod" >&2
    exit 1
fi

if test -e /proc/vni; then
    echo "/proc/vni still exists after rmmod" >&2
    exit 1
fi

log "test passed"
