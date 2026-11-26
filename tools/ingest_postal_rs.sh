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

DATA_DIR="${SITUS_DATA_DIR:-var/data}/patch002_eval"
BR_ZIP="${DATA_DIR}/BR.zip"
OSM_PBF="${DATA_DIR}/sul-latest.osm.pbf"
DB_HOST="${SITUS_DB_HOST:-127.0.0.1}"
DB_PORT="${SITUS_DB_PORT:-55436}"
DB_NAME="${SITUS_DB_NAME:-situs}"
DB_ADMIN_USER="${SITUS_DB_ADMIN_USER:-situs_migrator}"
DB_ADMIN_PASSWORD="${SITUS_DB_ADMIN_PASSWORD:-change_me_migrator_local}"
export PGPASSWORD="$DB_ADMIN_PASSWORD"

run_psql() {
    psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_ADMIN_USER" -d "$DB_NAME" "$@"
}

if [ ! -f "$BR_ZIP" ] || [ ! -f "$OSM_PBF" ]; then
    echo "[ERRO] Artefatos BR.zip ou sul-latest.osm.pbf nao encontrados em $DATA_DIR" >&2
    exit 1
fi

BR_SHA=$(sha256sum "$BR_ZIP" | awk '{print $1}')
OSM_SHA=$(sha256sum "$OSM_PBF" | awk '{print $1}')

TMP_DIR="${DATA_DIR}/osm_tmp_ingest"
mkdir -p "$TMP_DIR"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "=== 1. Extraindo feicoes territoriais Categoria A do OSM (osmium) ==="
osmium extract -b -57.7,-33.8,-49.6,-27.0 "$OSM_PBF" -o "${TMP_DIR}/rs_bbox.osm.pbf" --overwrite
osmium tags-filter "${TMP_DIR}/rs_bbox.osm.pbf" w/highway nwr/boundary=postal_code nwr/boundary=administrative nwr/place -o "${TMP_DIR}/rs_territorial.osm.pbf" --overwrite
osmium tags-filter "${TMP_DIR}/rs_territorial.osm.pbf" postal_code -o "${TMP_DIR}/rs_postal.osm.pbf" --overwrite
osmium export -c config/osmium_postal_cat_a.json -f geojsonseq "${TMP_DIR}/rs_postal.osm.pbf" -o "${TMP_DIR}/rs_postal.geojsonseq" --overwrite

echo "=== 2. Preparando TSV de ingestao (GeoNames R1 + OSM R1-R4) ==="
python3 - "$BR_ZIP" "${TMP_DIR}/rs_postal.geojsonseq" "${TMP_DIR}/geonames_rs.tsv" "${TMP_DIR}/osm_rs.tsv" <<'PY'
import io, json, re, sys, zipfile

br_zip, osm_seq, out_geo, out_osm = sys.argv[1:5]
CEP_RE = re.compile(r'\b(9\d{4})-?(\d{3})\b')

# 1. GeoNames RS
with zipfile.ZipFile(br_zip) as zf, zf.open('BR.txt') as bf, open(out_geo, 'w', encoding='utf-8') as out:
    for raw in io.TextIOWrapper(bf, encoding='utf-8'):
        cols = raw.rstrip('\n').split('\t')
        if len(cols) >= 11 and cols[6].startswith('43'):
            cep = cols[1].replace('-', '').strip()
            if not re.match(r'^9\d{7}$', cep):
                continue
            cd_mun = cols[6].strip()
            nm_mun = cols[5].strip().replace('\t', ' ')
            lon, lat = cols[10].strip(), cols[9].strip()
            out.write(f"{cep}\t{cd_mun}\t{nm_mun}\t{lon}\t{lat}\n")

