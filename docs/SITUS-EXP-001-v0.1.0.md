---
document_id: SITUS-EXP-001
title: Diagnóstico Experimental de Fontes Postais Categoria A no Rio Grande do Sul
version: 0.1.0
date: 2026-11-26
scope: RS
policy: CATEGORY_A_ONLY
status: CONCLUIDO
---

# SITUS-EXP-001 (v0.1.0) — Investigação Experimental de Fontes Postais (RS)

Este relatório documenta a avaliação empírica das fontes postais candidatas para o **SITUS-PATCH-002**, realizada em ambiente isolado e fora da base operacional do SITUS, preservando estritamente a política `CATEGORY_A_ONLY`. Nenhuma correspondência foi inferida por proximidade numérica entre CEPs e nenhum resolvedor definitivo foi ativado nesta etapa.

---

## 1. Proveniência, Versões, Licenças e Hashes

| Fonte | Artefato avaliado | Licença | Data / Referência | SHA-256 |
|---|---|---|---|---|
| **IBGE — Malha Oficial RS** | `var/data/RS_setores_CD2022.gpkg` (referência de validação) | Dados Abertos IBGE | Censo 2022 | `3816950f005616207e21b120b64d6d587d8457fd7788ed2a231595ab3ca2f711` |
| **GeoNames** | `BR.zip` (`132 KB`) | CC BY 3.0 | 2026-11-26 (download) | `0365fb77aaa1da9d932eb4c044aae544c2066db874a9319623a03fdec0e71c1b` |
| **OpenStreetMap / Geofabrik** | `sul-latest.osm.pbf` (`411,4 MiB`) | ODbL 1.0 | 2026-11-25 00:11 UTC | `0a097c11ebb300e7f6aecde426e0fc051627ff31b1237737ca46c709c59aa236` |
| **IBGE — CNEFE Agregados por CEP** | `Agregados_por_CEP_CNEFE_Censo_2022.zip` (`8,6 MB`) | Dados Abertos IBGE | 2024-05-23 / pub. 2024-06-14 | `38ff712959f26e5f40b81dc5001f34b675d6ced04aa8a379fb004a4d94945908` |

> **Nota sobre IBGE Agregados por CEP:** O CSV (`38,7 MB`, `931+ mil` linhas) contém apenas `CEP` e contagens agregadas de tipos de estabelecimentos/domicílios (`TOT_END`, `TOT_DP`, etc.), sem `CD_MUN`, UF, localidade ou geometria. Não resolve território sozinho; pode atuar apenas como sinal secundário de existência censitária.

---

## 2. Métodos e Comandos Empregados

1. **GeoNames (`BR.zip`):** Cruzamento direto dos registros com prefixo `9xxxx` / `Rio Grande do Sul` / `CD_MUN 43xxxxx` contra os **497 municípios oficiais do RS** presentes na tabela `territorial_unit` (`uf_code = '43'`).
2. **OpenStreetMap (`sul-latest.osm.pbf`):** Extração isolada com `osmium-tool 1.19.1` dentro da toolbox `situs-dev` em diretório temporário (`var/data/patch002_eval/osm_tmp/`, eliminado após a análise):
   ```bash
   osmium extract -b -57.7,-33.8,-49.6,-27.0 sul-latest.osm.pbf -o osm_tmp/rs_bbox.osm.pbf
   osmium tags-filter osm_tmp/rs_bbox.osm.pbf w/highway nwr/boundary=postal_code nwr/boundary=administrative nwr/place -o osm_tmp/rs_territorial.osm.pbf
   osmium tags-filter osm_tmp/rs_territorial.osm.pbf postal_code -o osm_tmp/rs_postal_only.osm.pbf
   osmium export -c osmium_export_cat_a.json -f geojsonseq osm_tmp/rs_postal_only.osm.pbf -o osm_tmp/rs_postal_cat_a.geojsonseq
   ```
   - **Lista positiva de atributos (`include_tags`):** `postal_code`, `highway`, `name`, `boundary`, `admin_level`, `place`, `access`, `service` (sem metadados de usuário OSM).
   - **Critérios de exclusão aplicados:** edificações (`building=*`), endereços individualizados (`addr:housenumber=*`), vias privadas/restritas (`access=private`, `access=no`, `highway=service`) e CEPs fora da faixa gaúcha (`^9[0-9]{7}$`).

---

## 3. Resultados Quantitativos

### 3.1. GeoNames (Nível `R1` — Município)

- **Registros RS:** `467`
- **Municípios distintos validados na malha IBGE:** `467 / 497` (`93,96%`)
- **CEPs distintos:** `467` (relação `1:1` exata)
- **Códigos inválidos ou inexistentes no IBGE:** `0`
- **Duplicidades ou divergências de grafia:** `0`
- **Municípios ausentes (`30`):** majoritariamente municípios emancipados em 1995–2001 (ex.: `4300034` Aceguá, `4300471` Almirante Tamandaré do Sul, `4301073` Arroio do Padre, `4302220` Boa Vista do Cadeado, `4302238` Boa Vista do Incra, `4302584` Bozano, `4304614` Canudos do Vale, `4304622` Capão Bonito do Sul, `4304655` Capão do Cipó, `4305835` Coqueiro Baixo, `4305934` Coronel Pilar, `4306130` Cruzaltense, `4308433` Forquetinha, `4310652` Itati, `4310876` Jacuizinho).

### 3.2. OpenStreetMap (Níveis `R1`–`R4`, Categoria A estrita)

- **Feições excluídas por acesso privado / `highway=service`:** `858`
- **Feições excluídas por CEP fora da faixa `9xxxx-xxx` (SC/PR na borda do bbox):** `16.042`
- **Feições válidas pós-filtro dentro do RS:** `9.762`
- **CEPs distintos territorialmente admissíveis no RS:** `4.039`
- **Municípios do RS com ao menos 1 CEP no OSM:** `483 / 497` (`97,18%`)

