#!/usr/bin/env bash
# Derleme + tüm testler, CI ile aynı imajda. vcan0 için konteyner NET_ADMIN ister:
#   docker run --rm --cap-add NET_ADMIN -v "$PWD:/src" aviyonik-veriyolu:dev scripts/docker-test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if ! ip link show vcan0 >/dev/null 2>&1; then
    ip link add dev vcan0 type vcan 2>/dev/null && ip link set up vcan0 || echo "uyari: vcan0 kurulamadi, SocketCAN testleri atlanacak"
fi

BUILD=${BUILD:-build}
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAVB_WERROR=ON "$@"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