# 2. OSM RS (exclusoes defensivas)
def classify(props):
    if props.get('boundary') == 'postal_code':
        return ('POSTAL_REGION', 'POSTAL_BOUNDARY')
    if props.get('boundary') == 'administrative':
        al = str(props.get('admin_level', ''))
        if al == '8':
            return ('R1', 'MUNICIPALITY')
        return ('R2', 'DISTRICT_LOCALITY')
    place = props.get('place', '')
    if place:
        if place in ('city', 'municipality'):
            return ('R1', 'MUNICIPALITY')
        if place in ('suburb', 'neighbourhood', 'quarter'):
            return ('R3', 'NEIGHBORHOOD')
        return ('R2', 'DISTRICT_LOCALITY')
    if props.get('highway'):
        return ('R4', 'PUBLIC_WAY')
    return (None, None)

with open(osm_seq, encoding='utf-8') as f, open(out_osm, 'w', encoding='utf-8') as out:
    for line in f:
        line = line.strip()
        if not line:
            continue
        if line.startswith('\x1e'):
            line = line[1:]
        feat = json.loads(line)
        props = feat.get('properties', {})
        if props.get('access') in ('private', 'no') or props.get('highway') == 'service':
            continue
        if 'building' in props or 'addr:housenumber' in props:
            continue
        geom = feat.get('geometry')
        if not geom:
            continue
        ceps = [m[0] + m[1] for m in CEP_RE.findall(str(props.get('postal_code', '')))]
        if not ceps:
            continue
        res_lvl, ref_kind = classify(props)
        if not res_lvl:
            continue
        ref_name = str(props.get('name', '')).replace('\t', ' ').replace('\n', ' ').strip()
        geom_json = json.dumps(geom, separators=(',', ':'))
        for cep in set(ceps):
            out.write(f"{cep}\t{res_lvl}\t{ref_kind}\t{ref_name}\t{geom_json}\n")
PY

echo "=== 3. Carregando e validando espacialmente no PostGIS (transacional) ==="
run_psql <<SQL
BEGIN;

INSERT INTO source (source_id, title, provider, category, approval_status, license, source_url, scope, spatial_level, native_crs, native_resolution, allowed_fields, limitations)
VALUES
  ('geonames_postal_br', 'GeoNames Referencias Postais (BR/RS)', 'GeoNames', 'A', 'APPROVED', 'CC BY 3.0', 'https://download.geonames.org/export/zip/BR.zip', 'RS', 'R1_MUNICIPALITY', 'EPSG:4326', 'Sede municipal (ponto representativo + poligono IBGE)', '["cep_code","municipality_code","municipality_name","lat","lon"]'::jsonb, 'Cobre 467 CEPs gerais de sedes municipais no RS; nao resolve logradouros.'),
  ('osm_postal_sul_rs', 'OpenStreetMap Vias e Limites Postais (RS)', 'OpenStreetMap / Geofabrik', 'A', 'APPROVED', 'ODbL 1.0', 'https://download.geofabrik.de/south-america/brazil/sul-latest.osm.pbf', 'RS', 'R1_TO_R4_PUBLIC_TERRITORIAL', 'EPSG:4326', 'Escala colaborativa OSM (linhas/poligonos publicos)', '["postal_code","highway","name","boundary","admin_level","place"]'::jsonb, 'Exclui edificacoes e enderecos individuais. Cobertura submunicipal heterogenea entre municipios.')
ON CONFLICT (source_id) DO UPDATE SET approval_status = 'APPROVED', license = EXCLUDED.license;

UPDATE dataset_version SET is_active = FALSE WHERE source_id IN ('geonames_postal_br', 'osm_postal_sul_rs');

INSERT INTO dataset_version (version_id, source_id, dataset_version, artifact_sha256, is_active, ingested_at)
VALUES
  ('geonames_postal_br:2026-11', 'geonames_postal_br', '2026-11', '${BR_SHA}', TRUE, now()),
  ('osm_postal_sul_rs:2026-11-25', 'osm_postal_sul_rs', '2026-11-25', '${OSM_SHA}', TRUE, now())
