#include "swim_machine.h"
#include "UDPEventSender.h"
#include "NetworkSetup.h"
#include <vector>

/* ------------ internal state ----------------------------------- */
namespace
{
  std::vector<SwimMachine::Segment> workout;
  bool motorOn = false;
  bool machineFound = false;

  // Function pointer to push network event
  void (*push_network_event_func)(const uint8_t *data, size_t len) = nullptr;

  struct
  {
    int32_t idx = -1;      // current segment
    uint32_t segStart = 0; // ms at seg start
    bool active = false;
    bool paused = false;
  } sim;
}

/* ------------ multicast/control via UDPEventSender -------------- */

const IPAddress multicastAddr(239, 255, 0, 1);
const uint16_t multicastPort = 45654;

static IPAddress peer_ip(255, 255, 255, 255);
static uint16_t PEER_PORT = 9750;

// Two UDPEventSender instances:
// - mcast: joined to multicast group for incoming status/acks
// - ctrl:  unicast/broadcast for control commands to the swim machine
static UDPEventSender mcast(multicastAddr, multicastPort);
static UDPEventSender ctrl(peer_ip, PEER_PORT);

/* ------------ helpers: CRC-32 & monotonic tick ------------------ */
static uint32_t crc32(const uint8_t *d, size_t n)
{
  uint32_t c = 0xFFFFFFFF;
  while (n--)
  {
    c ^= *d++;
    for (int k = 0; k < 8; ++k)
      c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : (c >> 1);
  }
  return ~c;
}
static uint32_t monoTick()
{ // never repeats
  static uint32_t last = 0, now;
  now = millis();
  if (now <= last)
    now = last + 1;
  last = now;
  return last;
}

/* ------------ send queue and flow control ----------------------- */
// Robust ring buffer of command/param pairs to avoid out-of-bounds access
struct Msg { uint16_t cmd; uint16_t param; };
static Msg messagequeue[16];
static size_t q_head = 0; // index of next message to send
static size_t q_tail = 0; // index of next free slot (insertion point)

static uint8_t lastSentIdx2 = 0xC5; // so first increment is 0x64
static uint8_t idx2Counter = 0xC5;  // so first increment is 0x64
static bool readyToSendNext = true;
static bool resendLastPacket = false;
static uint8_t lastReceivedCmdByte = 0;

static void queuePkt(uint16_t command, uint16_t param = 0)
{
  const size_t N = sizeof(messagequeue) / sizeof(messagequeue[0]);
  // Write at tail
  messagequeue[q_tail].cmd = command;
  messagequeue[q_tail].param = param;
  // Advance tail
  size_t next_tail = (q_tail + 1) % N;
  // If buffer is full, drop the oldest (advance head)
  if (next_tail == q_head) {
    q_head = (q_head + 1) % N;
  }
  q_tail = next_tail;
}

static void confirmPacket(uint8_t idx2)
{
  if (readyToSendNext)
    return;
  static uint8_t lastReceivedIdx2 = 0;
  static int consecutiveMisses = 0;

  // Track if we received the expected idx2 value
  if (idx2 == lastSentIdx2)
  {
    consecutiveMisses = 0;
    lastReceivedIdx2 = idx2;
    readyToSendNext = true;
    resendLastPacket = false;

    // Consume the message that was just acknowledged
    const size_t N = sizeof(messagequeue) / sizeof(messagequeue[0]);
    if (q_head != q_tail) {
      q_head = (q_head + 1) % N;
    }
  }
  else
  {
    consecutiveMisses++;
    if (consecutiveMisses >= 2)
    {
      // Resend the last packet
      resendLastPacket = true;
      consecutiveMisses = 0;
    }
  }
}

/* ------------ high-level opcodes ------------------------------- */
void setDuration(uint16_t s) { queuePkt(0x25, s); }

void setPace(uint16_t p)
{
  static uint16_t lastpace = 0;
  if (lastpace != p)
    queuePkt(0x24, p);
  lastpace = p;
}
void sendStart() { queuePkt(0x1F); }
void sendStop() { queuePkt(0x21); }

void motorStart()
{
  if (!motorOn)
  {
    sendStart();
    motorOn = true;
  }
}
void motorStop()
{
  if (motorOn)
  {
    sendStop();
    motorOn = false;
  }
}

/* ------------ raw packet emitter ------------------------------- */
static void sendPkt()
{
  // Empty queue?
  if (q_head == q_tail)
    return;

  const size_t N = sizeof(messagequeue) / sizeof(messagequeue[0]);
  const Msg &msg = messagequeue[q_head];

  // do not send message when turning off or slowing down
  if ((lastReceivedCmdByte == 0x4E) ||
      (lastReceivedCmdByte == 0x0E) ||
      (lastReceivedCmdByte == 0x4A) ||
      (lastReceivedCmdByte == 0x0A))
    return;

  // Only send if ready or if a resend is required
  if (!readyToSendNext && !resendLastPacket)
    return;

  uint16_t command = msg.cmd;
  uint16_t param = msg.param;

  uint8_t idx2;
  if (resendLastPacket)
  {
    // For resend, use the last sent idx2
    idx2 = lastSentIdx2;
  }
  else
  {
    // For new packet, increment idx2Counter and wrap in [0x64, 0xC6]
    idx2Counter++;
    if (idx2Counter > 0xC6)
      idx2Counter = 0x64;
    if (idx2Counter < 0x64)
      idx2Counter = 0x64;
    idx2 = idx2Counter;
  }

  // Prepare packet
  uint8_t b[44];
  memset(b, 0, sizeof b);
  b[0] = 0x0A; // header
  b[1] = 0xF0;
  b[36] = 0x97; // footer
  b[37] = 0x01;
  b[2] = idx2;
  b[3] = command & 0xFF;
  b[4] = param & 0xFF;
  b[5] = param >> 8;
  uint32_t t = monoTick();
  memcpy(b + 32, &t, 4);
  uint32_t c = crc32(b, 40);
  memcpy(b + 40, &c, 4);

  // Send via UDPEventSender (control socket)
  (void)ctrl.sendBytes(b, sizeof b);

  if (push_network_event_func)
    push_network_event_func(b, sizeof b);

  // Update state
  lastSentIdx2 = idx2;
  readyToSendNext = false;
  resendLastPacket = false;
}

