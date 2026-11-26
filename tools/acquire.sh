#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT_DIR"

DATA_DIR="${SITUS_DATA_DIR:-var/data}"
mkdir -p "$DATA_DIR"

IBGE_URL="https://geoftp.ibge.gov.br/organizacao_do_territorio/malhas_territoriais/malhas_de_setores_censitarios__divisoes_intramunicipais/censo_2022/setores/gpkg/UF/RS/RS_setores_CD2022.gpkg"
OUT_FILE="${DATA_DIR}/RS_setores_CD2022.gpkg"
SHA_FILE="${OUT_FILE}.sha256"

echo "=== SITUS Aquisicao Administrativa (Categoria A) ==="
echo "Fonte: ibge_setores_rs_2022"
echo "URL:   $IBGE_URL"

if [ -f "$OUT_FILE" ] && [ -s "$OUT_FILE" ]; then
    echo "[OK] Arquivo ja presente em $OUT_FILE"
else
    echo "Baixando GeoPackage oficial do IBGE (RS 2022)..."
    curl -fL --retry 3 -o "${OUT_FILE}.tmp" "$IBGE_URL"
    mv "${OUT_FILE}.tmp" "$OUT_FILE"
fi

sha256sum "$OUT_FILE" | tee "$SHA_FILE"
ls -lh "$OUT_FILE"
