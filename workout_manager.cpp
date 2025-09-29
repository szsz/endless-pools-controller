#include "workout_manager.h"
#include "web_ui.h"
#include <ArduinoJson.h>
#include "hub75.h"

static Workout current_workout_; // store currently active workout

void WorkoutManager::begin()
{
  WorkoutStorage::begin();
  // Clear currently active workout on start
  current_workout_ = Workout{};
}

bool WorkoutManager::run(const String &workout_id)
{
  Workout w;
  if (!WorkoutStorage::load(workout_id, w))
  {
    Serial.printf("WorkoutManager::run: failed to load workout id=%s\n", workout_id.c_str());
    return false; // Workout not found or failed to load
  }
  if (!SwimMachine::isMachineFound())
  {
    Serial.printf("Swim machine not found");
    return false;
  }

  current_workout_ = w;

  std::vector<SwimMachine::Segment> segments;
  for (const auto &step : w.steps)
  {
    SwimMachine::Segment seg;
    seg.pace100s = step.pace100s;
    seg.durSec = step.durSec;
    segments.push_back(seg);
  }

  SwimMachine::loadWorkout(segments);
  if (!SwimMachine::start())
  {
    Serial.printf("Could not start swim machine");
    return false;
  }

  push_status_();
  return true;
}

void WorkoutManager::pause()
{
  SwimMachine::pause();
  push_status_();
}

void WorkoutManager::stop()
{
  SwimMachine::stop();
  push_status_();
}

void WorkoutManager::tick()
{
  // Just push status, no internal timer needed
  push_status_();
}

void WorkoutManager::push_status_()
{
  SwimMachine::SwimStatus st = SwimMachine::getStatus();

  // Build status JSON on the heap to avoid loopTask stack overflow
  const size_t base = 2048;              // base for fixed fields
  const size_t per  = 64;                // per remaining_swims entry (approx)
  size_t cap = base + (current_workout_.steps.size() * per);
  if (cap > 6144) cap = 6144;            // clamp to a sane upper bound
  DynamicJsonDocument doc(cap);
  doc["running"] = st.active;
  doc["paused"] = st.paused;
  doc["current_step"] = st.idx;
  doc["elapsed_ms"] = st.elapsedMs;
  doc["workout_title"] = current_workout_.name;

  if (st.idx >= 0 && st.idx < (int)current_workout_.steps.size())
  {
    doc["current_step_note"] = current_workout_.steps[st.idx].note;
  }

  // Add remaining swims from current step onward
  JsonArray remaining = doc.createNestedArray("remaining_swims");
  if (st.idx >= 0)
    for (int i = st.idx; i < (int)current_workout_.steps.size(); ++i)
    {
      const auto &s = current_workout_.steps[i];
      JsonObject swim = remaining.createNestedObject();
      swim["pace100s"] = s.pace100s;
      swim["durSec"] = s.durSec;
      swim["note"] = s.note;
    }


  String out;
  serializeJson(doc, out);
  WebUI::push_event("status", out.c_str());

  // Update HUB75 display with seconds left in the current segment
  if (st.active && st.idx >= 0 && st.idx < (int)current_workout_.steps.size())
  {
    // Treat elapsed_ms as signed to handle initial negative lag (mirrors web UI logic)
    int32_t durMs = (int32_t)current_workout_.steps[st.idx].durSec * 1000;
    int32_t elapsedSigned = (int32_t)st.elapsedMs; // interpret uint32 as signed
    int32_t remainMs = durMs - elapsedSigned;
    if (remainMs < 0) remainMs = 0;
    int remSec = (remainMs + 999) / 1000; // ceil to seconds
    setCurrentText(String(remSec));
  }
}
