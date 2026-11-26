---
document_id: SITUS-EXEC-002
title: Investigação experimental de territórios rurais
version: 0.1.0
date: 2026-11-26
status: PROPOSTA_PARA_EXECUCAO
document_role: MANDATO_EXPERIMENTAL
antecedent: SITUS-DIR-002-v0.1.0
scope: RS
experimental_municipality: Bagé
data_policy: CATEGORY_A_ONLY
---

# SITUS — Investigação experimental de territórios rurais

## 1. Mandato

Prosseguir com as duas alternativas apresentadas, de forma integrada.

A validação das referências territoriais rurais de Bagé deverá fundamentar a implementação das novas capacidades de investigação.

Não condicionar o início da investigação à implementação integral de todas as funcionalidades previstas na SITUS-DIR-002.

Preservar a arquitetura existente.

## 2. Inventário e verificação dos recursos locais

Inspecionar efetivamente os arquivos, bancos e configurações disponíveis.

Priorizar:

- Malha de setores censitários IBGE 2022;
- BC250 e respectivas camadas de localidades rurais;
- Biomas;
- Pedologia;
- TOPODATA;
- Hidrografia;
- Produtos MapBiomas disponíveis.

Esclarecer a divergência entre a caracterização MapBiomas demonstrada no PATCH-004 e a ausência atual do respectivo artefato raster.

Distinguir configuração de exemplo, configuração operacional, existência física do arquivo e disponibilidade efetiva da operação.

## 3. Reconhecimento das referências rurais

Localizar e validar as referências públicas de Joca Tavares, Palmas e Piraí.

Utilizar inicialmente os dados já existentes na malha censitária e na BC250.

Verificar:

- Denominações e códigos territoriais;
- Vinculação administrativa;
- Geometria de suporte;
- Classificação territorial rural;
- Concordância entre fontes;
- Resolução e limitações.

Avaliar a construção de geometrias distritais pela união dos setores censitários correspondentes, confrontando-as com os limites distritais oficiais.

Integrar posteriormente o produto Localidades do Brasil quando agregar informações relevantes.

Não utilizar cadastros individualizados ou limites de propriedades.

## 4. Unidades regulares de investigação

Implementar experimentalmente unidades espaciais métricas de:

- 500 m × 500 m;
- 1 km × 1 km;
- 2 km × 2 km;
- 5 km × 5 km.

Adotar sistema de coordenadas, origem e método de indexação explícitos e reproduzíveis.

Gerar unidades sob demanda, evitando a materialização desnecessária de toda a grade estadual.

Preservar a distinção entre geometria de referência, geometria de investigação e geometria de visualização.

## 5. Primeira investigação comparativa

Selecionar inicialmente três unidades rurais de aproximadamente 100 hectares, contextualizadas em Joca Tavares, Palmas e Piraí.

Definir e documentar os critérios de seleção antes de examinar os resultados ambientais.

Caracterizar cada unidade com as fontes efetivamente disponíveis:

- Área e localização territorial;
- Bioma;
- Altitude e declividade;
- Pedologia;
- Hidrografia;
- Uso e cobertura da terra, quando disponível.

Apresentar as diferenças entre as unidades, sem atribuir representatividade estatística às localidades a partir de apenas três recortes.

## 6. Ampliação das fontes

Priorizar:

P0 — MapBiomas multiclasse completo.

P0 — Localidades rurais e limites distritais.

P1 — Hidrografia BCRS25 e bacias hidrográficas.

P1 — Séries históricas MapBiomas.

P2 — Propriedades estimadas dos solos e séries climáticas.

P2 — Rede viária pública rural.

Verificar licença, proveniência, qualidade, resolução e admissibilidade antes da integração.

## 7. Interface

Priorizar as referências de investigação rural.

Permitir que o usuário localize um distrito ou localidade e selecione a dimensão da unidade a investigar.

Preservar as modalidades de entrada existentes, sem ampliar funcionalidades de endereçamento urbano.

O resultado deverá ser uma ficha territorial integrada, com observações quantitativas, mapas e limitações.

## 8. Entregáveis

- Relatório de diagnóstico das fontes locais;
- Referências rurais validadas;
- Método reproduzível de geração das unidades espaciais;
- Três caracterizações territoriais reais;
- Primeira comparação entre unidades;
- Relação das limitações encontradas;
- Plano incremental de integração das fontes restantes;
- Código e documentação versionados.

Registrar os resultados experimentais em SITUS-EXP-003-v0.1.0.md.

Não declarar capacidades operacionais sem evidência.

Sempre pronto. Sempre incompleto.
