// — on page load, subscribe to any status event. If a run is active, go to runner:
window.addEventListener('DOMContentLoaded', () => {
  const es = new EventSource('/events');
  es.addEventListener('status', e => {
    const st = JSON.parse(e.data);
    // if a run is active, and we're still on the editor page, redirect:
    if (st.running && !location.pathname.endsWith('run.html')) {
      sessionStorage.setItem('currentWorkouts', JSON.stringify(DB.workouts));
      window.location.href = `/run.html?id=${current?.id || ''}`;
    }
  });
});

/* ---------- helpers ---------- */
const $ = id => document.getElementById(id);
const secsToMMSS = sec => {
  const m = Math.floor(sec / 60), s = sec % 60;
  return `${m}:${s.toString().padStart(2, '0')}`;
};
const mmssToSecs = txt => {
  const [m, s] = txt.split(':').map(Number);
  return (m || 0) * 60 + (s || 0);
};
const calcDistance = s => s.speed ? Math.round(s.dur * 100 / s.speed) : 0;

const editorPane = $('editorPane');
const pauseBtn = $('pauseBtn');
let dirty = false;          // ← tracks unsaved edits

const toggleSidebarBtn = $('toggleSidebarBtn');
const sidebar = $('sidebar');

function setSidebarVisibility(visible) {
  if (visible) {
    sidebar.style.display = 'block';
    toggleSidebarBtn.setAttribute('aria-expanded', 'true');
    toggleSidebarBtn.textContent = 'Hide Workouts';
  } else {
    sidebar.style.display = 'none';
    toggleSidebarBtn.setAttribute('aria-expanded', 'false');
    toggleSidebarBtn.textContent = 'Show Workouts';
  }
}

function updateEditorVisibility() {
  // Show editor pane if a workout is selected, regardless of edit mode
  editorPane.style.display = current ? 'block' : 'none';
  $('startSrv').disabled = !current;
  $('delBtn').disabled = !current;
  $('saveBtn').disabled = !current || !dirty;
}

/* ---------- state ---------- */
let DB = { workouts: [] };            // <-- initialise with empty array
let current = null, timer = null, paused = false;
let editMode = false;

/* ---------- DOM refs ---------- */
let workoutList = $('workoutList');
let titleIn = $('titleIn');
let tbody = $('swimTab').querySelector('tbody');
let runner = $('runner'), runNote = $('runNote'), runTime = $('runTime'), runDist = $('runDist'), queueDiv = $('queue');

/* ---------- live calculations ---------- */

function updateButtons() {
  $('saveBtn').disabled = !dirty;
  $('startSrv').disabled = dirty || !current;
}

async function togglePause() {
  const res = await fetch('/api/pause', { method: 'POST' });
  const json = await res.json();             // {paused: true|false}
  const paused = json.paused;

  pauseBtn.textContent = paused ? '▶ Resume' : '⏸ Pause';
}

/* ---------- CRUD ---------- */
const fetchAll = async () => {
  const ids = await (await fetch('/api/workouts')).json();   // ["1749…", "1750…"]
  const promises = ids.map(id =>
    fetch(`/api/workout?id=${id}`)
      .then(r => r.json())
      .catch(err => { console.error('Failed to load', id, err); return null; })
  );

  DB.workouts = (await Promise.all(promises)).filter(Boolean);  // drop failed
  renderList();
};
const renderList = () => {
  workoutList.innerHTML = '';
  DB.workouts?.forEach(w => {
    const li = document.createElement('li');
    li.textContent = w.title || '(untitled)';
    li.onclick = () => loadWorkout(w.id);
    workoutList.appendChild(li);
  });
  updateEditorVisibility();
};
const newWorkout = () => {
  current = { id: Date.now().toString(), title: '', swims: [] };
  dirty = true;
  editMode = true; // Immediately enter edit mode
  fillForm();
  redrawSwims();
  updateEditorVisibility(); // Show editor pane now
  updateButtons();
  updateEditModeUI && updateEditModeUI();

  // Hide sidebar and update toggle button
  setSidebarVisibility(false);
};

