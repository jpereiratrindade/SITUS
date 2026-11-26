-- SITUS — Schema inicial (Categoria A estrita)
-- Nenhuma tabela de usuários, histórico de consultas, endereços ou indivíduos.

CREATE EXTENSION IF NOT EXISTS postgis;

CREATE TABLE IF NOT EXISTS schema_migration (
    version TEXT PRIMARY KEY,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    description TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS source (
    source_id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    provider TEXT NOT NULL,
    category TEXT NOT NULL CHECK (category = 'A'),
    approval_status TEXT NOT NULL CHECK (approval_status IN ('APPROVED', 'PENDING_ACQUISITION', 'PENDING_VALIDATION', 'REJECTED')),
    license TEXT NOT NULL,
    source_url TEXT NOT NULL,
    catalog_url TEXT,
    scope TEXT NOT NULL,
    spatial_level TEXT NOT NULL,
    reference_date DATE,
    native_crs TEXT NOT NULL,
    native_resolution TEXT NOT NULL,
    allowed_fields JSONB NOT NULL DEFAULT '[]'::jsonb,
    limitations TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS dataset_version (
    version_id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL REFERENCES source(source_id) ON DELETE RESTRICT,
    dataset_version TEXT NOT NULL,
    artifact_sha256 TEXT NOT NULL,
    acquired_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    ingested_at TIMESTAMPTZ,
    feature_count BIGINT NOT NULL DEFAULT 0,
    is_active BOOLEAN NOT NULL DEFAULT FALSE,
    notes TEXT NOT NULL DEFAULT '',
    UNIQUE (source_id, dataset_version)
);

CREATE TABLE IF NOT EXISTS layer (
    layer_id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL REFERENCES source(source_id) ON DELETE RESTRICT,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE RESTRICT,
    title TEXT NOT NULL,
    geometry_kind TEXT NOT NULL CHECK (geometry_kind IN ('MULTIPOLYGON', 'RASTER')),
    storage_srid INTEGER NOT NULL DEFAULT 4326,
    is_available BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS territorial_unit (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    unit_code TEXT NOT NULL,
    unit_level TEXT NOT NULL,
    municipality_code TEXT,
    municipality_name TEXT,
    uf_code TEXT,
    situation_code TEXT,
    situation_label TEXT,
    district_name TEXT,
    neighborhood_name TEXT,
    attributes JSONB NOT NULL DEFAULT '{}'::jsonb,
    geom geometry(MultiPolygon, 4326) NOT NULL,
    area_m2 DOUBLE PRECISION NOT NULL,
    UNIQUE (version_id, unit_code)
);

CREATE INDEX IF NOT EXISTS idx_territorial_unit_geom
    ON territorial_unit USING GIST (geom);

CREATE INDEX IF NOT EXISTS idx_territorial_unit_mun
    ON territorial_unit (municipality_code, unit_level);

CREATE INDEX IF NOT EXISTS idx_territorial_unit_code
    ON territorial_unit (unit_code);

CREATE TABLE IF NOT EXISTS postal_reference (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    cep_code TEXT NOT NULL CHECK (cep_code ~ '^[0-9]{8}$'),
    resolution_level TEXT NOT NULL CHECK (resolution_level IN ('MUNICIPALITY', 'DISTRICT', 'PUBLIC_LOCALITY')),
    unit_code TEXT NOT NULL,
    municipality_code TEXT NOT NULL,
    support_note TEXT NOT NULL,
    UNIQUE (version_id, cep_code, unit_code)
);

INSERT INTO schema_migration (version, description)
VALUES ('001', 'Schema inicial SITUS Categoria A (source, dataset_version, layer, territorial_unit, postal_reference)')
ON CONFLICT (version) DO NOTHING;
