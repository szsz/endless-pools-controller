const tbody = document.querySelector('#tbl tbody');
const MAX   = 10000;
const rows  = [];                   // circular buffer of <tr> elements
const msgs  = [];                   // circular buffer of msg objects for comparison

let autoScroll = true;
const wrap = document.getElementById('wrap');
const autoScrollBtn = document.getElementById('autoScrollBtn');

autoScrollBtn.addEventListener('click', () => {
  autoScroll = !autoScroll;
  autoScrollBtn.textContent = 'Auto-Scroll: ' + (autoScroll ? 'ON' : 'OFF');
});

wrap.addEventListener('scroll', () => {
  // If user scrolls up, turn off auto-scroll
  const atBottom = wrap.scrollTop + wrap.clientHeight >= wrap.scrollHeight - 2;
  if (!atBottom && autoScroll) {
    autoScroll = false;
    autoScrollBtn.textContent = 'Auto-Scroll: OFF';
  } else if (atBottom && !autoScroll) {
    autoScroll = true;
    autoScrollBtn.textContent = 'Auto-Scroll: ON';
  }
});

// Helper function to copy hex string to clipboard
function copyHex(hex) {
  navigator.clipboard.writeText(hex).catch(() => {
    // fallback for older browsers
    const textarea = document.createElement('textarea');
    textarea.value = hex;
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    document.body.removeChild(textarea);
  });
}

// Function to compare all fields except idx and timestamp (and hexPayload, which is for copy)
function areRowsEqual(a, b) {
  if (!a || !b) return false;
  return (
    a.port === b.port &&
    a.msgId === b.msgId &&
    a.cmdText === b.cmdText &&
    (a.param ?? '') === (b.param ?? '') &&
    (a.curSpeed ?? '') === (b.curSpeed ?? '') &&
    (a.tgtSpeed ?? '') === (b.tgtSpeed ?? '') &&
    (a.pace ?? '') === (b.pace ?? '') &&
    (a.remaining ?? '') === (b.remaining ?? '') &&
    (a.runtimeSec?.toFixed?.(1) ?? '') === (b.runtimeSec?.toFixed?.(1) ?? '') &&
    (a.totalRuntimeSec?.toFixed?.(1) ?? '') === (b.totalRuntimeSec?.toFixed?.(1) ?? '')
  );
}

// New function to check if rows are equal except for runtimeSec and remaining
function areRowsEqualExceptRuntime(a, b) {
  if (!a || !b) return false;
  return (
    a.port === b.port &&
    a.msgId === b.msgId &&
    a.cmdText === b.cmdText &&
    (a.param ?? '') === (b.param ?? '') &&
    (a.curSpeed ?? '') === (b.curSpeed ?? '') &&
    (a.tgtSpeed ?? '') === (b.tgtSpeed ?? '') &&
    (a.pace ?? '') === (b.pace ?? '')
    // runtimeSec, totalRuntimeSec, and remaining are ignored here
  );
}

// Helper functions to convert mm:ss string to seconds and back
function remainingToSeconds(remainStr) {
  if (!remainStr) return 0;
  const parts = remainStr.split(':');
  if (parts.length !== 2) return 0;
  const minutes = parseInt(parts[0], 10);
  const seconds = parseInt(parts[1], 10);
  if (isNaN(minutes) || isNaN(seconds)) return 0;
  return minutes * 60 + seconds;
}

function secondsToRemaining(seconds) {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return m + ":" + s.toString().padStart(2, "0");
}

let repeatCount = 1;

// To track min and max runtime and remaining values for the last row
let lastRuntimeSecMin = null;
let lastRuntimeSecMax = null;
let lastRemainingMin = null;
let lastRemainingMax = null;

