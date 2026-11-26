// SITUS — Mapa vetorial SVG estritamente local (sem tiles externos)
'use strict';

window.SitusMap = (() => {
    const svg = () => document.getElementById('situs-map-svg');
    let features = [];
    let hydroFeatures = [];
    let highlightGeom = null;
    let showSectors = true;
    let showHydro = true;
    let showLabels = false;
    let selectedFeature = null;
    let bbox = { minLon: -57.65, minLat: -33.75, maxLon: -49.69, maxLat: -27.08 }; // RS aproximado
    let drawMode = false;
    let dragStart = null;
    let panStart = null;
    let onPolygonSelected = null;
    let onFeatureClicked = null;

    const SITUATION_COLORS = {
        '1': '#1a7dc4',
        '2': '#2ea8e8',
        '3': '#5ec2f0',
        '5': '#087f8c',
        '6': '#3da37d',
        '7': '#d4882a',
        '8': '#4d9e6b'
    };

    function colorFor(sitCode) {
        return SITUATION_COLORS[sitCode] || '#6b859c';
    }

    function project(lon, lat, w, h) {
        const pad = 22;
        const spanLon = Math.max(1e-6, bbox.maxLon - bbox.minLon);
        const spanLat = Math.max(1e-6, bbox.maxLat - bbox.minLat);
        const scale = Math.min((w - 2 * pad) / spanLon, (h - 2 * pad) / spanLat);
        const cx = (bbox.minLon + bbox.maxLon) / 2;
        const cy = (bbox.minLat + bbox.maxLat) / 2;
        return [
            w / 2 + (lon - cx) * scale,
            h / 2 - (lat - cy) * scale
        ];
    }

    function unproject(px, py, w, h) {
        const pad = 22;
        const spanLon = Math.max(1e-6, bbox.maxLon - bbox.minLon);
        const spanLat = Math.max(1e-6, bbox.maxLat - bbox.minLat);
        const scale = Math.min((w - 2 * pad) / spanLon, (h - 2 * pad) / spanLat);
        const cx = (bbox.minLon + bbox.maxLon) / 2;
        const cy = (bbox.minLat + bbox.maxLat) / 2;
        return [
            cx + (px - w / 2) / scale,
            cy - (py - h / 2) / scale
        ];
    }

    function ringToPath(ring, w, h) {
        return ring.map((pt, idx) => {
            const [x, y] = project(pt[0], pt[1], w, h);
            return (idx === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1);
        }).join(' ') + 'Z';
    }

    function lineToPath(coords, w, h) {
        return coords.map((pt, idx) => {
            const [x, y] = project(pt[0], pt[1], w, h);
            return (idx === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1);
        }).join(' ');
    }

    function geomToPath(geom, w, h) {
        if (!geom || !geom.coordinates) return '';
        if (geom.type === 'Polygon') {
            return geom.coordinates.map(r => ringToPath(r, w, h)).join(' ');
        }
        if (geom.type === 'MultiPolygon') {
            return geom.coordinates.map(poly => poly.map(r => ringToPath(r, w, h)).join(' ')).join(' ');
        }
        if (geom.type === 'LineString') {
            return lineToPath(geom.coordinates, w, h);
        }
        if (geom.type === 'MultiLineString') {
            return geom.coordinates.map(l => lineToPath(l, w, h)).join(' ');
        }
        return '';
    }

    function updateBboxFromFeatures() {
        let minLon = Infinity, minLat = Infinity, maxLon = -Infinity, maxLat = -Infinity;
        const visitCoords = (coords) => {
            if (!Array.isArray(coords)) return;
            if (typeof coords[0] === 'number') {
                minLon = Math.min(minLon, coords[0]);
                maxLon = Math.max(maxLon, coords[0]);
                minLat = Math.min(minLat, coords[1]);
                maxLat = Math.max(maxLat, coords[1]);
            } else {
                for (const c of coords) visitCoords(c);
            }
        };
        for (const f of features) {
            if (f.geometry) visitCoords(f.geometry.coordinates);
        }
        if (highlightGeom) visitCoords(highlightGeom.coordinates);
        if (Number.isFinite(minLon)) {
            bbox = { minLon, minLat, maxLon, maxLat };
        }
    }

    function updateScaleBar(w) {
        const spanLon = bbox.maxLon - bbox.minLon;
        const midLat = (bbox.minLat + bbox.maxLat) / 2;
        const metersPerDegLon = 111320 * Math.cos(midLat * Math.PI / 180);
        const totalMeters = spanLon * metersPerDegLon;
        const barMeters = totalMeters * (64 / w);
        const lbl = document.getElementById('map-scale-label');
        if (!lbl) return;
        if (barMeters >= 1000) lbl.textContent = (barMeters / 1000).toFixed(1) + ' km';
        else lbl.textContent = Math.max(1, Math.round(barMeters)) + ' m';
    }

    function render() {
        const el = svg();
        if (!el) return;
        const w = el.clientWidth || 720;
        const h = el.clientHeight || 580;
        el.setAttribute('viewBox', `0 0 ${w} ${h}`);

        let html = `<rect width="${w}" height="${h}" fill="#f5f9fc"/>`;
        if (features.length === 0 && !highlightGeom) {
            html += `<text x="${w / 2}" y="${h / 2 - 8}" text-anchor="middle" fill="#64788a" font-size="14" font-weight="600">Nenhuma geometria exibida</text>`;
            html += `<text x="${w / 2}" y="${h / 2 + 16}" text-anchor="middle" fill="#8296a8" font-size="12">Consulte um município, ponto ou selecione uma área.</text>`;
            el.innerHTML = html;
            updateScaleBar(w);
            return;
        }

        if (showSectors) {
            features.forEach((f, idx) => {
                const p = f.properties || {};
                const d = geomToPath(f.geometry, w, h);
                const fill = colorFor(p.situation_code);
                const isSel = selectedFeature && selectedFeature.kind === 'sector' && selectedFeature.idx === idx;
                html += `<path data-kind="sector" data-idx="${idx}" d="${d}" fill="${fill}" fill-opacity="${isSel ? '0.62' : '0.35'}" stroke="${isSel ? '#e2ad4a' : '#0c2844'}" stroke-width="${isSel ? '2.4' : '0.8'}"><title>Setor ${p.unit_code || ''} · ${p.situation_label || ''}</title></path>`;
            });
        }

        if (showHydro) {
            hydroFeatures.forEach((hf, idx) => {
                const hp = hf.properties || {};
                const hd = geomToPath(hf.geometry, w, h);
                if (!hd) return;
                const isBody = hp.feature_kind === 'WATER_BODY';
                const isSel = selectedFeature && selectedFeature.kind === 'hydro' && selectedFeature.idx === idx;
                if (isBody) {
                    html += `<path data-kind="hydro" data-idx="${idx}" d="${hd}" fill="#2ea8e8" fill-opacity="0.55" stroke="${isSel ? '#e2ad4a' : '#0a5b8a'}" stroke-width="${isSel ? '2.4' : '1'}"><title>${hp.feature_name || 'Massa d\'água'}</title></path>`;
                } else {
                    html += `<path data-kind="hydro" data-idx="${idx}" d="${hd}" fill="none" stroke="${isSel ? '#e2ad4a' : '#1a7dc4'}" stroke-width="${isSel ? '2.6' : '1.3'}" stroke-opacity="0.88"><title>${hp.feature_name || 'Curso d\'água'}</title></path>`;
                }
            });
        }

        if (showLabels) {
            const labels = [];
            if (showSectors) {
                const sorted = [...features]
                    .map((f, idx) => ({ f, idx, area: (f.properties && f.properties.area_m2) || 0 }))
                    .sort((a, b) => b.area - a.area)
                    .slice(0, 28);
                for (const item of sorted) {
                    const c = geomCentroid(item.f.geometry);
                    if (!c) continue;
                    const [lx, ly] = project(c[0], c[1], w, h);
                    if (lx < 10 || lx > w - 10 || ly < 12 || ly > h - 10) continue;
                    const code = (item.f.properties && item.f.properties.unit_code) || '';
                    // Formato IBGE 15 digitos: [0..6]=Mun, [7..8]=Distrito, [9..10]=Subdistrito, [11..14]=Setor
                    const shortCode = code.length === 15 ? (code.slice(7, 9) + '-' + code.slice(11)) : code;
                    labels.push(`<text x="${lx.toFixed(1)}" y="${ly.toFixed(1)}" text-anchor="middle" font-size="10" font-weight="700" fill="#071e38" stroke="#ffffff" stroke-width="2.5" paint-order="stroke" pointer-events="none">${shortCode}</text>`);
                }
            }
            if (showHydro) {
                const seenNames = new Set();
                for (const hf of hydroFeatures) {
                    const nm = hf.properties && hf.properties.feature_name;
                    if (!nm || seenNames.has(nm)) continue;
                    const c = geomCentroid(hf.geometry);
                    if (!c) continue;
                    const [lx, ly] = project(c[0], c[1], w, h);
                    if (lx < 10 || lx > w - 10 || ly < 12 || ly > h - 10) continue;
                    seenNames.add(nm);
                    labels.push(`<text x="${lx.toFixed(1)}" y="${ly.toFixed(1)}" text-anchor="middle" font-size="10" font-style="italic" fill="#0a4d78" stroke="#ffffff" stroke-width="2.5" paint-order="stroke" pointer-events="none">${nm}</text>`);
                    if (seenNames.size >= 15) break;
                }
            }
            html += labels.join('');
        }

        if (highlightGeom) {
            if (highlightGeom.type === 'Point') {
                const [px, py] = project(highlightGeom.coordinates[0], highlightGeom.coordinates[1], w, h);
                html += `<circle cx="${px.toFixed(1)}" cy="${py.toFixed(1)}" r="6" fill="#e2ad4a" stroke="#071e38" stroke-width="2"/>`;
            } else {
                const hd = geomToPath(highlightGeom, w, h);
                const isLine = highlightGeom.type === 'LineString' || highlightGeom.type === 'MultiLineString';
                html += `<path d="${hd}" fill="none" stroke="#e2ad4a" stroke-width="${isLine ? '4' : '2.5'}" ${isLine ? '' : 'stroke-dasharray="6 3"'}/>`;
            }
        }

        el.innerHTML = html;
        updateScaleBar(w);
    }

    function geomCentroid(geom) {
        if (!geom || !geom.coordinates) return null;
        let sx = 0, sy = 0, cnt = 0;
        const visit = (c) => {
            if (!Array.isArray(c)) return;
            if (typeof c[0] === 'number') { sx += c[0]; sy += c[1]; cnt++; }
            else { for (const x of c) visit(x); }
        };
        visit(geom.coordinates);
        return cnt > 0 ? [sx / cnt, sy / cnt] : null;
    }

    function init(polygonCallback, featureClickCallback) {
        onPolygonSelected = polygonCallback;
        onFeatureClicked = featureClickCallback || null;
        const el = svg();
        if (!el) return;

        el.addEventListener('mousedown', (ev) => {
            const rect = el.getBoundingClientRect();
            const px = ev.clientX - rect.left;
            const py = ev.clientY - rect.top;
            if (drawMode) {
                dragStart = { px, py };
            } else {
                const targetPath = ev.target && ev.target.closest ? ev.target.closest('path[data-kind]') : null;
                panStart = {
                    px, py,
                    bbox: { ...bbox },
                    hitKind: targetPath ? targetPath.dataset.kind : null,
                    hitIdx: targetPath ? Number(targetPath.dataset.idx) : null
                };
            }
        });

        window.addEventListener('mouseup', (ev) => {
            const rect = el.getBoundingClientRect();
            const w = el.clientWidth || 720;
            const h = el.clientHeight || 580;
            if (drawMode && dragStart) {
                const px2 = ev.clientX - rect.left;
                const py2 = ev.clientY - rect.top;
                if (Math.abs(px2 - dragStart.px) > 10 && Math.abs(py2 - dragStart.py) > 10) {
                    const [lon1, lat1] = unproject(dragStart.px, dragStart.py, w, h);
                    const [lon2, lat2] = unproject(px2, py2, w, h);
                    const minLon = Math.min(lon1, lon2);
                    const maxLon = Math.max(lon1, lon2);
                    const minLat = Math.min(lat1, lat2);
                    const maxLat = Math.max(lat1, lat2);
                    const poly = {
                        type: 'Polygon',
                        coordinates: [[
                            [minLon, minLat],
                            [maxLon, minLat],
                            [maxLon, maxLat],
                            [minLon, maxLat],
                            [minLon, minLat]
                        ]]
                    };
                    drawMode = false;
                    el.classList.remove('drawing');
                    const btn = document.getElementById('map-draw-btn');
                    if (btn) btn.classList.remove('active');
                    if (onPolygonSelected) onPolygonSelected(poly);
                }
                dragStart = null;
            } else if (!drawMode && panStart && panStart.hitKind != null) {
                const dx = Math.abs((ev.clientX - rect.left) - panStart.px);
                const dy = Math.abs((ev.clientY - rect.top) - panStart.py);
                if (dx < 5 && dy < 5) {
                    selectedFeature = { kind: panStart.hitKind, idx: panStart.hitIdx };
                    render();
                    if (onFeatureClicked) {
                        const feat = panStart.hitKind === 'sector'
                            ? features[panStart.hitIdx]
                            : hydroFeatures[panStart.hitIdx];
                        onFeatureClicked(panStart.hitKind, feat);
                    }
                }
            }
            panStart = null;
        });

        el.addEventListener('mousemove', (ev) => {
            if (!panStart || drawMode) return;
            const rect = el.getBoundingClientRect();
            const dx = ev.clientX - rect.left - panStart.px;
            const dy = ev.clientY - rect.top - panStart.py;
            const w = el.clientWidth || 720;
            const h = el.clientHeight || 580;
            const spanLon = panStart.bbox.maxLon - panStart.bbox.minLon;
            const spanLat = panStart.bbox.maxLat - panStart.bbox.minLat;
            const dLon = -(dx / w) * spanLon;
            const dLat = (dy / h) * spanLat;
            bbox = {
                minLon: panStart.bbox.minLon + dLon,
                maxLon: panStart.bbox.maxLon + dLon,
                minLat: panStart.bbox.minLat + dLat,
                maxLat: panStart.bbox.maxLat + dLat
            };
            render();
        });

        document.getElementById('map-zoom-in')?.addEventListener('click', () => zoom(0.7));
        document.getElementById('map-zoom-out')?.addEventListener('click', () => zoom(1.4));
        document.getElementById('map-fit')?.addEventListener('click', () => {
            updateBboxFromFeatures();
            render();
        });
        document.getElementById('map-draw-btn')?.addEventListener('click', (ev) => {
            drawMode = !drawMode;
            el.classList.toggle('drawing', drawMode);
            ev.currentTarget.classList.toggle('active', drawMode);
        });
        document.getElementById('layer-sectors')?.addEventListener('change', (ev) => {
            showSectors = ev.target.checked;
            render();
        });
        document.getElementById('layer-hydro')?.addEventListener('change', (ev) => {
            showHydro = ev.target.checked;
            render();
        });
        document.getElementById('layer-labels')?.addEventListener('change', (ev) => {
            showLabels = ev.target.checked;
            render();
        });
        window.addEventListener('resize', render);
        render();
    }

    function zoom(factor) {
        const cx = (bbox.minLon + bbox.maxLon) / 2;
        const cy = (bbox.minLat + bbox.maxLat) / 2;
        const hw = ((bbox.maxLon - bbox.minLon) / 2) * factor;
        const hh = ((bbox.maxLat - bbox.minLat) / 2) * factor;
        bbox = { minLon: cx - hw, maxLon: cx + hw, minLat: cy - hh, maxLat: cy + hh };
        render();
    }

    function setData(featureCollection, refGeom) {
        features = (featureCollection && featureCollection.features) || [];
        hydroFeatures = (featureCollection && featureCollection.hydro_features) || [];
        highlightGeom = refGeom || null;
        selectedFeature = null;
        updateBboxFromFeatures();
        render();
    }

    function clearSelection() {
        selectedFeature = null;
        render();
    }

    return { init, setData, render, clearSelection };
})();
