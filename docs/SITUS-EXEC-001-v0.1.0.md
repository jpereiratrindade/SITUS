# SITUS-EXEC-001 (v0.1.0) — Execução Computacional Adaptativa

## 1. Decisão do Experimento Fundador (P0)

Foi adotada a segunda opção prevista na seção 5.1 do mandato: **`territorial_area_synthesis`** (síntese determinística de áreas e proporções por unidade territorial e situação urbana/rural) como primeira operação compartilhada entre o servidor nativo C++26 e o navegador via WebAssembly (Emscripten + Web Worker).

- **Justificativa técnica:** A cadeia geométrica completa (GDAL/GEOS/PROJ + PostGIS `ST_Intersection`/`ST_Area(::geography)`) permanece no servidor nativo sem necessidade de portar toda a pilha geoespacial C/C++ para o navegador neste patch. No fluxo adaptativo/híbrido, o PostGIS calcula as interseções geodésicas reais (não simplificadas) e entrega o conjunto completo com denominador (`reference_area_m2`), regra explícita de sobreposição (`DISJOINT_SECTORS`) e `dataset_versions` para o núcleo portável C++26 (`src/core/synthesis.cpp`).
- **Limitação explícita:** Esta opção demonstra processamento local de síntese e execução híbrida, **não** demonstra interseção geométrica local no navegador.

## 2. Matriz de Operações e Destinos

| `operation_id` | Versão | Destinos suportados | Política | Motivo |
|---|---|---|---|---|
| `territorial_area_synthesis` | `0.2.0` | `NATIVE_SERVER`, `BROWSER_WASM`, `HYBRID` | `ADAPTIVE_ALLOWED` | Núcleo C++26 puro (`src/core/synthesis.cpp`), sem I/O de plataforma. |
| `territorial_intersect` | `0.2.0` | `NATIVE_SERVER` | `SERVER_ONLY` | Depende de PostGIS/GEOS/PROJ e catálogo oficial no banco. |
| `environmental_raster_composition` | `0.2.0` | `NATIVE_SERVER` | `SERVER_ONLY` | Depende de GDAL sobre GeoTIFF local aprovado. |

## 3. Política de Seleção e Perfis de Recursos

- **Modos:** `AUTO` (padrão), `PREFER_LOCAL`, `PREFER_SERVER`, `HYBRID`.
- **Regra conservadora em `AUTO`:** Executa no `NATIVE_SERVER` quando o Worker Wasm está frio ou a carga possui menos de `25` unidades; utiliza `HYBRID` (PostGIS + Worker Wasm) quando o Worker está aquecido, a carga tem `>= 25` unidades e está dentro dos limites do perfil ativo (`ECONOMY`: 500 unid / 256 KiB; `BALANCED`: 2.500 unid / 1 MiB; `PERFORMANCE`: 10.000 unid / 4 MiB).
- **Cancelamento e fallback:** Timeout e cancelamento encerram o Web Worker (`worker.terminate()`). Em falha recuperável e com `allow_fallback=true`, ocorre no máximo uma tentativa no `NATIVE_SERVER` com registro de `fallback_reason`.

## 4. Contrato de Equivalência e Tolerâncias

- **Ordenação determinística:** Grupos ordenados por `intersected_area_m2` decrescente (desempate por `group_key` ascendente); unidades por `unit_code` ascendente.
- **Tolerância numérica:** `area_m2_abs <= 1e-2 m²` e `fraction_abs <= 1e-6`.
- **Rejeição idêntica:** Entradas com `reference_area_m2 <= 0`, `dataset_versions` vazio ou soma de áreas excedendo a referência sob `DISJOINT_SECTORS` são rejeitadas igualmente em ambos os destinos.

## 5. Resultados Comparativos Reais

Medições executadas localmente via `tests/execution/benchmark_compare.mjs` (15 iterações aquecidas após warmup; inicialização fria Wasm em Node/Emscripten: `0.928 ms`):

| Carga | Unidades | Bytes | Nativo exec (mediana) | Nativo HTTP roundtrip (mediana / p95) | Wasm aquecido (mediana / p95) | Diferença máx. área |
|---|---:|---:|---:|---:|---:|---:|
| Pequena | 10 | 1.478 | `0.006 ms` | `1.780 ms` / `13.881 ms` | `0.102 ms` / `0.265 ms` | `0 m²` |
| Média | 250 | 31.676 | `0.074 ms` | `2.363 ms` / `3.251 ms` | `1.029 ms` / `1.369 ms` | `0 m²` |
| Maior | 2.000 | 251.930 | `0.626 ms` | `8.602 ms` / `15.116 ms` | `7.644 ms` / `9.362 ms` | `0 m²` |

**Conclusão operacional:** Para cálculo puro de síntese, o servidor nativo C++26 (GCC 16.2.1) é ~12–16x mais rápido que o Wasm (Emscripten 7.0.1) porque evita a cópia UTF-8/JSON na fronteira JS↔Wasm. Contudo, quando comparado ao roundtrip HTTP local (`1.78`–`8.60 ms`), o Wasm aquecido já no navegador é competitivo para cargas médias (`250` unidades: `1.029 ms` vs `2.363 ms`), justificando manter o modo `AUTO` conservador e a opção explícita `PREFER_LOCAL` / `HYBRID`.

> **Nota de escopo da medição:** A execução Wasm comparada acima foi medida em runtime Node/Emscripten sobre o mesmo binário `web/wasm/situs_core.js` + `situs_core.wasm`. A execução no navegador via Web Worker (`/wasm/worker.js`) está implementada e servida com MIME `application/wasm` e CSP compatível, podendo ser acionada diretamente em `Configurações → Processamento → Executar comparação`.
