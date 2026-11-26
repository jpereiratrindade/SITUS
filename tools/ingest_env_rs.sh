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

DATA_DIR="${SITUS_DATA_DIR:-var/data}/patch004"
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

BIOMA_SHA=$(sha256sum "${DATA_DIR}/Biomas_250mil.zip" | awk '{print $1}')
PEDO_SHA=$(sha256sum "${DATA_DIR}/pedo_area.zip" | awk '{print $1}')
BC250_SHA=$(sha256sum "${DATA_DIR}/bc250_gpkg_2026-03-03.zip" | awk '{print $1}')

echo "=== 1. Registrando fontes ambientais Categoria A ==="
run_psql <<SQL
BEGIN;
INSERT INTO source (source_id, title, provider, category, approval_status, license, source_url, scope, spatial_level, native_crs, native_resolution, allowed_fields, limitations)
VALUES
  ('ibge_biomas_250k', 'Biomas do Brasil 1:250.000', 'IBGE', 'A', 'APPROVED', 'Dados Abertos IBGE', 'https://geoftp.ibge.gov.br/informacoes_ambientais/estudos_ambientais/biomas/vetores/Biomas_250mil.zip', 'RS', 'BIOME_250K', 'EPSG:4674', '1:250.000', '["CD_Bioma","Bioma"]'::jsonb, 'Escala regional 1:250.000; nao representa detalhe cadastral local.'),
  ('ibge_bdia_pedologia_250k', 'BDiA Pedologia 1:250.000 (v2023)', 'IBGE', 'A', 'APPROVED', 'Dados Abertos IBGE', 'https://geoftp.ibge.gov.br/informacoes_ambientais/pedologia/vetores/escala_250_mil/versao_2023/pedo_area.zip', 'RS', 'SOIL_250K', 'EPSG:4674', '1:250.000', '["legenda","ordem","subordem","leg_ordem"]'::jsonb, 'Escala exploratoria/reconhecimento 1:250.000; nao substitui levantamento pedologico local ou sondagem.'),
  ('ibge_bc250_hidrografia', 'BC250 Hidrografia (v2025/2026)', 'IBGE', 'A', 'APPROVED', 'Dados Abertos IBGE', 'https://geoftp.ibge.gov.br/cartas_e_mapas/bases_cartograficas_continuas/bc250/versao2025/geopackage/bc250_gpkg_2026-03-03.zip', 'RS', 'HYDRO_250K', 'EPSG:4674', '1:250.000', '["nome","regime"]'::jsonb, 'Escala cartografica 1:250.000; cursos de agua menores podem nao constar.')
ON CONFLICT (source_id) DO UPDATE SET approval_status = 'APPROVED';

UPDATE dataset_version SET is_active = FALSE WHERE source_id IN ('ibge_biomas_250k', 'ibge_bdia_pedologia_250k', 'ibge_bc250_hidrografia');

INSERT INTO dataset_version (version_id, source_id, dataset_version, artifact_sha256, is_active, ingested_at)
VALUES
  ('ibge_biomas_250k:2019', 'ibge_biomas_250k', '2019', '${BIOMA_SHA}', TRUE, now()),
  ('ibge_bdia_pedologia_250k:2023', 'ibge_bdia_pedologia_250k', '2023', '${PEDO_SHA}', TRUE, now()),
  ('ibge_bc250_hidrografia:2026-03-03', 'ibge_bc250_hidrografia', '2026-03-03', '${BC250_SHA}', TRUE, now())
ON CONFLICT (version_id) DO UPDATE SET artifact_sha256 = EXCLUDED.artifact_sha256, is_active = TRUE, ingested_at = now();
COMMIT;
SQL

