#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "$ROOT_DIR"

BASE_URL="${1:-http://127.0.0.1:8096}"

echo "=== SITUS Verificação Territorial Real (A04, A05, A12, A15) ==="

# Recorte 1: Centro Histórico de Porto Alegre (RS)
POLY_CENTRO='{
  "kind": "POLYGON",
  "crs": "EPSG:4326",
  "value": {
    "type": "Polygon",
    "coordinates": [[
      [-51.235, -30.035],
      [-51.215, -30.035],
      [-51.215, -30.025],
      [-51.235, -30.025],
      [-51.235, -30.035]
    ]]
  }
}'

# Recorte 2: Extremo Sul de Porto Alegre (RS)
POLY_SUL='{
  "kind": "POLYGON",
  "crs": "EPSG:4326",
  "value": {
    "type": "Polygon",
    "coordinates": [[
      [-51.200, -30.230],
      [-51.140, -30.230],
      [-51.140, -30.180],
      [-51.200, -30.180],
      [-51.200, -30.230]
    ]]
  }
}'

echo "--- 1. Interseção Recorte 1 (Centro POA) ---"
curl -fsS -X POST "${BASE_URL}/api/v1/territories/intersect" \
  -H "Content-Type: application/json" -d "$POLY_CENTRO" | jq '{status, unit_count, reference_area_m2, total_intersected_area_m2, coverage_fraction, sample_units: (.units[:3] | map({unit_code, situation_code, intersected_area_m2}))}'

echo "--- 2. Interseção Recorte 2 (Extremo Sul POA) ---"
curl -fsS -X POST "${BASE_URL}/api/v1/territories/intersect" \
  -H "Content-Type: application/json" -d "$POLY_SUL" | jq '{status, unit_count, reference_area_m2, total_intersected_area_m2, coverage_fraction, sample_units: (.units[:3] | map({unit_code, situation_code, intersected_area_m2}))}'

echo "--- 3. Casos extremos (A05): CRS inválido, polígono fora de cobertura, ponto em fronteira ---"
echo "3a. CRS inválido (esperado HTTP 422):"
curl -s -o /dev/null -w "HTTP %{http_code}\n" -X POST "${BASE_URL}/api/v1/references/resolve" \
  -H "Content-Type: application/json" -d '{"kind":"POINT","value":[-51.23,-30.03],"crs":"EPSG:3857"}'

echo "3b. Ponto fora da cobertura (Oceano Atlântico):"
curl -fsS -X POST "${BASE_URL}/api/v1/references/resolve" \
  -H "Content-Type: application/json" -d '{"kind":"POINT","value":[-40.0,-30.0],"crs":"EPSG:4326"}' | jq '{status, reasons}'

echo "3c. Ponto válido em Porto Alegre (ST_Covers):"
curl -fsS -X POST "${BASE_URL}/api/v1/references/resolve" \
  -H "Content-Type: application/json" -d '{"kind":"POINT","value":[-51.2287,-30.0277],"crs":"EPSG:4326"}' | jq '{status, units: (.units | map({unit_code, municipality_name, situation_code}))}'

echo "--- 4. Confrontação Nativo vs Wasm sobre os dois recortes reais ---"
for LABEL in "Centro POA" "Extremo Sul POA"; do
  if [ "$LABEL" = "Centro POA" ]; then POLY="$POLY_CENTRO"; else POLY="$POLY_SUL"; fi
  INPUTS_JSON=$(curl -fsS -X POST "${BASE_URL}/api/v1/execution/inputs" \
    -H "Content-Type: application/json" \
    -d "{\"operation_id\":\"territorial_area_synthesis\",\"reference\":${POLY}}")

  NAT_OUT=$(echo "$INPUTS_JSON" | jq '{operation_id, contract_version, requested_mode: "PREFER_SERVER", inputs: .inputs}' | \
    curl -fsS -X POST "${BASE_URL}/api/v1/execution/run" -H "Content-Type: application/json" -d @-)

  WASM_OUT=$(INPUTS_JSON="$INPUTS_JSON" node - <<'NODE'
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const createMod = require('./web/wasm/situs_core.js');
const mod = await createMod({ locateFile: (f) => './web/wasm/' + f });
const bundle = JSON.parse(process.env.INPUTS_JSON);
const req = JSON.stringify({
  operation_id: 'territorial_area_synthesis',
  contract_version: '0.2.0',
  requested_mode: 'PREFER_LOCAL',
  inputs: bundle.inputs
});
const byteLen = mod.lengthBytesUTF8(req) + 1;
const ptr = mod._malloc(byteLen);
mod.stringToUTF8(req, ptr, byteLen);
const resPtr = mod._situs_wasm_run_synthesis(ptr);
const outStr = mod.UTF8ToString(resPtr);
mod._free(ptr);
console.log(outStr);
NODE
)

  NAT_AREA=$(echo "$NAT_OUT" | jq -r '.output.total_intersected_area_m2')
  WASM_AREA=$(echo "$WASM_OUT" | jq -r '.output.total_intersected_area_m2')
  DIFF=$(python3 -c "print(abs(float('$NAT_AREA') - float('$WASM_AREA')))")
  echo "$LABEL -> Nativo=$NAT_AREA m2 | Wasm=$WASM_AREA m2 | Diff=$DIFF m2"
  python3 -c "import sys; sys.exit(0 if float('$DIFF') <= 1e-2 else 1)"
done
