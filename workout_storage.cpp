#include "workout_storage.h"
#include <Arduino.h>

using namespace WorkoutStorage;

bool WorkoutStorage::begin() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists("/workouts")) {
    LittleFS.mkdir("/workouts");
  }
  return true;
}

static String path_for(const String& id) {
  return "/workouts/" + id + ".json";
}



String WorkoutStorage::to_json(const Workout &w) {
  StaticJsonDocument<2048> doc;
  doc["id"] = String(w.id);        // store as string
  doc["title"] = w.name;            // use "title" not "name"
  JsonArray arr = doc.createNestedArray("swims");   // "swims" key
  for (auto &s : w.steps) {
    JsonObject o = arr.createNestedObject();
    o["speed"] = s.pace100s; // rename field from pace100s -> speed
    o["dur"] = s.durSec;
    o["note"] = s.note;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool WorkoutStorage::from_json(const uint8_t *data, size_t len, Workout &w) {
  StaticJsonDocument<2048> doc;
  auto err = deserializeJson(doc, data, len);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }
  // convert string id to uint32_t using atol or strtoull
  const char* strid = doc["id"] | "";
  w.id = strtoull(strid, nullptr, 10);

  w.name = String(doc["title"] | "Unnamed");  // use "title"

  w.steps.clear();
  for (auto elem : doc["swims"].as<JsonArray>()) {  // use "swims"
    SwimStep s;
    s.pace100s = elem["speed"] | 0U;              // rename speed->pace100s
    s.durSec = elem["dur"] | 0UL;
    s.note = String(elem["note"] | "");
    w.steps.push_back(s);
  }
  return true;
}

std::vector<String> WorkoutStorage::list_ids() {
  std::vector<String> ids;
  File dir = LittleFS.open("/workouts","r");
  if (!dir) return ids;
  File file = dir.openNextFile();
  while (file) {
    String name = file.name();
    if ( name.endsWith(".json")) {
      String id_str = name.substring(0, name.length() - 5);
      ids.push_back(id_str);
    }
    file = dir.openNextFile();
  }
  return ids;
}

bool WorkoutStorage::load(String id, Workout &out) {
  String p = path_for(id);  
  Serial.println("opening file");
  Serial.println(p);
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  String j = f.readString();
  f.close();
  return from_json((const uint8_t*)j.c_str(), j.length(), out);
}

bool WorkoutStorage::save(const Workout &w) {
  if (!LittleFS.exists("/workouts")) LittleFS.mkdir("/workouts");  
  String p = path_for(w.id);  
  Serial.println("saving file");
  Serial.println(p);
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  String S = to_json(w);
  Serial.println("saving file");
  Serial.println(S);
  f.print(S);
  f.close();
  return true;
}

bool WorkoutStorage::erase(String id) {
  return LittleFS.remove(path_for(id));
}