Distribuição das `9.762` feições por tipo de suporte espacial:

| Tipo / Nível | Feições | Observação |
|---|---:|---|
| `R4_VIA_PUBLICA` (`highway` + `postal_code`) | `8.551` | Eixo de via pública (linha) |
| `R2_R3_SUBMUNICIPAL` (`admin_level=9/10` + `postal_code`) | `896` | Distritos e bairros administrativos |
| `R1_MUNICIPIO` (`admin_level=8` / `place=city`) | `211` | Limites municipais |
| `BOUNDARY_POSTAL` (`boundary=postal_code`) | `50` | Polígonos postais declarados no OSM |
| `R2_LOCALIDADE` (`place=town/village/hamlet/...`) | `30` | Localidades e sedes distritais |
| `R3_BAIRRO` (`place=suburb/neighbourhood`) | `19` | Bairros |
| `OUTRO` / `R2_R3_ADMIN` | `5` | Demais feições administrativas |

**Assimetria submunicipal observada no OSM:** A cobertura de vias (`R4`) varia fortemente entre municípios:
- Alta densidade: **Santa Maria (`4316907`)** com `1.450` CEPs, **Pelotas (`4314407`)** com `519` CEPs, **Osório (`4313508`)** com `435` CEPs, **Cachoeira do Sul (`4303004`)** com `131` CEPs, **Cruz Alta (`4306106`)** com `106` CEPs.
- Baixa densidade / esparsa: **Porto Alegre (`4314902`)** possui apenas `28` CEPs em vias públicas sob o filtro estrito sem edificações.

### 3.3. Combinação, Ambiguidades e Conflitos (`GeoNames ∪ OSM`)

- **Cobertura municipal combinada (`GeoNames ∪ OSM`):** **`497 / 497` municípios do RS (`100%`)**!
- **Interseção de CEPs (`GeoNames ∩ OSM`):** `420` CEPs.
- **CEPs ambíguos dentro do OSM (>1 município):** `12` CEPs (ex.: `98300000`, `97120000`, `97150000`, `96010620`, `93490460` — tipicamente vias ou localidades na divisa municipal).
- **Conflitos de município entre GeoNames e OSM:** `3` CEPs (`95599000`, `96745000`, `95795000`), associados a municípios limítrofes/emancipados onde o centroide da feição OSM ou o ponto GeoNames diverge quanto ao código IBGE.

---

## 4. Exemplos Reais Observados

- **Resolução Municipal (`R1` — GeoNames + malha oficial IBGE):**
  - `96540-000` → **Agudo** (`CD_MUN 4300109`), suporte = polígono oficial do município no IBGE.
  - `99965-000` → **Água Santa** (`CD_MUN 4300059`), suporte = polígono oficial do município no IBGE.
- **Resolução Submunicipal (`R2` / `R3` / `R4` — OSM Categoria A):**
  - `95240-000` → `[R2_LOCALIDADE]` **Ipê** (`CD_MUN 4310439`).
  - `97130-000` → `[R3_BAIRRO]` **Palma / Faxinal da Palma / Vila Almeida** em **Santa Maria** (`CD_MUN 4316907`).
  - `90240-552` → `[R4_VIA_PUBLICA]` **Avenida Bahia** em **Porto Alegre** (`CD_MUN 4314902`).
  - `91350-170` → `[R4_VIA_PUBLICA]` **Rua Jari** em **Porto Alegre** (`CD_MUN 4314902`).

---

## 5. Comparação entre Fontes e Recomendação para o PATCH-002

| Critério | GeoNames (`BR.zip`) | OpenStreetMap (`sul-latest.osm.pbf`) |
|---|---|---|
| **Licença** | CC BY 3.0 | ODbL 1.0 |
| **Granularidade** | Exclusivamente `R1` (sede municipal) | `R1` a `R4` (município, localidade, bairro, via pública) |
| **Cobertura RS** | `467 / 497` municípios (`467` CEPs) | `483 / 497` municípios (`4.039` CEPs) |
| **Consistência IBGE** | `100%` dos códigos válidos | `12` ambiguidades de divisa + `3` conflitos com GeoNames |
| **Limitação principal** | Não resolve ruas/bairros em cidades codificadas | Cobertura submunicipal heterogênea entre municípios |

### Recomendação fundamentada para implementação do `SITUS-PATCH-002`

1. **Adotar arquitetura em duas camadas complementares (`GeoNames + OSM`):**
   - **Camada base `R1` (Município):** Combinar `GeoNames` (`467` municípios) + `OSM` (`boundary=administrative` / `place` com `postal_code`) validados contra a malha oficial do IBGE para atingir **cobertura municipal de 100% (`497/497`)** dos municípios do RS com geometria poligonal oficial.
   - **Camada submunicipal `R2`–`R4` (OSM Categoria A):** Indexar os `4.039` CEPs distintos de localidades, bairros e eixos de vias públicas do OSM (com `postal_reference` armazenando `resolution_level`, `source_id`, `license`, `support_geom` e `uncertainty_note`).
2. **Política explícita de conflito e ambiguidade:** Nos `12` CEPs multi-município e `3` conflitos GeoNames×OSM, retornar `status = "AMBIGUOUS"` com todas as unidades candidatas, sem desempate silencioso.
3. **Honestidade para CEPs não cobertos em `R2`–`R4`:** Quando um CEP de município codificado por logradouro não constar no OSM nem no GeoNames, retornar `UNAVAILABLE` com explicação clara e manter os botões de seleção manual de área no mapa ou consulta por município.
