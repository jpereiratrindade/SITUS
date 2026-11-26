# SITUS-REL-001 (v0.2.1) — Consolidação da Fundação (Etapa I)

**Data:** 2026-11-26  
**Referência:** `SITUS-DIR-001` (Etapa I — `SITUS-PATCH-001 v0.2.1`)

## 1. Esclarecimento da Divergência na Ingestão IBGE (3.1)

A auditoria direta sobre `var/data/RS_setores_CD2022.gpkg` (`25.575` feições) esclareceu integralmente a diferença entre lidas, válidas e inseridas:

- **2 geometrias inválidas (`skipped_invalid = 2`, não reparadas silenciosamente):**
  1. `430470505000123` (Carazinho, Rural, `21,92657 km²`) — `Ring Self-intersection[-52.7395567 -28.2567965]`
  2. `431440705160134` (Pelotas, Urbana, `0,11276 km²`) — `Ring Self-intersection[-52.3321901 -31.7662752]`
- **6 setores multiparte com múltiplas linhas no GPKG (`merged_multipart_features = 6`):**
  - `431490205001131` (Porto Alegre), `431490205001259` (Porto Alegre), `431490205003016` (Porto Alegre), `431490205003029` (Porto Alegre), `431850705060006` (São José do Norte), `432150105000013` (Torres).
  - **Causa original:** `ON CONFLICT DO NOTHING` descartava a segunda parte disjunta de cada um desses 6 setores.
  - **Correção aplicada (`2022.2`):** `ON CONFLICT (version_id, unit_code) DO UPDATE SET geom = ST_Multi(ST_Union(...)), area_m2 = ST_Area(...::geography)`.
  - **Balanço fechado:** `25.567` inseridos + `6` partes unidas + `2` inválidos = `25.575` feições lidas.

## 2. Esclarecimento da Cobertura nos Recortes de Porto Alegre (3.2)

Após a união dos 6 setores multiparte (que incluíam `431490205003029` no Centro e `431490205003016` no Extremo Sul):
- **Centro POA:** cobertura subiu de `93,05%` (`90` setores) para **`99,99986%` (`91` setores)** (`2.138.511,12 m²` / `2.138.514,02 m²`).
- **Extremo Sul POA:** cobertura subiu de `88,26%` (`39` setores) para **`100,0%` (`40` setores)** (`32.022.164,53 m²` / `32.022.064,30 m²`).

Isso comprova que a perda anterior não era ausência na fonte oficial, mas sim o descarte da segunda parte disjunta dos setores multiparte.

## 3. Execução Adaptativa e Confrontação Nativo × Wasm (3.3–3.4)

- Eliminada a execução redundante de `territorial_area_synthesis` no servidor quando a UI solicita caracterização (`skip_synthesis: true`), executando a síntese **uma única vez** no destino selecionado.
- Modo `AUTO` ajustado para selecionar `HYBRID` (`>= 25` unidades dentro dos limites) sem exigir aquecimento manual prévio do Worker.
- `tests/e2e/verify_real_territories.sh` corrigido e verificado com confrontação real **Nativo × Wasm** (`Diff = 0.0 m²` em ambos os recortes de Porto Alegre).

## 4. Auditoria de Segurança e Privacidade (3.5)

- Zero URLs externas em `web/`; zero gravação de consultas em `localStorage`/`IndexedDB`/servidor; logs HTTP restritos a método e rota sem query string ou payload.
