#!/usr/bin/env bash
# Generate the four-file skeleton for a new <periph> HDF platform driver.
#   usage: new_driver.sh <periph> <VENDOR> <CHIP> <outdir>
#   e.g.   new_driver.sh spi NSING N32H7xx /tmp/spi-driver
set -euo pipefail
PERIPH="${1:?usage: $0 <periph> <VENDOR> <CHIP> <outdir>}"
VENDOR="${2:?}"
CHIP="${3:?}"
OUT="${4:?}"
TPL="$(cd "$(dirname "$0")/../templates" && pwd)"

pl=$(echo "$PERIPH" | tr 'A-Z' 'a-z')
Pl="${PERIPH^}"
PL=$(echo "$PERIPH" | tr 'a-z' 'A-Z')
cl=$(echo "$CHIP" | tr 'A-Z' 'a-z')
Cl="${CHIP^}"
CL=$(echo "$CHIP" | tr 'a-z' 'A-Z')
vl=$(echo "$VENDOR" | tr 'A-Z' 'a-z')
VL=$(echo "$VENDOR" | tr 'a-z' 'A-Z')

sub() {  # sub <src> <dst>
  sed -e "s/<periph>/$pl/g"   -e "s/<Periph>/$Pl/g"  -e "s/<PERIPH>/$PL/g" \
      -e "s/<chip>/$cl/g"     -e "s/<Chip>/$Cl/g"    -e "s/<CHIP>/$CL/g" \
      -e "s/<vendor>/$vl/g"   -e "s/<VENDOR>/$VL/g"  "$1" > "$2"
  echo "  $2"
}

mkdir -p "$OUT/include" "$OUT/src" "$OUT/adapter" "$OUT/hcs"
echo "generated:"
sub "$TPL/driver_hw.h"  "$OUT/include/${cl}_${pl}_hw.h"
sub "$TPL/driver_hw.c"  "$OUT/src/${cl}_${pl}_hw.c"
sub "$TPL/driver_hdf.c" "$OUT/adapter/${pl}_${cl}.c"
sub "$TPL/hcs_two_part.hcs" "$OUT/hcs/${pl}_config.hcs"
sub "$TPL/app_task.c"   "$OUT/${pl}_app_task.c"

cat <<TIP

next steps (do not skip):
  1. fill the TODO blocks in ${cl}_${pl}_hw.c with facts copied from the vendor
     example - pins, AF numbers, clock source, timing values. Do not guess.
  2. decide the service model: controller (policy 0 + manager node) or service
     (policy 2 + serviceName "HDF_PLATFORM_${PL}_<num>").
  3. wire three places: adapter BUILD.gn, soc BUILD.gn sources, kernel config.
  4. add the deviceNode to device_info.hcs and include ${pl}_config.hcs in hdf.hcs.
  5. build, then: scripts/verify_image.sh <ELF> <expected driver count>
TIP
