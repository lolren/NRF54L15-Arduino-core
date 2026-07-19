#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:-$(mktemp -d -t nrf-ocd-relink-XXXXXX)}"
APP="$WORK/open-nrf-ocd"
LIBUSB="$WORK/libusb-1.0.27"
PREFIX="$WORK/libusb-install"
BUILD="$WORK/libusb-build"

rm -rf "$APP" "$LIBUSB" "$PREFIX" "$BUILD"
mkdir -p "$APP" "$BUILD"
tar -xzf "$ROOT/open-nrf-ocd-v0.3.7-source.tar.gz" -C "$APP" --strip-components=1
tar -xjf "$ROOT/libusb-1.0.27.tar.bz2" -C "$WORK"

read -r -a CC_COMMAND <<< "${NRF_OCD_CC:-cc}"
read -r -a AR_COMMAND <<< "${NRF_OCD_AR:-ar}"

pushd "$BUILD" >/dev/null
CC="${CC_COMMAND[*]}" AR="${AR_COMMAND[*]}" \
  "$LIBUSB/configure" \
    --disable-udev \
    --enable-static \
    --disable-shared \
    --prefix="$PREFIX"
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
make install
popd >/dev/null

mkdir -p "$APP/build/obj" "$APP/build/bin"
CFLAGS=(
  -std=c11 -Os -g0
  -Wno-implicit-function-declaration
  -Wno-date-time
  -Wno-format
  -Wno-multichar
  -D_GNU_SOURCE
  -D_POSIX_C_SOURCE=200809L
  -DNRF_OCD_USE_LIBUSB=1
  -I"$APP/src"
  -I"$PREFIX/include"
  -I"$PREFIX/include/libusb-1.0"
)
SOURCES=(
  log util hex elf probe cmsis_dap swd dap target target_nrf54l
  target_nrf54lm20a flash flash_algo_nrf54l commander cli main hid_libusb
)

for source in "${SOURCES[@]}"; do
  "${CC_COMMAND[@]}" "${CFLAGS[@]}" \
    -c "$APP/src/$source.c" -o "$APP/build/obj/$source.o"
done

"${CC_COMMAND[@]}" -o "$APP/build/bin/nrf_ocd" \
  "$APP"/build/obj/*.o "$PREFIX/lib/libusb-1.0.a" -lpthread

printf 'Rebuilt executable: %s\n' "$APP/build/bin/nrf_ocd"
printf 'Work directory: %s\n' "$WORK"
