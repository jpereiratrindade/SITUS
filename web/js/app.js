// SITUS — Aplicação principal da interface web única
'use strict';

(() => {
    let activeAbort = null;
    let lastReference = null;
    const historyStack = []; // Apenas em memória da sessão (zero persistência)

    function updateBackButton() {
        const btn = document.getElementById('map-back-btn');
        if (btn) btn.hidden = historyStack.length === 0;
    }

    function fmtAreaHa(m2) {
        if (typeof m2 !== 'number' || !Number.isFinite(m2)) return '—';
        if (m2 >= 1e6) return (m2 / 1e6).toFixed(2) + ' km²';
        return (m2 / 1e4).toFixed(2) + ' ha';
    }

    function escapeHtml(s) {
        return String(s ?? '').replace(/[&<>"']/g, c => ({
            '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
        }[c]));
    }

    async function refreshHealthAndCatalog() {
        const connEl = document.getElementById('connection');
        const wasmEl = document.getElementById('wasm-badge');
        try {
            const [hRes, cRes, caps] = await Promise.all([
                fetch('/health'),
                fetch('/api/v1/catalog'),
                window.SitusExecution.refreshCapabilities()
            ]);
            const h = await hRes.json();
            const c = await cRes.json();

            const dbUp = h.dependencies && h.dependencies.postgis === 'up';
            connEl.textContent = dbUp ? 'PostGIS local conectado' : 'PostGIS indisponível';
            connEl.className = 'badge ' + (dbUp ? '' : 'error');

            const wasmAvail = window.SitusExecution.isWasmAvailable();
            wasmEl.textContent = wasmAvail ? 'Wasm C++26 pronto' : 'Wasm indisponível (modo nativo)';
            wasmEl.className = 'badge ' + (wasmAvail ? 'info' : 'warn');

            renderStatsAndCatalog(h, c, caps);
            renderCapabilitiesTable(caps);
        } catch (err) {
            connEl.textContent = 'Erro de conexão local';
            connEl.className = 'badge error';
        }
    }

    function renderStatsAndCatalog(health, catalog, caps) {
        const sources = (catalog && catalog.sources) || [];
        const approvedActive = sources.filter(s => s.approval_status === 'APPROVED' && s.is_active);
        const totalFeatures = approvedActive.reduce((acc, s) => acc + (Number(s.feature_count) || 0), 0);

        document.getElementById('stat-sources').textContent = String(approvedActive.length);
        document.getElementById('stat-sources-note').textContent = approvedActive.length > 0
            ? 'Fontes Categoria A ativas'
            : 'Catálogo inicial vazio — aguarda ingestão';
        document.getElementById('stat-units').textContent = totalFeatures > 0 ? totalFeatures.toLocaleString('pt-BR') : '0';
        document.getElementById('stat-exec').textContent = window.SitusExecution.prefs.mode;
        document.getElementById('stat-exec-note').textContent = window.SitusExecution.isWasmAvailable()
            ? 'Servidor nativo + Worker Wasm disponíveis'
            : 'Somente servidor nativo (Wasm ausente)';

        // Tabela de camadas e fontes
        const layersBody = document.getElementById('layers-tbody');
        const sourcesList = document.getElementById('sources-list');
        if (sources.length === 0) {
            layersBody.innerHTML = '<tr><td colspan="5" class="muted">Nenhuma camada ingerida no banco local ainda.</td></tr>';
            sourcesList.innerHTML = '<div class="notice-box">Catálogo vazio. Execute a aquisição administrativa e ingestão para carregar a malha oficial do RS.</div>';
        } else {
            layersBody.innerHTML = sources.map(s => `
                <tr>
                    <td><strong>${escapeHtml(s.title)}</strong><br><small class="muted">${escapeHtml(s.source_id)}</small></td>
                    <td>${escapeHtml(s.spatial_level)}</td>
                    <td>${escapeHtml(s.native_crs)}</td>
                    <td>${escapeHtml(s.dataset_version || '—')}</td>
                    <td><span class="badge ${s.is_active ? '' : 'warn'}">${s.is_active ? 'Ativa' : escapeHtml(s.approval_status)}</span></td>
                </tr>
            `).join('');

            sourcesList.innerHTML = sources.map(s => `
                <article class="obs-card">
                    <h4>${escapeHtml(s.title)}</h4>
                    <p class="muted">Provedor: ${escapeHtml(s.provider)} · Categoria: ${escapeHtml(s.category)} · Licença: ${escapeHtml(s.license)}</p>
                    <p><strong>Resolução nativa:</strong> ${escapeHtml(s.native_resolution)} · <strong>CRS:</strong> ${escapeHtml(s.native_crs)}</p>
                    ${s.artifact_sha256 ? `<p><strong>SHA-256:</strong> <code>${escapeHtml(s.artifact_sha256)}</code></p>` : ''}
                    <p class="muted"><strong>Limitações:</strong> ${escapeHtml(s.limitations)}</p>
                </article>
            `).join('');
        }

        // Estado do sistema
        document.getElementById('system-status-pre').textContent = JSON.stringify({ health, wasm_artifact: caps.wasm_artifact }, null, 2);
    }

    function renderCapabilitiesTable(caps) {
        const tbody = document.getElementById('caps-tbody');
        if (!tbody) return;
        const ops = (caps && caps.operations) || [];
        const wasmOk = window.SitusExecution.isWasmAvailable();
        tbody.innerHTML = ops.map(op => {
            const targets = (op.supported_targets || []).map(t => {
                if ((t === 'BROWSER_WASM' || t === 'HYBRID') && !wasmOk) return `${t} (indisponível)`;
                return t;
            }).join(', ');
            return `
                <tr>
                    <td><strong>${escapeHtml(op.title)}</strong><br><code>${escapeHtml(op.operation_id)}</code></td>
                    <td>${escapeHtml(op.algorithm_version)}</td>
                    <td>${escapeHtml(op.policy_constraint)}</td>
                    <td>${escapeHtml(targets)}</td>
                </tr>
            `;
        }).join('');
    }

    function buildReferenceFromForm() {
        const kind = document.getElementById('ref-kind').value;
        const rawVal = document.getElementById('ref-value').value.trim();
        if (kind === 'DISTRICT') {
            const ruralOnly = !!document.getElementById('ref-rural-only')?.checked;
            const gridSize = Number(document.getElementById('ref-district-grid')?.value || 0);
            const commaIdx = rawVal.lastIndexOf(',');
            const ref = { kind: 'DISTRICT', value: rawVal, rural_only: ruralOnly, crs: 'EPSG:4326' };
            if (commaIdx > 0) {
                ref.value = rawVal.slice(0, commaIdx).trim();
                ref.municipality_code = rawVal.slice(commaIdx + 1).trim();
            }
            if (gridSize > 0) ref.target_grid_size_m = gridSize;
            return ref;
        }
        if (kind === 'GRID_CELL') {
            const cellSize = Number(document.getElementById('ref-grid-size')?.value || 1000);
            if (rawVal.startsWith('GRID5880_')) {
                return { kind: 'GRID_CELL', value: rawVal, crs: 'EPSG:4326' };
            }
            const parts = rawVal.split(',').map(x => Number(x.trim()));
            if (parts.length !== 2 || parts.some(Number.isNaN)) {
                throw new Error('Informe coordenada (lon, lat) ou código GRID5880_<S>_<ix>_<iy>');
            }
            return { kind: 'GRID_CELL', value: parts, cell_size_m: cellSize, crs: 'EPSG:4326' };
        }
        if (kind === 'MUNICIPALITY') {
            return { kind: 'MUNICIPALITY', value: rawVal, crs: 'EPSG:4326' };
        }
        if (kind === 'STREET') {
            const commaIdx = rawVal.lastIndexOf(',');
            if (commaIdx > 0) {
                return {
                    kind: 'STREET',
                    value: rawVal.slice(0, commaIdx).trim(),
                    municipality_code: rawVal.slice(commaIdx + 1).trim(),
                    crs: 'EPSG:4326'
                };
            }
            return { kind: 'STREET', value: rawVal, crs: 'EPSG:4326' };
        }
        if (kind === 'CEP') {
            return { kind: 'CEP', value: rawVal, crs: 'EPSG:4326' };
        }
        if (kind === 'POINT') {
            const parts = rawVal.split(',').map(x => Number(x.trim()));
            if (parts.length !== 2 || parts.some(Number.isNaN)) {
                throw new Error('Informe ponto como: longitude, latitude (ex: -51.23, -30.03)');
            }
            const radius = Number(document.getElementById('ref-radius').value || 0);
            const ref = { kind: 'POINT', value: parts, crs: 'EPSG:4326' };
            if (radius > 0) ref.radius_m = radius;
            return ref;
        }
        throw new Error('Selecione uma referência ou desenhe uma área no mapa.');
    }

    async function runInvestigation(reference, { pushHistory = true } = {}) {
        if (activeAbort) activeAbort.abort();
        activeAbort = new AbortController();
        const signal = activeAbort.signal;
        if (pushHistory && lastReference && JSON.stringify(lastReference) !== JSON.stringify(reference)) {
            historyStack.push(lastReference);
        }
        lastReference = reference;
        updateBackButton();

        const notice = document.getElementById('explore-notice');
        const cancelBtn = document.getElementById('cancel-query-btn');
        const inspector = document.getElementById('inspector-content');

        notice.className = 'notice-box';
        notice.textContent = 'Executando investigação territorial local…';
        cancelBtn.hidden = false;

        try {
            // 1. Resolver referência
            const resRef = await fetch('/api/v1/references/resolve', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(reference),
                signal
            });
            const refData = await resRef.json();
            if (!resRef.ok) {
                throw new Error((refData.error && refData.error.message) || 'Referência rejeitada');
            }

            if (refData.status === 'UNAVAILABLE' || refData.status === 'OUTSIDE_COVERAGE') {
                window.SitusMap.setData({ features: [] }, null);
                inspector.innerHTML = `
                    <span class="badge warn">${escapeHtml(refData.status)}</span>
                    <h3>Referência não resolvida para análise local</h3>
                    <p>${(refData.reasons || []).map(escapeHtml).join(' ')}</p>
                    <p class="muted">O sistema explicita quando não dispõe de fonte aprovada ou cobertura territorial.</p>
                    <div style="display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;">
                        <button type="button" class="button primary" id="fallback-draw-area">▢ Selecionar área no mapa</button>
                        <button type="button" class="button outline" id="fallback-use-mun">Buscar por Município (IBGE)</button>
                    </div>
                `;
                document.getElementById('fallback-draw-area')?.addEventListener('click', () => {
                    document.getElementById('map-draw-btn')?.click();
                    notice.textContent = 'Arraste sobre o mapa para desenhar uma área retangular de investigação.';
                });
                document.getElementById('fallback-use-mun')?.addEventListener('click', () => {
                    const kindEl = document.getElementById('ref-kind');
                    const valEl = document.getElementById('ref-value');
                    kindEl.value = 'MUNICIPALITY';
                    kindEl.dispatchEvent(new Event('change'));
                    valEl.value = '';
                    valEl.focus();
                });
                notice.textContent = 'Consulta concluída (sem cobertura ou fonte indisponível).';
                return;
            }

            if (refData.status === 'AMBIGUOUS' && refData.kind === 'DISTRICT') {
                window.SitusMap.setData({ features: [] }, null);
                const matchesHtml = (refData.matches || []).map(m => `
                    <article class="obs-card nodata">
                        <h4>${escapeHtml(m.district_name)} · ${escapeHtml(m.municipality_name)} (${escapeHtml(m.municipality_code)})</h4>
                        <p class="muted">Distrito IBGE ${escapeHtml(m.district_code)} · ${escapeHtml(String(m.sector_count))} setores · ${fmtAreaHa(m.area_m2)}</p>
                        <button type="button" class="button outline pick-district-btn" data-dist="${escapeHtml(m.district_code)}">Investigar distrito</button>
                    </article>
                `).join('');
                inspector.innerHTML = `
                    <span class="badge warn">DISTRITO AMBÍGUO</span>
                    <h3>Múltiplos municípios possuem distrito com este nome</h3>
                    ${matchesHtml}
                `;
                inspector.querySelectorAll('.pick-district-btn').forEach(btn => {
                    btn.addEventListener('click', () => {
                        const nextRef = { kind: 'DISTRICT', value: btn.dataset.dist, rural_only: !!reference.rural_only, crs: 'EPSG:4326' };
                        if (reference.target_grid_size_m) nextRef.target_grid_size_m = reference.target_grid_size_m;
                        runInvestigation(nextRef);
                    });
                });
                notice.textContent = 'Distrito ambíguo entre municípios; selecione o desejado.';
                return;
            }

            if (refData.kind === 'DISTRICT' && refData.status === 'RESOLVED' && reference.target_grid_size_m > 0 && Array.isArray(refData.anchor_point)) {
                return runInvestigation({
                    kind: 'GRID_CELL',
                    value: refData.anchor_point,
                    cell_size_m: reference.target_grid_size_m,
                    context_label: `${refData.district_name} (${refData.municipality_name})`,
                    crs: 'EPSG:4326'
                }, { pushHistory: false });
            }

            if (refData.status === 'AMBIGUOUS' && refData.kind === 'STREET') {
                window.SitusMap.setData({ features: [] }, null);
                const matchesHtml = (refData.matches || []).map(m => `
                    <article class="obs-card nodata">
                        <h4>${escapeHtml(m.reference_name)} · ${escapeHtml(m.municipality_name)} (${escapeHtml(m.municipality_code)})</h4>
                        <p class="muted">${escapeHtml(String(m.face_count))} faces · Setores: ${escapeHtml(m.sectors || '—')} · Fonte: ${escapeHtml(m.source_id)}</p>
                        <button type="button" class="button outline pick-street-btn" data-street="${escapeHtml(m.reference_name)}" data-mun="${escapeHtml(m.municipality_code)}">Selecionar logradouro</button>
                    </article>
                `).join('');
                inspector.innerHTML = `
                    <span class="badge warn">AMBIGUOUS</span>
                    <h3>Logradouro com múltiplas correspondências</h3>
                    <p>${(refData.reasons || []).map(escapeHtml).join(' ')}</p>
                    ${matchesHtml}
                `;
                inspector.querySelectorAll('.pick-street-btn').forEach(btn => {
                    btn.addEventListener('click', () => {
                        runInvestigation({ kind: 'STREET', value: btn.dataset.street, municipality_code: btn.dataset.mun, crs: 'EPSG:4326' });
                    });
                });
                notice.textContent = 'Múltiplos logradouros encontrados; selecione o desejado.';
                return;
            }

            if (refData.kind === 'STREET' && refData.status === 'RESOLVED' && !reference.investigate_surroundings) {
                const munCode = refData.municipality_code;
                const mapRes = await fetch('/api/v1/map/features', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ kind: 'MUNICIPALITY', value: munCode, crs: 'EPSG:4326' }),
                    signal
                });
                const mapData = await mapRes.json();
                window.SitusMap.setData(mapData, refData.support_geometry || refData.analysis_geometry);
                const m = (refData.matches && refData.matches[0]) || {};
                inspector.innerHTML = `
                    <span class="badge">R4 · RESOLVED</span>
                    <h3>${escapeHtml(refData.reference_name || reference.value)} → ${escapeHtml(refData.municipality_name)}</h3>
                    <p>${(refData.reasons || []).map(escapeHtml).join(' ')}</p>
                    <div style="display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:12px 0;padding:10px;background:#eef6fb;border-radius:8px;">
                        <label style="font-size:12px;font-weight:700;">Entorno (m):
                            <select id="street-radius-select" style="padding:5px 8px;border-radius:6px;border:1px solid #cbdce8;">
                                <option value="250">250 m</option>
                                <option value="500" selected>500 m</option>
                                <option value="1000">1.000 m</option>
                                <option value="2000">2.000 m</option>
                            </select>
                        </label>
                        <button type="button" class="button primary" id="street-investigate-surroundings">Caracterizar entorno do logradouro</button>
                    </div>
                    <article class="obs-card">
                        <h4>R4 · ${escapeHtml(m.reference_name || '')}</h4>
                        <p class="muted">Faces IBGE: ${escapeHtml(String(m.face_count || '—'))} · Setores: ${escapeHtml(m.sectors || '—')}</p>
                        <p class="muted">Fonte: ${escapeHtml(m.source_id || 'ibge_faces_rs_2022')} · Licença: ${escapeHtml(m.license || 'Dados Abertos IBGE')}</p>
                        <p class="muted">${escapeHtml(m.limitations || '')}</p>
                    </article>
                `;
                document.getElementById('street-investigate-surroundings')?.addEventListener('click', () => {
                    const r = Number(document.getElementById('street-radius-select').value || 500);
                    runInvestigation({
                        kind: 'STREET',
                        value: refData.reference_name || reference.value,
                        municipality_code: munCode,
                        radius_m: r,
                        crs: 'EPSG:4326',
                        investigate_surroundings: true
                    });
                });
                notice.textContent = `Logradouro resolvido em R4 (${refData.municipality_name}).`;
                return;
            }

            if (refData.status === 'AMBIGUOUS' && refData.kind === 'CEP') {
                window.SitusMap.setData({ features: [] }, null);
                const matchesHtml = (refData.matches || []).map(m => `
                    <article class="obs-card nodata">
                        <h4>${escapeHtml(m.resolution_level)} · ${escapeHtml(m.municipality_name)} (${escapeHtml(m.municipality_code)})</h4>
                        <p>${escapeHtml(m.reference_name || m.reference_kind)} · Fonte: ${escapeHtml(m.source_id)} (${escapeHtml(m.license)})</p>
                        <button type="button" class="button outline pick-mun-btn" data-mun="${escapeHtml(m.municipality_code)}">Explorar ${escapeHtml(m.municipality_name)}</button>
                    </article>
                `).join('');
                inspector.innerHTML = `
                    <span class="badge warn">AMBIGUOUS</span>
                    <h3>CEP ${escapeHtml(refData.cep_code)} com múltiplas correspondências</h3>
                    <p>${(refData.reasons || []).map(escapeHtml).join(' ')}</p>
                    ${matchesHtml}
                `;
                inspector.querySelectorAll('.pick-mun-btn').forEach(btn => {
                    btn.addEventListener('click', () => {
                        runInvestigation({ kind: 'MUNICIPALITY', value: btn.dataset.mun, crs: 'EPSG:4326' });
                    });
                });
                notice.textContent = 'CEP ambíguo entre municípios; selecione o contexto desejado.';
                return;
            }

            if (refData.kind === 'CEP' && refData.status === 'RESOLVED' && !reference.investigate_surroundings) {
                const munCode = refData.municipality_code;
                const mapRes = await fetch('/api/v1/map/features', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ kind: 'MUNICIPALITY', value: munCode, crs: 'EPSG:4326' }),
                    signal
                });
                const mapData = await mapRes.json();
                window.SitusMap.setData(mapData, refData.support_geometry || refData.analysis_geometry);

                const matchesHtml = (refData.matches || []).map(m => `
                    <article class="obs-card">
                        <h4>${escapeHtml(m.resolution_level)} · ${escapeHtml(m.reference_name || m.reference_kind)}</h4>
                        <p class="muted">Fonte: ${escapeHtml(m.source_id)} · Licença: ${escapeHtml(m.license)}</p>
                        <p class="muted">${escapeHtml(m.limitations)}</p>
                    </article>
                `).join('');

                const isSubmun = refData.resolution_level && refData.resolution_level !== 'R1';
                const surroundHtml = isSubmun ? `
                    <div style="display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:12px 0;padding:10px;background:#eef6fb;border-radius:8px;">
                        <label style="font-size:12px;font-weight:700;">Entorno (m):
                            <select id="cep-radius-select" style="padding:5px 8px;border-radius:6px;border:1px solid #cbdce8;">
                                <option value="250">250 m</option>
                                <option value="500" selected>500 m</option>
                                <option value="1000">1.000 m</option>
                                <option value="2000">2.000 m</option>
                            </select>
                        </label>
                        <button type="button" class="button primary" id="cep-investigate-surroundings">Caracterizar entorno do CEP</button>
                    </div>
                ` : '';

                inspector.innerHTML = `
                    <span class="badge">${escapeHtml(refData.resolution_level)} · RESOLVED</span>
                    <h3>CEP ${escapeHtml(refData.cep_code)} → ${escapeHtml(refData.municipality_name)}</h3>
                    <p>${(refData.reasons || []).map(escapeHtml).join(' ')}</p>
                    ${surroundHtml}
                    <div style="display:flex;flex-wrap:wrap;gap:8px;margin:12px 0;">
                        <button type="button" class="button ${isSubmun ? 'outline' : 'primary'}" id="cep-investigate-mun">Explorar município (${escapeHtml(refData.municipality_name)})</button>
                        <button type="button" class="button outline" id="cep-draw-area">▢ Selecionar área menor</button>
                    </div>
                    ${matchesHtml}
                `;
                document.getElementById('cep-investigate-surroundings')?.addEventListener('click', () => {
                    const r = Number(document.getElementById('cep-radius-select').value || 500);
                    runInvestigation({ kind: 'CEP', value: refData.cep_code, radius_m: r, crs: 'EPSG:4326', investigate_surroundings: true });
                });
                document.getElementById('cep-investigate-mun')?.addEventListener('click', () => {
                    runInvestigation({ kind: 'MUNICIPALITY', value: munCode, crs: 'EPSG:4326' });
                });
                document.getElementById('cep-draw-area')?.addEventListener('click', () => {
                    document.getElementById('map-draw-btn')?.click();
                });
                notice.textContent = `CEP resolvido em ${refData.resolution_level} (${refData.municipality_name}).`;
                return;
            }

            // 2. Caracterização territorial (sem síntese duplicada no servidor) + feições do mapa
            const [charRes, mapRes] = await Promise.all([
                fetch('/api/v1/territories/characterize', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ...reference, skip_synthesis: true }),
                    signal
                }),
                fetch('/api/v1/map/features', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(reference),
                    signal
                })
            ]);

            const charData = await charRes.json();
            const mapData = await mapRes.json();
            if (!charRes.ok) throw new Error((charData.error && charData.error.message) || 'Erro na caracterização');

            const refOverlay = reference.kind === 'MUNICIPALITY'
                ? null
                : (refData.analysis_geometry || refData.support_geometry);
            window.SitusMap.setData(mapData, refOverlay);

            // 3. Executar síntese adaptativa (Nativo / Worker Wasm / Híbrido)
            let execEnvelope = null;
            if (charData.intersection && charData.intersection.unit_count > 0) {
                execEnvelope = await window.SitusExecution.runAdaptiveSynthesis(reference, signal);
            }

            renderInspector(refData, charData, execEnvelope);
            notice.textContent = `Consulta concluída: ${charData.intersection ? charData.intersection.unit_count : 0} unidades territoriais intersectadas.`;
        } catch (err) {
            if (err.name === 'AbortError') {
                notice.className = 'notice-box warn';
                notice.textContent = 'Consulta cancelada pelo usuário.';
            } else {
                notice.className = 'notice-box error';
                notice.textContent = err.message || 'Erro na investigação';
            }
        } finally {
            cancelBtn.hidden = true;
            activeAbort = null;
        }
    }

    function renderInspector(refData, charData, execEnv) {
        const el = document.getElementById('inspector-content');
        const inter = charData.intersection || {};
        const obsList = charData.observations || [];

        let execBadgeHtml = '';
        let execDetailsHtml = '';
        if (execEnv) {
            const target = execEnv.actual_target || 'UNAVAILABLE';
            const pillClass = target === 'BROWSER_WASM' ? 'wasm' : (target === 'HYBRID' ? 'hybrid' : '');
            execBadgeHtml = `<span class="exec-pill ${pillClass}">Ambiente efetivo: ${escapeHtml(target)}</span>`;

            if (window.SitusExecution.prefs.show_execution_details) {
                const t = execEnv.timings_ms || {};
                execDetailsHtml = `
                    <details>
                        <summary>Detalhes de execução e proveniência computacional</summary>
                        <dl>
                            <dt>Modo solicitado</dt><dd>${escapeHtml(execEnv.requested_mode)}</dd>
                            <dt>Destino selecionado</dt><dd>${escapeHtml(execEnv.selected_target || '—')}</dd>
                            <dt>Destino efetivo</dt><dd>${escapeHtml(execEnv.actual_target || '—')}</dd>
                            <dt>Razão da decisão</dt><dd>${escapeHtml(execEnv.decision_reason)}</dd>
                            ${execEnv.fallback_reason ? `<dt>Fallback</dt><dd>${escapeHtml(execEnv.fallback_reason)}</dd>` : ''}
                            <dt>Operação / Versão</dt><dd>${escapeHtml(execEnv.operation_id)} · v${escapeHtml(execEnv.algorithm_version)}</dd>
                            <dt>Tempos (ms)</dt><dd>servidor: ${t.server_ms != null ? t.server_ms.toFixed(2) : '—'} | worker: ${t.worker_ms != null ? t.worker_ms.toFixed(2) : '—'} | total: ${t.total_ms != null ? t.total_ms.toFixed(2) : '—'}</dd>
                        </dl>
                    </details>
                `;
            }
        }

        const synthOutput = execEnv && execEnv.output ? execEnv.output : null;
        const groupsHtml = synthOutput && synthOutput.groups
            ? `<table><thead><tr><th>Situação / Grupo</th><th>Setores</th><th>Área</th><th>Proporção</th></tr></thead><tbody>` +
              synthOutput.groups.map(g => `
                <tr>
                    <td>${escapeHtml(g.group_label)}</td>
                    <td>${g.unit_count}</td>
                    <td>${fmtAreaHa(g.intersected_area_m2)}</td>
                    <td>${(g.proportion_of_reference * 100).toFixed(1)}%</td>
                </tr>
              `).join('') + `</tbody></table>`
            : '';

        const obsHtml = obsList.map(o => {
            let bodyHtml = '';
            const v = o.value || {};
            if (o.status === 'AVAILABLE') {
                if (o.indicator_id === 'biome_composition' && Array.isArray(v.biomes)) {
                    bodyHtml = v.biomes.map(b => `<p><strong>${escapeHtml(b.biome_name)}</strong>: ${b.area_ha.toFixed(1)} ha (${(b.fraction * 100).toFixed(1)}%)</p>`).join('');
                } else if (o.indicator_id === 'land_cover_composition' && Array.isArray(v.classes)) {
                    bodyHtml = `<table><thead><tr><th>Classe (MapBiomas)</th><th>Área</th><th>% pixels</th></tr></thead><tbody>` +
                        v.classes.map(c => `<tr><td>${escapeHtml(c.class_label)}</td><td>${fmtAreaHa(c.area_m2)}</td><td>${(c.pixel_fraction * 100).toFixed(1)}%</td></tr>`).join('') +
                        `</tbody></table>`;
                } else if (o.indicator_id === 'elevation_m') {
                    bodyHtml = `<p>Altitude média: <strong>${v.mean.toFixed(1)} m</strong> (mín ${v.min.toFixed(1)} m · máx ${v.max.toFixed(1)} m · DP ${v.stddev.toFixed(1)} m)</p>`;
                } else if (o.indicator_id === 'slope_pct') {
                    bodyHtml = `<p>Declividade média: <strong>${v.mean.toFixed(1)}%</strong> (mín ${v.min.toFixed(1)}% · máx ${v.max.toFixed(1)}%)</p>`;
                } else if (o.indicator_id === 'hydrography_summary') {
                    bodyHtml = `<p>Cursos d'água: <strong>${v.watercourse_segments}</strong> trechos (${v.watercourse_length_km.toFixed(2)} km) · Massas d'água: <strong>${v.water_bodies}</strong> (${v.water_body_area_ha.toFixed(1)} ha)${v.named_features ? `<br><small class="muted">Nomes: ${escapeHtml(v.named_features)}</small>` : ''}</p>`;
                } else if (o.indicator_id === 'soil_composition' && Array.isArray(v.soils)) {
                    bodyHtml = `<table><thead><tr><th>Solo (BDiA)</th><th>Área</th><th>%</th></tr></thead><tbody>` +
                        v.soils.slice(0, 5).map(s => `<tr><td>${escapeHtml(s.soil_symbol)} · ${escapeHtml(s.soil_class)}</td><td>${s.area_ha.toFixed(1)} ha</td><td>${(s.fraction * 100).toFixed(1)}%</td></tr>`).join('') +
                        `</tbody></table>`;
                }
            } else if (o.status === 'NOT_APPLICABLE') {
                bodyHtml = `<p class="muted">${escapeHtml(o.limitations)}</p>`;
            }
            const labelMap = {
                biome_composition: 'Bioma',
                land_cover_composition: 'Uso e cobertura da terra',
                elevation_m: 'Relevo · Altitude',
                slope_pct: 'Relevo · Declividade',
                hydrography_summary: 'Hidrografia',
                soil_composition: 'Solos'
            };
            return `
                <article class="obs-card ${o.status === 'NO_DATA' || o.status === 'NOT_APPLICABLE' ? 'nodata' : ''}">
                    <h4>${escapeHtml(labelMap[o.indicator_id] || o.indicator_id)} <span class="badge ${o.status === 'AVAILABLE' ? '' : 'warn'}">${escapeHtml(o.status)}</span></h4>
                    ${bodyHtml}
                    <details style="margin-top:6px;">
                        <summary style="font-size:11px;color:var(--muted);cursor:pointer;">Proveniência, resolução e limitações</summary>
                        <p class="muted" style="margin-top:4px;font-size:11.5px;">Fonte: ${escapeHtml(o.source_id)} (v${escapeHtml(o.dataset_version)}) · Ref.: ${escapeHtml(o.reference_date || '—')} · Resolução: ${escapeHtml(o.native_resolution)} · Cobertura: ${o.coverage_fraction != null ? (o.coverage_fraction * 100).toFixed(1) + '%' : '—'}<br>Método: ${escapeHtml(o.method)}<br>${escapeHtml(o.limitations)}</p>
                    </details>
                </article>
            `;
        }).join('');

        const interUnits = inter.units || [];
        const munNames = [...new Set(interUnits.map(u => u.municipality_name).filter(Boolean))];
        const munCodes = [...new Set(interUnits.map(u => u.municipality_code).filter(Boolean))];
        const titleText = munNames.length === 1
            ? `${munNames[0]} (IBGE ${munCodes[0]})`
            : (munNames.length > 1 ? `${munNames.join(', ')}` : 'Investigação territorial');

        el.innerHTML = `
            <div class="section-heading" style="margin-bottom:8px;">
                <span class="badge">${escapeHtml(refData.status)}</span>
                ${execBadgeHtml}
            </div>
            <h3>${escapeHtml(titleText)}</h3>
            <dl>
                <dt>Nível espacial</dt><dd>${escapeHtml(refData.spatial_level)}</dd>
                <dt>Área de referência</dt><dd>${fmtAreaHa(inter.reference_area_m2)}</dd>
                <dt>Área intersectada</dt><dd>${fmtAreaHa(inter.total_intersected_area_m2)}</dd>
                <dt>Cobertura</dt><dd>${inter.coverage_fraction != null ? (inter.coverage_fraction * 100).toFixed(1) + '%' : '—'}</dd>
                <dt>Setores</dt><dd>${inter.unit_count || 0}</dd>
            </dl>
            ${groupsHtml}
            ${execDetailsHtml}
            <h4 style="margin:14px 0 6px;">Observações e fontes</h4>
            ${obsHtml}
        `;
    }

    function initSettings() {
        const prefs = window.SitusExecution.prefs;
        const modeSel = document.getElementById('cfg-mode');
        const profSel = document.getElementById('cfg-profile');
        const fbChk = document.getElementById('cfg-fallback');
        const detChk = document.getElementById('cfg-details');

        modeSel.value = prefs.mode;
        profSel.value = prefs.profile;
        fbChk.checked = prefs.allow_fallback;
        detChk.checked = prefs.show_execution_details;

        const sync = () => {
            prefs.mode = modeSel.value;
            prefs.profile = profSel.value;
            prefs.allow_fallback = fbChk.checked;
            prefs.show_execution_details = detChk.checked;
            window.SitusExecution.savePrefs();
            document.getElementById('stat-exec').textContent = prefs.mode;
        };

        [modeSel, profSel, fbChk, detChk].forEach(el => el.addEventListener('change', sync));

        document.querySelectorAll('.settings-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                const target = tab.dataset.tab;
                document.querySelectorAll('.settings-tab').forEach(t => {
                    t.classList.toggle('active', t === tab);
                    t.setAttribute('aria-selected', t === tab ? 'true' : 'false');
                });
                document.querySelectorAll('[data-tab-panel]').forEach(p => {
                    p.hidden = p.dataset.tabPanel !== target;
                });
            });
        });

        document.getElementById('run-diag-btn')?.addEventListener('click', async () => {
            const out = document.getElementById('diag-output');
            out.textContent = 'Executando comparação controlada (fixture pública)...';
            const slices = Array.from({ length: 50 }, (_, i) => ({
                unit_code: `43149020500${String(i + 1).padStart(4, '0')}`,
                group_key: i % 2 === 0 ? '1' : '8',
                group_label: i % 2 === 0 ? 'Área urbana' : 'Zona rural',
                intersected_area_m2: 1000.0 + i * 10,
                unit_total_area_m2: 5000.0
            }));
            const totalArea = slices.reduce((a, b) => a + b.intersected_area_m2, 0);
            const res = await window.SitusExecution.runDirectBenchmark(slices, totalArea);
            out.textContent = JSON.stringify(res, null, 2);
        });
    }

    function showSelectedFeatureMetadata(kind, feat) {
        if (!feat) return;
        const p = feat.properties || {};
        const boxId = 'selected-feature-card';
        let box = document.getElementById(boxId);
        const inspector = document.getElementById('inspector-content');
        if (!box && inspector) {
            box = document.createElement('div');
            box.id = boxId;
            box.className = 'obs-card';
            inspector.prepend(box);
        }
        if (!box) return;

        if (kind === 'sector') {
            box.innerHTML = `
                <div class="section-heading" style="margin-bottom:4px;">
                    <strong>Setor selecionado: ${escapeHtml(p.unit_code)}</strong>
                    <button type="button" class="button outline" id="clear-feat-sel" style="min-height:28px;padding:4px 9px;">✕</button>
                </div>
                <p class="muted" style="margin:0 0 8px;">Município: ${escapeHtml(p.municipality_name)} (${escapeHtml(p.municipality_code)}) · Situação: ${escapeHtml(p.situation_label)} · Área: ${fmtAreaHa(p.area_m2)}</p>
                <button type="button" class="button primary" id="investigate-selected-sector" style="min-height:32px;padding:6px 12px;">Investigar este setor (30 m)</button>
            `;
            document.getElementById('clear-feat-sel')?.addEventListener('click', () => {
                box.remove();
                window.SitusMap.clearSelection();
            });
            document.getElementById('investigate-selected-sector')?.addEventListener('click', () => {
                if (feat.geometry) {
                    runInvestigation({ kind: 'POLYGON', value: feat.geometry, crs: 'EPSG:4326' });
                }
            });
        } else {
            box.innerHTML = `
                <div class="section-heading" style="margin-bottom:4px;">
                    <strong>${p.feature_kind === 'WATER_BODY' ? 'Massa d\'água' : 'Curso d\'água'} (BC250)</strong>
                    <button type="button" class="button outline" id="clear-feat-sel" style="min-height:28px;padding:4px 9px;">✕</button>
                </div>
                <p class="muted" style="margin:0;">Nome: ${escapeHtml(p.feature_name || 'Sem nome cadastrado no BC250')}</p>
            `;
            document.getElementById('clear-feat-sel')?.addEventListener('click', () => {
                box.remove();
                window.SitusMap.clearSelection();
            });
        }
    }

    document.addEventListener('DOMContentLoaded', () => {
        window.SitusMap.init(
            (poly) => {
                runInvestigation({ kind: 'POLYGON', value: poly, crs: 'EPSG:4326' });
            },
            (kind, feat) => {
                showSelectedFeatureMetadata(kind, feat);
            }
        );

        document.getElementById('ref-kind')?.addEventListener('change', (ev) => {
            const k = ev.target.value;
            const inp = document.getElementById('ref-value');
            const radWrap = document.getElementById('radius-wrap');
            const gridWrap = document.getElementById('grid-size-wrap');
            const distWrap = document.getElementById('district-opts-wrap');
            radWrap.hidden = k !== 'POINT';
            if (gridWrap) gridWrap.hidden = k !== 'GRID_CELL';
            if (distWrap) distWrap.hidden = k !== 'DISTRICT';
            if (k === 'MUNICIPALITY') {
                inp.setAttribute('list', 'municipality-datalist');
                inp.placeholder = 'Nome do município ou código IBGE (ex: Bagé, Porto Alegre, 4314902)';
            } else if (k === 'DISTRICT') {
                inp.setAttribute('list', 'municipality-datalist');
                inp.placeholder = 'Distrito ou código IBGE (ex: Joca Tavares, Palmas, Piraí, 430160217)';
            } else {
                inp.removeAttribute('list');
                if (k === 'GRID_CELL') inp.placeholder = 'Coordenada (lon, lat) ou código GRID5880_1000_... (ex: -53.82089, -30.91943)';
                else if (k === 'POINT') inp.placeholder = 'longitude, latitude (ex: -51.23, -30.03)';
                else if (k === 'STREET') inp.placeholder = 'Logradouro [, Município] (ex: Rua Rádio Cultura, Bagé)';
                else if (k === 'CEP') inp.placeholder = 'CEP 8 dígitos (ex: 96540-000 Agudo, 90240-552 Porto Alegre)';
            }
        });

        let munTimer = null;
        document.getElementById('ref-value')?.addEventListener('input', (ev) => {
            const k = document.getElementById('ref-kind').value;
            if (k !== 'MUNICIPALITY' && k !== 'DISTRICT') return;
            const q = ev.target.value.trim();
            clearTimeout(munTimer);
            munTimer = setTimeout(async () => {
                try {
                    const url = k === 'DISTRICT'
                        ? '/api/v1/districts?q=' + encodeURIComponent(q)
                        : '/api/v1/municipalities?q=' + encodeURIComponent(q);
                    const res = await fetch(url);
                    if (!res.ok) return;
                    const data = await res.json();
                    const dl = document.getElementById('municipality-datalist');
                    if (k === 'DISTRICT') {
                        dl.innerHTML = (data.districts || []).map(d =>
                            `<option value="${escapeHtml(d.district_name)}, ${escapeHtml(d.municipality_code)}">${escapeHtml(d.district_name)} — ${escapeHtml(d.municipality_name)} (${d.rural_sectors} rurais)</option>`
                        ).join('');
                    } else {
                        dl.innerHTML = (data.municipalities || []).map(m =>
                            `<option value="${escapeHtml(m.municipality_name)}">${escapeHtml(m.municipality_name)} (${escapeHtml(m.municipality_code)})</option>`
                        ).join('');
                    }
                } catch (_) {}
            }, 180);
        });

        document.getElementById('explore-form')?.addEventListener('submit', (ev) => {
            ev.preventDefault();
            try {
                const ref = buildReferenceFromForm();
                runInvestigation(ref);
            } catch (err) {
                const notice = document.getElementById('explore-notice');
                notice.className = 'notice-box error';
                notice.textContent = err.message;
            }
        });

        document.getElementById('cancel-query-btn')?.addEventListener('click', () => {
            if (activeAbort) activeAbort.abort();
        });

        document.getElementById('map-back-btn')?.addEventListener('click', () => {
            if (historyStack.length === 0) return;
            const prev = historyStack.pop();
            updateBackButton();
            runInvestigation(prev, { pushHistory: false });
        });

        initSettings();
        refreshHealthAndCatalog();
    });
})();
