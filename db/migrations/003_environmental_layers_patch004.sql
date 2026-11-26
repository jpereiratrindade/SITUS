-- SITUS-PATCH-004 v0.1.0 — Camadas ambientais e territoriais multidimensionais (Categoria A)

CREATE TABLE IF NOT EXISTS biome_unit (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    biome_code TEXT NOT NULL,
    biome_name TEXT NOT NULL,
    geom geometry(MultiPolygon, 4326) NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_biome_unit_geom ON biome_unit USING GIST (geom);

CREATE TABLE IF NOT EXISTS soil_unit (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    soil_symbol TEXT NOT NULL DEFAULT '',
    soil_class TEXT NOT NULL DEFAULT '',
    soil_order TEXT NOT NULL DEFAULT '',
    soil_suborder TEXT NOT NULL DEFAULT '',
    geom geometry(MultiPolygon, 4326) NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_soil_unit_geom ON soil_unit USING GIST (geom);

CREATE TABLE IF NOT EXISTS hydro_feature (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    feature_kind TEXT NOT NULL CHECK (feature_kind IN ('WATERCOURSE', 'WATER_BODY', 'WATERSHED')),
    feature_name TEXT NOT NULL DEFAULT '',
    regime TEXT NOT NULL DEFAULT '',
    geom geometry(Geometry, 4326) NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_hydro_feature_geom ON hydro_feature USING GIST (geom);

INSERT INTO schema_migration (version, description)
VALUES ('003', 'Camadas ambientais biome_unit, soil_unit e hydro_feature (PATCH-004)')
ON CONFLICT (version) DO NOTHING;
