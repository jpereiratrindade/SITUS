#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT_DIR"

if [ -f .env ]; then
    set -a
    # shellcheck disable=SC1091
    source .env
    set +a
fi

DATA_DIR="${SITUS_DATA_DIR:-var/data}/exp002"
ZIP_PATH="${DATA_DIR}/RS_faces_de_logradouros_2022_shp.zip"
FACES_URL="https://geoftp.ibge.gov.br/recortes_para_fins_estatisticos/malha_de_setores_censitarios/censo_2022/base_de_faces_de_logradouros_versao_2022_censo_demografico/shp/RS_faces_de_logradouros_2022_shp.zip"

DB_HOST="${SITUS_DB_HOST:-127.0.0.1}"
DB_PORT="${SITUS_DB_PORT:-55436}"
DB_NAME="${SITUS_DB_NAME:-situs}"
DB_ADMIN_USER="${SITUS_DB_ADMIN_USER:-situs_migrator}"
DB_ADMIN_PASSWORD="${SITUS_DB_ADMIN_PASSWORD:-change_me_migrator_local}"
export PGPASSWORD="$DB_ADMIN_PASSWORD"

PG_CONN="PG:host=${DB_HOST} port=${DB_PORT} dbname=${DB_NAME} user=${DB_ADMIN_USER} password=${DB_ADMIN_PASSWORD}"

run_psql() {
    psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_ADMIN_USER" -d "$DB_NAME" "$@"
}

if [ ! -f "$ZIP_PATH" ]; then
    mkdir -p "$DATA_DIR"
    echo "=== Baixando RS_faces_de_logradouros_2022_shp.zip ==="
    curl -fL -o "$ZIP_PATH" "$FACES_URL"
fi

FACES_SHA="$(sha256sum "$ZIP_PATH" | awk '{print $1}')"
echo "=== 1. Registrando fonte Categoria A (ibge_faces_rs_2022, SHA-256: ${FACES_SHA}) ==="

run_psql <<SQL
BEGIN;
INSERT INTO source (source_id, title, provider, category, approval_status, license, source_url, scope, spatial_level, native_crs, native_resolution, allowed_fields, limitations)
VALUES (
  'ibge_faces_rs_2022',
  'Base de Faces de Logradouros — Censo 2022 (RS)',
  'IBGE',
  'A',
  'APPROVED',
  'Dados Abertos IBGE',
  '${FACES_URL}',
  'RS',
  'STREET_FACE',
  'EPSG:4674',
  'Escala cadastral censitaria IBGE 2022 (eixos/faces de quadra)',
  '["CD_SETOR","CD_QUADRA","CD_FACE","NM_TIP_LOG","NM_TIT_LOG","NM_LOG","TOT_RES","TOT_GERAL"]'::jsonb,
  'Contem apenas faces publicas de logradouros e totais agregados; nao possui coluna de CEP, numeros de imoveis ou nomes de moradores.'
)
ON CONFLICT (source_id) DO UPDATE SET
  approval_status = 'APPROVED',
  allowed_fields = EXCLUDED.allowed_fields,
  limitations = EXCLUDED.limitations;

UPDATE dataset_version SET is_active = FALSE WHERE source_id = 'ibge_faces_rs_2022';

INSERT INTO dataset_version (version_id, source_id, dataset_version, artifact_sha256, is_active, ingested_at)
VALUES ('ibge_faces_rs_2022:2022', 'ibge_faces_rs_2022', '2022', '${FACES_SHA}', TRUE, now())
ON CONFLICT (version_id) DO UPDATE SET
  artifact_sha256 = EXCLUDED.artifact_sha256,
  is_active = TRUE,
  ingested_at = now();
COMMIT;
SQL

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "=== 2. Extraindo shapefiles municipais para staging ==="
unzip -q "$ZIP_PATH" -d "$TMP_DIR"

run_psql -c "DROP TABLE IF EXISTS stg_street_face;"

first=1
count=0
while IFS= read -r shp; do
    if [ "$first" -eq 1 ]; then
        ogr2ogr -f PostgreSQL "$PG_CONN" "$shp" -nln stg_street_face -overwrite -t_srs EPSG:4326 -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom -lco PRECISION=NO
        first=0
    else
        ogr2ogr -f PostgreSQL "$PG_CONN" "$shp" -nln stg_street_face -append -t_srs EPSG:4326 -nlt PROMOTE_TO_MULTI
    fi
    count=$((count + 1))
    if [ $((count % 100)) -eq 0 ]; then
        echo "  Carregados ${count} municipios..."
    fi
done < <(find "$TMP_DIR" -type f -name '*_faces_de_logradouros_2022.shp' | sort)

echo "=== 3. Consolidando street_face (${count} arquivos municipais) ==="
run_psql <<'SQL'
BEGIN;
DELETE FROM street_face WHERE version_id = 'ibge_faces_rs_2022:2022';

INSERT INTO street_face (
    version_id,
    municipality_code,
    sector_code,
    quadra_code,
    face_code,
    tip_log,
    tit_log,
    log_name,
    full_name,
    normalized_name,
    tot_res,
    tot_geral,
    geom
)
SELECT
    'ibge_faces_rs_2022:2022',
    LEFT(COALESCE(cd_setor, ''), 7),
    COALESCE(cd_setor, ''),
    COALESCE(cd_quadra, ''),
    COALESCE(cd_face, ''),
    TRIM(COALESCE(nm_tip_log, '')),
    TRIM(COALESCE(nm_tit_log, '')),
    TRIM(COALESCE(nm_log, '')),
    TRIM(regexp_replace(concat_ws(' ', NULLIF(TRIM(COALESCE(nm_tip_log, '')), ''), NULLIF(TRIM(COALESCE(nm_tit_log, '')), ''), NULLIF(TRIM(COALESCE(nm_log, '')), '')), '\s+', ' ', 'g')),
    UPPER(translate(
        TRIM(regexp_replace(concat_ws(' ', NULLIF(TRIM(COALESCE(nm_tip_log, '')), ''), NULLIF(TRIM(COALESCE(nm_tit_log, '')), ''), NULLIF(TRIM(COALESCE(nm_log, '')), '')), '\s+', ' ', 'g')),
        'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ',
        'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn'
    )),
    COALESCE(tot_res, 0),
    COALESCE(tot_geral, 0),
    ST_Multi(ST_CollectionExtract(ST_MakeValid(geom), 2))
FROM stg_street_face
WHERE geom IS NOT NULL AND NOT ST_IsEmpty(geom);

DROP TABLE IF EXISTS stg_street_face;

UPDATE dataset_version
SET feature_count = (SELECT COUNT(*) FROM street_face WHERE version_id = 'ibge_faces_rs_2022:2022')
WHERE version_id = 'ibge_faces_rs_2022:2022';
COMMIT;
SQL

run_psql -c "SELECT version_id, feature_count FROM dataset_version WHERE version_id = 'ibge_faces_rs_2022:2022';"
