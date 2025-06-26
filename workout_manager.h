#pragma once

#include <Arduino.h>
#include <vector>
#include "workout_storage.h"
#include "swim_machine.h"

class WorkoutManager {
 public:
  static void begin();
  static void tick();          // call in loop()

  // UPDATED: accept an ID to run that specific workout
  static bool run(const String& workout_id);
  static void pause();
  static void stop();

 private:
  static void push_status_();
};
