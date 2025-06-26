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

struct Workout {
  String id;                 // unique workout ID
  String name;
  std::vector<SwimStep> steps;
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
}
