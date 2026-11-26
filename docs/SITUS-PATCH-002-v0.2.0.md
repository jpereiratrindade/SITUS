---
document_id: SITUS-PATCH-002
title: Resolução Territorial de Referências Postais (Categoria A)
version: 0.2.0
date: 2026-11-26
status: IMPLEMENTADO_E_VERIFICADO_COM_DADOS_REAIS
scope: RS
data_policy: CATEGORY_A_ONLY
---

# SITUS — PATCH-002 (v0.2.0)

**Resolução territorial de CEP (`R0`–`R5`), busca por nome de município e preservação de ambiguidades**

## 1. Implementação Entregue

- **Migração `002` ([002_postal_reference_patch002.sql](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/db/migrations/002_postal_reference_patch002.sql)):** `postal_reference` evoluída para suportar `resolution_level` (`R1`–`R5`, `POSTAL_REGION`), `reference_kind`, `support_geom` (`Geometry(Geometry, 4326)` com índice GiST), `license`, `association_method`, `limitations` e `uncertainty`.
- **Ingestão reproduzível ([ingest_postal_rs.sh](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/tools/ingest_postal_rs.sh)):**
  - `geonames_postal_br` (`CC BY 3.0`): `467` registros `R1` validados contra IBGE.
  - `osm_postal_sul_rs` (`ODbL 1.0`): `4.523` registros (`211` `R1`, `912` `R2`, `19` `R3`, `3.374` `R4`, `7` `POSTAL_REGION`), excluindo imóveis, edificações e vias privadas antes da carga, e validando interseção espacial contra o RS.
- **Resolvedor e UI ([postgis_store.cpp](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/src/spatial/postgis_store.cpp), [server.cpp](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/src/http/server.cpp), [app.js](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/web/js/app.js)):**
  - Busca por nome de município via `GET /api/v1/municipalities?q=...` com autocomplete na UI.
  - Resolução de CEP preservando ambiguidade entre municípios (`AMBIGUOUS`), concordância `GeoNames + OSM` (`R1`), suporte submunicipal público (`R2`–`R4`) e estado `UNAVAILABLE` (`R0`).

## 2. Verificação com Dados Reais

- `MUNICIPALITY` por nome `"Bagé"` → `RESOLVED` (`4.090,36 km²`).
- `CEP 96540-000` (Agudo) → `RESOLVED` (`R1`, `geonames_postal_br` + `osm_postal_sul_rs`).
- `CEP 90240-552` (Porto Alegre) → `RESOLVED` (`R4`, `Avenida Bahia`, `osm_postal_sul_rs`).
- `CEP 98300-000` → `AMBIGUOUS` (`4313706` Palmeira das Missões vs `4320206` Seberi).
- `CEP 96415-610` → `UNAVAILABLE` (`R0`).
