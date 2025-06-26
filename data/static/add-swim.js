// /static/add-swim.js – Web Component for composing a single swim entry
// ---------------------------------------------------------------------
// Distance slider: 25 m → 1500 m (UI step 25)
//   • ≤ 200 m  → 25 m increments
//   • 200–400 m → 50 m increments
//   • ≥ 400 m → 100 m increments (max 1500 m)
// Time slider: 10 s → 3600 s (60 min) – UI step 1 s
//   • ≤ 3 min  (≤ 180 s) → 10 s increments
//   • ≤ 10 min (≤ 600 s) → 30 s increments
//   • ≤ 20 min (≤ 1200 s) → 60 s increments
//   • ≤ 60 min (= 3600 s) → 300 s increments
// Pace slider: **min 71 s (1:11) → max 180 s** per 100 m, step 1 s
// Rest checkbox scaled to 160% size.
// Time ± buttons support continuous press (repeat every 200 ms).
// Emits `CustomEvent('swim-add', {detail:{speed,dur,note}})`

const tpl = document.createElement('template');
tpl.innerHTML = /*html*/`
  <style>
    @import "/static/style.css";
    :host { display: block; }

    .field {
      margin: .3rem 0;
      display: grid;
      grid-template-columns: auto auto 1fr auto auto; /* label, (−/checkbox), slider, (+), value */
      gap: .35rem;
      align-items: center;
    }
    label { font-weight: 500; grid-column: 1/2; }

    .btn-step {
      width: 1.6rem; height: 1.6rem;
      font-weight: 700;
      display: flex; align-items: center; justify-content: center;
      background: var(--gray-200,#eee);
      border: 0; border-radius: .25rem;
      cursor: pointer;
      user-select: none;
      line-height: 1;
    }
    input[type="range"] { width: 100%; }
    .val { font-size: .8rem; color: var(--gray-600,#555); white-space: nowrap; }

    /* enlarge rest checkbox */
    input[type="checkbox"]#rest {
      transform: scale(1.6);
      transform-origin: center;
    }

    button.add {
      margin-top: .5rem;
      padding: .4rem 1rem;
      font-size: 1rem;
    }
  </style>

  <!-- Distance -->
  <div class="field">
    <label for="dist">Distance</label>
    <button id="distMinus" class="btn-step" type="button">−</button>
    <input id="dist" type="range" min="25" max="1500" step="25" value="100" />
    <button id="distPlus" class="btn-step" type="button">+</button>
    <span id="distVal" class="val">100&nbsp;m</span>
  </div>

  <!-- Pace -->
  <div class="field">
    <label for="pace">Pace</label>
    <button id="paceMinus" class="btn-step" type="button">−</button>
    <input id="pace" type="range" min="71" max="180" step="1" value="90" />
    <button id="pacePlus" class="btn-step" type="button">+</button>
    <span id="paceVal" class="val">1:30&nbsp;/&nbsp;100&nbsp;m</span>
  </div>

  <!-- Time with ± buttons -->
  <div class="field">
    <label for="time">Time</label>
    <button id="tMinus" class="btn-step" type="button">−</button>
    <input id="time" type="range" min="10" max="3600" step="1" value="90" />
    <button id="tPlus"  class="btn-step" type="button">+</button>
    <span id="timeVal" class="val">1:30</span>
  </div>

  <!-- Rest -->
  <div class="field">
    <label for="rest">Rest</label>
    <input id="rest" type="checkbox">
    <span></span><span></span><span></span>
  </div>

  <!-- Note -->
  <div class="field">
    <label for="note">Note</label>
    <span></span>
    <input id="note" type="text" placeholder="e.g. Drill" />
    <span></span><span></span>
  </div>

  <button id="add" class="add">➕&nbsp;Add</button>
`;


