//#define NOUDPTEST
#define SWIMMACHINE

#include <Arduino.h>
#ifdef SWIMMACHINE
#include "swim_machine.h"
#include "workout_manager.h"
#endif
#include "web_ui.h"

//#define MEMTEST
#ifdef MEMTEST
//static byte g[180000];
//static byte _h[40000];
#endif
void setup()
{
  Serial.begin(115200);
  delay(100);


  WebUI::begin();            // Wi-Fi + HTTP server
  Serial.println("web ui begin done");
  
  
#ifdef SWIMMACHINE
  SwimMachine::begin(WebUI::push_network_event);   // initialise UDP + state machine with function pointer
  Serial.println("swim machine begin done");
  WorkoutManager::begin();   // load saved workouts & prefs
  Serial.println("workout manager begin done");
  #endif
}

void loop()
{
#ifdef MEMTEST
 int * memleak = new int[2];
#endif
  WebUI::loop();             // handles AsyncEventSource pings
 

#ifdef SWIMMACHINE
  WorkoutManager::tick();    // 1 Hz countdown
  SwimMachine::tick();       // drive swim-machine protocol
#endif
  static uint32_t lastMemLogMs = 0;
  uint32_t now = millis();
  if (now - lastMemLogMs >= 10000) {
    lastMemLogMs = now;
    size_t heapTotal = ESP.getHeapSize();
    size_t heapFree = ESP.getFreeHeap();
    size_t heapUsed = heapTotal - heapFree;
    size_t heapMaxAlloc = ESP.getMaxAllocHeap();
    size_t heapMinFree = ESP.getMinFreeHeap();
    Serial.printf("MEM: heap total=%u bytes, used=%u bytes, free=%u bytes, maxAlloc=%u, minFree=%u\n",
                  (unsigned)heapTotal, (unsigned)heapUsed, (unsigned)heapFree,
                  (unsigned)heapMaxAlloc, (unsigned)heapMinFree);
  }

  delay(49);
}
