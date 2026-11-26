---
document_id: SITUS-EXP-003
title: Investigação Experimental de Territórios Rurais — Bagé (Joca Tavares, Palmas e Piraí)
version: 0.1.0
date: 2026-11-26
status: EXECUTADO_E_VERIFICADO_COM_DADOS_REAIS
antecedent: SITUS-EXEC-002-v0.1.0
directive: SITUS-DIR-002-v0.1.0
scope: RS
experimental_municipality: Bagé (4301602)
data_policy: CATEGORY_A_ONLY
---

# SITUS-EXP-003 (v0.1.0) — Investigação Experimental de Territórios Rurais

## 1. Diagnóstico das fontes locais e esclarecimento da divergência MapBiomas

### 1.1. Distinção entre configuração de exemplo, configuração operacional e existência física

| Recurso | Config. de Exemplo | Config. Operacional (sem `.env`) | Existência Física | Disponibilidade Efetiva |
|---|---|---|---|---|
| **IBGE Setores 2022 (`RS`)** | `sources.example.json` (`APPROVED`) | PostGIS `ibge_setores_rs_2022:2022.2` (`is_active=TRUE`) | `var/data/RS_setores_CD2022.gpkg` (`54,4 MiB`) | **Operacional** (`25.567` setores; `199` em Bagé) |
| **IBGE BC250 (`2026-03-03`)** | — | PostGIS `hydro_feature` (`55.808` feições drenagem/massa) | `var/data/patch004/bc250/bc250_2026-03-03.gpkg` | **Parcialmente operacional** (hidrografia ingerida; camadas `lml_vila_p`, `lml_aglomerado_rural_p`, `indice_dos_nomes` existem no GPKG, mas não foram ingeridas) |
| **IBGE Biomas (`2019`)** | — | PostGIS `biome_unit` (`2` polígonos RS) | `var/data/patch004/biomas/lm_bioma_250.shp` | **Operacional** (`1:250.000`) |
| **IBGE BDiA Pedologia (`2023`)** | — | PostGIS `soil_unit` (`9.177` polígonos RS) | `var/data/patch004/pedologia/pedo_area.shp` | **Operacional** (`1:250.000`) |
| **INPE TOPODATA (`2008.v1`)** | — | `src/cli/main.cpp` (`SITUS_TOPODATA_APPROVED="1"`) | `var/data/patch004/topodata_alt_rs.vrt`, `topodata_decliv_rs.vrt` + 6 GeoTIFFs | **Operacional** (`~30 m` / refinamento SRTM `90 m`) |
| **MapBiomas (`2023.col9`)** | `.env.example` (`SITUS_RASTER_APPROVED=0`, caminho vazio); `sources.example.json` (`PENDING_ACQUISITION`) | `src/cli/main.cpp` fallback (`SITUS_RASTER_APPROVED="1"`, `SITUS_RASTER_PATH=/run/media/.../farina/ut_rs_mapbiomas_2023_campos_sq_32722.tif`) | **Externo** a `SITUS/var/data/`: existe em `../farina/ut_rs_mapbiomas_2023_campos_sq_32722.tif` (`2,4 GiB`, `EPSG:32722`) | **Operacional apenas para máscara de Formação Campestre** (classe `12` vs `0`); **indisponível** para multiclasse completo e séries históricas |
| **Referências Postais (`CEP`)** | `sources.example.json` (`PENDING_VALIDATION`) | PostGIS `postal_reference` (`geonames_postal_br` + `osm_postal_sul_rs`) | `var/data/patch002_eval/` | **Operacional** (em Bagé: `96400-000` `R1`, `96435-000`/`96437-000`/`96443-000`/`96431-000` `R2`, `R4` urbano) |

### 1.2. Esclarecimento da divergência do PATCH-004
A caracterização MapBiomas demonstrada no `SITUS-PATCH-004` funcionou porque, na ausência de arquivo `.env`, o binário (`src/cli/main.cpp`) utiliza um caminho padrão apontando para um artefato externo em `../farina/ut_rs_mapbiomas_2023_campos_sq_32722.tif` com `SITUS_RASTER_APPROVED=1`. Não há cópia nem registro desse raster dentro de `SITUS/var/data/` ou `config/sources.example.json`, e o artefato externo representa exclusivamente a máscara binária de **Formação Campestre (2023)**, não o produto MapBiomas multiclasse completo.

---

## 2. Reconhecimento e validação das referências rurais de Bagé (`4301602`)

### 2.1. Malha censitária IBGE 2022 (`ibge_setores_rs_2022:2022.2`)
Bagé possui **199 setores ativos** (`409.035,98 ha`), distribuídos em **5 distritos** (`Bagé`, `Joca Tavares`, `José Otávio`, `Palmas`, `Piraí`), todos com geometrias válidas (`ST_IsValid = TRUE`):