const loadWorkout = async id => {
  let w = DB.workouts.find(w => w.id === id);
  if (!w) {
    // not yet in memory – fetch on demand
    w = await (await fetch(`/${id}.json`)).json();
    DB.workouts.push(w);              // cache it for later
  }
  current = w;
  editMode = false; // Not in edit mode by default when loading
  updateEditorVisibility();
  fillForm();
  redrawSwims();
  dirty = false;
  updateButtons();
  updateEditModeUI && updateEditModeUI();

  // Ensure edit button is visible when workout is loaded and not in edit mode
  const editBtn = document.getElementById('editBtn');
  if (editBtn) {
    editBtn.style.display = (!editMode && current) ? '' : 'none';
  }
};
const fillForm = () => {
  titleIn.value = current?.title || '';
  $('paneTitle').textContent = current
    ? (editMode ? 'Edit workout' : 'Workout')
    : 'New workout';
};

let editedSwimIndex = null;

window.editSwim = function(index) {
  if (!current) return;
  const swim = current.swims[index];
  if (!swim) return;
  editedSwimIndex = index;
  editMode = true;
  redrawSwims();
  updateEditModeUI();
  updateButtons();
};

const redrawSwims = () => {
  tbody.innerHTML = '';
  if (!current) return;
  current.swims.forEach((s, i) => {
    const tr = tbody.insertRow();
    if (i === editedSwimIndex) {
      // Replace row content with add-swim component in edit mode
      const addSwimComponent = document.createElement('add-swim');
      addSwimComponent.setMode('edit', i, s);
      // Listen for swim-update event to update swim and exit edit mode
      addSwimComponent.addEventListener('swim-update', ({ detail: { index, swim } }) => {
        if (!current) return;
        if (typeof index !== 'number' || index < 0 || index >= current.swims.length) return;
        current.swims[index] = swim;
        editedSwimIndex = null;
        redrawSwims();
        dirty = true;
        updateButtons();
      });
      // Listen for swim-change event to update swim automatically
      addSwimComponent.addEventListener('swim-change', ({ detail: swim }) => {
        if (!current) return;
        if (typeof editedSwimIndex !== 'number' || editedSwimIndex < 0 || editedSwimIndex >= current.swims.length) return;
        current.swims[editedSwimIndex] = swim;
        dirty = true;
      });
      const td = tr.insertCell();
      td.colSpan = 6; // span all columns
      td.appendChild(addSwimComponent);
    } else {
      tr.insertCell().textContent = i + 1;
      tr.insertCell().textContent = s.speed ? secsToMMSS(s.speed) : 'rest';
      tr.insertCell().textContent = secsToMMSS(s.dur);
      tr.insertCell().textContent = `${calcDistance(s)}m`;
      tr.insertCell().textContent = s.note || '';
      const act = tr.insertCell();
      if (editMode) {
        act.innerHTML = `<button class="swim-btn" onclick="moveSwim(${i},-1)">↑</button>
                     <button class="swim-btn" onclick="moveSwim(${i},1)">↓</button>
                     <button class="swim-btn" onclick="delSwim(${i})">✖</button>
                     <button class="swim-btn" onclick="editSwim(${i})">✎</button>`;
      } else {
        act.innerHTML = '';
      }
    }
  });
  updateEditModeUI();
};
window.moveSwim = (i, d) => { if (!current) return; const s = current.swims; if (i + d < 0 || i + d >= s.length) return;[s[i], s[i + d]] = [s[i + d], s[i]]; redrawSwims(); dirty = true; updateButtons(); };
window.delSwim = i => { current.swims.splice(i, 1); redrawSwims(); dirty = true; updateButtons(); };

const addSwimEl = document.getElementById('addRow');

addSwimEl.addEventListener('swim-add', ({ detail: swim }) => {
  if (!current) return;
  current.swims.push(swim);
  redrawSwims();
  dirty = true;
  updateButtons();
});

addSwimEl.addEventListener('swim-update', ({ detail: { index, swim } }) => {
  if (!current) return;
  if (typeof index !== 'number' || index < 0 || index >= current.swims.length) return;
  current.swims[index] = swim;
  editedSwimIndex = null;
  redrawSwims();
  dirty = true;
  updateButtons();
});

