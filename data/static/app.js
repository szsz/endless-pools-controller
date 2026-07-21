/* ============================================================
   Swim workout planner — hash-routed, mobile-first.

   Routes
     #/          full-screen workout list
     #/w/:id     full-screen workout view / editor

   Workout JSON (must match firmware workout_storage.cpp)
     { id, title, swims: [ item, ... ] }
       item = swim  { speed, dur, note }
            | set   { type:"set", repeat:N, note, swims:[ swim, ... ] }
     A set's swims are repeated `repeat` times when the workout runs.
   ============================================================ */

const $ = id => document.getElementById(id);

/* ---------- state ---------- */
let DB = { workouts: [] };
let current = null;      // workout currently shown
let editMode = false;
let dirty = false;
let addTarget = null;    // null = append to workout; number = index of the set we're adding into
let editingPath = null;  // [i] or [i,j] while editing an existing swim
// Older firmware stores only a flat swim list: it would silently mangle a set
// into {speed:0,dur:0}. /api/settings advertises {"sets":true} when the device
// can store them, so we hide the set editor unless it does.
let setsSupported = false;

async function detectCapabilities() {
  try {
    const res = await fetch('/api/settings');
    if (!res.ok) return;
    const j = await res.json();
    setsSupported = j && j.sets === true;
  } catch (e) { /* leave sets disabled if we can't tell */ }
}

/* ---------- small helpers ---------- */
const secsToMMSS = sec => {
  sec = Math.max(0, Math.round(sec || 0));
  const m = Math.floor(sec / 60), s = sec % 60;
  return `${m}:${String(s).padStart(2, '0')}`;
};
const calcDistance = s => (s && s.speed) ? Math.round(s.dur * 100 / s.speed) : 0;
const isSet   = it => !!(it && it.type === 'set');
const setReps = it => Math.min(100, Math.max(1, parseInt(it && it.repeat, 10) || 1));
const itemsOf = w => (w && Array.isArray(w.swims)) ? w.swims : [];

function workoutTotals(w) {
  let sec = 0, dist = 0;
  itemsOf(w).forEach(it => {
    if (isSet(it)) {
      const r = setReps(it);
      (it.swims || []).forEach(s => { sec += r * (s.dur || 0); dist += r * calcDistance(s); });
    } else if (it) {
      sec += it.dur || 0;
      dist += calcDistance(it);
    }
  });
  return { sec, dist };
}

function mkBtn(label, aria, onClick, cls) {
  const b = document.createElement('button');
  b.type = 'button';
  b.className = 'btn-small' + (cls ? ' ' + cls : '');
  b.textContent = label;
  b.setAttribute('aria-label', aria);
  b.addEventListener('click', ev => { ev.stopPropagation(); onClick(); });
  return b;
}

function markDirty() { dirty = true; updateButtons(); }

/* ---------- path helpers (a swim lives at [i] or [i,j]) ---------- */
function arrForPath(path) {
  return path.length === 1 ? current.swims : (current.swims[path[0]].swims || []);
}
function getSwim(path) { return arrForPath(path)[path[path.length - 1]]; }
function setSwimAt(path, s) { arrForPath(path)[path[path.length - 1]] = s; }
function removeSwim(path) { arrForPath(path).splice(path[path.length - 1], 1); }
function moveItem(path, d) {
  const arr = arrForPath(path);
  const i = path[path.length - 1], j = i + d;
  if (j < 0 || j >= arr.length) return;
  [arr[i], arr[j]] = [arr[j], arr[i]];
  markDirty();
  renderItems();
}

/* ============================================================
   Router
   ============================================================ */