| Distrito | `CD_DIST` | Setores Rurais | Setores Urbanos | Área Rural (`ha`) | Área Urbana (`ha`) |
|---|---|---|---|---:|---:|
| **Joca Tavares** | `430160217` | 5 | 0 | `114.252,93` | `0,00` |
| **Palmas** | `430160221` | 7 | 1 | `104.343,63` | `1,08` |
| **Piraí** | `430160222` | 2 | 0 | `61.759,93` | `0,00` |

### 2.2. Concordância entre IBGE 2022, BC250 (`lml_vila_p`) e `postal_reference`
As três localidades constam como feições pontuais oficiais na camada `lml_vila_p` de `bc250_2026-03-03.gpkg` e caem integralmente dentro dos respectivos distritos gerados pela união dos setores censitários:

| Localidade | Ponto `bc250:lml_vila_p` (`lon, lat`) | Setor IBGE 2022 que o contém | Situação do setor | CEP `R2` (`osm_postal_sul_rs`) |
|---|---|---|---|---|
| **Joca Tavares** | `-53.82089, -30.91943` | `430160217000007` (`430160217`) | Rural | `96435-000` |
| **Palmas** | `-53.67348, -30.93302` | `430160221000001` (`430160221`) | Urbana (`1,08 ha`, sede distrital) | `96437-000` |
| **Piraí** | `-54.64836, -31.39663` | `430160222000004` (`430160222`) | Rural | `96443-000` |

- **Avaliação da união distrital:** A união dos setores censitários por `CD_DIST` reproduz geometrias distritais topologicamente válidas e concordantes com os pontos `lml_vila_p`, porém **não substitui** a confrontação futura com a malha oficial de limites distritais do IBGE/SEPLAG-RS quando integrada.

---

## 3. Método reproduzível de unidades regulares de investigação (`GRID_CELL`)

Implementado em `PostgisStore` (`kind: "GRID_CELL"`), `policy::validate_grid_cell_size_m` e `policy::parse_grid_cell_id`:
- **Sistema de referência métrico:** `EPSG:5880` (**SIRGAS 2000 / Brazil Polyconic**), adotado para evitar a descontinuidade da divisa UTM 21S/22S (`-54°`) que corta o município de Bagé e para preservar equivalência de área (`~99,998–100,000 ha` por célula de `1 km × 1 km`).
- **Origem:** `(0 m, 0 m)` em `EPSG:5880`.
- **Dimensões suportadas:** `500 m` (`25 ha`), `1.000 m` (`100 ha`), `2.000 m` (`400 ha`), `5.000 m` (`2.500 ha`).
- **Indexação determinística:** `ix = floor(x_5880 / S)`, `iy = floor(y_5880 / S)`, código `GRID5880_<S>_<ix>_<iy>`.
- **Geração sob demanda:** envelope `[ix·S, iy·S, (ix+1)·S, (iy+1)·S]` construído em `EPSG:5880` e projetado para `EPSG:4326` apenas no momento da consulta, sem materializar a grade estadual.

Verificação das 4 dimensões sobre o ponto-âncora de Joca Tavares (`-53.91038, -31.10211`):
- `500 m`: `GRID5880_500_10017_13115` -> `25,0000 ha`
- `1000 m`: `GRID5880_1000_5008_6557` -> `99,9999 ha`
- `2000 m`: `GRID5880_2000_2504_3278` -> `399,9996 ha`
- `5000 m`: `GRID5880_5000_1001_1311` -> `2.499,9983 ha`

---

## 4. Primeira investigação comparativa (~100 ha)

### 4.1. Critério de seleção (definido previamente ao exame ambiental)
Para cada distrito rural-alvo (`430160217` Joca Tavares, `430160221` Palmas e `430160222` Piraí), selecionou-se a célula `1 km × 1 km` (`EPSG:5880`) contendo o ponto interior determinístico (`ST_PointOnSurface`) da união dos **setores exclusivamente rurais** (`situation_label = 'Rural'`) do distrito — garantindo cobertura rural integral sem viés fundiário nem inspeção prévia de variáveis ambientais.

### 4.2. Síntese quantitativa das três unidades

