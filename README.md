# SITUS — Sistema de Conhecimento Territorial

Sistema independente, local, simples e leve em **C++26** para investigação territorial submunicipal no Rio Grande do Sul (RS), com interface web única inspirada no **BB-CORE / MORFO** (Ecossistema SisTer), ícones e grafismo SVG próprios, **PostGIS**, **GDAL/GEOS/PROJ** e núcleo computacional portável para **WebAssembly (Emscripten)**.

**Licença:** GNU General Public License v3.0 ([LICENSE](LICENSE))  
**Política de dados:** Categoria A estrita (apenas território não pessoal e fontes públicas aprovadas).

---

## Pré-requisitos no Fedora Silverblue

O desenvolvimento e build ocorrem dentro de uma toolbox dedicada (`situs-dev`) e o banco PostGIS roda em container Podman rootless dedicado (`127.0.0.1:55436`).

## Início rápido

```bash
# 1. Diagnóstico do ambiente (nativo + Wasm)
toolbox run -c situs-dev ./run.sh doctor

# 2. Build e testes unitários/contrato nativos (C++26)
toolbox run -c situs-dev ./run.sh build
toolbox run -c situs-dev ./run.sh test

# 3. Build e testes do núcleo portável WebAssembly (opcional para o servidor, obrigatório para modo adaptativo)
toolbox run -c situs-dev ./run.sh build-wasm
toolbox run -c situs-dev ./run.sh test-wasm

# 4. Iniciar PostGIS dedicado e aplicar migrações
./tools/db.sh start
./tools/db.sh migrate

# 5. Aquisição administrativa e ingestão idempotente da malha oficial IBGE RS 2022
./tools/acquire.sh
toolbox run -c situs-dev ./run.sh cli ingest \
  --file var/data/RS_setores_CD2022.gpkg \
  --source config/sources.example.json \
  --source-id ibge_setores_rs_2022 \
  --version 2022.1

# 6. Iniciar servidor web/API local (127.0.0.1:8096)
toolbox run -c situs-dev ./run.sh serve

# 7. Em outro terminal, executar smoke test da API
./tools/smoke.sh
```

## Estrutura principal

- `include/situs/core/` e `src/core/`: Núcleo C++26 portável (sem I/O de plataforma).
- `src/wasm/` e `web/wasm/`: Fronteira Emscripten e Web Worker dedicado.
- `src/execution/`: Contratos, envelope de proveniência computacional e catálogo de capacidades.
- `src/policy/`: Validação de política Categoria A, CRS, geometrias e limites.
- `src/spatial/`: Persistência PostGIS parametrizada (`libpq`), ingestão GDAL/OGR e leitura raster GDAL.
- `src/http/` e `src/cli/`: Servidor Boost.Asio/Beast e CLI unificada sobre `SitusService`.
- `web/`: Interface web única (HTML/CSS/JS + SVG local, zero CDN, zero telemetria).
- `contracts/`: OpenAPI 3.1, JSON Schemas e contrato futuro MCP somente leitura.