function parseHash() {
  const h = location.hash || '#/';
  const m = h.match(/^#\/w\/(.+)$/);
  return m ? { view: 'workout', id: decodeURIComponent(m[1]) } : { view: 'list' };
}

async function route() {
  const r = parseHash();
  if (r.view === 'workout') {
    const ok = await showWorkout(r.id);
    if (!ok) { location.hash = '#/'; return; }
  } else {
    showList();
  }
}

function showList() {
  current = null; editMode = false; dirty = false;
  addTarget = null; editingPath = null;
  $('workoutView').hidden = true;
  $('listView').hidden = false;
  renderList();
}

async function showWorkout(id) {
  let w = DB.workouts.find(x => String(x.id) === String(id));
  if (!w) {
    try {
      const res = await fetch(`/api/workout?id=${encodeURIComponent(id)}`);
      if (!res.ok) return false;
      w = await res.json();
      DB.workouts.push(w);
    } catch (e) { return false; }
  }
  if (!w) return false;
  if (!Array.isArray(w.swims)) w.swims = [];
  current = w;
  dirty = false;
  addTarget = null;
  editingPath = null;
  // a brand-new (unsaved) workout opens straight into edit mode
  editMode = !!w.__isNew;
  $('listView').hidden = true;
  $('workoutView').hidden = false;
  renderWorkout();
  return true;
}

/* ============================================================
   List view
   ============================================================ */
const fetchAll = async () => {
  try {
    const ids = await (await fetch('/api/workouts')).json();
    const loaded = await Promise.all(ids.map(id =>
      fetch(`/api/workout?id=${encodeURIComponent(id)}`)
        .then(r => r.json())
        .catch(() => null)
    ));
    // keep any unsaved local workout around
    const unsaved = DB.workouts.filter(w => w.__isNew);
    DB.workouts = loaded.filter(Boolean).concat(unsaved);
  } catch (e) {
    console.error('Failed to load workouts', e);
  }
  if (!$('listView').hidden) renderList();
};

function renderList() {
  const ul = $('workoutList');
  ul.innerHTML = '';
  const ws = DB.workouts || [];
  $('listEmpty').hidden = ws.length > 0;
  ws.forEach(w => {
    const li = document.createElement('li');
    li.className = 'list-row';
    li.tabIndex = 0;

    const name = document.createElement('span');
    name.className = 'list-name';
    name.textContent = w.title || '(untitled)';

    const meta = document.createElement('span');
    meta.className = 'list-meta';
    const t = workoutTotals(w);
    const nSets = itemsOf(w).filter(isSet).length;
    meta.textContent = `${secsToMMSS(t.sec)} · ${t.dist} m` + (nSets ? ` · ${nSets} set${nSets > 1 ? 's' : ''}` : '');

    li.appendChild(name);
    li.appendChild(meta);
    const go = () => { location.hash = `#/w/${encodeURIComponent(w.id)}`; };
    li.addEventListener('click', go);
    li.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); go(); } });
    ul.appendChild(li);
  });
}

/* ============================================================
   Workout view
   ============================================================ */
function renderWorkout() {
  if (!current) return;
  $('wTitle').textContent = current.title || '(untitled)';
  $('titleRow').hidden = !editMode;
  $('titleIn').value = current.title || '';
  $('editBtn').textContent = editMode ? '✓' : '✎';
  $('editBtn').setAttribute('aria-label', editMode ? 'Done editing' : 'Edit workout');

  const t = workoutTotals(current);
  $('wSummary').textContent = `${secsToMMSS(t.sec)} · ${t.dist} m`;

  renderItems();
  updateAddTools();
  updateButtons();
}

function renderItems() {
  const list = $('itemList');
  list.innerHTML = '';
  const items = itemsOf(current);
  if (!items.length) {
    const li = document.createElement('li');
    li.className = 'empty';
    li.textContent = editMode ? 'Add a swim or a set below.' : 'No segments yet.';
    list.appendChild(li);
    return;
  }
  items.forEach((it, i) => {
    list.appendChild(isSet(it) ? renderSetItem(it, i) : renderSwimItem(it, [i], String(i + 1)));
  });
}

function rowActions(path) {
  const acts = document.createElement('div');
  acts.className = 'row-actions';
  acts.appendChild(mkBtn('↑', 'Move up', () => moveItem(path, -1)));
  acts.appendChild(mkBtn('↓', 'Move down', () => moveItem(path, 1)));
  acts.appendChild(mkBtn('✎', 'Edit swim', () => beginEditSwim(path)));
  acts.appendChild(mkBtn('✖', 'Delete swim', () => {
    removeSwim(path);
    if (editingPath && editingPath.join() === path.join()) cancelEditSwim();
    markDirty(); renderWorkout();
  }, 'btn-danger'));
  return acts;
}

function renderSwimItem(s, path, label) {
  const li = document.createElement('li');
  li.className = 'item swim' + (editingPath && editingPath.join() === path.join() ? ' editing' : '');

  const main = document.createElement('div');
  main.className = 'item-main';

  const no = document.createElement('span'); no.className = 'item-no'; no.textContent = label;
  const pace = document.createElement('span'); pace.className = 'item-pace';
  pace.textContent = s.speed ? `${secsToMMSS(s.speed)}/100m` : 'rest';
  const time = document.createElement('span'); time.className = 'item-time'; time.textContent = secsToMMSS(s.dur);
  const dist = document.createElement('span'); dist.className = 'item-dist';
  dist.textContent = s.speed ? `${calcDistance(s)} m` : '';

  [no, pace, time, dist].forEach(el => main.appendChild(el));
  li.appendChild(main);

  if (s.note) {
    const n = document.createElement('div');
    n.className = 'item-note';
    n.textContent = s.note;          // textContent: never inject user text as HTML
    li.appendChild(n);
  }
  if (editMode) li.appendChild(rowActions(path));
  return li;
}

