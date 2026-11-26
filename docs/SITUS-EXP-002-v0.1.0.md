# SITUS-EXP-002 — Ampliação da cobertura das referências postais

**Versão:** 0.1.0  
**Data:** 26/11/2026  
**Política:** `CATEGORY_A_ONLY`  
**Referência base:** commit `e778922` (pós-`SITUS-PATCH-004` / `SITUS-PATCH-002 v0.2.0`)

---

## 1. Resumo executivo

A linha de base atual do SITUS (pós-[SITUS-PATCH-002 v0.2.0](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/docs/SITUS-PATCH-002-v0.2.0.md)) resolve localmente apenas **~4.039 a 4.086 CEPs distintos** no Rio Grande do Sul (`467` registros `R1` via `geonames_postal_br` e `4.523` registros `R1`–`R4`/`POSTAL_REGION` via `osm_postal_sul_rs`). Em contraste, o arquivo oficial **Agregados por CEP do CNEFE — Censo 2022** do IBGE registra **48.771 CEPs** na faixa `90000-000` a `99999-999` (RS), indicando que a cobertura submunicipal/municipal atual atinge apenas **~8,3% dos CEPs ativos** do estado.

O experimento **SITUS-EXP-002** investigou 7 fontes e estratégias complementares sem integrar dados não aprovados ao banco operacional e sem utilizar números de imóveis, coordenadas domiciliares ou cadastros individualizados:

1. **Faixas municipais (`R1`):** `datasets-br/city-codes` (`postalCode_ranges`) cobre **497/497 municípios do RS (100%)** com **0 sobreposições** entre municípios distintos, permitindo resolver **100% dos CEPs válidos do RS em nível municipal (`R1`)** — um ganho potencial de **+~44.700 CEPs resolvíveis em `R1`**. Contudo, embora o repositório declare licença `ODC-PDDL 1.0` no `datapackage.json`, a coluna `postalCode_ranges` deriva originalmente de faixas postais dos Correios sem documentação de cessão explícita, exigindo parecer/aprovação antes da carga operacional.
2. **Geometria submunicipal de vias (`R4`):** Tanto a **Base de Faces de Logradouros do IBGE (Censo 2022)** (`497/497` municípios do RS; `7.631` faces / `939` logradouros distintos em Bagé) quanto o **OpenStreetMap** já possuem a geometria pública da **Rua Rádio Cultura** (piloto Bagé, CEP `96415-610`) e da ampla maioria das vias urbanas. O gargalo do nível `R4` **não é cartográfico**, mas sim a ausência de uma **tabela pública autorizada de correspondência `CEP ↔ logradouro`**.
3. **Fontes rejeitadas ou condicionadas:** O **OpenCEP** (`SeuAliado/OpenCEP`) possui código sob licença MIT, mas sua base provém diretamente dos Correios/DNE sem licença aberta independente — **inadmissível** para integração direta sob `CATEGORY_A_ONLY`.
4. **Caminhos institucionais recomendados para `R4`:** (a) adesão institucional à API/base do **Cadastro Base de Endereço (CBE)** via **Conecta Gov.br** (em arquitetura híbrida/autorizada); (b) solicitação formal ao **IBGE** de um produto agregado `CEP ↔ CD_MUN / CD_SETOR / CD_FACE` derivado do CNEFE (estendendo o já existente `Agregados_por_CEP_CNEFE_Censo_2022.csv`); e (c) aproveitamento oportunístico de **cadastros municipais públicos** (ex.: GeoPOA / SMAMUS / DataPOA em Porto Alegre) onde houver correspondência aberta publicada.

---

## 2. Linha de base observada no SITUS