ON CONFLICT (version_id) DO UPDATE SET artifact_sha256 = EXCLUDED.artifact_sha256, is_active = TRUE, ingested_at = now();

DELETE FROM postal_reference WHERE version_id IN ('geonames_postal_br:2026-11', 'osm_postal_sul_rs:2026-11-25');
COMMIT;
SQL

# Copiar arquivos staging via stdin
{
  echo "BEGIN;"
  echo "CREATE TEMP TABLE stg_geo (cep_code TEXT, cd_mun TEXT, nm_mun TEXT, lon DOUBLE PRECISION, lat DOUBLE PRECISION);"
  echo "COPY stg_geo FROM STDIN;"
  cat "${TMP_DIR}/geonames_rs.tsv"
  echo "\\."
  echo "INSERT INTO postal_reference (version_id, cep_code, resolution_level, reference_kind, reference_name, municipality_code, municipality_name, source_id, license, association_method, limitations, support_geom)"
  echo "SELECT 'geonames_postal_br:2026-11', g.cep_code, 'R1', 'MUNICIPALITY', m.municipality_name, m.municipality_code, m.municipality_name, 'geonames_postal_br', 'CC BY 3.0', 'geonames_cd_mun_validated_against_ibge', 'CEP geral municipal; nao indica posicao submunicipal.', ST_SetSRID(ST_MakePoint(g.lon, g.lat), 4326)"
  echo "FROM stg_geo g JOIN (SELECT DISTINCT municipality_code, municipality_name FROM territorial_unit WHERE uf_code = '43' AND municipality_code <> '') m ON m.municipality_code = g.cd_mun;"
  echo "CREATE TEMP TABLE stg_osm (cep_code TEXT, res_lvl TEXT, ref_kind TEXT, ref_name TEXT, geom_json TEXT);"
  echo "COPY stg_osm FROM STDIN;"
  cat "${TMP_DIR}/osm_rs.tsv"
  echo "\\."
  echo "INSERT INTO postal_reference (version_id, cep_code, resolution_level, reference_kind, reference_name, municipality_code, municipality_name, source_id, license, association_method, limitations, support_geom)"
  echo "SELECT DISTINCT ON (s.cep_code, s.res_lvl, s.ref_kind, s.ref_name, tu.municipality_code) 'osm_postal_sul_rs:2026-11-25', s.cep_code, s.res_lvl, s.ref_kind, s.ref_name, tu.municipality_code, tu.municipality_name, 'osm_postal_sul_rs', 'ODbL 1.0', 'osm_territorial_st_intersects_ibge_rs', 'Suporte territorial publico OSM (exclui imoveis/edificacoes); cobertura heterogenea.', ST_SetSRID(ST_GeomFromGeoJSON(s.geom_json), 4326)"
  echo "FROM stg_osm s JOIN territorial_unit tu ON tu.uf_code = '43' AND tu.municipality_code <> '' AND ST_Intersects(tu.geom, ST_Centroid(ST_SetSRID(ST_GeomFromGeoJSON(s.geom_json), 4326))) JOIN dataset_version dv ON dv.version_id = tu.version_id AND dv.is_active = TRUE;"
  echo "UPDATE dataset_version dv SET feature_count = (SELECT COUNT(*) FROM postal_reference pr WHERE pr.version_id = dv.version_id) WHERE dv.version_id IN ('geonames_postal_br:2026-11', 'osm_postal_sul_rs:2026-11-25');"
  echo "COMMIT;"
} | run_psql

echo "=== 4. Resumo pos-ingestao postal ==="
run_psql -c "
SELECT source_id, resolution_level, COUNT(*) AS registros, COUNT(DISTINCT cep_code) AS ceps_distintos, COUNT(DISTINCT municipality_code) AS municipios_distintos
FROM postal_reference
GROUP BY source_id, resolution_level
ORDER BY source_id, resolution_level;
"