function renderSetItem(it, i) {
  const li = document.createElement('li');
  li.className = 'item set' + (addTarget === i ? ' targeted' : '');

  const head = document.createElement('div');
  head.className = 'set-head';

  if (editMode) {
    // Editable set name. Updates the model in place and does NOT re-render,
    // otherwise the input would lose focus on every keystroke.
    const nameIn = document.createElement('input');
    nameIn.type = 'text';
    nameIn.className = 'set-name-input';
    nameIn.placeholder = `Set ${i + 1} name`;
    nameIn.value = it.note || '';
    nameIn.setAttribute('aria-label', `Name for set ${i + 1}`);
    nameIn.addEventListener('input', () => { it.note = nameIn.value; markDirty(); });
    head.appendChild(nameIn);
  } else {
    const title = document.createElement('span');
    title.className = 'set-title';
    title.textContent = it.note ? `Set — ${it.note}` : `Set ${i + 1}`;
    head.appendChild(title);
  }

  const reps = document.createElement('span');
  reps.className = 'set-reps';
  reps.textContent = `×${setReps(it)}`;
  head.appendChild(reps);
  li.appendChild(head);

  if (editMode) {
    const acts = document.createElement('div');
    acts.className = 'row-actions set-actions';
    acts.appendChild(mkBtn('−', 'Fewer repeats', () => { it.repeat = Math.max(1, setReps(it) - 1); markDirty(); renderWorkout(); }));
    acts.appendChild(mkBtn('+', 'More repeats', () => { it.repeat = Math.min(100, setReps(it) + 1); markDirty(); renderWorkout(); }));
    acts.appendChild(mkBtn('↑', 'Move set up', () => moveItem([i], -1)));
    acts.appendChild(mkBtn('↓', 'Move set down', () => moveItem([i], 1)));
    acts.appendChild(mkBtn('➕ swim', 'Add swim to this set', () => { addTarget = i; editingPath = null; updateAddTools(); renderItems(); }));
    acts.appendChild(mkBtn('✖', 'Delete set', () => {
      current.swims.splice(i, 1);
      if (addTarget === i) addTarget = null;
      editingPath = null;
      markDirty(); renderWorkout();
    }, 'btn-danger'));
    li.appendChild(acts);
  }

  const sub = document.createElement('ol');
  sub.className = 'sub-items';
  const swims = it.swims || [];
  if (!swims.length) {
    const e = document.createElement('li');
    e.className = 'empty';
    e.textContent = editMode ? 'Empty — tap “➕ swim” to add one.' : 'Empty set';
    sub.appendChild(e);
  } else {
    swims.forEach((s, j) => sub.appendChild(renderSwimItem(s, [i, j], `${i + 1}.${j + 1}`)));
  }
  li.appendChild(sub);

  // per-set totals: N × (one round) = total
  const r = setReps(it);
  const rt = swims.reduce((a, s) => a + (s.dur || 0), 0);
  const rd = swims.reduce((a, s) => a + calcDistance(s), 0);
  const foot = document.createElement('div');
  foot.className = 'set-foot';
  foot.textContent = `${r} × (${secsToMMSS(rt)} · ${rd} m) = ${secsToMMSS(r * rt)} · ${r * rd} m`;
  li.appendChild(foot);

  return li;
}

/* ---------- add / edit tools ---------- */
function updateAddTools() {
  $('addTools').hidden = !editMode;
  // Only offer sets when the firmware can actually store them.
  const setBtn = $('addSetBtn');
  if (setBtn) {
    setBtn.hidden = !setsSupported;
    setBtn.title = setsSupported ? '' : 'This device’s firmware does not support sets yet';
  }
  const lbl = $('addTargetLbl');
  const reset = $('addTargetReset');
  if (addTarget !== null && isSet(current && current.swims[addTarget])) {
    const st = current.swims[addTarget];
    lbl.textContent = st.note ? `set “${st.note}”` : `set ${addTarget + 1}`;
    reset.hidden = false;
  } else {
    addTarget = null;
    lbl.textContent = 'workout';
    reset.hidden = true;
  }
}

function beginEditSwim(path) {
  editingPath = path.slice();
  const s = getSwim(path);
  const el = $('addRow');
  if (el && typeof el.setMode === 'function') el.setMode('edit', 0, s);
  renderItems();
  el && el.scrollIntoView({ behavior: 'smooth', block: 'center' });
}

function cancelEditSwim() {
  editingPath = null;
  const el = $('addRow');
  if (el && typeof el.setMode === 'function') el.setMode('add');
}

function updateButtons() {
  $('saveBtn').disabled = !current || !dirty;
  $('startBtn').disabled = !current || dirty || !itemsOf(current).length;
  $('delBtn').disabled = !current;
}