Conforme documentado em [SITUS-EXP-001](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/docs/SITUS-EXP-001-v0.1.0.md) e [SITUS-PATCH-002 v0.2.0](file:///run/media/jpereiratrindade/labeco10T/dev/cpp/SITUS/docs/SITUS-PATCH-002-v0.2.0.md):

| Fonte integrada | Nível espacial | Registros RS | Limitação estrutural |
| :--- | :--- | ---: | :--- |
| `geonames_postal_br` (`CC BY 3.0`) | `R1` (município) | `467` | Apenas 1 CEP genérico/sede por município; faltam 30 municípios |
| `osm_postal_sul_rs` (`ODbL 1.0`) | `R1`–`R4`, `POSTAL_REGION` | `4.523` (`~4.039` CEPs distintos) | Tag `postal_code` ausente na maioria das vias (`3.374` registros `R4` em todo o RS) |
| **Referência universo RS (IBGE CNEFE 2022)** | **CEPs com endereços no RS (`90*`–`99*`)** | **`48.771` CEPs** | **~91,7% dos CEPs do RS sem resolução local no SITUS** |

Em consequência, consultas como `96415-610` (Bagé/RS) retornam atualmente `UNAVAILABLE` na interface de exploração, embora o município e a malha viária existam localmente.

---

## 3. Avaliação das 7 fontes e estratégias

### 3.1. Faixas oficiais de CEP por município (Correios / DNE)
- **O que é:** Tabela oficial de faixas de CEP atribuídas por município/localidade pela ECT (Correios).
- **Resolução alcançada:** `R1` (município) — ou `R2` quando houver faixa por distrito.
- **Proveniência e licenciamento:** Dados proprietários / sujeitos aos termos de uso do DNE (Diretório Nacional de Endereços) dos Correios. Não há redistribuição aberta irrestrita gratuita para uso irrestrito sem contrato/licença específica.
- **Viabilidade em `CATEGORY_A_ONLY`:** Não integrável diretamente via raspagem ou cópias não licenciadas. Exige via institucional (Correios / Conecta Gov.br) ou fonte derivada validada.

### 3.2. `datasets-br/city-codes` (`postalCode_ranges`)
- **Artefato avaliado:** `city_codes.csv` (`415 KB`), SHA-256 `ba723bda1d3b10225f35c896d377d3b1814bf4e9f138f8c330a8025101ee33c9`.
- **Cobertura e consistência verificada (RS):**
  - `497 / 497` municípios do RS presentes (**100%**).
  - `0` municípios sem `postalCode_ranges`.
  - `497` faixas válidas analisadas; **0 sobreposições** entre municípios distintos.
  - Teste piloto: `96415-610` resolve deterministicamente para `4301602` (**Bagé**, faixa `96400001`–`96444999`).
- **Resolução espacial alcançada:** `R1` (município inteiro via polígono oficial IBGE), sem inferência submunicipal.
- **Proveniência e licenciamento:** O repositório não possui arquivo `LICENSE` dedicado (HTTP 404). O `datapackage.json` declara licença `ODC-PDDL 1.0` (`http://opendatacommons.org/licenses/pddl/1.0/`) e cita ISO, IBGE e Wikipedia, enquanto o `README.md` menciona IBGE + Wikipedia + CEP. Como a coluna `postalCode_ranges` reproduz faixas definidas pelos Correios, há risco de ambiguidade de proveniência a jusante.
- **Custo e manutenção local:** Custo praticamente nulo (`415 KB`, atualização rara pois faixas municipais mudam apenas em emancipações ou revisões postais).

### 3.3. Base de Faces de Logradouros do IBGE — Censo 2022
- **Artefato avaliado:** `RS_faces_de_logradouros_2022_shp.zip` (`39,71 MB`), publicado em `2024-06-07` em `geoftp.ibge.gov.br`, SHA-256 `f1b38ad79997cd00ab47e326dad8c6c8118bdebe59bff0c0a4eb03a730de0fec`.
- **Estrutura e conformidade `CATEGORY_A_ONLY`:**
  - `497` shapefiles municipais (`<CD_MUN>_faces_de_logradouros_2022.shp`), CRS `EPSG:4674` (`SIRGAS 2000`), geometria `LineString`.
  - Campos: `CD_SETOR`, `CD_QUADRA`, `CD_FACE`, `NM_TIP_LOG`, `NM_TIT_LOG`, `NM_LOG`, `TOT_RES`, `TOT_GERAL`.
  - **Não contém** números de imóveis, coordenadas domiciliares, nomes de moradores **nem coluna de CEP**.
- **Cobertura amostral verificada:**
  - **Bagé (`4301602`):** `7.631` faces, `7.159` com nome, `939` logradouros distintos (`NM_LOG`).
  - **Porto Alegre (`4314902`):** `56.645` faces, `49.202` com nome, `6.951` logradouros distintos.
  - **Santa Maria (`4316907`):** `15.529` faces, `12.632` com nome, `1.865` logradouros distintos.
- **Proveniência e licenciamento:** Dados abertos oficiais do IBGE (Categoria A).
- **Custo e manutenção local:** Baixo (`~40 MB` compactado, `~150 MB` descompactado para todo o RS); facilmente indexável em PostGIS (`LineString`, GiST + índice textual trigrama/normalizado em `NM_LOG` por `CD_MUN`).

### 3.4. Cadastros municipais públicos de logradouros
- **Exemplos verificados:** Prefeitura de Porto Alegre (SMAMUS / DataPOA / GeoPOA) disponibiliza eixos de logradouros e divisão territorial em `Shapefile`/`CSV`/OGC.
- **Limitações:** Cobertura altamente heterogênea entre os 497 municípios do RS; maioria dos municípios médios e pequenos (incluindo Bagé) não publica tabela aberta `CEP ↔ logradouro` em formato padronizado com licença explícita.
- **Papel no SITUS:** Útil como camada complementar municipal onde houver publicação oficial aberta com licença explícita, mas insuficiente como solução estadual uniforme.

### 3.5. Base OpenCEP (`SeuAliado/OpenCEP`)
- **Artefato avaliado:** Repositório GitHub `SeuAliado/OpenCEP` (`LICENSE` e `README.md`).
- **Proveniência e licenciamento:** O código possui licença `MIT License (Copyright (c) 2023 Seu Aliado)`, porém o `README.md` informa explicitamente: *"Faça download do banco de dados dos correios ibge e CEP na versão mais atual... Atualizamos frequentemente a base de dados junto com os Correios!"*.
- **Veredito (`CATEGORY_A_ONLY`):** **Inadmissível para hospedagem/integração local** no SITUS sem autorização/licenciamento explícito da base de dados subjacente (Correios/DNE). Licença MIT no código não confere licença aberta aos dados postais redistribuídos.

### 3.6. Cadastro Base de Endereço (CBE) — Conecta Gov.br
- **Descrição:** Conjunto mínimo de dados padronizados de endereçamento instituído pelo Comitê Central de Governança de Dados (CCGD) em parceria com os Correios e disponibilizado na plataforma de interoperabilidade **Conecta Gov.br** (`https://www.gov.br/conecta/catalogo/`).
- **Elegibilidade e acesso:** Restrito a órgãos e entidades da Administração Pública (Federal — vinculados ao SISP/PTD; Estaduais/Municipais — alinhados à ENGD com ponto focal designado), mediante credenciamento do órgão, solicitação de adesão à API de *CEP / Cadastro Base de Endereço* e aprovação pelo órgão gestor.
- **Implicações para o SITUS:**
  - Permite resolver oficialmente `CEP → logradouro, bairro, município, UF` com respaldo jurídico.
  - **Atenção à privacidade e localidade:** Por padrão o Conecta Gov.br opera via API externa autenticada (gateway federal), o que conflitaria com a regra de *não enviar CEPs de usuários a serviços externos* em tempo de consulta. Para preservar a operação 100% local, seria necessário verificar nos termos de adesão se há modalidade de carga/espelho institucional autorizado ou restringir o uso a consultas autorizadas em ambiente institucional fechado com consentimento explícito.

### 3.7. Produto agregado oficial do IBGE (CNEFE / Faces)
- **Achado empírico relevante:** O IBGE já publica desde `14/06/2024` o conjunto **`Agregados_por_CEP_CNEFE_Censo_2022.zip`** (`8,6 MB`, SHA-256 `38ff712959f26e5f40b81dc5001f34b675d6ced04aa8a379fb004a4d94945908`), contendo **`931.499` CEPs no Brasil** e **`48.771` CEPs no RS** (`90*`–`99*`).
- **Colunas atuais do arquivo público:** `CEP`, `TOT_END`, `TOT_DP`, `TOT_TIPO_101`..`104`, `TOT_DOM_COND`, `TOT_DC`, `TOT_EAGRO`, `TOT_EENSINO`, `TOT_ESAUDE`, `TOT_ERELIG`, `TOT_EOF`, `TOT_ECR`.
- **Lacuna identificada:** O arquivo já prova que o IBGE possui pipeline de agregação não individualizante por CEP (sem coordenadas domiciliares, números de imóveis ou moradores), **porém não inclui chaves territoriais** (`CD_MUN`, `CD_SETOR`, `CD_FACE` ou `NM_LOG`).
- **Viabilidade de solicitação ao IBGE:** **Alta viabilidade técnica e institucional.** Um produto agregado oficial contendo `(CEP, CD_MUN, CD_SETOR, list(CD_FACE) ou NM_LOG, TOT_END)` — opcionalmente filtrado por limiar mínimo de anonimização ($k \ge 5$ endereços por CEP/face) — eliminaria qualquer dado individualizante e permitiria cruzamento direto e 100% Categoria A com a **Base de Faces de Logradouros 2022** e a **Malha de Setores Censitários 2022** já integradas no SITUS.

---

## 4. Experimento piloto: Bagé (`4301602`) e generalização

### 4.1. Piloto `CEP 96415-610` ↔ `Rua Rádio Cultura`
1. **Resolução `R1` (`city-codes`):** `96415-610` cai na faixa `96400001`–`96444999` → município `4301602` (**Bagé**).
2. **Existência censitária (`IBGE Agregados por CEP 2022`):** `96415610` consta oficialmente com `TOT_END=39`, `TOT_DP=38` (casas `101`), `TOT_ECR=1`.
3. **Geometria no OpenStreetMap (`sul-latest.osm.pbf`):** A via existe com geometria exata (`LineString`, `highway=residential`, `name="Rua Rádio Cultura"`, de `[-54.0901002, -31.3254179]` a `[-54.0866898, -31.3262436]`), porém **sem a tag `postal_code`**.
4. **Geometria no IBGE (`4301602_faces_de_logradouros_2022.shp`):** Filtrando `NM_LOG LIKE '%CULTURA%'`, foram recuperadas **4 faces de logradouro** para `RUA RADIO CULTURA` no setor `430160205000097P` (`CD_QUADRA` `001`, `002`, `013`, `014`), em coordenadas perfeitamente alinhadas ao eixo do OSM (`LINESTRING (-54.090122 -31.325511, -54.088447 -31.326227)` etc.).

### 4.2. Verificação de generalização
O mesmo padrão se repete em outras vias e municípios do RS:
- **Bagé (`4301602`):** `SETE DE SETEMBRO` possui `30` faces no IBGE (`939` logradouros distintos no município).
- **Santa Maria (`4316907`):** `RIO BRANCO` possui `77` faces no IBGE (`1.865` logradouros distintos no município).
- **Porto Alegre (`4314902`):** `6.951` logradouros distintos cobertos na Base de Faces do IBGE (`56.645` faces).

**Conclusão do piloto:** A junção `(Município + Nome normalizado do logradouro) → Geometria da via/faces` funciona de forma generalizada em todo o RS usando bases já disponíveis localmente (IBGE Faces + OSM). O único elo faltante para automatizar a resolução `R4` sem perder privacidade é a correspondência autorizada `CEP → (CD_MUN, NM_LOG)`.

---

## 5. Quadro comparativo consolidado

| Fonte / Estratégia | Cobertura RS | Novos CEPs resolvíveis | Resolução | Proveniência / Licença | Custo / Manutenção local | Status `CATEGORY_A_ONLY` |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Linha de base (GeoNames + OSM)** | `~4.039` CEPs (`~8,3%`) | `0` (atual) | `R1`–`R4` | `CC BY 3.0` / `ODbL 1.0` | Já integrado | Aprovado |
| **`datasets-br/city-codes`** | `497/497` mun. (`100%`) | `+~44.700` em `R1` | `R1` | `ODC-PDDL 1.0` (faixas derivadas de Correios) | Muito baixo (`415 KB`) | **Condicionado à revisão jurídica** |
| **IBGE Faces de Logradouros 2022** | `497/497` mun. (`100%` vias) | `0` sozinha (sem CEP) | `R4` (geometria) | Dados Abertos IBGE | Baixo (`40 MB` zip) | **Aprovado (cartografia)** |
| **IBGE Agregados por CEP 2022** | `48.771` CEPs RS | `0` espacial (sem `CD_MUN`/`NM_LOG`) | Tabular (`CEP`) | Dados Abertos IBGE | Muito baixo (`8,6 MB` zip) | Aprovado (estatística) |
| **OpenCEP (`SeuAliado/OpenCEP`)** | Nacional (alta) | `+~44.000` | `R1`–`R4` | Código MIT; **dados Correios/DNE sem licença** | Baixo | **Rejeitado** |
| **Cadastros municipais públicos** | Parcial (grandes mun.) | Variável por município | `R4` (onde houver CEP) | Varia por prefeitura | Médio (heterogêneo) | Caso a caso |
| **Conecta Gov.br (CBE)** | Nacional (`100%`) | `+~44.700` (`R1`–`R4`) | `R1`–`R4` | Interoperabilidade Gov.br / Correios | Requer credenciamento institucional | **Elegível via órgão público** |
| **Produto agregado IBGE (solicitado)** | `48.771+` CEPs RS | `+~44.700` (`R1`–`R4`) | `R2`/`R4` (setor/face) | Dados Abertos IBGE (não individualizante) | Muito baixo (carga local única) | **Recomendado (Categoria A pura)** |

---

## 6. Ambiguidades, conflitos e riscos

1. **Logradouros extensos com múltiplos CEPs:** Vias longas (ex.: avenidas ou estradas rurais) possuem mais de um CEP por trecho de numeração. Sem faixa de numeração ou `CD_FACE` específico, a resolução por `(CD_MUN, NM_LOG)` retorna **todas as faces daquele logradouro** — o que é metodologicamente honesto, desde que a interface explicite que a geometria representa o logradouro completo e não um trecho específico.
2. **Normalização toponímica:** Grafias diferem entre bases postais e cartográficas (ex.: `RUA RADIO CULTURA` sem acento no IBGE vs `Rua Rádio Cultura` no OSM; títulos como `Rua`, `Av.`, `Dr.`, `Cel.`). Exige função determinística de normalização (`unaccent`, maiúsculas, remoção de tipo/título opcional) com cálculo de escore de correspondência e sinalização de ambiguidade quando houver homônimos no mesmo município.
3. **CEPs únicos de grandes usuários / caixas postais:** Em grandes cidades, CEPs especiais pertencem a edifícios públicos, universidades ou agências postais, não a trechos de logradouro comuns; devem ser tratados como `R1` ou `UNAVAILABLE` submunicipal caso não haja face pública associada sem individualização.
4. **Privacidade em consulta externa:** Qualquer consulta em tempo real do CEP digitado pelo usuário a uma API externa viola o princípio de zero vazamento de localização. Apenas bases locais (ou espelhos locais institucionalmente autorizados) atendem integralmente ao requisito operacional do SITUS.

---

## 7. Recomendação fundamentada de implementação

Recomenda-se uma estratégia em **três frentes ordenadas por viabilidade imediata e aderência à política `CATEGORY_A_ONLY`**:

1. **Curto prazo (Frente A — `R1` + `R4` cartográfico local):**
   - **Ingerir a `Base de Faces de Logradouros 2022` (IBGE)** como camada cartográfica local de arruamentos (`CD_MUN`, `CD_SETOR`, `CD_QUADRA`, `CD_FACE`, `NM_LOG`), complementando o OSM para normalização e exibição submunicipal de vias quando o usuário investigar por logradouro ou setor.
   - **Submeter à aprovação de governança/jurídica** o uso das faixas municipais (`datasets-br/city-codes` / `postalCode_ranges`) como fallback estrito de nível **`R1` (município)** — com rótulo explícito de que o sistema determinou apenas o município (`497/497`, `0` sobreposições), eliminando respostas `UNAVAILABLE` para CEPs válidos do RS como `96415-610` (Bagé).
2. **Médio prazo (Frente B — Solicitação de produto agregado ao IBGE):**
   - Protocolar solicitação formal junto ao IBGE (CNEFE / Diretoria de Geociências) para disponibilização de uma tabela agregada não individualizante **`CEP ↔ (CD_MUN, CD_SETOR, CD_FACE / NM_LOG)`** (seguindo o precedente de `Agregados_por_CEP_CNEFE_Censo_2022.zip`, com limiar mínimo de agregação por face/setor). Essa é a solução definitiva 100% local, aberta e `CATEGORY_A_ONLY`.
3. **Médio/Longo prazo (Frente C — Via institucional Conecta Gov.br CBE):**
   - Caso haja vínculo institucional público (ex.: Embrapa / órgão federal SISP), solicitar adesão ao **Cadastro Base de Endereço (CBE)** no **Conecta Gov.br** avaliando a viabilidade contratual de sincronização local (ou modo institucional autorizado) para mapear `CEP → NM_LOG` e cruzar localmente com a Base de Faces do IBGE e o OSM já validados.

---

## 8. Registro de artefatos temporários verificados

Os arquivos baixados apenas para diagnóstico em `var/data/exp002/` (não versionados no Git e não integrados ao banco operacional):
- `city_codes.csv` — SHA-256: `ba723bda1d3b10225f35c896d377d3b1814bf4e9f138f8c330a8025101ee33c9`
- `RS_faces_de_logradouros_2022_shp.zip` — SHA-256: `f1b38ad79997cd00ab47e326dad8c6c8118bdebe59bff0c0a4eb03a730de0fec`
- `Agregados_por_CEP_CNEFE_Censo_2022.zip` — SHA-256: `38ff712959f26e5f40b81dc5001f34b675d6ced04aa8a379fb004a4d94945908`
