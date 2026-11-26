#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$ROOT_DIR"

if [ -f .env ]; then
    set -a
    # shellcheck disable=SC1091
    source .env
    set +a
fi

cmd="${1:-help}"
shift || true

case "$cmd" in
    doctor)
        ./tools/doctor.sh "$@"
        ;;
    build)
        cmake --preset dev
        cmake --build --preset dev
        ;;
    test)
        ctest --preset dev --output-on-failure
        ;;
    build-wasm)
        EMSDK_ENV="/opt/emsdk/emsdk_env.sh"
        if [ ! -f "$EMSDK_ENV" ]; then
            echo "[ERRO] Emscripten SDK nao encontrado em $EMSDK_ENV" >&2
            exit 1
        fi
        # shellcheck disable=SC1090
        EMSDK_QUIET=1 source "$EMSDK_ENV"
        emcmake cmake --preset wasm-dev
        cmake --build --preset wasm-dev
        ;;
    test-wasm)
        EMSDK_ENV="/opt/emsdk/emsdk_env.sh"
        if [ ! -f "$EMSDK_ENV" ]; then
            echo "[ERRO] Emscripten SDK nao encontrado em $EMSDK_ENV" >&2
            exit 1
        fi
        # shellcheck disable=SC1090
        EMSDK_QUIET=1 source "$EMSDK_ENV"
        emcmake cmake --preset wasm-test
        cmake --build --preset wasm-test
        ctest --preset wasm-test --output-on-failure
        ;;
    serve)
        exec ./build/dev/situs serve "$@"
        ;;
    cli)
        exec ./build/dev/situs "$@"
        ;;
    *)
        echo "Uso: $0 {doctor|build|test|build-wasm|test-wasm|serve|cli ...}"
        exit 1
        ;;
esac
