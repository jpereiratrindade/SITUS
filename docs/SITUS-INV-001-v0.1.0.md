---
document_id: SITUS-INV-001
title: Inventário de fontes e capacidades de conhecimento territorial
version: 0.1.0
date: 2026-11-26
status: PROPOSTA_PARA_VALIDACAO
document_role: INVENTARIO_TECNICO_CIENTIFICO
scope: RS
repository: SITUS
workspace: /run/media/jpereiratrindade/labeco10T/dev/cpp/situs
environment: Fedora Silverblue
data_policy: CATEGORY_A_ONLY
reference_commit: 7a868b0ef99bb4e273dc57385d26d18979a21731
references: [SITUS-PATCH-001-v0.2.0, SITUS-REL-001-v0.1.0, SITUS-EXEC-001-v0.1.0, SITUS-EXP-001-v0.1.0, SITUS-PATCH-002-v0.1.0, SITUS-DIR-001-v0.1.0]
principle: Sempre pronto. Sempre incompleto.
---

# SITUS — Inventário de fontes e capacidades de conhecimento territorial

## 1. Propósito e alcance

Converter o planejamento do SITUS em um catálogo de conhecimento potencial, com fontes concretas e critérios de aquisição. A investigação territorial deve ser independente de CEP e de Wasm: qualquer referência territorial admissível pode iniciar uma consulta. A unidade de análise é a geometria explicitamente escolhida; a resolução de cada informação continua sendo a da fonte. O documento é um inventário inicial, não autorização de ingestão nem confirmação de funcionamento.

