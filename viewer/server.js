/* -----  server.js  -------------------------------------------------
   Run with:  node server.js
   Then open http://localhost:3000
--------------------------------------------------------------------*/
import fs        from 'fs';
import path      from 'path';
import { fileURLToPath } from 'url';
import dgram     from 'dgram';
import express   from 'express';
import cors      from 'cors';
import { createServer } from 'http';
import { Server as SocketIO } from 'socket.io';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

/* ---------- read commands.csv once and build a map --------------- */
const csv = fs.readFileSync(path.join('commands.csv'), 'utf8')
              .trim().split(/\r?\n/).slice(1);          // skip header
const COMMANDS = {};                                    // e.g. 0x24 → "Set Pace"
for (const line of csv) {
  const [hex, name] = line.split(/[,;]/);
  COMMANDS[parseInt(hex, 16)] = name.trim();
}

/* ---------------- basic Express + Socket-IO ---------------------- */
const app   = express();
app.use(cors());
const http  = createServer(app);
const io    = new SocketIO(http);
app.use(express.static(path.join(__dirname, 'public'))); // serves index.html robustly
http.listen(3000, () => console.log('open http://localhost:3000'));

/* -------------- tiny helper functions ---------------------------- */
let globalIndex = 0;                                    // monotonically increases

function leUint16(buf, off) { return buf[off] | (buf[off+1] << 8); }
function mmss(minByte, secByte) {
  const totalSeconds = minByte + secByte * 256;
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${seconds.toString().padStart(2, '0')}`;
}

/* -------------------- UDP listeners ------------------------------ */
function parse44(buf) {
  return {
    idx            : globalIndex++,                     // 0-based index
    port           : 9750,
    msgId          : buf[2],
    cmd            : buf[3],
    cmdText        : COMMANDS[buf[3]] ?? 'unknown',
    param          : leUint16(buf, 4),                  // LSB first
    timestamp      : new Date(leUint16(buf,32) | (buf[34]<<16) | (buf[35]<<24)), // seconds → JS Date
    hexPayload     : Buffer.from(buf).toString('hex')
  };
}

function parse111(buf) {
  // Timestamp is at bytes 71-74 (little-endian, seconds since epoch)
  const ts = buf[71] | (buf[72] << 8) | (buf[73] << 16) | (buf[74] << 24);
  return {
    idx            : globalIndex++,
    port           : 45654,
    msgId          : buf[2],
    cmd            : buf[3],
    cmdText        : COMMANDS[buf[3]] ?? 'unknown',
    curSpeed       : buf[4],
    tgtSpeed       : buf[5],
    pace           : mmss(buf[7],  buf[8]),
    remaining      : mmss(buf[11], buf[12]),
    runtimeSec     : buf.readFloatLE(23),
    totalRuntimeSec: buf.readFloatLE(27),
    timestamp      : new Date(ts * 1000),
    hexPayload     : Buffer.from(buf).toString('hex')
  };
}

function startUdp(port, size, parser) {
  const sock = dgram.createSocket('udp4');
  sock.on('message', (msg) => {
    if (msg.length === size) io.emit('udpMsg', parser(msg));
  });
  sock.bind(port, () => console.log(`listening UDP ${port}`));
}

startUdp( 9750,  44, parse44);
startUdp(45654, 111, parse111);
