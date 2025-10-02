#define NOUDPTEST
//#define SWIMMACHINE
//#define WORKOUTMANAGER

#define DEBUGCRASH
#define WEBUI



#include <Arduino.h>

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

#ifdef SWIMMACHINE
#include "swim_machine.h"
#ifdef WORKOUTMANAGER
#include "workout_manager.h"
#endif
#endif
#ifdef WEBUI
#include "web_ui.h"
#endif

#define MEMTEST
#ifdef MEMTEST
static byte gg1[180000];
static byte gg2[80000];
static byte gg3[40000];
#endif
void setup()
{
  Serial.begin(115200);
  delay(100);
#ifdef MEMTEST
Serial.printf("%i%i",gg1[0],gg2[0],gg3[3]);
#endif
  Serial.setDebugOutput(true);


#ifdef DEBUGCRASH
Serial.println("a");
HEAP_CHECK_HARD();
#endif
#ifdef WEBUI
  WebUI::begin();            // Wi-Fi + HTTP server
  Serial.println("web ui begin done");
#endif

  
#ifdef DEBUGCRASH
Serial.println("a");
HEAP_CHECK_HARD();
#endif
  
#ifdef SWIMMACHINE
  SwimMachine::begin();   // initialise UDP + state machine
  Serial.println("swim machine begin done");
  #ifdef WEBUI
  SwimMachine::setPushNetworkEvent(WebUI::push_network_event);
  #endif
  #ifdef WORKOUTMANAGER
  WorkoutManager::begin();   // load saved workouts & prefs
  Serial.println("workout manager begin done");
  #endif
  #endif
  
#ifdef DEBUGCRASH
Serial.println("problem?");
HEAP_CHECK_HARD();
Serial.println("problem.");
#endif
}

void loop()
{
  
#ifdef DEBUGCRASH
HEAP_CHECK_HARD();
#endif
#ifdef WEBUI
  WebUI::loop();             // handles AsyncEventSource pings
#endif

#ifdef SWIMMACHINE

#ifdef WORKOUTMANAGER
  WorkoutManager::tick();    // 1 Hz countdown
#endif
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

  delay(1);
}