/* =================================================================
 *                    P U B L I C   A P I
 * ================================================================= */
void SwimMachine::begin(void (*push_network_event)(const uint8_t *data, size_t len))
{
  push_network_event_func = push_network_event;

  // Subscribe UDP senders to connection changes so they rebind automatically
  g_conn.subscribe([](bool ethHasIp, bool wifiHasIp, bool softApActive) {
    mcast.handleConnectionChange(ethHasIp, wifiHasIp, softApActive);
    ctrl.handleConnectionChange(ethHasIp, wifiHasIp, softApActive);
  });
  // Ensure UDP sockets are unbound before STA disconnects
  g_conn.subscribePreWifiStop([]() {
    mcast.unbind();
    ctrl.unbind();
  });

  // Join multicast group to receive status/acks
  mcast.onReceive([](const uint8_t *data, size_t len, const IPAddress &remote, uint16_t rport) {
    // Process only expected 111-byte packets
    if (len == 111)
    {
      lastReceivedCmdByte = data[3];
      confirmPacket(data[2]);

      if (push_network_event_func)
        push_network_event_func(data, len);

      if (!machineFound)
      {
        lastSentIdx2 = data[2] + 1;
        Serial.printf("Swim machine found at IP: %s\n", remote.toString().c_str());
        machineFound = true;
      }
    }
  });

  // Initialize sockets (rebinding handled by connection changes)
  mcast.begin(multicastAddr, multicastPort, 45654); // bind to group port
  ctrl.begin(peer_ip, PEER_PORT, 40000);            // distinct local port to avoid conflict
}

/* deep-copy caller’s data so it can go out of scope */
void SwimMachine::loadWorkout(const std::vector<SwimMachine::Segment> &segs)
{
  workout.clear();
  workout.insert(workout.end(), segs.begin(), segs.end()); // append the rest
  sim.idx = -1;
  sim.active = false;
  sim.paused = false;
}

/* --------------------------------------------------------------- */
bool SwimMachine::start()
{
  if (workout.empty())
  {
    return false;
  }

  /* send TOTAL ACTIVE duration once */
  uint16_t tot = 0;
  for (auto &s : workout)
    if (s.pace100s)
      tot += s.durSec;

  if (tot > 90 * 60 - 10)
  {
    return false; // too long workout.
  }
  setDuration(tot + 10);

  sim.idx = -1; // will advance on first tick()
  sim.segStart = millis();
  sim.active = true;
  sim.paused = false;
  return true;
}

/* toggle pause */
void SwimMachine::pause()
{
  if (!sim.active)
    return;
  sim.paused = !sim.paused;
  if (sim.paused)
  {
    motorStop();
    /* pause: keep elapsed time intact */
    sim.segStart = millis() - sim.segStart;
  }
  else
  {
    /* resume: keep elapsed time intact */
    sim.segStart = millis() - sim.segStart;
    if (workout[sim.idx].pace100s)
      motorStart();
  }
}

/* force stop */
void SwimMachine::stop()
{
  sim.active = false;
  sim.paused = false;
  sim.idx = -1;
  motorStop();
}

/* --------------------------------------------------------------- */
void SwimMachine::tick()
{
  static uint32_t prev =0;
  uint32_t now = millis();
  if(now > 250 && now<prev+250)
    return;
  prev = now;

  // Process UDP (receives multicast acks and keeps sockets rebound)
  mcast.loop();
  ctrl.loop();

  // Attempt send if ready
  sendPkt();

  if (!sim.active || sim.paused)
    return;


  /* start next segment if idx == -1 OR segment finished */
  if (sim.idx == -1 ||
      now - sim.segStart >= workout[sim.idx].durSec * 1000UL)
  {
    sim.idx++;
    if (sim.idx >= (int)workout.size())
    {
      stop();
      return;
    }

    Segment s = workout[sim.idx];
    sim.segStart = now;

    if (s.pace100s == 0)
    { // rest
      motorStop();
      // set future pace for faster transition
      if (sim.idx + 1 < (int)workout.size() && workout[sim.idx + 1].pace100s)
      {
        setPace(workout[sim.idx + 1].pace100s);
      }
    }
    else
    {
      setPace(s.pace100s);
      motorStart();
    }
  }
}

/* --------------------------------------------------------------- */

SwimMachine::SwimStatus SwimMachine::getStatus()
{
  SwimMachine::SwimStatus st;
  st.active = sim.active;
  st.found = machineFound;
  st.paused = sim.paused;
  st.idx = sim.idx;
  const uint32_t lag = 1000;
  st.elapsedMs = (sim.paused ? sim.segStart - lag : (millis() - sim.segStart - lag));
  return st;
}

bool SwimMachine::isMachineFound()
{
  return machineFound;
}

void SwimMachine::setPeerIP(IPAddress ip)
{
  peer_ip = ip;
  // Reconfigure control sender to new peer (keep distinct local port)
  ctrl.begin(peer_ip, PEER_PORT, 40000);
}
