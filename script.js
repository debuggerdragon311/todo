// Read the SSR array injected into window by C Daemon
const serverInjectedData = window.__INITIAL_DATA__ || null;

let todos = [];
let activeIndex = 0;
let currentFilter = 'all';
let currentTypeFilter = 'all';
let currentSort = 'prio-desc';
let searchQuery = '';

// Opens file via C Daemon backend endpoint (/api/open)
function openInEditor(filePath, line, col) {
    if (!filePath) return;
    const url = `/api/open?file=${encodeURIComponent(filePath)}&line=${line || 1}&col=${col || 1}`;
    fetch(url)
        .then(() => {
            const shortName = filePath.split('/').pop();
            showToast(`Opened ${shortName}:${line || 1} in editor`);
        })
        .catch(err => console.error("Failed to open editor:", err));
}

async function fetchTodos() {
    if (serverInjectedData && Array.isArray(serverInjectedData)) {
        todos = serverInjectedData;
        document.getElementById('syncLatency').textContent = '<1ms (SSR)';
        const approxBytes = JSON.stringify(todos).length;
        document.getElementById('memFootprint').textContent = `${(approxBytes / 1024).toFixed(1)} KB`;
        render();
        return;
    }

    const t0 = performance.now();
    try {
        const res = await fetch('/api/todos');
        if (!res.ok) throw new Error();
        todos = await res.json();
        const t1 = performance.now();
        document.getElementById('syncLatency').textContent = `${Math.round(t1 - t0)}ms`;
    } catch (e) {
        todos = [];
        document.getElementById('syncLatency').textContent = '<1ms (empty)';
    }

    const approxBytes = JSON.stringify(todos).length;
    document.getElementById('memFootprint').textContent = `${(approxBytes / 1024).toFixed(1)} KB`;
    render();
}

function getFilteredList() {
    return todos.filter(item => {
        const matchesState = (currentFilter === 'all') || (item.state === currentFilter);
        const itemType = (item.type || 'TODO').toUpperCase();
        const matchesType = (currentTypeFilter === 'all') || (itemType === currentTypeFilter);

        const pathStr = typeof item.address === 'object'
            ? `${item.address.file_name} ${item.address.file_path}:${item.address.line_number}`
            : String(item.address || '');

        const matchesQuery = !searchQuery ||
            item.data.toLowerCase().includes(searchQuery) ||
            itemType.toLowerCase().includes(searchQuery) ||
            pathStr.toLowerCase().includes(searchQuery) ||
            String(item.ref_id).includes(searchQuery);

        return matchesState && matchesType && matchesQuery;
    });
}

function sortList(list) {
    return list.sort((a, b) => {
        switch (currentSort) {
            case 'prio-desc': return b.priority - a.priority;
            case 'prio-asc': return a.priority - b.priority;
            case 'type-asc': return (a.type || '').localeCompare(b.type || '');
            case 'alpha-asc': return a.data.localeCompare(b.data);
            case 'alpha-desc': return b.data.localeCompare(a.data);
            case 'ref-asc': return a.ref_id - b.ref_id;
            case 'path-asc': {
                const pA = (typeof a.address === 'object' ? a.address.file_name : a.address) || '';
                const pB = (typeof b.address === 'object' ? b.address.file_name : b.address) || '';
                return pA.localeCompare(pB);
            }
            default: return 0;
        }
    });
}

function render() {
    document.getElementById('cnt-all').textContent = todos.length;
    document.getElementById('cnt-open').textContent = todos.filter(t => t.state === 'open').length;
    document.getElementById('cnt-prog').textContent = todos.filter(t => t.state === 'in_progress').length;
    document.getElementById('cnt-closed').textContent = todos.filter(t => t.state === 'closed').length;

    const filtered = sortList(getFilteredList());
    if (activeIndex >= filtered.length) activeIndex = Math.max(0, filtered.length - 1);

    document.getElementById('matchesCount').textContent = filtered.length;
    document.getElementById('totalCount').textContent = todos.length;

    const listEl = document.getElementById('todoListing');
    const emptyEl = document.getElementById('emptyNotice');

    listEl.innerHTML = '';
    if (filtered.length === 0) {
        emptyEl.style.display = 'block';
        return;
    }
    emptyEl.style.display = 'none';

    filtered.forEach((item, idx) => {
        const isSelected = (idx === activeIndex);
        const row = document.createElement('div');
        row.className = `todo-item ${isSelected ? 'selected' : ''} ${item.state === 'closed' ? 'is-closed' : ''}`;
        row.dataset.idx = idx;

        let prioTier = 'prio-low';
        if (item.priority >= 80) prioTier = 'prio-crit';
        else if (item.priority >= 50) prioTier = 'prio-high';

        const itemType = (item.type || 'TODO').toUpperCase();
        let typeClass = 'type-todo';
        if (itemType === 'FIXME') typeClass = 'type-fixme';
        else if (itemType === 'NOTE') typeClass = 'type-note';
        else if (itemType === 'HACK') typeClass = 'type-hack';

        let fName = 'src';
        let fPath = '/';
        let line = 0;
        let col = 0;

        if (typeof item.address === 'object' && item.address !== null) {
            fName = item.address.file_name || 'src';
            fPath = item.address.file_path || fName;
            line = item.address.line_number || 0;
            col = item.address.column_number || 0;
        } else {
            fName = item.address || 'src';
            fPath = fName;
        }

        const coords = `:${line}${col ? ':' + col : ''}`;

        row.innerHTML = `
        <div class="row-cell cell-state">
        <button class="state-pill state-${item.state}" onclick="toggleState(${item.ref_id}, event)" title="Click or press 'x' to cycle state">
        <span class="status-pip"></span>
        <span class="state-txt">${formatState(item.state)}</span>
        </button>
        </div>

        <div class="row-cell cell-id">
        <span class="ref-num">#${item.ref_id}</span>
        </div>

        <div class="row-cell cell-prio">
        <span class="prio-tag ${prioTier}">P${item.priority}</span>
        </div>

        <div class="row-cell cell-type">
        <span class="type-pill ${typeClass}">${itemType}</span>
        </div>

        <div class="row-cell cell-data">
        <span class="data-text">${escapeHtml(item.data)}</span>
        ${item.description ? `
            <pre class="data-desc" style="margin: 4px 0 0 0; font-size: 0.82em; opacity: 0.62; white-space: pre-wrap; font-family: inherit; line-height: 1.4;">${escapeHtml(item.description)}</pre>
            ` : ''}
            </div>

            <div class="row-cell cell-addr">
            <a href="#" class="addr-box" onclick="openInEditor('${fPath.replace(/'/g, "\\'")}', ${line}, ${col}); return false;" title="Click or press '↵ Enter' to open in system editor">
            <span class="file-name">${escapeHtml(fName)}</span>
            <span class="file-coords">${coords}</span>
            </a>
            </div>
            `;

        row.addEventListener('click', () => {
            activeIndex = idx;
            updateSelectionVisuals();
        });

        listEl.appendChild(row);
    });
}

