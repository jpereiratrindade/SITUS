// SITUS — Web Worker dedicado para o núcleo C++26 compilado em WebAssembly
'use strict';

let modulePromise = null;

async function ensureModule() {
    if (!modulePromise) {
        importScripts('/wasm/situs_core.js');
        modulePromise = createSitusCoreModule({
            locateFile(path) {
                return '/wasm/' + path;
            }
        });
    }
    return modulePromise;
}

self.onmessage = async (event) => {
    const { id, type, payload } = event.data || {};
    const t0 = performance.now();
    try {
        const mod = await ensureModule();
        const tReady = performance.now();

        if (type === 'ping') {
            const ver = mod.ccall('situs_wasm_version', 'string', [], []);
            self.postMessage({
                id,
                ok: true,
                version: ver,
                init_ms: tReady - t0
            });
            return;
        }

        if (type === 'run_synthesis') {
            const reqStr = JSON.stringify(payload);
            const tExec0 = performance.now();
            const byteLen = mod.lengthBytesUTF8(reqStr) + 1;
            const ptr = mod._malloc(byteLen);
            let resStr;
            try {
                mod.stringToUTF8(reqStr, ptr, byteLen);
                const resPtr = mod._situs_wasm_run_synthesis(ptr);
                resStr = mod.UTF8ToString(resPtr);
            } finally {
                mod._free(ptr);
            }
            const tExec1 = performance.now();
            const parsed = JSON.parse(resStr);
            self.postMessage({
                id,
                ok: true,
                result: parsed,
                timings_ms: {
                    worker_init_ms: tReady - t0,
                    worker_exec_ms: tExec1 - tExec0,
                    worker_total_ms: tExec1 - t0
                },
                bytes_transferred: reqStr.length + resStr.length
            });
            return;
        }

        self.postMessage({ id, ok: false, error: 'Tipo de mensagem desconhecido no Worker' });
    } catch (err) {
        self.postMessage({
            id,
            ok: false,
            error: err && err.message ? err.message : String(err)
        });
    }
};
