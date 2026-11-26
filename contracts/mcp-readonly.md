# SITUS — Contrato Futuro de Interface MCP Somente Leitura

**Status neste patch (`0.2.0`):** CONTRATO DOCUMENTADO (servidor MCP **não** implementado nesta versão).

## Princípios

1. **Somente leitura e Categoria A estrita:** o futuro servidor MCP exporá apenas consultas territoriais sobre fontes aprovadas de Categoria A. Nenhuma operação de ingestão, migração, escrita ou administração será exposta via MCP.
2. **Sem persistência de consultas:** requisições MCP não gravam histórico de localização, CEP, coordenadas ou geometrias consultadas.
3. **Limites explícitos:** aplicam-se os mesmos limites de área, vértices, raio e corpo da API REST v1.

## Ferramentas previstas (patch posterior)

- `situs_catalog_list` → equivale a `GET /api/v1/catalog`
- `situs_reference_resolve` → equivale a `POST /api/v1/references/resolve`
- `situs_territories_intersect` → equivale a `POST /api/v1/territories/intersect`
- `situs_territories_characterize` → equivale a `POST /api/v1/territories/characterize`
- `situs_execution_capabilities` → equivale a `GET /api/v1/execution/capabilities`