| Dimensão | Joca Tavares (`GRID5880_1000_5008_6557`) | Palmas (`GRID5880_1000_5037_6559`) | Piraí (`GRID5880_1000_4955_6516`) |
|---|---|---|---|
| **Centróide (`WGS84`)** | `-53.91090, -31.10393` | `-53.60700, -31.08532` | `-54.46828, -31.47289` |
| **Área geodésica** | `99,9999 ha` (`100% Rural`, 1 setor) | `99,9983 ha` (`100% Rural`, 2 setores) | `99,9976 ha` (`100% Rural`, 1 setor) |
| **Bioma (`1:250k`)** | Pampa (`100%`) | Pampa (`100%`) | Pampa (`100%`) |
| **Altitude (`TOPODATA`)** | `287,1 m` (`260,2`–`318,3 m`, DP `16,3 m`) | `329,0 m` (`307,2`–`345,1 m`, DP `9,5 m`) | `171,3 m` (`156,2`–`185,4 m`, DP `6,4 m`) |
| **Declividade (`TOPODATA`)** | `12,5%` (`1,6%`–`32,1%`, DP `5,8%`) | `7,2%` (`0,6%`–`15,1%`, DP `2,8%`) | `5,8%` (`1,1%`–`13,3%`, DP `2,3%`) |
| **Solos (`BDiA 1:250k`)** | `RLe` Neossolo Litólico Eutrófico (`100%`) | `RLe` (`74,8%`) + `RLd` Neossolo Litólico Distrófico (`25,2%`) | `SXk` Planossolo Háplico Carbonático (`100%`) |
| **Hidrografia (`BC250 1:250k`)** | 2 trechos (`1,41 km`, `Arroio Banhado Grande`) | 1 trecho sem nome (`0,81 km`) | 0 feições na escala `1:250.000` |
| **Cobertura (`MapBiomas 2023` máscara campos)** | `23,04 ha` (`23,0%`, `256 px`) Formação Campestre; `77,31 ha` (`77,0%`) classe 0 | `34,20 ha` (`34,2%`, `380 px`) Formação Campestre; `65,97 ha` (`65,9%`) classe 0 | `12,69 ha` (`12,7%`, `141 px`) Formação Campestre; `87,30 ha` (`87,3%`) classe 0 |

### 4.3. Comparação inicial (sem extrapolação estatística)
Mesmo em apenas três recortes equivalentes de `~100 ha` no Pampa de Bagé, observa-se clara diferenciação geomorfológica e pedológica:
- **Palmas** e **Joca Tavares** situam-se em cotas mais elevadas (`329 m` e `287 m`), sobre **Neossolos Litólicos (`RLe`/`RLd`)**, com maior declividade média em Joca Tavares (`12,5%`, chegando a `32,1%`) e maior proporção de Formação Campestre mapeada na máscara 2023 em Palmas (`34,2%`) e Joca Tavares (`23,0%`).
- **Piraí** situa-se em cota bem inferior (`171 m`) e relevo mais suave (`5,8%`), sobre **Planossolo Háplico Carbonático (`SXk`)**, sem drenagem capturada na escala `1:250.000` e com menor fração de Formação Campestre (`12,7%`) na máscara 2023.
- **Advertência metodológica:** Três células de `100 ha` não conferem representatividade estatística aos respectivos distritos (`61.760` a `114.253 ha`).

---

## 5. Limitações encontradas

1. **MapBiomas:** Apenas máscara binária de Formação Campestre (`2023`) via caminho externo `../farina/...`; faltam classes de floresta, agricultura, silvicultura, água e séries históricas multi-ano.
2. **Hidrografia (`BC250 1:250.000`):** Escala exploratória omite cursos d'água de menor ordem (como evidenciado na célula de Piraí com `0` trechos).
3. **Pedologia (`BDiA 1:250.000`):** Escala de reconhecimento (`1:250.000`), sem atributos físico-hídricos quantitativos locais.
4. **Limites distritais e localidades:** Geometria distrital derivada por união de setores censitários IBGE 2022; camadas `bc250:lml_vila_p` / `lml_aglomerado_rural_p` e produto Localidades do Brasil IBGE 2022 ainda não ingeridos no PostGIS.
5. **Clima e infraestrutura rural pública:** Sem fontes integradas nesta etapa.

---

## 6. Plano incremental de integração (P0 → P2)

| Prioridade | Fonte / Capacidade | Ação prevista |
|---|---|---|
| **P0** | MapBiomas multiclasse completo (`30 m`, RS) | Adquirir GeoTIFF oficial multiclasse em `var/data/`, registrar em `sources.example.json` / `.env` e mapear todas as classes MapBiomas em `raster_service.cpp`. |
| **P0** | Localidades rurais e limites distritais | Ingerir `bc250:lml_vila_p`, `lml_aglomerado_rural_p` e Localidades do Brasil IBGE 2022 + malha distrital oficial para confrontação e busca direta por localidade rural. |
| **P1** | Hidrografia BCRS25 (`1:25.000`) e Bacias hidrográficas | Ingerir BCRS25 e ottobacias/bacias em `hydro_feature` (`WATERSHED`) + habilitar `kind: "WATERSHED"` em `resolve_reference`. |
| **P1** | Séries históricas MapBiomas | Estender `RasterService` para múltiplas safras/anos (ex.: 1985, 2000, 2010, 2023) retornando trajetória temporal de cobertura. |
| **P2** | Solos (propriedades estimadas), clima e rede viária rural | Avaliar licença/resolução (SoilGrids / séries climáticas públicas / OSM ou DAER viário rural Categoria A) antes da ingestão. |
