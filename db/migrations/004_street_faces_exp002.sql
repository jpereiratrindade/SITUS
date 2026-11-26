-- SITUS-EXP-002 — Base de Faces de Logradouros do IBGE (Censo 2022, Categoria A)

CREATE TABLE IF NOT EXISTS street_face (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    municipality_code TEXT NOT NULL,
    sector_code TEXT NOT NULL,
    quadra_code TEXT NOT NULL DEFAULT '',
    face_code TEXT NOT NULL DEFAULT '',
    tip_log TEXT NOT NULL DEFAULT '',
    tit_log TEXT NOT NULL DEFAULT '',
    log_name TEXT NOT NULL DEFAULT '',
    full_name TEXT NOT NULL DEFAULT '',
    normalized_name TEXT NOT NULL DEFAULT '',
    tot_res INTEGER NOT NULL DEFAULT 0,
    tot_geral INTEGER NOT NULL DEFAULT 0,
    geom geometry(MultiLineString, 4326) NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_street_face_mun_norm ON street_face (municipality_code, normalized_name);
CREATE INDEX IF NOT EXISTS idx_street_face_norm ON street_face (normalized_name);
CREATE INDEX IF NOT EXISTS idx_street_face_sector ON street_face (sector_code);
CREATE INDEX IF NOT EXISTS idx_street_face_geom ON street_face USING GIST (geom);

INSERT INTO schema_migration (version, description)
VALUES ('004', 'Base de Faces de Logradouros do IBGE Censo 2022 (EXP-002)')
ON CONFLICT (version) DO NOTHING;
