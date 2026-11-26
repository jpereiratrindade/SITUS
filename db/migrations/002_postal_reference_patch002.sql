-- SITUS-PATCH-002 v0.2.0 — Evolucao do modelo postal_reference (Categoria A)
-- Suporta niveis R1-R5, geometrias publicas (ponto, linha, poligono) e proveniencia por fonte.

DROP TABLE IF EXISTS postal_reference CASCADE;

CREATE TABLE postal_reference (
    id BIGSERIAL PRIMARY KEY,
    version_id TEXT NOT NULL REFERENCES dataset_version(version_id) ON DELETE CASCADE,
    cep_code TEXT NOT NULL CHECK (cep_code ~ '^[0-9]{8}$'),
    resolution_level TEXT NOT NULL CHECK (resolution_level IN ('R1', 'R2', 'R3', 'R4', 'R5', 'POSTAL_REGION')),
    reference_kind TEXT NOT NULL CHECK (reference_kind IN ('MUNICIPALITY', 'DISTRICT_LOCALITY', 'NEIGHBORHOOD', 'PUBLIC_WAY', 'POSTAL_BOUNDARY')),
    reference_name TEXT NOT NULL DEFAULT '',
    municipality_code TEXT NOT NULL,
    municipality_name TEXT NOT NULL DEFAULT '',
    source_id TEXT NOT NULL REFERENCES source(source_id) ON DELETE RESTRICT,
    license TEXT NOT NULL,
    association_method TEXT NOT NULL,
    limitations TEXT NOT NULL,
    uncertainty TEXT NOT NULL DEFAULT 'Referencia territorial publica; nao representa area oficial de entrega dos Correios.',
    support_geom geometry(Geometry, 4326)
);

CREATE INDEX idx_postal_reference_cep ON postal_reference (cep_code);
CREATE INDEX idx_postal_reference_mun ON postal_reference (municipality_code);
CREATE INDEX idx_postal_reference_geom ON postal_reference USING GIST (support_geom);

INSERT INTO schema_migration (version, description)
VALUES ('002', 'Evolucao postal_reference para niveis R1-R5, geometria de suporte e proveniencia (PATCH-002)')
ON CONFLICT (version) DO NOTHING;