class AddSwim extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: 'open' }).appendChild(tpl.content.cloneNode(true));

    // refs
    const $ = id => this.shadowRoot.getElementById(id);
    this.dist = $("dist");
    this.pace = $("pace");
    this.time = $("time");
    this.tMinus = $("tMinus");
    this.tPlus = $("tPlus");
    this.rest = $("rest");
    this.note = $("note");
    this.addBtn = $("add");
    this.distVal = $("distVal");
    this.paceVal = $("paceVal");
    this.timeVal = $("timeVal");

    this.distMinus = $("distMinus");
    this.distPlus = $("distPlus");
    this.paceMinus = $("paceMinus");
    this.pacePlus = $("pacePlus");

    // precise values (metres & seconds)
    this.realDist = +this.dist.value;
    this.realTime = +this.time.value;

    this.mode = 'add'; // 'add' or 'edit'
    this.editIndex = null; // index of swim being edited

    let lock = false; // re‑entrancy guard;

    // helpers
    const fmtSecs = s => `${Math.floor(s / 60)}:${(s % 60).toString().padStart(2, '0')}`;

    const clamp = (v, min, max) => Math.min(Math.max(v, min), max);

    // distance snapping
    const snapDist = d => {
      d = clamp(d, 25, 1500);
      if (d <= 200) return Math.round(d / 25) * 25;
      if (d <= 400) return Math.round(d / 50) * 50;
      return Math.round(d / 100) * 100;
    };

    // time snapping
    const snapTime = t => {
      t = clamp(t, 10, 3600);
      if (t <= 180) return Math.round(t / 10) * 10;   // ≤3 min
      if (t <= 600) return Math.round(t / 30) * 30;   // ≤10 min
      if (t <= 1200) return Math.round(t / 60) * 60;   // ≤20 min
      return Math.round(t / 300) * 300;                  // ≤60 min
    };

    const updateLabels = () => {
      this.distVal.textContent = this.rest.checked ? '— rest —' : `${this.realDist}m`;
      this.paceVal.textContent = this.rest.checked ? '— rest —' : `${fmtSecs(+this.pace.value)} / 100m`;
      this.timeVal.textContent = fmtSecs(this.realTime);
    };


    // events

    // Helper to dispatch change event with current swim data
    const dispatchChangeEvent = () => {
      const swim = {
        speed: this.rest.checked ? 0 : +this.pace.value,
        dur: this.realTime,
        note: this.note.value.trim()
      };
      this.dispatchEvent(new CustomEvent('swim-change', { detail: swim, bubbles: true }));
    };

    this.pace.oninput = () => { this.updateLabels(); this.recalcTimeFromDist(this.realDist); dispatchChangeEvent(); };

    this.dist.oninput = () => {
      const snapped = this.snapDist(+this.dist.value);
      this.recalcTimeFromDist(snapped);
      dispatchChangeEvent();
    };

    this.time.oninput = () => {
      const snapped = this.snapTime(+this.time.value);
      this.recalcDistFromTime(snapped);
      dispatchChangeEvent();
    };

    this.note.oninput = () => {
      dispatchChangeEvent();
    };

    // ± buttons with hold‑repeat
    let holdInt = null;
    const startHold = (bumpFunc, delta) => {
      bumpFunc(delta); // immediate
      holdInt = setInterval(() => bumpFunc(delta), 200);
    };
    const stopHold = () => clearInterval(holdInt);

    const bumpTime = delta => {
      const newTime = this.clamp(this.realTime + delta, 10, 3600);
      this.recalcDistFromTime(newTime);
      this.time.value = this.snapTime(newTime);
      dispatchChangeEvent();
    };

    const getNextSnappedDist = (current, delta) => {
      // Generate possible snapped values in range
      const snappedValues = [];
      for (let d = 25; d <= 1500; d += 25) {
        let snapped;
        if (d <= 200) snapped = Math.round(d / 25) * 25;
        else if (d <= 400) snapped = Math.round(d / 50) * 50;
        else snapped = Math.round(d / 100) * 100;
        if (!snappedValues.includes(snapped)) snappedValues.push(snapped);
      }
      snappedValues.sort((a,b) => a-b);
      // Find current index or closest
      let idx = snappedValues.findIndex(v => v >= current);
      if (idx === -1) idx = snappedValues.length - 1;
      if (delta > 0) {
        // Move to next higher snapped value if possible
        if (idx < snappedValues.length - 1) return snappedValues[idx + 1];
        return snappedValues[snappedValues.length - 1];
      } else {
        // Move to next lower snapped value if possible
        if (idx > 0) return snappedValues[idx - 1];
        return snappedValues[0];
      }
    };

    const bumpDist = delta => {
      let newDist = getNextSnappedDist(this.realDist, delta);
      newDist = this.clamp(newDist, 25, 1500);
      this.recalcTimeFromDist(newDist);
      this.dist.value = newDist;
      dispatchChangeEvent();
    };

    const bumpPace = delta => {
      let newPace = this.clamp(+this.pace.value + delta, 71, 180);
      this.pace.value = newPace;
      this.updateLabels();
      this.recalcTimeFromDist(this.realDist);
      dispatchChangeEvent();
    };

    // attach listeners for continuous press
    [[this.tMinus, bumpTime, -1], [this.tPlus, bumpTime, 1]].forEach(([btn, func, delta]) => {
      btn.addEventListener('mousedown', () => startHold(func, delta));
      btn.addEventListener('touchstart', e => { e.preventDefault(); startHold(func, delta); });
      ['mouseup', 'mouseleave', 'touchend', 'touchcancel'].forEach(ev =>
        btn.addEventListener(ev, stopHold));
    });

    [[this.distMinus, bumpDist, -1], [this.distPlus, bumpDist, 1]].forEach(([btn, func, delta]) => {
      btn.addEventListener('mousedown', () => startHold(func, delta));
      btn.addEventListener('touchstart', e => { e.preventDefault(); startHold(func, delta); });
      ['mouseup', 'mouseleave', 'touchend', 'touchcancel'].forEach(ev =>
        btn.addEventListener(ev, stopHold));
    });

    [[this.paceMinus, bumpPace, -1], [this.pacePlus, bumpPace, 1]].forEach(([btn, func, delta]) => {
      btn.addEventListener('mousedown', () => startHold(func, delta));
      btn.addEventListener('touchstart', e => { e.preventDefault(); startHold(func, delta); });
      ['mouseup', 'mouseleave', 'touchend', 'touchcancel'].forEach(ev =>
        btn.addEventListener(ev, stopHold));
    });

