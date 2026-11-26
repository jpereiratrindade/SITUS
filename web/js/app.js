// SITUS — Aplicação principal da interface web única
'use strict';

(() => {
    let activeAbort = null;
    let lastReference = null;

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
        if (kind === 'MUNICIPALITY') {
            return { kind: 'MUNICIPALITY', value: rawVal, crs: 'EPSG:4326' };
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

    async function runInvestigation(reference) {
        if (activeAbort) activeAbort.abort();
        activeAbort = new AbortController();
        const signal = activeAbort.signal;
        lastReference = reference;

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
                `;
                notice.textContent = 'Consulta concluída (sem cobertura ou fonte indisponível).';
                return;
            }

            // 2. Caracterização territorial nativa + feições do mapa + síntese adaptativa (se aplicável)
            const [charRes, mapRes] = await Promise.all([
                fetch('/api/v1/territories/characterize', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(reference),
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

            window.SitusMap.setData(mapData, refData.analysis_geometry || refData.support_geometry);

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

        const obsHtml = obsList.map(o => `
            <article class="obs-card ${o.status === 'NO_DATA' ? 'nodata' : ''}">
                <h4>${escapeHtml(o.indicator_id)} <span class="badge ${o.status === 'AVAILABLE' ? '' : 'warn'}">${escapeHtml(o.status)}</span></h4>
                <p class="muted">Fonte: ${escapeHtml(o.source_id)} · Resolução: ${escapeHtml(o.native_resolution)} · Método: ${escapeHtml(o.method)}</p>
                <p class="muted">${escapeHtml(o.limitations)}</p>
            </article>
        `).join('');

        el.innerHTML = `
            <div class="section-heading" style="margin-bottom:8px;">
                <span class="badge">${escapeHtml(refData.status)}</span>
                ${execBadgeHtml}
            </div>
            <h3>Investigação territorial</h3>
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

    document.addEventListener('DOMContentLoaded', () => {
        window.SitusMap.init((poly) => {
            runInvestigation({ kind: 'POLYGON', value: poly, crs: 'EPSG:4326' });
        });

        document.getElementById('ref-kind')?.addEventListener('change', (ev) => {
            const k = ev.target.value;
            const inp = document.getElementById('ref-value');
            const radWrap = document.getElementById('radius-wrap');
            radWrap.hidden = k !== 'POINT';
            if (k === 'MUNICIPALITY') inp.placeholder = 'Código IBGE 7 dígitos (ex: 4314902 para Porto Alegre)';
            else if (k === 'POINT') inp.placeholder = 'longitude, latitude (ex: -51.23, -30.03)';
            else if (k === 'CEP') inp.placeholder = 'CEP 8 dígitos (ex: 90010-150)';
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

        initSettings();
        refreshHealthAndCatalog();
    });
})();
