# SITUS-REL-001 (v0.1.0) — Relatório de Aceite (PATCH-001 v0.2.0)

**Data:** 2026-11-26  
**Ambiente:** Fedora Silverblue (toolbox `situs-dev`: GCC 16.2.1, CMake 4.3.0, Ninja 1.13.2, Emscripten 7.0.1, Boost 1.90, nlohmann_json 3.12.0, libpq 18, GDAL 3.12.4, GEOS 3.14.1, PROJ 9.8.1, OpenSSL 3.5.9).  
**Banco dedicado:** Podman rootless `situs-postgis-db` (`docker.io/postgis/postgis:17-3.5`) em `127.0.0.1:55436/situs`.  
**Servidor web/API:** Loopback `127.0.0.1:8096`.

## 1. Classificação por Critério de Aceitação

| Critério | Estado | Evidência observada |
|---|---|---|
| A01 | `TESTADO` | Build C++26 (`SITUS_HAS_CXX26 - Success`) nos presets `dev` e `wasm-test`; `policy_tests`, `synthesis_contract_tests` e `wasm_core_synthesis_contract` passaram 100%. |
| A02 | `VERIFICADO_COM_DADOS_REAIS` | `/health` e `/api/v1/catalog` servidos em `127.0.0.1:8096`; após ingestão, catálogo exibe `ibge_setores_rs_2022` (`2022.1`, SHA-256, `EPSG:4674`). |
| A03 | `VERIFICADO_COM_DADOS_REAIS` | `RS_setores_CD2022.gpkg` (SHA-256 `3816950f...`) ingerido: `25.575` lidos, `25.573` válidos (`2` inválidos por `Ring Self-intersection` pulados), `25.567` inseridos; segunda ingestão retornou `already_ingested: true`. |
| A04 | `VERIFICADO_COM_DADOS_REAIS` | Dois recortes em Porto Alegre (`4314902`): Centro (`90` setores, `1.989.792,07 m²`, `93,05%` cobertura) vs Extremo Sul (`39` setores, `28.263.627,40 m²`, `88,26%` cobertura em 3 grupos). |
| A05 | `TESTADO` | CRS `EPSG:3857` rejeitado com HTTP `422`; ponto no oceano retornou `OUTSIDE_COVERAGE`; ponto em POA retornou `RESOLVED` via `ST_Covers`. |
| A06 | `TESTADO` | `test_policy` valida rejeição de fonte Categoria B e sem licença, além de filtragem positiva de `allowed_fields`; servidor sem log de payloads ou persistência de consultas. |
| A07 | `PENDENTE` | CEP sintaticamente válido (`90010-150`) retorna `UNAVAILABLE` honestamente; sem fonte CEP Categoria A licenciada disponível nesta fase. |
| A08 | `PENDENTE` | Leitor GDAL categórico (`src/spatial/raster_service.cpp`) implementado; sem GeoTIFF MapBiomas adquirido administrativamente nesta fase (`NO_DATA`). |
| A09 | `TESTADO` | Interface local sem CDN/telemetria, CSP restritiva, bind loopback `127.0.0.1:8096`, `Origin` externo bloqueado, SQL parametrizado via `PQexecParams`. |
| A10 | `IMPLEMENTADO` | Repositório Git em `SITUS`, licença GPL v3.0, documentação versionada, scripts reproduzíveis; nenhum projeto irmão alterado; MCP apenas contratado (`contracts/mcp-readonly.md`). |
| A11 | `TESTADO` | Núcleo `situs_core` compilado em C++26 tanto no GCC 16.2.1 quanto no Emscripten 7.0.1 (`web/wasm/situs_core.wasm`, SHA-256 `42a2fa02...`). |
| A12 | `TESTADO` | Operação `territorial_area_synthesis` confrontada entre servidor nativo e módulo Wasm com `0 m²` de diferença máxima; interseção geométrica permanece nativa no PostGIS. |
| A13 | `IMPLEMENTADO` | UI única com `Configurações → Processamento`, modos `AUTO`, `PREFER_LOCAL`, `PREFER_SERVER`, `HYBRID`, perfis, cancelamento e fallback; fluxo nativo funciona sem Wasm. |
| A14 | `TESTADO` | Somente fontes A aprovadas, sem credenciais no navegador, sem persistência de consultas no browser/servidor. |
| A15 | `TESTADO` | Benchmark comparativo reproduzível (`tests/execution/benchmark_compare.mjs`) registrado em `SITUS-EXEC-001-v0.1.0.md`. |

## 2. Síntese das Três Dimensões de Aceite

- **Fundação territorial nativa (A01–A06, A09–A10):** **ATINGIDA** (fundação vetorial real verificada com a malha IBGE RS 2022; CEP e raster permanecem `PENDENTE` conforme A07/A08).
- **Execução adaptativa demonstrada (A11–A15):** **DEMONSTRADA** em código, build dual C++26 e confronto numérico servidor vs Wasm (Nota: a execução em Web Worker no navegador real pode ser inspecionada abrindo `http://127.0.0.1:8096` no Firefox).
- **Aceite integral do PATCH-001 v0.2.0:** **PARCIAL** (pendente apenas aquisição administrativa de GeoTIFF MapBiomas RS para A08 e validação de fonte CEP Categoria A para A07).