this.rest.onchange = () => {
  const r = this.rest.checked;
  this.pace.disabled = this.dist.disabled = r;
  this.recalcDistFromTime(this.realTime);
  this.updateLabels();
  dispatchChangeEvent();
};

    this.addBtn.onclick = () => {
      if (this.mode === 'add') {
        const swim = {
          speed: this.rest.checked ? 0 : +this.pace.value,
          dur: this.realTime,
          note: this.note.value.trim()
        };
        this.dispatchEvent(new CustomEvent('swim-add', { detail: swim, bubbles: true }));
        this.reset();
      } else if (this.mode === 'edit') {
        const swim = {
          speed: this.rest.checked ? 0 : +this.pace.value,
          dur: this.realTime,
          note: this.note.value.trim()
        };
        this.dispatchEvent(new CustomEvent('swim-update', { detail: { index: this.editIndex, swim }, bubbles: true }));
        this.reset();
        this.setMode('add');
      }
    };

    // init labels
    updateLabels();

    // Set initial button text
    this.updateButtonText();
  }

  updateButtonText() {
    if (this.mode === 'add') {
      this.addBtn.textContent = '➕\u00A0Add';
    } else if (this.mode === 'edit') {
      this.addBtn.textContent = '✔\u00A0Update';
    }
  }

  fmtSecs(s) {
    return `${Math.floor(s / 60)}:${(s % 60).toString().padStart(2, '0')}`;
  }

  clamp(v, min, max) {
    return Math.min(Math.max(v, min), max);
  }

  snapDist(d) {
    d = this.clamp(d, 25, 1500);
    if (d <= 200) return Math.round(d / 25) * 25;
    if (d <= 400) return Math.round(d / 50) * 50;
    return Math.round(d / 100) * 100;
  }

  snapTime(t) {
    t = this.clamp(t, 10, 3600);
    if (t <= 180) return Math.round(t / 10) * 10;   // ≤3 min
    if (t <= 600) return Math.round(t / 30) * 30;   // ≤10 min
    if (t <= 1200) return Math.round(t / 60) * 60;  // ≤20 min
    return Math.round(t / 300) * 300;                // ≤60 min
  }

  updateLabels() {
    this.distVal.textContent = this.rest.checked ? '— rest —' : `${this.realDist}m`;
    this.paceVal.textContent = this.rest.checked ? '— rest —' : `${this.fmtSecs(+this.pace.value)} / 100m`;
    this.timeVal.textContent = this.fmtSecs(this.realTime);
  }

  recalcTimeFromDist(distVal) {
    if (this._lock) return;
    this._lock = true;
    this.realDist = distVal;
    const exactTime = Math.round(distVal * this.pace.value / 100);
    this.realTime = exactTime;
    this.time.value = this.snapTime(this.realTime);
    this.updateLabels();
    this._lock = false;
  }

  recalcDistFromTime(timeVal) {
    if (this._lock) return;
    this._lock = true;
    this.realTime = timeVal;
    if (!this.rest.checked) {
      const exactDist = Math.round(timeVal * 100 / this.pace.value);
      this.realDist = exactDist;
      this.dist.value = this.snapDist(this.realDist);
    }
    this.updateLabels();
    this._lock = false;
  }

  setMode(mode, index = null, swim = null) {
    this.mode = mode;
    this.editIndex = index;
    if (mode === 'edit' && swim) {
      // Load swim data into inputs
if (swim.speed === 0) {
  this.rest.checked = true;
  this.pace.disabled = true;
  this.dist.disabled = true;
} else {
  this.rest.checked = false;
  this.pace.disabled = false;
  this.dist.disabled = false;
  this.pace.value = swim.speed;
  this.realTime = swim.dur;
  this.recalcDistFromTime(swim.dur);
}
      
      this.note.value = swim.note || '';
    } else if (mode === 'add') {
      this.reset();
    }
    this.updateButtonText();
  }

  reset() {
    this.realDist = 100;
    this.realTime = 90;
    this.dist.value = 100;
    this.time.value = 90;
    this.pace.value = 90;
    this.note.value = '';
    this.rest.checked = false;
    this.pace.disabled = this.dist.disabled = false;
    this.shadowRoot.querySelectorAll('input[type="range"]').forEach(r => r.dispatchEvent(new Event('input')));
    this.mode = 'add';
    this.editIndex = null;
    this.updateButtonText();
  }
}
customElements.define('add-swim', AddSwim);
