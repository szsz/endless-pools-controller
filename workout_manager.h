#pragma once
#include <Arduino.h>

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
