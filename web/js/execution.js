// SITUS — Controlador de execução computacional adaptativa (Servidor Nativo / Worker Wasm / Híbrido)
'use strict';

window.SitusExecution = (() => {
    const state = {
        capabilities: null,
        prefs: {
            mode: 'AUTO',
            profile: 'BALANCED',
            allow_fallback: true,
            show_execution_details: true
        },
        worker: null,
        workerWarm: false,
        workerSupported: typeof Worker !== 'undefined' && typeof WebAssembly !== 'undefined',
        lastError: null
    };

    function loadPrefs() {
        try {
            const raw = localStorage.getItem('situs_exec_prefs');
            if (!raw) return;
            const p = JSON.parse(raw);
            if (['AUTO', 'PREFER_LOCAL', 'PREFER_SERVER', 'HYBRID'].includes(p.mode)) state.prefs.mode = p.mode;
            if (['ECONOMY', 'BALANCED', 'PERFORMANCE'].includes(p.profile)) state.prefs.profile = p.profile;
            if (typeof p.allow_fallback === 'boolean') state.prefs.allow_fallback = p.allow_fallback;
            if (typeof p.show_execution_details === 'boolean') state.prefs.show_execution_details = p.show_execution_details;
        } catch (_) {}
    }

    function savePrefs() {
        try {
            localStorage.setItem('situs_exec_prefs', JSON.stringify(state.prefs));
        } catch (_) {}
    }

    async function refreshCapabilities() {
        const res = await fetch('/api/v1/execution/capabilities');
        if (!res.ok) throw new Error('Falha ao obter capacidades de execucao');
        state.capabilities = await res.json();
        return state.capabilities;
    }

    function isWasmAvailable() {
        return Boolean(
            state.workerSupported &&
            state.capabilities &&
            state.capabilities.wasm_artifact &&
            state.capabilities.wasm_artifact.available
        );
    }

    function getProfileLimits() {
        const profs = (state.capabilities && state.capabilities.profiles) || {};
        return profs[state.prefs.profile] || {
            max_input_bytes: 1048576,
            max_units: 2500,
            max_vertices: 250000,
            timeout_ms: 8000
        };
    }

    function decideTarget(unitCount, inputBytes) {
        const mode = state.prefs.mode;
        const limits = getProfileLimits();
        const wasmReady = isWasmAvailable();
        const withinLimits = unitCount <= limits.max_units && inputBytes <= limits.max_input_bytes;

        if (mode === 'PREFER_SERVER') {
            return { target: 'NATIVE_SERVER', reason: 'USER_PREFERENCE_SERVER: Modo PREFER_SERVER ativo' };
        }
        if (mode === 'PREFER_LOCAL') {
            if (wasmReady && withinLimits) {
                return { target: 'BROWSER_WASM', reason: 'USER_PREFERENCE_LOCAL: Nucleo C++26 em Web Worker Wasm' };
            }
            if (!state.prefs.allow_fallback) {
                return {
                    target: null,
                    reason: !wasmReady
                        ? 'WASM_UNAVAILABLE: Modulo Wasm indisponivel e fallback desabilitado'
                        : 'LIMIT_EXCEEDED: Entrada excede limite do perfil e fallback desabilitado'
                };
            }
            return {
                target: 'NATIVE_SERVER',
                fallback_from: 'BROWSER_WASM',
                reason: !wasmReady
                    ? 'FALLBACK_TO_SERVER: Modulo Wasm indisponivel'
                    : 'FALLBACK_TO_SERVER: Entrada excede limites do perfil local'
            };
        }
        if (mode === 'HYBRID') {
            if (wasmReady && withinLimits) {
                return { target: 'HYBRID', reason: 'HYBRID_COMPOSITION: Intersecao oficial no PostGIS + sintese local em Worker Wasm' };
            }
            if (!state.prefs.allow_fallback) {
                return { target: null, reason: 'HYBRID_UNAVAILABLE: Worker Wasm indisponivel e fallback desabilitado' };
            }
            return {
                target: 'NATIVE_SERVER',
                fallback_from: 'HYBRID',
                reason: 'FALLBACK_TO_SERVER: Etapa Wasm indisponivel para composicao hibrida'
            };
        }

        // Modo AUTO conservador (seção 5.2)
        if (wasmReady && state.workerWarm && withinLimits && unitCount >= 25) {
            return {
                target: 'HYBRID',
                reason: 'AUTO_HYBRID_WARM_WORKER: Worker Wasm aquecido e volume >= 25 unidades dentro dos limites'
            };
        }
        return {
            target: 'NATIVE_SERVER',
            reason: wasmReady
                ? 'AUTO_SERVER_CONSERVATIVE: Execucao nativa de referencia (carga pequena ou worker frio)'
                : 'AUTO_SERVER_NO_WASM: Execucao nativa de referencia (Wasm indisponivel)'
        };
    }

    function runInWorker(payload, timeoutMs, signal) {
        return new Promise((resolve, reject) => {
            if (!state.worker) {
                state.worker = new Worker('/wasm/worker.js');
            }
            const reqId = 'req_' + Date.now() + '_' + Math.random().toString(36).slice(2);
            let done = false;

            const timer = setTimeout(() => {
                if (done) return;
                done = true;
                state.worker.terminate();
                state.worker = null;
                state.workerWarm = false;
                reject(new Error('TIMEOUT: Worker Wasm excedeu ' + timeoutMs + ' ms e foi encerrado'));
            }, timeoutMs);

            const onAbort = () => {
                if (done) return;
                done = true;
                clearTimeout(timer);
                state.worker.terminate();
                state.worker = null;
                state.workerWarm = false;
                reject(new Error('CANCELLED: Execucao cancelada pelo usuario'));
            };

            if (signal) {
                if (signal.aborted) {
                    onAbort();
                    return;
                }
                signal.addEventListener('abort', onAbort, { once: true });
            }

            const onMsg = (ev) => {
                if (!ev.data || ev.data.id !== reqId) return;
                if (done) return;
                done = true;
                clearTimeout(timer);
                if (signal) signal.removeEventListener('abort', onAbort);
                state.worker.removeEventListener('message', onMsg);
                if (ev.data.ok) {
                    state.workerWarm = true;
                    resolve(ev.data);
                } else {
                    reject(new Error(ev.data.error || 'Erro desconhecido no Worker Wasm'));
                }
            };

            state.worker.addEventListener('message', onMsg);
            state.worker.postMessage({ id: reqId, type: 'run_synthesis', payload });
        });
    }

    async function runAdaptiveSynthesis(reference, signal) {
        const tTotal0 = performance.now();
        if (!state.capabilities) await refreshCapabilities();

        const tInp0 = performance.now();
        const inpRes = await fetch('/api/v1/execution/inputs', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ operation_id: 'territorial_area_synthesis', reference }),
            signal
        });
        const tInp1 = performance.now();
        if (!inpRes.ok) {
            const err = await inpRes.json().catch(() => ({}));
            throw new Error((err.error && err.error.message) || 'Falha ao obter entradas completas');
        }
        const inputsBundle = await inpRes.json();
        const slices = (inputsBundle.inputs && inputsBundle.inputs.slices) || [];
        const payloadBytes = JSON.stringify(inputsBundle.inputs).length;

        const decision = decideTarget(slices.length, payloadBytes);
        if (!decision.target) {
            return {
                status: 'UNAVAILABLE',
                requested_mode: state.prefs.mode,
                selected_target: null,
                actual_target: null,
                decision_reason: decision.reason,
                fallback_reason: null,
                operation_id: 'territorial_area_synthesis',
                algorithm_version: '0.2.0',
                contract_version: '0.2.0',
                dataset_versions: inputsBundle.inputs.dataset_versions || [],
                output: null
            };
        }

        const limits = getProfileLimits();

        if (decision.target === 'BROWSER_WASM' || decision.target === 'HYBRID') {
            try {
                const workerOut = await runInWorker({
                    operation_id: 'territorial_area_synthesis',
                    contract_version: '0.2.0',
                    requested_mode: state.prefs.mode,
                    inputs: inputsBundle.inputs
                }, limits.timeout_ms, signal);

                const tTotal1 = performance.now();
                const res = workerOut.result;
                res.selected_target = decision.target;
                res.actual_target = decision.target;
                res.decision_reason = decision.reason;
                res.stages = [
                    { stage: 'postgis_intersection_inputs', target: 'NATIVE_SERVER' },
                    { stage: 'area_synthesis', target: 'BROWSER_WASM' }
                ];
                res.timings_ms = {
                    server_ms: tInp1 - tInp0,
                    worker_ms: workerOut.timings_ms.worker_exec_ms,
                    worker_init_ms: workerOut.timings_ms.worker_init_ms,
                    total_ms: tTotal1 - tTotal0
                };
                res.bytes_transferred = payloadBytes;
                return res;
            } catch (err) {
                if (String(err.message).startsWith('CANCELLED')) {
                    return {
                        status: 'CANCELLED',
                        requested_mode: state.prefs.mode,
                        selected_target: decision.target,
                        actual_target: null,
                        decision_reason: err.message,
                        operation_id: 'territorial_area_synthesis',
                        output: null
                    };
                }
                if (!state.prefs.allow_fallback) {
                    return {
                        status: 'FAILED',
                        requested_mode: state.prefs.mode,
                        selected_target: decision.target,
                        actual_target: null,
                        decision_reason: 'WASM_EXECUTION_FAILED: ' + err.message,
                        operation_id: 'territorial_area_synthesis',
                        output: null
                    };
                }
                // Fallback único autorizado para o servidor nativo
                const srvRes = await fetch('/api/v1/execution/run', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        operation_id: 'territorial_area_synthesis',
                        contract_version: '0.2.0',
                        requested_mode: state.prefs.mode,
                        inputs: inputsBundle.inputs
                    }),
                    signal
                });
                const srvJson = await srvRes.json();
                srvJson.selected_target = decision.target;
                srvJson.fallback_reason = 'FALLBACK_AFTER_WASM_ERROR: ' + err.message;
                return srvJson;
            }
        }

        // Execução nativa no servidor
        const srvRes = await fetch('/api/v1/execution/run', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                operation_id: 'territorial_area_synthesis',
                contract_version: '0.2.0',
                requested_mode: state.prefs.mode,
                inputs: inputsBundle.inputs
            }),
            signal
        });
        const tTotal1 = performance.now();
        const srvJson = await srvRes.json();
        srvJson.decision_reason = decision.reason;
        if (decision.fallback_from) {
            srvJson.selected_target = decision.fallback_from;
            srvJson.fallback_reason = decision.reason;
        }
        if (srvJson.timings_ms) {
            srvJson.timings_ms.total_ms = tTotal1 - tTotal0;
        }
        srvJson.bytes_transferred = JSON.stringify(srvJson).length;
        return srvJson;
    }

    async function runDirectBenchmark(sampleSlices, referenceAreaM2) {
        const reqPayload = {
            operation_id: 'territorial_area_synthesis',
            contract_version: '0.2.0',
            requested_mode: 'PREFER_SERVER',
            inputs: {
                reference_area_m2: referenceAreaM2,
                overlap_rule: 'DISJOINT_SECTORS',
                dataset_versions: ['2022.1'],
                slices: sampleSlices
            }
        };

        const tSrv0 = performance.now();
        const srvRes = await fetch('/api/v1/execution/run', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(reqPayload)
        });
        const srvJson = await srvRes.json();
        const tSrv1 = performance.now();

        let wasmOut = null;
        let wasmErr = null;
        if (isWasmAvailable()) {
            try {
                const tW0 = performance.now();
                const wRes = await runInWorker({ ...reqPayload, requested_mode: 'PREFER_LOCAL' }, 8000, null);
                const tW1 = performance.now();
                wasmOut = {
                    status: wRes.result.status,
                    total_intersected_area_m2: wRes.result.output.total_intersected_area_m2,
                    worker_init_ms: wRes.timings_ms.worker_init_ms,
                    worker_exec_ms: wRes.timings_ms.worker_exec_ms,
                    client_roundtrip_ms: tW1 - tW0,
                    bytes_transferred: wRes.bytes_transferred
                };
            } catch (e) {
                wasmErr = e.message;
            }
        } else {
            wasmErr = 'Wasm indisponivel';
        }

        return {
            native_server: {
                status: srvJson.status,
                total_intersected_area_m2: srvJson.output ? srvJson.output.total_intersected_area_m2 : null,
                server_exec_ms: srvJson.timings_ms ? srvJson.timings_ms.server_ms : null,
                client_roundtrip_ms: tSrv1 - tSrv0
            },
            browser_wasm: wasmOut,
            wasm_error: wasmErr
        };
    }

    loadPrefs();

    return {
        get prefs() { return state.prefs; },
        get capabilities() { return state.capabilities; },
        isWasmAvailable,
        savePrefs,
        refreshCapabilities,
        runAdaptiveSynthesis,
        runDirectBenchmark
    };
})();
