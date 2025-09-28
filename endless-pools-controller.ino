#include <Arduino.h>
#include "swim_machine.h"
#include "workout_manager.h"
#include "web_ui.h"
#include "network.h"
#include "NetworkSetup.h"

void setup()
{
  Serial.begin(115200);
  delay(100);

  WebUI::begin();            // Wi-Fi + HTTP server
  Serial.println("web ui begin done");
  
  SwimMachine::begin(WebUI::push_network_event);   // initialise UDP + state machine with function pointer
  Serial.println("swim machine begin done");
  WorkoutManager::begin();   // load saved workouts & prefs
  Serial.println("workout manager begin done");
}

void loop()
{
  g_conn.loop();             // enforce connection policy and SoftAP control
  WebUI::loop();             // handles AsyncEventSource pings
  WorkoutManager::tick();    // 1 Hz countdown
  SwimMachine::tick();       // drive swim-machine protocol
  delay(250);
}