echo "=== 2. Carregando staging via ogr2ogr (recorte RS EPSG:4326) ==="
ogr2ogr -f PostgreSQL "$PG_CONN" "${DATA_DIR}/biomas/lm_bioma_250.shp" -nln stg_bioma -overwrite -t_srs EPSG:4326 -spat -57.7 -33.8 -49.6 -27.0 -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom
ogr2ogr -f PostgreSQL "$PG_CONN" "${DATA_DIR}/pedologia/pedo_area.shp" -nln stg_pedo -overwrite -t_srs EPSG:4326 -spat -57.7 -33.8 -49.6 -27.0 -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom
ogr2ogr -f PostgreSQL "$PG_CONN" "${DATA_DIR}/bc250/bc250_2026-03-03.gpkg" hid_trecho_drenagem_l -nln stg_hydro_l -overwrite -t_srs EPSG:4326 -spat -57.7 -33.8 -49.6 -27.0 -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom
ogr2ogr -f PostgreSQL "$PG_CONN" "${DATA_DIR}/bc250/bc250_2026-03-03.gpkg" hid_massa_dagua_a -nln stg_hydro_a -overwrite -t_srs EPSG:4326 -spat -57.7 -33.8 -49.6 -27.0 -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom

echo "=== 3. Consolidando tabelas finais e removendo staging ==="
run_psql <<'SQL'
BEGIN;
DELETE FROM biome_unit WHERE version_id = 'ibge_biomas_250k:2019';
INSERT INTO biome_unit (version_id, biome_code, biome_name, geom)
SELECT 'ibge_biomas_250k:2019', COALESCE(cd_bioma::text, ''), COALESCE(bioma, ''), ST_MakeValid(geom)
FROM stg_bioma WHERE geom IS NOT NULL;

DELETE FROM soil_unit WHERE version_id = 'ibge_bdia_pedologia_250k:2023';
INSERT INTO soil_unit (version_id, soil_symbol, soil_class, soil_order, soil_suborder, geom)
SELECT 'ibge_bdia_pedologia_250k:2023', COALESCE(leg_ordem, ''), COALESCE(legenda, ''), COALESCE(ordem, ''), COALESCE(subordem, ''), ST_Multi(ST_CollectionExtract(ST_MakeValid(geom), 3))
FROM stg_pedo WHERE geom IS NOT NULL AND NOT ST_IsEmpty(geom);

DELETE FROM hydro_feature WHERE version_id = 'ibge_bc250_hidrografia:2026-03-03';
INSERT INTO hydro_feature (version_id, feature_kind, feature_name, regime, geom)
SELECT 'ibge_bc250_hidrografia:2026-03-03', 'WATERCOURSE', COALESCE(nome, ''), COALESCE(regime, ''), ST_MakeValid(geom)
FROM stg_hydro_l WHERE geom IS NOT NULL;

INSERT INTO hydro_feature (version_id, feature_kind, feature_name, regime, geom)
SELECT 'ibge_bc250_hidrografia:2026-03-03', 'WATER_BODY', COALESCE(nome, ''), COALESCE(regime, ''), ST_MakeValid(geom)
FROM stg_hydro_a WHERE geom IS NOT NULL;

DROP TABLE IF EXISTS stg_bioma, stg_pedo, stg_hydro_l, stg_hydro_a;

UPDATE dataset_version SET feature_count = (SELECT COUNT(*) FROM biome_unit WHERE version_id = 'ibge_biomas_250k:2019') WHERE version_id = 'ibge_biomas_250k:2019';
UPDATE dataset_version SET feature_count = (SELECT COUNT(*) FROM soil_unit WHERE version_id = 'ibge_bdia_pedologia_250k:2023') WHERE version_id = 'ibge_bdia_pedologia_250k:2023';
UPDATE dataset_version SET feature_count = (SELECT COUNT(*) FROM hydro_feature WHERE version_id = 'ibge_bc250_hidrografia:2026-03-03') WHERE version_id = 'ibge_bc250_hidrografia:2026-03-03';
COMMIT;
SQL

run_psql -c "SELECT version_id, feature_count FROM dataset_version WHERE source_id IN ('ibge_biomas_250k','ibge_bdia_pedologia_250k','ibge_bc250_hidrografia') ORDER BY version_id;"
