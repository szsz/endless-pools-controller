#pragma once
#include <Arduino.h>
#include <vector>
#include <WiFi.h>

namespace SwimMachine
{

  /* -------- one swim block --------------------------------------- */
  struct Segment
  {
    uint16_t pace100s; // seconds / 100 m; 0 = rest
    uint16_t durSec;   // seconds
  };

  /* -------- status snapshot -------------------------------------- */
    struct SwimStatus
    {
      bool active;        // workout started & not finished
      bool found;
      bool paused;        // true while paused
      int32_t idx;        // current segment, −1 = none
      uint32_t elapsedMs; // ms elapsed inside current segment
    };

  /* -------- public API ------------------------------------------- */
  void begin(void (*push_network_event)(const uint8_t *data, size_t len)); // call in setup()
  void loadWorkout(const std::vector<Segment> &); // copy full list
  bool start();                                   // begin playback
  void pause();                                   // toggle pause/resume
  void stop();                                    // abort workout
  void tick();                                    // call in loop()
  void setPeerIP(IPAddress ip);
  bool isMachineFound();

  SwimStatus getStatus(); // query live state

} // namespace SwimMachine