function updateEditModeUI() {
  // Show/hide Actions column in table header
  const swimTab = document.getElementById('swimTab');
  if (swimTab) {
    const ths = swimTab.querySelectorAll('thead th');
    if (ths.length > 0) {
      // Actions is the last column
      ths[ths.length - 1].style.display = editMode ? '' : 'none';
    }
    // Show/hide Actions column in all rows
    const trs = swimTab.querySelectorAll('tbody tr');
    trs.forEach(tr => {
      if (tr.cells.length > 5) {
        tr.cells[5].style.display = editMode ? '' : 'none';
      }
    });
  }

  // Show/hide Add Swim element
  const addSwimEl = document.getElementById('addRow');
  if (addSwimEl) {
    // Show only if in edit mode and no swim is being edited
    addSwimEl.style.display = (editMode && editedSwimIndex === null) ? '' : 'none';
    if (addSwimEl.style.display === '') {
      addSwimEl.setMode('add');
    }
  }

  // Show/hide Edit button
  let editBtn = document.getElementById('editBtn');
  if (!editBtn) {
    editBtn = document.createElement('button');
    editBtn.id = 'editBtn';
    editBtn.textContent = '✎ Edit';
    editBtn.type = 'button';
    editBtn.style.marginLeft = '1em';
    editBtn.onclick = () => {
      editMode = true;
      updateEditModeUI();
      fillForm();
      redrawSwims();
      updateButtons();

      // Hide sidebar and update toggle button
      setSidebarVisibility(false);
    };
    // Insert after title input
    const titleIn = document.getElementById('titleIn');
    if (titleIn && titleIn.parentNode) {
      titleIn.parentNode.appendChild(editBtn);
    }
  }
  editBtn.style.display = (!editMode && current) ? '' : 'none';
}

const saveWorkout = async () => {
  if (!current || !dirty) return;
  current.title = titleIn.value.trim();
  const method = 'POST';
  const url = `/api/workout?id=${current.id}`;
  await fetch(url, { method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(current) });
  await fetchAll();
  dirty = false;
  editMode = false;
  updateEditModeUI();
  fillForm();
  redrawSwims();
  updateButtons();
};

const deleteWorkout = async () => {
  if (!current) return;
  /* native confirm dialog – return if user cancels */
  const ok = confirm(`Delete workout “${current.title || current.id}” ?`);
  if (!ok) return;

  await fetch(`/api/workout?id=${current.id}`, {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: current.id })
  });
  current = null;          // clear editor
  await fetchAll();        // <- refresh list & cache
  tbody.innerHTML = '';
  titleIn.value = '';
  updateEditorVisibility();
};

function resetSrvUI() {
  clearInterval(statusTimer);
  statusTimer = null;
  $('srvStatus').textContent = '';
  $('startSrv').disabled = !current;
  pauseBtn.hidden = true;
  pauseBtn.disabled = true;
}

/* -------- server-side simulation --------------------------------- */
let statusTimer = null;

async function startServerSide() {
  if (!current) return;

  // build URL with workout ID in the query parameter
  const url = `/api/run?id=${encodeURIComponent(current.id)}`;

  // fire the POST with no body and check response
  try {
    const res = await fetch(url, { method: 'POST' });
    if (res.ok) {
      // disable the “Start” button, enable “Pause”
      document.getElementById('startSrv').disabled = true;
      pauseBtn.hidden = false;
      pauseBtn.disabled = false;
      pauseBtn.textContent = '⏸ Pause';

      // store workouts and redirect into the runner page
      sessionStorage.setItem('currentWorkouts', JSON.stringify(DB.workouts));
      window.location.href = `/run.html?id=${encodeURIComponent(current.id)}`;
    } else {
      let errorMsg = '';
      try {
        errorMsg = (await res.text()) || res.statusText;
      } catch (e) {
        errorMsg = res.statusText;
      }
      alert('Failed to start: ' + errorMsg);
    }
  } catch (err) {
    alert('Failed to start: ' + err);
  }
}

async function pollStatus() {
  const st = await (await fetch('/api/status')).json();
  if (!st.active) { resetSrvUI(); return; }

  pauseBtn.textContent = st.paused ? '▶ Resume' : '⏸ Pause';

  const swim = current.swims[st.idx] || {};
  $('srvStatus').textContent =
    `${st.paused ? 'Paused – ' : ''}`
    + `Swim #${st.idx + 1} (${swim.note || (swim.speed ? 'swim' : 'rest')}) `
    + `elapsed ${Math.floor(st.elapsed / 1000)} s`;
}

/* ---------- wire UI ---------- */
$('newBtn').onclick = newWorkout;
$('saveBtn').onclick = saveWorkout;
$('delBtn').onclick = deleteWorkout;
$('startSrv').onclick = startServerSide;
pauseBtn.onclick = togglePause;

/* title box */
titleIn.oninput = () => { dirty = true; updateButtons(); };

/* in addSwim() right after current.swims.push(swim) */
dirty = true; updateButtons();

/* ---------- init ---------- */
fetchAll();
updateButtons();

toggleSidebarBtn.addEventListener('click', () => {
  setSidebarVisibility(sidebar.style.display !== 'block');
});
