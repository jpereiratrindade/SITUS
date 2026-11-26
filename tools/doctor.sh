#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT_DIR"

echo "=== SITUS Doctor ==="
echo "Workspace: $ROOT_DIR"

echo "--- Ferramentas nativas ---"
for cmd in g++ cmake ninja git podman psql curl jq; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf "  [OK] %-8s %s\n" "$cmd" "$("$cmd" --version 2>/dev/null | head -n1)"
    else
        printf "  [MISSING] %s\n" "$cmd"
    fi
done

echo "--- Bibliotecas C++ (pkg-config) ---"
for pkg in libpq gdal geos proj nlohmann_json openssl; do
    if pkg-config --exists "$pkg" 2>/dev/null; then
        printf "  [OK] %-15s %s\n" "$pkg" "$(pkg-config --modversion "$pkg")"
    else
        printf "  [MISSING] %s\n" "$pkg"
    fi
done

echo "--- Destino WebAssembly (Emscripten) ---"
EMSDK_ENV="/opt/emsdk/emsdk_env.sh"
if [ -f "$EMSDK_ENV" ]; then
    # shellcheck disable=SC1090
    EMSDK_QUIET=1 source "$EMSDK_ENV"
    if command -v em++ >/dev/null 2>&1; then
        echo "  [OK] em++: $(em++ --version | head -n1)"
    else
        echo "  [UNAVAILABLE] emsdk encontrado, mas em++ nao esta no PATH"
    fi
else
    echo "  [UNAVAILABLE] /opt/emsdk/emsdk_env.sh nao encontrado (execucao nativa continua operacional)"
fi

if [ -f "web/wasm/situs_core.wasm" ]; then
    echo "  [OK] Artefato Wasm compilado: $(sha256sum web/wasm/situs_core.wasm)"
else
    echo "  [INFO] web/wasm/situs_core.wasm ainda nao compilado (execute ./run.sh build-wasm)"
fi

echo "--- Banco PostGIS dedicado ---"
if command -v podman >/dev/null 2>&1; then
    podman ps -a --filter "name=situs-postgis-db" --format "  Container: {{.Names}} | Status: {{.Status}} | Ports: {{.Ports}}" || true
fi
