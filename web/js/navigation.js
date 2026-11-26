// SITUS — Navegação lateral por fragmentos (estilo BB-CORE)
'use strict';
(() => {
    const routes = {
        explorar: 'Explorar território',
        camadas: 'Camadas',
        fontes: 'Fontes e métodos',
        estado: 'Estado do sistema',
        configuracoes: 'Configurações'
    };
    let current = null;

    function render({ focus = true } = {}) {
        const fragment = location.hash.slice(1);
        if (fragment === 'main' && current) return;
        const view = Object.hasOwn(routes, fragment) ? fragment : 'explorar';
        if (view === current) return;
        current = view;

        for (const panel of document.querySelectorAll('[data-view]')) {
            panel.hidden = panel.dataset.view !== view;
        }
        for (const link of document.querySelectorAll('.sidebar nav a[href^="#"]')) {
            const active = link.hash === '#' + view;
            link.classList.toggle('active', active);
            if (active) link.setAttribute('aria-current', 'page');
            else link.removeAttribute('aria-current');
        }
        document.getElementById('page-name').textContent = routes[view];
        document.title = 'SITUS · ' + routes[view];

        if (focus) {
            const panel = document.querySelector('[data-view="' + view + '"]');
            const heading = panel && panel.querySelector('h1, h2');
            if (heading) {
                heading.tabIndex = -1;
                heading.focus({ preventScroll: true });
            }
            window.scrollTo({ top: 0, behavior: 'instant' });
        }
        document.dispatchEvent(new CustomEvent('situs-view-changed', { detail: view }));
    }

    window.SitusViews = {
        open(view) {
            if (!Object.hasOwn(routes, view)) return;
            if (location.hash === '#' + view) render();
            else { location.hash = view; render(); }
        },
        current: () => current
    };
    window.addEventListener('hashchange', () => render());
    render({ focus: false });
})();
