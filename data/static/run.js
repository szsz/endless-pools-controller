const $ = (id) => document.getElementById(id);
const pauseBtn = $("pauseBtn");
const returnBtn = $("returnBtn");

// Parse ?id=workoutId from URL
const params = new URLSearchParams(location.search);
const workoutId = params.get("id");

const es = new EventSource("/events");

es.addEventListener("status", (e) => {
  const st = JSON.parse(e.data);

  pauseBtn.textContent = st.paused ? "▶ Resume" : "⏸ Pause";
  pauseBtn.setAttribute("aria-pressed", st.paused ? "true" : "false");

  $("runNote").textContent = st.current_step_note || ((st.pace == 0) ? "Rest" : "Swim");
  document.title = st.workout_title || "Workout Runner";

  let timeLeftMSecs = 0;
  if (st.remaining_swims && st.remaining_swims.length > 0) {
    const currentSwim = st.remaining_swims[0];
    timeLeftMSecs = Math.max(0, currentSwim.durSec*1000 - Math.floor(st.elapsed_ms));
  }
  let timeLeftSecs = Math.ceil(timeLeftMSecs / 1000);
  const mins = Math.floor(timeLeftSecs / 60).toString().padStart(2, '0');
  const secs = (timeLeftSecs % 60).toString().padStart(2, '0');
  $("runTime").textContent = `${mins}:${secs}`;

  if (st.remaining_swims && st.remaining_swims.length > 0) {
    const pace = st.remaining_swims[0].pace100s || 0;
    if (pace === 0) {
      $("runDist").textContent = "Rest";
      $("runPace").textContent = "";
    } else {
      const distLeft = Math.round((timeLeftMSecs / 10) / pace);
      $("runDist").textContent = distLeft + " m";
      // Show pace as mm:ss/100m
      const paceMin = Math.floor(pace / 60);
      const paceSec = pace % 60;
      $("runPace").textContent = `Pace: ${paceMin}:${paceSec.toString().padStart(2, "0")} /100m`;
    }
  } else {
    $("runDist").textContent = "";
    $("runPace").textContent = "";
  }

  let isSwim = false;
  if (st.remaining_swims && st.remaining_swims.length > 0) {
    const pace = st.remaining_swims[0].pace100s || 0;
    isSwim = pace !== 0;
  }
  window._isSwimMode = isSwim;
  updateTimeBigMode(isSwim);

  if (st.remaining_swims && st.remaining_swims.length > 1) {
    let tableHtml = `<table id="queueTable" aria-label="Upcoming swim steps">
      <thead>
        <tr>
          <th>Dist</th>
          <th>Pace</th>
          <th>Time</th>
          <th>Note</th>
        </tr>
      </thead><tbody>`;
    st.remaining_swims.slice(1).forEach((s) => {
      const isRest = s.pace100s === 0;
      const distance = isRest ? "Rest" : `${Math.round(s.durSec * 100 / s.pace100s)}m`;
      const paceMin = Math.floor((s.pace100s || 0) / 60);
      const paceSec = (s.pace100s || 0) % 60;
      const pace = isRest ? "—" : `${paceMin}:${paceSec.toString().padStart(2, "0")}`;
      const totalMin = Math.floor(s.durSec / 60);
      const totalSec = s.durSec % 60;
      const totalTime = `${totalMin}:${totalSec.toString().padStart(2, "0")}`;
      const note = s.note || "";

      tableHtml += `<tr>
        <td>${distance}</td>
        <td>${pace}</td>
        <td>${totalTime}</td>
        <td>${note}</td>
      </tr>`;
    });

    tableHtml += "</tbody></table>";
    $("queueList").innerHTML = tableHtml;
  } else {
    $("queueList").innerHTML = "<em>No upcoming steps</em>";
  }

  // Show/hide Pause and Return buttons based on workout running status
  if (!st.running) {
    pauseBtn.style.display = "none";
    returnBtn.style.display = "block";
  } else {
    pauseBtn.style.display = "inline-block";
    returnBtn.style.display = "none";
  }
  if (st.paused) {
    returnBtn.style.display = "block";
  }
});

pauseBtn.onclick = async () => {
  await fetch("/api/pause", { method: "POST" });
  // SSE updates status, so no UI update needed here
};

returnBtn.onclick = async () => {
  try {
    await fetch('/api/stop', { method: 'POST' });
  } catch (e) {
    console.error('Failed to stop workout:', e);
  }
  window.location.href = '/';
};
function updateTimeBigMode() {
  const isMobileLandscape = window.innerWidth > window.innerHeight;
  const runTime = $("runTime");
  if (isMobileLandscape) {
    runTime.classList.add("superbig");
  } else {
    runTime.classList.remove("superbig");
  }
}

// Add event listener to toggle on orientation change or resize
window.addEventListener("resize", updateTimeBigMode);
window.addEventListener("orientationchange", updateTimeBigMode);
