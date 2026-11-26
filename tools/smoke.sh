#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8096}"

echo "=== SITUS API Smoke Test ($BASE_URL) ==="

echo "1. GET /health"
curl -fsS "${BASE_URL}/health" | jq .

echo "2. GET /api/v1/catalog"
curl -fsS "${BASE_URL}/api/v1/catalog" | jq '{policy, source_count: (.sources | length)}'

echo "3. GET /api/v1/execution/capabilities"
curl -fsS "${BASE_URL}/api/v1/execution/capabilities" | jq '{contract_version, default_mode, wasm_artifact}'

echo "4. POST /api/v1/references/resolve (CEP sem fonte aprovada -> UNAVAILABLE)"
curl -fsS -X POST "${BASE_URL}/api/v1/references/resolve" \
    -H "Content-Type: application/json" \
    -d '{"kind":"CEP","value":"90010-150","crs":"EPSG:4326"}' | jq '{status, kind, spatial_level}'

echo "5. POST /api/v1/execution/run (territorial_area_synthesis)"
curl -fsS -X POST "${BASE_URL}/api/v1/execution/run" \
    -H "Content-Type: application/json" \
    -d '{
      "operation_id": "territorial_area_synthesis",
      "contract_version": "0.2.0",
      "requested_mode": "PREFER_SERVER",
      "inputs": {
        "reference_area_m2": 10000.0,
        "overlap_rule": "DISJOINT_SECTORS",
        "dataset_versions": ["2022.1"],
        "slices": [
          {"unit_code": "S1", "group_key": "1", "group_label": "Urbana", "intersected_area_m2": 6000.0, "unit_total_area_m2": 12000.0},
          {"unit_code": "S2", "group_key": "8", "group_label": "Rural", "intersected_area_m2": 4000.0, "unit_total_area_m2": 8000.0}
        ]
      }
    }' | jq '{status, actual_target, total: .output.total_intersected_area_m2, groups: (.output.groups | length)}'

echo "[PASS] Smoke test concluido"