function add(msg) {
  // Lookup command text from commandMap using msgId hex string
  const msgIdHex = msg.msgId !== undefined ? msg.msgId.toString(16).toLowerCase().padStart(2, '0') : '';

  const prevMsg = msgs[msgs.length - 1];
  if (areRowsEqual(msg, prevMsg)) {
    repeatCount++;
    if (rows.length > 0) {
      rows[rows.length - 1].children[0].textContent = repeatCount;
    }
  } else if (areRowsEqualExceptRuntime(msg, prevMsg)) {
    // Only runtimeSec and remaining differ
    const runtimeSec = msg.runtimeSec ?? 0;
    const remainingSec = remainingToSeconds(msg.remaining ?? '');

    if (lastRuntimeSecMin === null || runtimeSec < lastRuntimeSecMin) lastRuntimeSecMin = runtimeSec;
    if (lastRuntimeSecMax === null || runtimeSec > lastRuntimeSecMax) lastRuntimeSecMax = runtimeSec;
    if (lastRemainingMin === null || remainingSec < lastRemainingMin) lastRemainingMin = remainingSec;
    if (lastRemainingMax === null || remainingSec > lastRemainingMax) lastRemainingMax = remainingSec;

    repeatCount++;
    if (rows.length > 0) {
      const lastRow = rows[rows.length - 1];
      lastRow.children[0].textContent = repeatCount;

      // Update runtimeSec and remaining columns to show min - max
      lastRow.children[10].textContent = lastRuntimeSecMin.toFixed(1) + ' - ' + lastRuntimeSecMax.toFixed(1);
      lastRow.children[9].textContent = secondsToRemaining(lastRemainingMin) + ' - ' + secondsToRemaining(lastRemainingMax);
    }
  } else {
    // Reset min/max trackers for new row
    lastRuntimeSecMin = msg.runtimeSec ?? 0;
    lastRuntimeSecMax = msg.runtimeSec ?? 0;
    lastRemainingMin = remainingToSeconds(msg.remaining ?? '');
    lastRemainingMax = remainingToSeconds(msg.remaining ?? '');

    repeatCount = 1;
    const tr = document.createElement('tr');
    tr.innerHTML = 
      "<td>1</td>" +
      "<td>" + msg.port + "</td>" +
      "<td>" + (msg.msgId !== undefined ? "0x" + msg.msgId.toString(16).toUpperCase().padStart(2, "0") : "") + "</td>" +
      "<td>" + msg.cmdText + "</td>" +
      "<td>" + (msg.param ?? "") + "</td>" +
      "<td>" + (msg.timestamp ? new Date(msg.timestamp).toLocaleString() : "") + "</td>" +
      "<td>" + (msg.curSpeed ?? "") + "</td>" +
      "<td>" + (msg.tgtSpeed ?? "") + "</td>" +
      "<td>" + (msg.pace ?? "") + "</td>" +
      "<td>" + (msg.remaining ?? "") + "</td>" +
      "<td>" + (msg.runtimeSec?.toFixed?.(1) ?? "") + "</td>" +
      "<td></td>" +
      "<td><button onclick=\"copyHex('" + msg.hexPayload + "')\">Copy</button></td>";
    tbody.appendChild(tr);
    rows.push(tr);
    msgs.push(msg);

    if (rows.length > MAX) {
      tbody.removeChild(rows.shift());
      msgs.shift();
    }
    if (autoScroll) wrap.scrollTop = tbody.scrollHeight;
  }
}

// Commands CSV and parsing
const commandsCsv = `Value,Command
00,heartbeat during workout pause
08,ildle off
0a,end of swim
0b,idle increasing spead
0e,idle turning off
0f,idle on
1f,Start
21,Stop
22,pause workout
23,unpause workout
24,Set Pace
25,Set Duration
27,Intitilize workout2
33,Initialize workout
34,add swim to workout
35,start workout
36,cancel workout
40,got 33
48,got  info off
49,turning on
4a,slowing down
4b,increasing speed turning on
4e,turning off
4f,got info on
`;

function parseCommands(csv) {
  const lines = csv.trim().split('\n');
  const map = new Map();
  for (let i = 1; i < lines.length; i++) {
    const [value, command] = lines[i].split(',');
    map.set(value.toLowerCase(), command);
  }
  return map;
}

const commandMap = parseCommands(commandsCsv);

// Helper functions to parse hex string to byte array and parse messages
function hexToBytes(hex) {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i++) {
    bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
  }
  return bytes;
}

function leUint16(buf, off) {
  return buf[off] | (buf[off + 1] << 8);
}

function mmss(minByte, secByte) {
  const totalSeconds = minByte + secByte * 256;
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return minutes + ":" + seconds.toString().padStart(2, "0");
}

let globalIndex = 0;

function parse44(buf) {
  return {
    idx: globalIndex++,
    port: 9750,
    msgId: buf[2],
    cmd: buf[3],
    cmdText: commandMap.get(buf[3].toString(16).padStart(2, '0')) || 'unknown',
    param: leUint16(buf, 4),
    timestamp: new Date((leUint16(buf, 32) | (buf[34] << 16) | (buf[35] << 24)) * 1000),
    hexPayload: Array.from(buf).map(b => b.toString(16).padStart(2, '0')).join(''),
    is44Byte: true
  };
}

function parse111(buf) {
  const ts = buf[71] | (buf[72] << 8) | (buf[73] << 16) | (buf[74] << 24);
  return {
    idx: globalIndex++,
    port: 45654,
    msgId: buf[2],
    cmd: buf[3],
    cmdText: commandMap.get(buf[3].toString(16).padStart(2, '0')) || 'unknown',
    curSpeed: buf[4],
    tgtSpeed: buf[5],
    pace: mmss(buf[7], buf[8]),
    remaining: mmss(buf[11], buf[12]),
    runtimeSec: new DataView(buf.buffer).getFloat32(23, true),
    totalRuntimeSec: new DataView(buf.buffer).getFloat32(27, true),
    timestamp: new Date(ts * 1000),
    hexPayload: Array.from(buf).map(b => b.toString(16).padStart(2, '0')).join('')
  };
}

// Connect to /network SSE endpoint
const networkSse = new EventSource('/events');

networkSse.onopen = function() {
  console.log('SSE connection opened');
};

networkSse.onerror = function(e) {
  console.error('SSE error:', e);
};

networkSse.addEventListener('network', function(event) {
  try {
    const data = JSON.parse(event.data);
    if (data.packet) {
      const buf = hexToBytes(data.packet);
      let msg = null;
      if (buf.length === 44) {
        msg = parse44(buf);
      } else if (buf.length === 111) {
        msg = parse111(buf);
      }
      if (msg) {
        add(msg);
      }
    }
  } catch (e) {
    console.error('Failed to parse network SSE data:', e);
  }
});
