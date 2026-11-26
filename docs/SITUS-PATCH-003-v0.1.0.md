---
document_id: SITUS-PATCH-003
title: Conhecimento Ambiental do Território (Etapa III)
version: 0.1.0
date: 2026-11-26
status: IMPLEMENTADO_E_VERIFICADO_COM_DADOS_REAIS
scope: RS
data_policy: CATEGORY_A_ONLY
---

# SITUS — PATCH-003 (v0.1.0)

**Caracterização ambiental raster mascarada pela geometria efetiva de investigação**

## 1. Correção da Operação Raster (`5.1`)

- Substituída a chamada fixa `characterize_raster_over_bbox(..., 0, 0, 0, 0)` em [service.cpp](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/src/application/service.cpp) por [`characterize_raster_over_geometry`](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/src/spatial/raster_service.cpp), utilizando a geometria efetiva da investigação (`POLYGON`, `MUNICIPALITY` ou `POINT` com buffer).
- Reprojeção automática via GDAL/PROJ (`EPSG:4326` → CRS nativo do raster, ex.: `EPSG:32722` UTM 22S).
- Máscara poligonal exata em memória (`GDALRasterizeGeometries`, regra explícita `center-in-polygon` / `ALL_TOUCHED=FALSE`), leitura categórica sem interpolação bilinear, tratamento de `NoData` (`Float32` `-3.4028235e+38`) e distinção explícita entre **proporção de pixels** (`pixel_fraction`) e **área territorial** (`area_m2`, `area_ha`, `area_fraction_of_reference`).

## 2. Primeira Investigação Ambiental Demonstrável (`5.3`)

Executada sobre a camada real **MapBiomas 2023 — Formação Campestre (`30 m`, `EPSG:32722`, `900 m²/pixel`)** em dois recortes distintos de **Bagé (`4301602`)**:

| Recorte (Bagé/RS) | Área de referência | Setores IBGE | Classe 12 (Formação Campestre) | Classe 0 (Demais coberturas) | Cobertura válida |
|---|---:|---:|---:|---:|---:|
| **Bagé Urbano Central** | `422,10 ha` | `46` | `3` px (`0,27 ha` / `0,064%`) | `4.694` px (`422,46 ha` / `99,936%`) | `100%` (`0` NoData) |
| **Bagé Rural Campestre** | `2.108,94 ha` | `2` | `1.476` px (`132,84 ha` / `6,29%`) | `21.997` px (`1.979,73 ha` / `93,71%`) | `100%` (`0` NoData) |

### Confrontação independente (`gdalwarp -cutline`)

Para o recorte **Bagé Rural Campestre**, `gdalwarp -cutline` contabilizou **`1.476` pixels** de classe `12` (`0` pixels de diferença) e `21.967` pixels de classe `0` (`0,128%` de diferença na borda entre rasterização na grade nativa e `cutline`).

## 3. Limitações e Próximos Passos (`5.2`)

- A camada raster verificada nesta etapa distingue exclusivamente **Formação Campestre (classe 12 MapBiomas 2023)** de **Demais coberturas / Não-campo (classe 0)**, preservando a regra de não inferir classes não sustentadas pela fonte.
- A incorporação de camada MapBiomas multi-classe completa (floresta, agricultura, água), TOPODATA/SRTM, ANA e solos (Embrapa/SoilGrids) utilizará o mesmo motor poligonal sem alteração arquitetural.
