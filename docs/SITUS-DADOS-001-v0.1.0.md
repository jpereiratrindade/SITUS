# SITUS-DADOS-001 (v0.1.0) — Governança de Dados e Fontes (Categoria A)

## 1. Política Categoria A Estrita

Dados publicamente acessíveis **não** são automaticamente admissíveis. O SITUS admite apenas dados territoriais não pessoais (Categoria A). Estão expressamente proibidos: CNEFE individualizado, CAR de imóveis, microdados identificáveis, cadastros residenciais e estatísticas demográficas de pequena área sem avaliação específica de divulgação.

## 2. Inventário de Fontes

| `source_id` | Título | Provedor | Estado | CRS Nativo | Resolução | Limitações |
|---|---|---|---|---|---|---|
| `ibge_setores_rs_2022` | Malha de Setores Censitários 2022 — RS | IBGE | `APPROVED` | `EPSG:4674` | Escala cadastral censitária (poligonal) | Apenas geometria e códigos territoriais permitidos (`allowed_fields`). Sem população ou domicílios. |
| `mapbiomas_cobertura_30m_rs` | Cobertura e Uso da Terra 30m — RS | MapBiomas | `PENDING_ACQUISITION` | `EPSG:4326` | `30 m` | Aquisição administrativa manual pendente; resolução 30m não descreve imóvel individual. |
| `cep_referencia_territorial` | Referência Territorial de CEP | Correios / a validar | `PENDING_VALIDATION` | N/A | N/A | DNE é comercializado e CNEFE individualizado é proibido. Consultas de CEP retornam `UNAVAILABLE`. |

## 3. Regras de Ingestão

1. Toda fonte passa por `situs::policy::validate_category_a_source`.
2. Apenas atributos listados em `allowed_fields` são extraídos; colunas não autorizadas são descartadas antes da persistência.
3. A ingestão grava `artifact_sha256` e `dataset_version` de forma transacional e idempotente (`ON CONFLICT DO NOTHING` + verificação de SHA-256).