function updateSelectionVisuals() {
    const rows = document.querySelectorAll('.todo-item');
    rows.forEach((r, idx) => {
        if (idx === activeIndex) {
            r.classList.add('selected');
            r.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
        } else {
            r.classList.remove('selected');
        }
    });
}

function formatState(s) {
    if (s === 'in_progress') return 'ACTIVE';
    if (s === 'closed') return 'DONE';
    return 'OPEN';
}

function toggleState(refId, evt) {
    if (evt) evt.stopPropagation();
    const target = todos.find(t => t.ref_id === refId);
    if (!target) return;

    if (target.state === 'open') target.state = 'in_progress';
    else if (target.state === 'in_progress') target.state = 'closed';
    else target.state = 'open';

    render();
}

function copyLocation(str, evt) {
    if (evt && evt.metaKey) return;
    if (evt) evt.preventDefault();
    navigator.clipboard.writeText(str).then(() => showToast(`Copied: ${str}`));
}

function showToast(msg) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.classList.add('visible');
    setTimeout(() => t.classList.remove('visible'), 2000);
}

function escapeHtml(str) {
    return (str || '').replace(/[&<>'"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' }[c] || c));
}

// Global Key Listeners
window.addEventListener('keydown', (e) => {
    const inInput = (document.activeElement.tagName === 'INPUT' || document.activeElement.tagName === 'SELECT');

    if (e.key === '/' && !inInput) {
        e.preventDefault();
        document.getElementById('queryInput').focus();
        return;
    }

    if (e.key === 'Escape') {
        if (inInput) document.activeElement.blur();
        document.getElementById('queryInput').value = '';
        searchQuery = '';
        render();
        return;
    }

    if (!inInput) {
        const currentList = sortList(getFilteredList());
        if (currentList.length === 0) return;

        if (e.key === 'j' || e.key === 'ArrowDown') {
            e.preventDefault();
            activeIndex = (activeIndex + 1) % currentList.length;
            updateSelectionVisuals();
        } else if (e.key === 'k' || e.key === 'ArrowUp') {
            e.preventDefault();
            activeIndex = (activeIndex - 1 + currentList.length) % currentList.length;
            updateSelectionVisuals();
        } else if (e.key === 'x') {
            e.preventDefault();
            const target = currentList[activeIndex];
            if (target) toggleState(target.ref_id);
        } else if (e.key === 'c') {
            e.preventDefault();
            const target = currentList[activeIndex];
            if (target && target.address) {
                const pathStr = typeof target.address === 'object'
                    ? `${target.address.file_path}:${target.address.line_number}:${target.address.column_number}`
                    : target.address;
                copyLocation(pathStr);
            }
        } else if (e.key === 'Enter') {
            e.preventDefault();
            const target = currentList[activeIndex];
            if (target && target.address && typeof target.address === 'object') {
                openInEditor(
                    target.address.file_path,
                    target.address.line_number,
                    target.address.column_number
                );
            }
        }
    }
});

// Attach toolbar event listeners
document.getElementById('queryInput').addEventListener('input', (e) => {
    searchQuery = e.target.value.toLowerCase().trim();
    activeIndex = 0;
    render();
});

document.getElementById('clearBtn').addEventListener('click', () => {
    document.getElementById('queryInput').value = '';
    searchQuery = '';
    render();
});

document.getElementById('typeSelect').addEventListener('change', (e) => {
    currentTypeFilter = e.target.value;
    activeIndex = 0;
    render();
});

document.getElementById('sortSelect').addEventListener('change', (e) => {
    currentSort = e.target.value;
    render();
});

document.querySelectorAll('.seg-item').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.seg-item').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentFilter = btn.dataset.state;
        activeIndex = 0;
        render();
    });
});

fetchTodos();