/* ============================================================
   CRUD / actions
   ============================================================ */
function newWorkout() {
  const w = { id: Date.now().toString(), title: '', swims: [], __isNew: true };
  DB.workouts.push(w);
  location.hash = `#/w/${encodeURIComponent(w.id)}`;
}

const saveWorkout = async () => {
  if (!current || !dirty) return;
  current.title = $('titleIn').value.trim();
  const body = { id: current.id, title: current.title, swims: itemsOf(current) };
  try {
    const res = await fetch(`/api/workout?id=${encodeURIComponent(current.id)}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (!res.ok) { alert('Save failed: ' + res.status); return; }
  } catch (e) { alert('Save failed: ' + e); return; }
  delete current.__isNew;
  dirty = false;
  editMode = false;
  cancelEditSwim();
  await fetchAll();
  // re-point `current` at the refreshed copy
  const again = DB.workouts.find(x => String(x.id) === String(current.id));
  if (again) current = again;
  renderWorkout();
};

const deleteWorkout = async () => {
  if (!current) return;
  if (!confirm(`Delete workout “${current.title || current.id}”?`)) return;
  if (!current.__isNew) {
    try {
      await fetch(`/api/workout?id=${encodeURIComponent(current.id)}`, { method: 'DELETE' });
    } catch (e) { /* fall through – refresh below will show the truth */ }
  }
  DB.workouts = DB.workouts.filter(w => String(w.id) !== String(current.id));
  await fetchAll();
  location.hash = '#/';
};

async function startWorkout() {
  if (!current) return;
  try {
    const res = await fetch(`/api/run?id=${encodeURIComponent(current.id)}`, { method: 'POST' });
    if (!res.ok) { alert('Failed to start: ' + ((await res.text()) || res.statusText)); return; }
    sessionStorage.setItem('currentWorkouts', JSON.stringify(DB.workouts));
    window.location.href = `/run.html?id=${encodeURIComponent(current.id)}`;
  } catch (err) {
    alert('Failed to start: ' + err);
  }
}

/* ============================================================
   Wire up
   ============================================================ */
$('newBtn').addEventListener('click', newWorkout);
$('backBtn').addEventListener('click', () => { location.hash = '#/'; });
$('saveBtn').addEventListener('click', saveWorkout);
$('delBtn').addEventListener('click', deleteWorkout);
$('startBtn').addEventListener('click', startWorkout);

$('editBtn').addEventListener('click', () => {
  editMode = !editMode;
  if (!editMode) cancelEditSwim();
  renderWorkout();
});

$('titleIn').addEventListener('input', () => {
  if (!current) return;
  current.title = $('titleIn').value;
  $('wTitle').textContent = current.title || '(untitled)';
  markDirty();
});

$('addTargetReset').addEventListener('click', () => { addTarget = null; updateAddTools(); renderItems(); });

$('addSetBtn').addEventListener('click', () => {
  if (!current) return;
  if (!setsSupported) {
    alert('This device’s firmware does not support sets yet.\nFlash the latest firmware over USB to enable them.');
    return;
  }
  current.swims.push({ type: 'set', repeat: 2, note: '', swims: [] });
  addTarget = current.swims.length - 1;   // new swims go into the set we just made
  editingPath = null;
  markDirty();
  renderWorkout();
});

/* add-swim component events */
const addSwimEl = $('addRow');

addSwimEl.addEventListener('swim-add', ({ detail: swim }) => {
  if (!current) return;
  if (addTarget !== null && isSet(current.swims[addTarget])) {
    current.swims[addTarget].swims.push(swim);
  } else {
    current.swims.push(swim);
  }
  markDirty();
  renderWorkout();
});

addSwimEl.addEventListener('swim-update', ({ detail }) => {
  if (!current || !editingPath) return;
  setSwimAt(editingPath, detail.swim);
  editingPath = null;
  markDirty();
  renderWorkout();
});

/* If a run is already active, jump straight to the runner. */
window.addEventListener('DOMContentLoaded', () => {
  try {
    const es = new EventSource('/events');
    es.addEventListener('status', e => {
      let st; try { st = JSON.parse(e.data); } catch (_) { return; }
      if (st && st.running && !location.pathname.endsWith('run.html')) {
        sessionStorage.setItem('currentWorkouts', JSON.stringify(DB.workouts));
        window.location.href = `/run.html?id=${encodeURIComponent(current ? current.id : '')}`;
      }
    });
  } catch (e) { /* SSE unsupported – non-fatal */ }
});

window.addEventListener('hashchange', route);

/* ---------- init ---------- */
(async function init() {
  await detectCapabilities();
  await fetchAll();
  await route();
})();
