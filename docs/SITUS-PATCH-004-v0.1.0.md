---
document_id: SITUS-PATCH-004
title: Caracterização Territorial Multidimensional — Relatório de Entrega
version: 0.1.0
date: 2026-11-26
status: IMPLEMENTADO_E_VERIFICADO_COM_DADOS_REAIS
scope: RS
data_policy: CATEGORY_A_ONLY
---

# SITUS — PATCH-004 (v0.1.0)

**Caracterização territorial multidimensional real (Geografia, Uso/Cobertura, Relevo, Hidrografia e Solos)**

## 1. Fontes Reais Integradas (Categoria A)

| Dimensão | Fonte | Versão / Ref. | Escala / Resolução | Registros / Suporte |
|---|---|---|---|---|
| **Geografia territorial** | IBGE Setores Censitários RS | `2022.2` | Cadastral censitária | `25.567` setores (`PostGIS`) |
| **Geografia territorial** | GeoNames + OSM Sul (RS) | `2026-11` | `R1`–`R4` público | `4.990` referências (`PostGIS`) |
| **Biomas** | IBGE Biomas `1:250.000` | `2019` | `1:250.000` | `2` polígonos RS (`biome_unit`) |
| **Uso e cobertura** | MapBiomas 2023 Formação Campestre | `2023.col9` | `30 m` (`900 m²/px`) | GeoTIFF `EPSG:32722` (`GDAL`) |
| **Relevo (Alt. e Decliv.)** | INPE TOPODATA (`30S525`, `31S54_`, `31S555`) | `2008.v1` | `~30 m` (SRTM `90 m`) | VRTs `ZN`/`SN` (`GDAL`) |
| **Hidrografia** | IBGE BC250 Hidrografia | `2026-03-03` | `1:250.000` | `55.808` feições RS (`hydro_feature`) |
| **Solos** | IBGE BDiA Pedologia | `2023` | `1:250.000` | `9.177` polígonos RS (`soil_unit`) |

> **Nota de escopo pendente:** MapBiomas multi-classe completo (floresta, agricultura, água, urbano), ANA bacias hidrográficas e SoilGrids permanecem no inventário `SITUS-INV-001` para ampliação incremental sem alteração arquitetural.

## 2. Demonstração dos Critérios de Aceitação (Seção 5)

### 2.1. Investigação iniciada por CEP submunicipal (`90240-552` · Av. Bahia, Porto Alegre · `R4`, entorno `500 m`)
- **Área de investigação:** `106,79 ha` (`29` setores censitários).
- **Bioma:** `Pampa` (`100%`).
- **Hidrografia (`1:250.000`):** `0` feições na escala `1:250.000`.
- **Solos (`BDiA`):** `Área Urbana` (`100%`).
- **Uso e cobertura (`MapBiomas 2023`):** `106,74 ha` (`1.186` px, `100%` classe `0` não-campo).
- **Relevo (`TOPODATA`):** Altitude média **`11,00 m`** (`7,75` a `18,25 m`); Declividade média **`2,66%`** (`0,32` a `9,80%`).

### 2.2. Investigação iniciada por seleção manual de área (Bagé Rural Campestre · `4301602`)
- **Área de investigação:** `2.108,94 ha` (`2` setores censitários).
- **Bioma:** `Pampa` (`100%`).
- **Hidrografia (`BC250`):** `10` trechos de cursos d'água (`12,88 km`, incluindo `Arroio Bocarra`) e `3` massas d'água (`84,81 ha`).
- **Solos (`BDiA`):** `SXk - Planossolo Háplico Carbonático` (`87,93%`, `1.854,41 ha`), `SXe - Planossolo Háplico Eutrófico` (`8,05%`, `169,78 ha`), `Corpo d'água continental` (`4,02%`, `84,81 ha`).
- **Uso e cobertura (`MapBiomas 2023`):** `132,84 ha` (`6,29%`, `1.476` px) de `Formação Campestre` (classe `12`).
- **Relevo (`TOPODATA`):** Altitude média **`169,15 m`** (`144,36` a `203,51 m`, DP `10,82 m`); Declividade média **`4,65%`** (`0,11` a `24,34%`).

### 2.3. Confrontação Independente (`gdalwarp -cutline`)
- **MapBiomas classe 12 (Bagé Rural):** SITUS = `1.476` px | `gdalwarp` = `1.476` px (`0` px de diferença).
- **TOPODATA Altitude (Bagé Rural):** SITUS = `min 144,357 m / max 203,506 m / mean 169,150 m` | `gdalwarp` = `min 144,357 m / max 203,506 m / mean 169,166 m` (`0,009%` na média).
