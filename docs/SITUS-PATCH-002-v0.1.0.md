---
document_id: SITUS-PATCH-002
title: Resolução Territorial de Referências Postais (Categoria A)
version: 0.1.0
date: 2026-11-26
status: PROPOSTA_PARA_INVESTIGACAO
scope: RS
data_policy: CATEGORY_A_ONLY
---

# SITUS — PATCH-002 (Proposta)

**Resolução territorial de referências postais sem identificação pessoal ou domiciliar**

## 1. Premissa e Critério de Admissibilidade

O CEP é tratado exclusivamente como chave de **suporte territorial público** (município, distrito/localidade, bairro ou eixo de via pública), jamais como endereço residencial ou identificação de morador.

| Informação | Admissibilidade |
|---|---|
| CEP associado a município, distrito, localidade rural ou bairro | Sim (quando não individualizante) |
| CEP associado a eixo de via pública ou região aproximada | Sim, mediante avaliação |
| CEP associado a residência específica ou identidade de morador | **Não** |

## 2. Fontes Candidatas a Avaliar

1. **OpenStreetMap — Vias públicas (`highway` + `postal_code`)** via recorte Sul/RS (Geofabrik, licença ODbL 1.0), excluindo edificações e endereços individualizados (`addr:housenumber`).
2. **GeoNames — Referências postais geográficas (`BR.zip`)** (licença CC BY 3.0), cobertura complementar de localidades e municípios.
3. **IBGE — CNEFE Agregados por CEP (Censo 2022)** (publicação 14/06/2024), verificar atributos territoriais e ausência de microdados individualizados.
4. **OpenCEP** — condicionado à validação de proveniência e licença dos dados originais.

## 3. Níveis Progressivos de Resolução (`R0`–`R5`)

| Nível | Referência recuperada | Geometria de suporte |
|---|---|---|
| `R0` | Não resolvido | Nenhuma (`UNAVAILABLE`) |
| `R1` | Município | Polígono |
| `R2` | Distrito ou localidade | Polígono ou referência pública |
| `R3` | Bairro | Polígono |
| `R4` | Logradouro público | Linha |
| `R5` | Trecho de logradouro público | Linha |

## 4. Próximos Passos

1. Inspecionar cobertura efetiva no RS, licenças e granularidade das fontes candidatas (OSM Sul, GeoNames BR, IBGE Agregados por CEP).
2. Atualizar o schema de `postal_reference` para suportar níveis `R1`–`R5`, geometria de suporte e proveniência por fonte.
3. Validar cobertura nos 497 municípios gaúchos e demonstrar resolução submunicipal real com dados públicos.
