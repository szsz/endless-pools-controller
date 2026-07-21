#pragma once

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct SwimStep {
  uint16_t pace100s;   // seconds per 100 m (0 = rest)
  uint32_t durSec;     // duration in seconds
  String   note;       // user‐entered note
};

// A workout is an ordered list of items. Each item is either a single swim or a
// "set": a group of swims repeated `repeat` times. Sets do not nest (one level).
struct WorkoutItem {
  bool     isSet  = false;      // false = plain swim, true = a repeated set
  uint16_t repeat = 1;          // set repeat count (>= 1); ignored for a plain swim
  String   note;                // optional set label
  SwimStep swim{};              // valid when !isSet
  std::vector<SwimStep> swims;  // valid when isSet
};

struct Workout {
  String id;                    // unique workout ID
  String name;
  std::vector<WorkoutItem> items;   // authored structure (preserves sets)
};

// One run-time step after expanding every set, carrying set context so the
// run screen / LED panel can show "Set 2/3".
struct FlatStep {
  SwimStep swim;
  int16_t  setIndex = -1;   // -1 = not in a set; else 0-based index among the workout's sets
  uint16_t setRep   = 0;    // 1-based repetition within its set
  uint16_t setReps  = 0;    // total repetitions of its set
  uint16_t setSize  = 0;    // number of distinct swims in its set (one repetition)
  uint16_t setPos   = 0;    // 0-based position of this swim within one repetition
};

namespace WorkoutStorage {
  /** Initialise LittleFS; must be called once in setup(). */
  bool begin();

  /** List all workout IDs (reads /workouts/*.json). */
  std::vector<String> list_ids();

  /** Load a single workout by ID. Returns false if not found. */
  bool load(String id, Workout &out);

  /** Save or overwrite a workout (writes /workouts/w<ID>.json). */
  bool save(const Workout &w);

  /** Erase the workout file for this ID. */
  bool erase(String id);


  /** JSON <→> Workout codecs. */
  String  to_json(const Workout &w);

  bool from_json(const uint8_t *data, size_t len, Workout &w);

  /** Expand all sets into a flat run-time list (with set context per step). */
  std::vector<FlatStep> flatten(const Workout &w);
}
