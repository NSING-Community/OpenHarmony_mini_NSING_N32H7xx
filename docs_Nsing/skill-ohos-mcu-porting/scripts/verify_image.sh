#!/usr/bin/env bash
# Verify that what you think you changed is actually in the image.
#   usage: verify_image.sh <ELF> <expected_driver_count>
set -uo pipefail
ELF="${1:?usage: $0 <ELF> <expected_driver_count>}"
EXP="${2:?usage: $0 <ELF> <expected_driver_count>}"

PFX=""
for p in arm-none-eabi- riscv64-unknown-elf- riscv32-unknown-elf-; do
  if command -v "${p}nm" >/dev/null 2>&1; then PFX="$p"; break; fi
done
[ -n "$PFX" ] || { echo "no cross binutils found (looked for arm/riscv nm)"; exit 2; }
echo "toolchain prefix: $PFX"
echo "image: $ELF ($(stat -c%s "$ELF" 2>/dev/null || echo '?') bytes)"
FAIL=0

# 1) driver table: 4 bytes per driver on a 32-bit target
S=$("${PFX}nm" "$ELF" | awk '/_hdf_drivers_start/{print $1; exit}')
E=$("${PFX}nm" "$ELF" | awk '/_hdf_drivers_end/{print $1; exit}')
if [ -n "$S" ] && [ -n "$E" ]; then
  BYTES=$(( 16#$E - 16#$S )); N=$(( BYTES / 4 ))
  echo "driver table : $BYTES bytes -> $N drivers (expect $EXP)"
  [ "$N" -eq "$EXP" ] || { echo "  ^ MISMATCH"; FAIL=1; }
else
  echo "driver table : symbols not found (linker script missing KEEP/_hdf_drivers_start?)"; FAIL=1
fi

# 2) kernel heap (NOLOAD: file size will not change when you edit it)
HS=$("${PFX}nm" "$ELF" | awk '/__heap_start/{print $1; exit}')
HE=$("${PFX}nm" "$ELF" | awk '/__heap_end/{print $1; exit}')
if [ -n "$HS" ] && [ -n "$HE" ]; then
  echo "kernel heap  : $(( 16#$HE - 16#$HS )) bytes"
else
  echo "kernel heap  : symbols not found"
fi

# 3) FPU context saved/restored (ARM only): C/ASM agreement
if [ "$PFX" = "arm-none-eabi-" ]; then
  # NOTE: do not use `objdump | grep -q`. With `set -o pipefail`, grep exits on
  # the first match, objdump dies of SIGPIPE, and the pipeline is reported as
  # failed even though the instruction was found. Count instead.
  NFPU=$("${PFX}objdump" -d "$ELF" 2>/dev/null | grep -cE 'vstmdb|vldmia' || true)
  if [ "${NFPU:-0}" -gt 0 ]; then
    echo "fpu context  : ok ($NFPU vstmdb/vldmia instructions)"
  else
    echo "fpu context  : MISSING (asm flags or __FPU_USED mismatch)"; FAIL=1
  fi
fi

# 4) build banner
echo "build string : $("${PFX}strings" "$ELF" | grep -o 'BUILD=[^ ]*' | head -1)"

# 5) application SYS_RUN entries (0 for XTS images built with app_demo=false)
APP=$("${PFX}nm" "$ELF" | grep -iE ' [tT] App[A-Za-z]*Entry$' | awk '{print $3}' | paste -sd' ' -)
if [ -z "$APP" ]; then
  echo "app entries  : 0"
else
  echo "app entries  : $(echo "$APP" | wc -w) -> $APP"
fi

[ "$FAIL" -eq 0 ] && echo "RESULT: pass" || echo "RESULT: FAIL"
exit "$FAIL"