Base documental: mandato reproduzido na conversa, revisão do commit de referência, relatórios [REL-001](https://github.com/jpereiratrindade/SITUS/blob/7a868b0ef99bb4e273dc57385d26d18979a21731/docs/SITUS-REL-001-v0.1.0.md), [EXP-001](https://github.com/jpereiratrindade/SITUS/blob/7a868b0ef99bb4e273dc57385d26d18979a21731/docs/SITUS-EXP-001-v0.1.0.md) e documentação pública consultada em 26/11/2026. Não houve reprodução local nem aquisição dos novos artefatos nesta revisão.

## 2. Convenções e política de admissão

- D: disponibilidade documental identificada em fonte pública; arquivo, cobertura e licença específica ainda devem ser comprovados localmente.
- E: avaliação experimental registrada pela equipe; não implica aprovação ou integração.
- I: integração com dados reais reportada pela equipe; ainda sujeita à consolidação independente.
- C: candidato para avaliação; não há confirmação suficiente de acesso, condições ou adequação do produto específico.
- P0: primeira entrega; P1: ampliação ambiental próxima; P2: desenvolvimento subsequente; P3: investigação posterior ou dependente de avaliação específica.
- R = raster (GeoTIFF/COG ou equivalente); V = vetor (GPKG/PostGIS); T = tabela (CSV/Parquet/PostgreSQL); S = séries temporais (NetCDF/GRIB/CSV conforme origem). Formatos indicados são destinos ou vias de aquisição propostas, não garantia de formato original.

Todo produto passa por aprovação individual: licença dos dados e artefatos derivados (não apenas do software), finalidade, cobertura, resolução, temporalidade, CRS, atributos permitidos, risco de reidentificação, proveniência, integridade e método. Onde não indicada licença nominal, não presumir autorização irrestrita. OSM exige atribuição e análise das obrigações ODbL para bases derivadas e distribuição. Dados públicos não são automaticamente categoria A. Não adquirir microdados pessoais, endereços individualizados, imóveis, CAR/SIGEF, registros de pacientes, alunos ou trabalhadores. Não importar arquivos mistos B/C para a base operacional; se não houver produto previamente filtrado admissível, suspender aquisição até aprovação explícita de procedimento de preparação compatível com a política. Estatísticas de pequenas populações ou atividades raras dependem de controle de divulgação, inclusive por cruzamentos e consultas sucessivas; não converter agregados em perfis individuais.

## 3. Inventário de produtos

### 3.1 Referências territoriais e cartografia

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| GEO-01 | [IBGE malha de setores censitários](https://www.ibge.gov.br/geociencias/organizacao-do-territorio/malhas-territoriais/26565-grade-estatistica.html) | Polígonos, Censo 2022 | Área, situação territorial, setores, interseções | GPKG oficial RS → V | P0 / I; esclarecer feições não inseridas |
| GEO-02 | [IBGE malhas territoriais](https://www.ibge.gov.br/geociencias/organizacao-do-territorio/malhas-territoriais.html) | Município, distrito, bairro quando disponível; versão explícita | Limites e identificação territorial | Download → V | P0 / D; preferir limites municipais oficiais, não presumir união de setores equivalente |
| GEO-03 | [IBGE BC250](https://geoftp.ibge.gov.br/cartas_e_mapas/bases_cartograficas_continuas/bc250/) | 1:250.000; versão 2025 | Cartografia de referência, vias e hidrografia | Download → V | P1 / D; escala não cadastral |
| GEO-04 | [BCRS25 SEMA/FEPAM](https://sema.rs.gov.br/si-dados-geoespaciais) | 1:25.000, RS | Cartografia estadual, drenagem e outras feições públicas aprovadas | Download / IEDE → V | P1 / D; verificar camadas, licença e data |
| GEO-05 | [GeoNames postal BR](https://www.geonames.org/export/zip/) | Localidade/município; versão e atualidade a verificar | Referência postal R1 | BR.zip → tabela de correspondências | P0 / E; CC BY 3.0 conforme documentação postal; conferir versão e códigos; coordenada não é endereço |
| GEO-06 | [OSM Geofabrik Sul](https://download.geofabrik.de/south-america/brazil/sul.html) / [licença](https://www.openstreetmap.org/copyright) | Vias, limites e localidades; snapshot | Referências postais R1–R5 e cartografia pública | Extrato previamente filtrado → V/T | P0–P2 / E; ODbL; cobertura desigual, sem imóveis ou endereços |
| GEO-07 | [IBGE CNEFE agregado por CEP](https://ftp.ibge.gov.br/Cadastro_Nacional_de_Enderecos_para_Fins_Estatisticos/Censo_Demografico_2022/Agregados_por_CEP/) | CEP, Censo 2022 | Sinal de existência censitária | Produto agregado; sem localização própria | P2 / E; não ingerir contagens domiciliares sem avaliação; evitar uso de CEP raro |
| GEO-08 | [IBGE biomas](https://www.ibge.gov.br/geociencias/informacoes-ambientais/estudos-ambientais/15842-biomas.html?edicao=45164) | 1:250.000, versão/revisão explicitada | Bioma e transições cartográficas | Download → V | P1 / D; não é mapa de vegetação atual |

### 3.2 Paisagem, vegetação e atividade agroprodutiva espacializada

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| ECO-01 | [MapBiomas Cobertura 30 m](https://brasil.mapbiomas.org/iniciativas-e-produtos/cobertura-e-uso-da-terra/cobertura-30m/cobertura/) | 30 m, anual; Coleção 11, 1985–2025 | Áreas por classe, campos, florestas, agricultura, água, transições | [GeoTIFF por recorte](https://brasil.mapbiomas.org/downloads/baixe-mapas-recortados-geotiff-via-google-earth-engine/) → R | P0 / D; CC BY 4.0 conforme portal; fixar coleção, ano, legenda e validação Pampa |
| ECO-02 | [MapBiomas Cobertura 10 m](https://brasil.mapbiomas.org/iniciativas-e-produtos/cobertura-e-uso-da-terra/cobertura-10m/cobertura/) | 10 m, anual; coleção 4 documentada, anos a confirmar | Detalhe da cobertura da terra | GeoTIFF → R | P2 / D; não identificar imóveis; não concatenar séries 10/30 m sem harmonização |
| ECO-03 | [MapBiomas Fogo](https://brasil.mapbiomas.org/destaques/) | 30 m; coleção 5, 1985–2025 | Área queimada, recorrência e último evento mapeado | GeoTIFF → R/S | P2 / D; cicatriz não é foco nem causa |
| ECO-04 | [MapBiomas Degradação](https://brasil.mapbiomas.org/downloads/documentos-e-notas-tecnicas/) | Conforme produto/coleção | Vetores mapeados de degradação e fragmentação | GeoTIFF → R | P2 / D; indicadores não provam degradação ecológica em campo |
| ECO-05 | [IBGE BDiA vegetação](https://www.ibge.gov.br/geociencias/informacoes-ambientais/geologia/23382-banco-de-informacoes-ambientais.html) | 1:250.000 | Contexto fitoecológico | Download → V | P1 / D; distinguir vegetação de referência e cobertura atual |
| ECO-06 | [Sentinel-2 L2A](https://dataspace.copernicus.eu/data-collections/copernicus-sentinel-missions/sentinel-2) | 10/20/60 m por banda; cenas datadas | NDVI, EVI, NDMI, sazonalidade e anomalias | Data Space, aquisição administrativa → R | P2 / D; licença Copernicus a registrar; nuvens, QA, escala e normalização |
| ECO-07 | [Landsat Collection 2 L2](https://www.usgs.gov/landsat-missions/landsat-collection-2-level-2-science-products) | Reflectância 30 m; desde 1982, conforme sensor | Séries espectrais, temperatura de superfície em produto próprio | Download USGS → R | P2 / D; domínio público, atribuir; temperatura de superfície não é temperatura do ar |
| AGR-01 | [MapBiomas Pastagem](https://brasil.mapbiomas.org/iniciativas-e-produtos/cobertura-e-uso-da-terra/pastagem/pastagem/) | 30 m, anual, 1985–2025 para mapa de pastagem; derivados variáveis | Pastagem cultivada, idade, vigor, biomassa mapeada | GeoTIFF → R | P2 / D; NÃO confundir campo nativo manejado com pastagem cultivada |
| AGR-02 | [MapBiomas Agricultura e Irrigação](https://brasil.mapbiomas.org/downloads/assets-no-google-earth-engine/) | Conforme produto e coleção | Cultivos, frequência, irrigação mapeada | GeoTIFF via plataforma/GEE → R | P2 / D; sem inferência sobre produtor, propriedade ou rendimento |
| AGR-03 | [IBGE PAM](https://www.ibge.gov.br/estatisticas/economicas/agricultura-e-pecuaria/9117-producao-agricola-municipal-culturas-temporarias-e-permanentes.html) | Município, anual | Área colhida, produção, valor e rendimento médio | Tabelas agregadas via SIDRA → T | P2 / D; não desagregar para pixel ou produtor |
| AGR-04 | [IBGE PPM](https://www.ibge.gov.br/estatisticas/economicas/agricultura-e-pecuaria/9107-producao-da-pecuaria-municipal.html) | Município, anual | Efetivos e produção animal | SIDRA → T | P2 / D; não estimar rebanho de recorte local por simples rateio |

### 3.3 Relevo, geologia e solos

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| REL-01 | [TOPODATA INPE](https://data.inpe.br/dados/topodata/) | ~30 m, refinamento SRTM ~90 m | Elevação, declividade, orientação e curvaturas | Download/STAC → R | P1 / D; não é levantamento topográfico de 30 m independente |
| REL-02 | SRTM de 1 segundo de arco / [USGS EarthExplorer](https://earthexplorer.usgs.gov/) | ~30 m; missão 2000 | Elevação e relevo | Download → R | P1 / C; validar produto, datum vertical, licença e acesso |
| REL-03 | [IBGE BDiA geomorfologia](https://www.ibge.gov.br/geociencias/informacoes-ambientais/geologia/23382-banco-de-informacoes-ambientais.html) | 1:250.000; versão 2023 documentada | Unidades geomorfológicas | Download → V | P1 / D; escala regional |
| REL-04 | [IBGE BDiA geologia](https://www.ibge.gov.br/geociencias/informacoes-ambientais/geologia/23382-banco-de-informacoes-ambientais.html) | 1:250.000 | Unidades geológicas | Download → V | P1 / D; não substitui sondagem |
| SOL-01 | [IBGE BDiA pedologia](https://www.ibge.gov.br/geociencias/informacoes-ambientais/pedologia/23382-banco-de-informacoes-ambientais.html) | 1:250.000 | Classes e associações de solos | Download → V | P1 / D; não desagregar classes a pixel |
| SOL-02 | [ISRIC SoilGrids](https://isric.org/explore/soilgrids) / [acesso](https://docs.isric.org/globaldata/soilgrids/SoilGrids_faqs_02.html) | 250 m, seis profundidades | Textura, pH, carbono, densidade, N, CTC e incerteza | Recorte de rasters/VRT na aquisição → R | P2 / D; CC BY 4.0; registrar unidades e fatores de escala, não substituir amostragem |
| SOL-03 | [MapBiomas Solo](https://brasil.mapbiomas.org/downloads/documentos-e-notas-tecnicas/) | 30 m, coleção 3 documentada, 1985–2024, 0–30 cm | Estoque estimado de carbono orgânico (t/ha) | GeoTIFF → R | P2 / D; modelo, não medição; mudança entre anos exige cautela |

### 3.4 Água, clima e dinâmica ambiental

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| HID-01 | [ANA BHO6](https://www.gov.br/ana/pt-br/assuntos/noticias-e-eventos/noticias/base-de-dados-geoespaciais-da-ana-permite-gestao-mais-eficiente-das-bacias-hidrograficas/) | Multiescalar, rede e ottobacias | Bacias, trechos, conectividade montante/jusante | Catálogo SNIRH / download → V | P1 / D; usar topologia, verificar escala local |
| HID-02 | [BCRS25 hidrografia](https://sema.rs.gov.br/si-dados-geoespaciais) | 1:25.000 RS | Densidade de drenagem, proximidade de cursos | Download → V | P1 / D; não confundir com limite legal de APP |
| HID-03 | [MapBiomas Água](https://brasil.mapbiomas.org/downloads/documentos-e-notas-tecnicas/) | 30 m, mensal/anual, coleção 5 1985–2025 | Ocorrência, sazonalidade e permanência de água superficial | GeoTIFF → R/S | P1 / D; não mede qualidade nem vazão |
| HID-04 | [ANA HidroWebservice](https://www.ana.gov.br/hidrowebservice/swagger-ui/index.html) | Estações, séries de chuva/cota/vazão e qualidade onde disponível | Regime hidrológico observado | API administrativa → S | P2 / D; autenticação, qualidade e lacunas a validar; não inferir valor em ponto não medido |
| CLI-01 | [INMET dados históricos](https://portal.inmet.gov.br/dadoshistoricos/) | Estações automáticas, pacotes anuais desde 2000 | Temperatura, chuva, vento, umidade e radiação | CSV/ZIP → S | P2 / D; conferir completude, representatividade e horário |
| CLI-02 | [CHIRPS v3](https://www.chc.ucsb.edu/data/chirps3) | 0,05°, desde 1981 | Precipitação estimada, anomalias e períodos secos | Recortes e arquivos históricos → R/S | P2 / D; confirmar licença e versão, não inferir microclima |
| CLI-03 | [ERA5-Land](https://cds.climate.copernicus.eu/datasets/reanalysis-era5-land) | ~9 km, grade 0,1°, horária desde 1950 | Temperatura, umidade do solo modelada, evaporação e balanço de superfície | CDS, aquisição administrativa NetCDF/GRIB → S | P2 / D; reanálise, registrar licença CDS, unidade e convenção temporal |

### 3.5 Conservação, risco e exposição

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| CON-01 | [ICMBio limites federais](https://www.gov.br/icmbio/pt-br/dados-icmbio/dados_geoespaciais) / [CNUC](https://dados.mma.gov.br/dataset/unidadesdeconservacao) | Polígonos oficiais; versão | Proximidade e interseção com UCs públicas | Download SHP → V | P2 / D; excluir RPPN e outros polígonos individualizantes; somente atributos institucionais |
| CON-02 | [SEMA/FEPAM geodados](https://fepam.rs.gov.br/geoprocessamento) | Escala e vigência por camada | Zoneamentos e áreas públicas de interesse ambiental | Download → V | P2 / C; validar produto específico e evitar processos individualizados |
| RIS-01 | [SGB cartas de suscetibilidade RS](https://www.sgb.gov.br/rio-grande-do-sul-cartografia-de-suscetibilidade) | Geralmente 1:25.000, municípios cobertos | Suscetibilidade a inundação e movimentos de massa | Produtos vetoriais/raster onde disponíveis | P2 / D; cobertura parcial; não equivale a probabilidade de desastre ou risco individual |
| RIS-02 | [Atlas Digital de Desastres](https://www.gov.br/mdr/pt-br/noticias/atlas-digital-de-desastres-no-brasil-e-atualizado-com-dados-consolidados-de-2024) | Município, registros datados (1991–2024 no anúncio consultado) | Histórico de desastres registrados | Exportação agregada a validar → T | P2 / D; ausência de registro não prova ausência de evento |
| RIS-03 | [AdaptaBrasil MCTI](https://adaptabrasil.mcti.gov.br/sobre/perguntas-frequentes) | Município | Indicadores e índices de risco climático | Exportação administrativa a confirmar → T | P2 / D; contextualização municipal, sem interpolação local |
| RIS-04 | MapBiomas Fogo (ECO-03) e [INPE produtos satelitais](https://www.gov.br/pt-br/servicos/acessar-gratuitamente-dados-e-produtos-derivados-de-satelites-radares-e-instrumentos-meteorologicos-correlatos) | Cicatrizes ou focos com suporte por sensor | Ocorrência territorial de fogo | Agregados ou dados ambientais aprovados → R/S | P2 / D; não atribuir ignição, autoria ou imóvel |

### 3.6 Infraestrutura, serviços e contexto socioeconômico

| ID | Produto e fonte | Suporte e tempo | Conhecimento possível | Acesso / integração proposta | Prioridade e estado / ressalva |
|---|---|---|---|---|---|
| INF-01 | [DNIT SNV](https://www.gov.br/dnit/pt-br/assuntos/atlas-e-mapas/pnv-e-snv/capa-snv/) | Segmentos de rodovia, versão | Rede viária de referência e acessibilidade | Acervo → V/T | P2 / D; verificar geometria e atributos do produto |
| INF-02 | OSM (GEO-06) / BCRS25 (GEO-04) | Vias e equipamentos públicos | Distância e conectividade da rede pública | Extrato admissível → V | P2 / E para OSM postal, D para demais temas; não importar estabelecimentos particulares |
| INF-03 | [CNES estabelecimentos](https://dadosabertos.saude.gov.br/dataset/cnes_estabelecimentos_cadastrados) | Equipamentos institucionais, versões | Acesso territorial a unidades públicas de saúde | Produto previamente filtrado → V/T | P3 / D; não importar nomes/contatos pessoais, consultórios individuais nem dados de pacientes; coordenadas a verificar |
| INF-04 | [INEP Censo Escolar](https://www.gov.br/inep/pt-br/acesso-a-informacao/dados-abertos/microdados/censo-escolar) | Escola/município, anual | Distribuição de escolas públicas, se produto institucional admissível | Produto agregado/institucional separado → T/V | P3 / C; não ingerir microdados de alunos/docentes ou arquivo misto; verificar produto específico |
| INF-05 | [SINISA resultados](https://www.gov.br/cidades/pt-br/acesso-a-informacao/acoes-e-programas/saneamento/sinisa/resultados-sinisa/resultados-sinisa-2025) | Município/prestador, referência 2024 no produto | Água, esgoto, resíduos e drenagem agregados | Planilhas → T | P3 / D; apenas indicadores territoriais, não domicílios |
| INF-06 | [IBGE entorno urbano Censo 2022](https://www.ibge.gov.br/estatisticas/sociais/populacao/22827-censo-demografico-2022.html?edicao=41852&t=downloads) | Setor, 2022 e divulgação 2025 | Pavimentação, arborização, calçadas, iluminação, acessibilidade | Agregados aprovados → T | P3 / D; não expor contagens pequenas ou dados domiciliares |
| SOC-01 | [IBGE grade estatística 2022](https://geoftp.ibge.gov.br/recortes_para_fins_estatisticos/grade_estatistica/censo_2022/) | Células de diferentes dimensões; grades agregadas 1–500 km no diretório | Distribuição territorial de população/domicílios, após avaliação | Agregados → V/T | P3 / D; priorizar agregação robusta; suprimir pequenas contagens; resolução original a confirmar no produto |
| SOC-02 | [IBGE agregados por setores 2022](https://www.ibge.gov.br/estatisticas/sociais/populacao/22827-censo-demografico-2022.html?edicao=41852&t=conceitos-e-metodos) | Setor e agregações | Indicadores demográficos amplos efetivamente anônimos | Tabelas previamente selecionadas → T | P3 / D; excluir recortes sensíveis/raros e cruzamentos individualizantes |
| SOC-03 | [IBGE SIDRA](https://sidra.ibge.gov.br/) | Município, série conforme pesquisa | PIB, estrutura econômica e outros indicadores agregados | Exportação/API administrativa → T | P3 / C para produtos específicos; acesso ao portal/API não confirmado nesta revisão (HTTP 403 no ambiente de pesquisa) |

Outras linhas de pesquisa a inventariar em versões futuras: cobertura móvel Anatel, cartografia de transporte público oficial, produtividade primária MODIS, evapotranspiração por sensoriamento remoto, regionalizações estaduais, dados agregados de emprego e economia, alertas meteorológicos. Não se afirma acesso a produto específico antes da verificação.

## 4. Operações de conhecimento (independentes da fonte específica)

| Operação | Entradas | Saída | Regra científica |
|---|---|---|---|
| Interseção territorial | Geometria e polígonos | Área, proporção, unidades | Área geodésica/projeção adequada; evitar sobreposição dupla |
| Composição categórica | Polígono e raster de classes | ha e % por classe | Máscara efetiva; áreas de pixel, NoData, denominador, bordas, legenda |
| Estatística zonal contínua | Polígono e raster contínuo | Média ponderada, mediana, quantis, amplitude | Unidades, pesos de área, cobertura, incerteza e resolução |
| Proximidade | Área e feições públicas | Distância mínima ou distribuição | Distância geodésica não é distância de rede |
| Relação hidrológica | Área, drenagem e ottobacias | Trechos, bacias, conectividade | Topologia explícita, sem inferir qualidade hídrica |
| Dinâmica temporal | Mesma área, séries comparáveis | Mudanças, permanência e transições | Harmonizar coleção/legenda/CRS; não confundir mudança metodológica com mudança real |
| Fragmentação e conectividade | Máscara temática e escala | Área de manchas, bordas, distâncias | Declarar vizinhança, tamanho mínimo, extensão e sensibilidade; não inferir conectividade funcional de espécies |
| Contraste territorial | Duas geometrias e observações comparáveis | Diferenças por variável | Mesmos períodos, métodos, suporte; sem causalidade automática |
| Similaridade territorial (futura) | Vetores de observações válidas | Vizinhança por atributos | Escalas, dados ausentes, pesos e incerteza explícitos; sem ranking arbitrário |
| Contexto agregado | Unidade administrativa e tabela | Informação contextual | Não desagregar espacialmente nem associar a indivíduos |

A camada temática de origem, a observação calculada e a interpretação científica são objetos distintos. Não atribuir causalidade a mera coexistência espacial. Não produzir lotação, capacidade de suporte, biodiversidade específica ou condição de conservação de campos naturais sem evidências adicionais e validação de método.

## 5. Contrato mínimo de conhecimento e catálogo

Sem criar microserviços: ampliar o catálogo existente e o contrato `observation` do SITUS. Cada produto, camada, método e observação deverá registrar identificadores persistentes, `source_id`, produto/coleção/versão, URL e data de acesso, licença e atribuição, SHA-256, formato, variáveis, unidade, CRS e datum quando aplicável, tipo de suporte, resolução/escala original, cobertura espacial/temporal, método e versão, `support_geometry` e `analysis_geometry` separadas, `coverage_fraction`, denominador, NoData, incerteza publicada, limitações, `approval_status`, atributos autorizados e estado operacional. Identificar natureza `MEASURED`, `MAPPED`, `MODELLED`, `AGGREGATED`, `DERIVED` (proposta de vocabulário), sem confundir natureza com grau de certeza. Uma observação indisponível deve retornar NO_DATA/SUPPRESSED/NOT_APPLICABLE, não zero.

Aquisição administrativa por recorte público fixo (RS ou piloto), jamais envio da consulta do usuário. Arquivos e credenciais fora do Git. V no PostGIS; R em GeoTIFF/COG local via GDAL; T em PostgreSQL; S em arquivos locais indexados, sem nova infraestrutura obrigatória. Registrar tamanho em disco, tempo de aquisição, limites, checksum, versão e licença no ato da aquisição. Para rásteres grandes, recortar na preparação; não baixar séries nacionais/globais completas sem estimar custo. Não copiar dados de outros sistemas SisTer sem autorização e auditoria. Preservar fonte e licença separadamente do GPL do código.

O servidor nativo é o caminho de referência para leitura raster, interseção espacial e acesso ao catálogo. Wasm opcional para operações portáveis e aprovadas, como síntese determinística; não transferir pilhas inteiras nem dados desnecessários. Comparar caminhos completos no Firefox com os mesmos contratos, preservando equivalência e fallback.

## 6. Plano de execução vinculante somente após aprovação do mandato

1. Consolidação PATCH-001 v0.2.1: integridade da malha, discrepância de 6 feições válidas, cobertura dos recortes, testes independentes, fluxo adaptativo não redundante, privacidade.
2. PATCH-002 v0.2.0: reprodução do EXP-001 e resolução postal local com fontes admissíveis. Os 467 municípios GeoNames, 483 OSM, 497 combinados e 4.086 CEPs distintos combinados são valores reportados, não verificados aqui. Cobertura municipal não é cobertura postal. Incerteza, conflitos e geometrias de suporte devem permanecer explícitos.
3. PATCH-003 v0.1.0: primeiro MapBiomas ECO-01 funcional. Fixar uma coleção e um ano, idealmente 2025/coleção 11 se o artefato e a legenda forem adquiríveis e aprovados; manter alternativa de coleção anterior registrada. Corrigir chamada raster com bbox zerado; usar polígono efetivo; validar CRS, pixels de borda, NoData e áreas. Aquisição pode avançar em paralelo a 1 e 2.
4. Após validação: TOPODATA REL-01, BCRS25 HID-02 e BHO6 HID-01, bioma GEO-08 e pedologia SOL-01. Em seguida SoilGrids, clima, séries históricas e infraestrutura. Não colocar todo o inventário no próximo patch.
5. Avaliar produtos P3 somente após procedimento de anonimização e risco aprovado. Não implementar módulo demográfico antes disso.

## 7. Experimento de aceite ambiental proposto

Selecionar dois polígonos públicos de aproximadamente 100 ha em Bagé, escolhidos por método documentado e sem referir propriedades ou moradores. Usar mesma coleção, ano e método; registrar GeoJSON de recortes públicos não individualizantes. Extrair cobertura da terra com área válida por classe, área não observada, frações sobre área total e área observada. Confrontar com processamento independente (QGIS/GDAL ou consulta PostGIS Raster separada) com mesma regra de borda. Incluir polígono côncavo, com buraco, NoData, fora da cobertura e CRS distinto; verificar que soma de áreas e denominadores fecham dentro de tolerâncias explícitas. Somente após aceite estender para relevo, drenagem e séries. As diferenças entre áreas são questão de investigação, não resultado antecipado. Não declarar que campo nativo é pastagem cultivada, nem inferir manejo ou produtividade pecuária pela cobertura.

## 8. Entregáveis à equipe e critérios de promoção do inventário

Para cada fonte prioritária: ficha de avaliação, script reproduzível de aquisição, amostra admissível e hash, licença documentada, inventário de arquivos, cobertura no RS, CRS, tamanho, legenda/unidades, método, comparação independente e registro de falhas. Promover estados em etapas: IDENTIFICADA → ACESSO_VERIFICADO → ADMISSIBILIDADE_APROVADA → ADQUIRIDA → INTEGRADA → VALIDADA_COM_DADOS_REAIS. A transição pode ser bloqueada e não é automática. A classificação de aceite do código permanece a do mandato. Nenhuma camada é considerada operacional apenas por estar listada no catálogo.

Manter inventário versionado junto a `docs/`, inicialmente sem alterações nos demais projetos. Atualizar catálogo e documentação no mesmo commit que disponibiliza a camada. Separar aquisição, metodologia e interface. Relatar evidências, limitações e decisões. Não publicar artefatos brutos ou chaves. Não criar registros BB-CORE nesta etapa: preservar as fontes e descobertas para exame posterior, distinguindo constatações documentais, resultados experimentais e hipóteses.

## 9. Limitações desta versão

Inventário não exaustivo. Os portais e documentação dos produtos assinalados D foram identificados na pesquisa, mas os downloads não foram executados e seus hashes, tamanhos, cobertura regional e licenças específicas não foram validados localmente. Produtos C requerem confirmação adicional. O SIDRA bloqueou acesso no ambiente desta revisão. A data de consulta não é a data de referência do dado. Não houve execução do SITUS, atualização de repositório ou homologação de fonte. A seleção dos IDs, prioridades, vocabulário e sequência constitui proposta para revisão pela equipe.

**Sempre pronto. Sempre incompleto.**
