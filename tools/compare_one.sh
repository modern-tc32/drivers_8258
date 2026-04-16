#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <name-without-ext>" >&2
  exit 1
fi

name="$1"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

../tc32-vendor/bin/tc32-elf-gcc -O2 -fomit-frame-pointer -Wall -Wextra -std=gnu99 \
  -I../tc_ble_single_sdk/drivers/B85 \
  -I../tc_ble_single_sdk/drivers/B85/lib/include \
  -I../tc_ble_single_sdk/common \
  -I../tc_ble_single_sdk/common/usb \
  -I../tc_ble_single_sdk/mcu \
  -I../tc_ble_single_sdk \
  -c "$name.c" -o "$name.o"

mkdir -p reports
../tc32-vendor/bin/tc32-elf-objdump -drwC "../drivers/$name.o" > "reports/$name.orig.objdump.txt"
../tc32-vendor/bin/tc32-elf-objdump -drwC "$name.o" > "reports/$name.recon.objdump.txt"

diff -u "reports/$name.orig.objdump.txt" "reports/$name.recon.objdump.txt" > "reports/$name.diff" || true

echo "done: reports/$name.diff"
