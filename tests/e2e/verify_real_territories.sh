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
INPUTS_CENTRO=$(curl -fsS -X POST "${BASE_URL}/api/v1/execution/inputs" \
  -H "Content-Type: application/json" \
  -d "{\"operation_id\":\"territorial_area_synthesis\",\"reference\":${POLY_CENTRO}}")

INPUTS_SUL=$(curl -fsS -X POST "${BASE_URL}/api/v1/execution/inputs" \
  -H "Content-Type: application/json" \
  -d "{\"operation_id\":\"territorial_area_synthesis\",\"reference\":${POLY_SUL}}")

echo "Centro -> Servidor Nativo:"
echo "$INPUTS_CENTRO" | jq '{operation_id, contract_version: .contract_version, requested_mode: "PREFER_SERVER", inputs: .inputs}' | \
  curl -fsS -X POST "${BASE_URL}/api/v1/execution/run" -H "Content-Type: application/json" -d @- | \
  jq '{status, actual_target, timings_ms, total_intersected: .output.total_intersected_area_m2, groups: .output.groups}'

echo "Sul -> Servidor Nativo:"
echo "$INPUTS_SUL" | jq '{operation_id, contract_version: .contract_version, requested_mode: "PREFER_SERVER", inputs: .inputs}' | \
  curl -fsS -X POST "${BASE_URL}/api/v1/execution/run" -H "Content-Type: application/json" -d @- | \
  jq '{status, actual_target, timings_ms, total_intersected: .output.total_intersected_area_m2, groups: .output.groups}'
