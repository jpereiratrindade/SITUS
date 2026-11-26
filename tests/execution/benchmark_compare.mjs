#!/usr/bin/env node
// SITUS — Experimento comparativo de execução (Seção 8.1 / A12 / A15)
// Compara Servidor Nativo C++26 (via HTTP local) vs Módulo Wasm C++26 (Emscripten)
// Separa inicialização fria de execuções aquecidas e verifica equivalência numérica.

import { performance } from 'node:perf_hooks';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, '../..');
const require = createRequire(import.meta.url);

const BASE_URL = process.env.SITUS_URL || 'http://127.0.0.1:8096';

function percentile(arr, p) {
    const sorted = [...arr].sort((a, b) => a - b);
    const idx = Math.min(sorted.length - 1, Math.floor((p / 100) * sorted.length));
    return sorted[idx];
}

function makeSyntheticLoad(n) {
    const slices = [];
    let refArea = 0;
    for (let i = 0; i < n; i++) {
        const area = 1000.0 + (i % 7) * 250.5;
        refArea += area;
        slices.push({
            unit_code: `43149020500${String(i + 1).padStart(4, '0')}`,
            group_key: String((i % 3) + 1),
            group_label: i % 3 === 0 ? 'Urbana' : (i % 3 === 1 ? 'Aglomerado' : 'Rural'),
            intersected_area_m2: area,
            unit_total_area_m2: area * 2.0
        });
    }
    return {
        operation_id: 'territorial_area_synthesis',
        contract_version: '0.2.0',
        requested_mode: 'AUTO',
        inputs: {
            reference_area_m2: refArea * 1.05,
            overlap_rule: 'DISJOINT_SECTORS',
            dataset_versions: ['2022.1'],
            slices
        }
    };
}

async function runNativeHttp(payload) {
    const t0 = performance.now();
    const res = await fetch(`${BASE_URL}/api/v1/execution/run`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...payload, requested_mode: 'PREFER_SERVER' })
    });
    const json = await res.json();
    const t1 = performance.now();
    return {
        json,
        totalMs: t1 - t0,
        serverExecMs: json.timings_ms ? json.timings_ms.server_ms : null
    };
}

function callWasmSynthesis(wasmMod, reqStr) {
    const byteLen = wasmMod.lengthBytesUTF8(reqStr) + 1;
    const ptr = wasmMod._malloc(byteLen);
    try {
        wasmMod.stringToUTF8(reqStr, ptr, byteLen);
        const resPtr = wasmMod._situs_wasm_run_synthesis(ptr);
        return wasmMod.UTF8ToString(resPtr);
    } finally {
        wasmMod._free(ptr);
    }
}

async function main() {
    const createSitusCoreModule = require(path.join(rootDir, 'web/wasm/situs_core.js'));

    // Medir inicialização fria do módulo Wasm
    const tCold0 = performance.now();
    const wasmMod = await createSitusCoreModule({
        locateFile: (f) => path.join(rootDir, 'web/wasm', f)
    });
    const tCold1 = performance.now();
    const coldInitMs = tCold1 - tCold0;

    const loads = [
        { name: 'pequena_10_unidades', payload: makeSyntheticLoad(10) },
        { name: 'media_250_unidades', payload: makeSyntheticLoad(250) },
        { name: 'maior_2000_unidades', payload: makeSyntheticLoad(2000) }
    ];

    const iterations = 15;
    const report = {
        wasm_cold_init_ms: Number(coldInitMs.toFixed(3)),
        iterations_warm: iterations,
        loads: []
    };

    for (const load of loads) {
        const nativeTotals = [];
        const nativeExecs = [];
        const wasmExecs = [];
        let lastNativeOut = null;
        let lastWasmOut = null;

        // Warmup
        await runNativeHttp(load.payload);
        callWasmSynthesis(wasmMod, JSON.stringify(load.payload));

        for (let i = 0; i < iterations; i++) {
            const nat = await runNativeHttp(load.payload);
            nativeTotals.push(nat.totalMs);
            if (nat.serverExecMs != null) nativeExecs.push(nat.serverExecMs);
            lastNativeOut = nat.json.output;

            const reqStr = JSON.stringify({ ...load.payload, requested_mode: 'PREFER_LOCAL' });
            const tW0 = performance.now();
            const resStr = callWasmSynthesis(wasmMod, reqStr);
            const tW1 = performance.now();
            const wJson = JSON.parse(resStr);
            wasmExecs.push(tW1 - tW0);
            lastWasmOut = wJson.output;
        }

        const diffTotal = Math.abs(lastNativeOut.total_intersected_area_m2 - lastWasmOut.total_intersected_area_m2);
        const equivalent = diffTotal <= 1e-2 && lastNativeOut.groups.length === lastWasmOut.groups.length;

        report.loads.push({
            name: load.name,
            unit_count: load.payload.inputs.slices.length,
            payload_bytes: JSON.stringify(load.payload).length,
            equivalent_within_tolerance: equivalent,
            max_area_diff_m2: diffTotal,
            native_http_roundtrip_ms: {
                median: Number(percentile(nativeTotals, 50).toFixed(3)),
                p95: Number(percentile(nativeTotals, 95).toFixed(3))
            },
            native_server_exec_ms: {
                median: Number(percentile(nativeExecs, 50).toFixed(3)),
                p95: Number(percentile(nativeExecs, 95).toFixed(3))
            },
            wasm_warm_exec_ms: {
                median: Number(percentile(wasmExecs, 50).toFixed(3)),
                p95: Number(percentile(wasmExecs, 95).toFixed(3))
            }
        });
    }

    console.log(JSON.stringify(report, null, 2));
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
