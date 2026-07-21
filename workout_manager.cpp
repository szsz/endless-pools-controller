#include "workout_manager.h"
#include <Arduino.h>
#include "workout_storage.h"
#include "swim_machine.h"
#include "web_ui.h"
#include <ArduinoJson.h>
#define HUB75EBABLE
#ifdef HUB75EBABLE
#include "hub75.h"
#endif

#define DEBUGCRASH
#ifdef DEBUGCRASH
#include "esp_heap_caps.h"
// two-step stringify
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Use an inline function to keep the macro tiny & safe
inline void heapCheckHardImpl(const char* file, int line) {
  if (!heap_caps_check_integrity_all(true)) {
    Serial.printf("HEAP CORRUPTION DETECTED at %s:%d\n", file, line);
    abort();
  }
}

// This macro is now just a single function-like statement.
// You must end the call with a semicolon, e.g. HEAP_CHECK_HARD();
#define HEAP_CHECK_HARD() heapCheckHardImpl(__FILE__, __LINE__)
#endif

static Workout current_workout_; // store currently active workout
static std::vector<FlatStep> current_flat_; // active workout expanded to run-time steps (sets unrolled)


void WorkoutManager::begin()
{
#ifdef HUB75EBABLE
  setupHUB75();
#endif
#ifdef DEBUGCRASH
Serial.println("hub75");
HEAP_CHECK_HARD();
#endif
  WorkoutStorage::begin();
#ifdef DEBUGCRASH
Serial.println("workoutstorage");
HEAP_CHECK_HARD();
#endif
  // Clear currently active workout on start
  current_workout_ = Workout{};
  current_flat_.clear();
}

bool WorkoutManager::run(const String &workout_id)
{
  Workout w;
  if (!WorkoutStorage::load(workout_id, w))
  {
    Serial.printf("WorkoutManager::run: failed to load workout id=%s\n", workout_id.c_str());
    return false; // Workout not found or failed to load
  }
  // Apply the persisted dry-run setting to this run. In dry run nothing is ever
  // transmitted, so we also drop the "machine must be discovered" requirement —
  // that's the whole point: exercise a workout with no machine attached.
  const bool dryRun = AppSettings_getDryRun();
  SwimMachine::setDryRun(dryRun);

  if (!dryRun && !SwimMachine::isMachineFound())
  {
    Serial.printf("Swim machine not found");
    return false;
  }
  if (dryRun)
  {
    Serial.println("WorkoutManager: DRY RUN - swim machine will not be controlled");
  }

  current_workout_ = w;
  current_flat_ = WorkoutStorage::flatten(w);   // unroll sets into run-time steps

  std::vector<SwimMachine::Segment> segments;
  for (const auto &fs : current_flat_)
  {
    SwimMachine::Segment seg;
    seg.pace100s = fs.swim.pace100s;
    seg.durSec = fs.swim.durSec;
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


static bool s_active = false;
void WorkoutManager::tick()
{
#ifdef HUB75EBABLE
  // Keep the panel awake while a workout runs; otherwise count down to off.
  HUB75_screensaverTick(s_active);
#endif
  if(!s_active){
#ifdef HUB75EBABLE
static uint32_t prev2 =0;
  uint32_t now = millis();
  if(now > 250 && now<prev2+50)
    return;
  prev2 = now;
    drawSwimmerAnimationTick();
#endif
  }
  static uint32_t prev =0;
  uint32_t now = millis();
  if(now > 250 && now<prev+250)
    return;
  prev = now;
  // Just push status, no internal timer needed
  push_status_();
}

void WorkoutManager::push_status_()
{
  SwimMachine::SwimStatus st = SwimMachine::getStatus();

  // Build status JSON on the heap to avoid loopTask stack overflow
  const size_t base = 2048;              // base for fixed fields
  const size_t per  = 80;                // per remaining_swims entry (approx)
  size_t cap = base + (current_flat_.size() * per);
  if (cap > 6144) cap = 6144;            // clamp to a sane upper bound
  DynamicJsonDocument doc(cap);
  doc["running"] = st.active;
  s_active=st.active;
  doc["paused"] = st.paused;
  doc["current_step"] = st.idx;
  doc["elapsed_ms"] = st.elapsedMs;
  doc["workout_title"] = current_workout_.name;

  if (st.idx >= 0 && st.idx < (int)current_flat_.size())
  {
    const auto &cur = current_flat_[st.idx];
    doc["current_step_note"] = cur.swim.note;
    if (cur.setIndex >= 0)               // current step belongs to a set
    {
      doc["set_index"] = cur.setIndex;   // 0-based set number within the workout
      doc["set_rep"]   = cur.setRep;     // 1-based repetition ("2" of "2/3")
      doc["set_reps"]  = cur.setReps;    // total repetitions ("3" of "2/3")
      doc["set_pos"]   = cur.setPos;     // which exercise of the set is running
    }
  }

  // Add remaining swims from current step onward (sets already unrolled)
  JsonArray remaining = doc.createNestedArray("remaining_swims");
  if (st.idx >= 0)
    for (int i = st.idx; i < (int)current_flat_.size(); ++i)
    {
      const auto &fs = current_flat_[i];
      JsonObject swim = remaining.createNestedObject();
      swim["pace100s"] = fs.swim.pace100s;
      swim["durSec"]   = fs.swim.durSec;
      swim["note"]     = fs.swim.note;
      if (fs.setIndex >= 0)
      {
        swim["set_index"] = fs.setIndex;   // which set this step belongs to
        swim["set_rep"]  = fs.setRep;
        swim["set_reps"] = fs.setReps;
        swim["set_size"] = fs.setSize;     // exercises per repetition
        swim["set_pos"]  = fs.setPos;      // position within one repetition
        swim["set_name"] = fs.setName;     // shown when the swim has no note
      }
    }


  String out;
  serializeJson(doc, out);
  if(s_active){    
#ifdef HUB75EBABLE
    printJSon(doc);
#endif
  }
  WebUI::push_event("status", out.c_str());
  
}
