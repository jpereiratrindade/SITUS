# SITUS-ARQ-001 (v0.1.0) — Arquitetura do Sistema

## 1. Princípios arquiteturais

- **C++26 nativo + núcleo portável:** O núcleo `situs_core` (`src/core/synthesis.cpp`) usa C++26 puro sem dependências de plataforma (sem banco, sem rede, sem filesystem, sem DOM), permitindo compilação tanto pelo **GCC 16.2.1** (servidor nativo) quanto pelo **Emscripten 7.0.1** (Web Worker Wasm).
- **Interface web única:** A UI (`web/`) é servida localmente em `127.0.0.1:8096` com CSP restritiva (`script-src 'self' 'wasm-unsafe-eval'`), sem CDN, sem mapas de terceiros e sem armazenamento persistente de consultas.
- **Separação de responsabilidades:**
  - **PostGIS (`libpq`):** Fonte da verdade para catálogo, unidades territoriais (`territorial_unit` com índice GiST), interseções (`ST_Intersection`), cobertura (`ST_Covers`) e áreas geodésicas (`ST_Area(::geography)`).
  - **GDAL/GEOS/PROJ:** Leitura de GeoPackage (`RS_setores_CD2022.gpkg`), validação topológica (`IsValid`), reprojeção (`EPSG:4674 -> EPSG:4326`) e leitura categórica de raster GeoTIFF sem interpolação bilinear.
  - **Boost.Asio / Boost.Beast:** Servidor HTTP loopback síncrono/assíncrono com limites de corpo (`1 MiB`), bloqueio de `Origin` externo e zero log de payloads ou query strings.
  - **OpenSSL (`EVP`):** Verificação SHA-256 dos artefatos ingeridos e do binário `.wasm`.

## 2. Banco de dados dedicado

- Container Podman rootless `situs-postgis-db` (`docker.io/postgis/postgis:17-3.5`) em `127.0.0.1:55436`.
- Separação de papéis: `situs_migrator` (migrações e ingestão administrativa) e `situs_reader` (consultas somente leitura na API/web).
- Nenhuma tabela de usuários, histórico de consultas, endereços ou indivíduos.
