#include "workout_storage.h"
#include <Arduino.h>

using namespace WorkoutStorage;

bool WorkoutStorage::begin() {
  // Try to mount the "spiffs" labeled partition used by huge_app.csv; format on failure.
  if (!LittleFS.begin(false, "/littlefs", 10, "spiffs")) {
    Serial.println("LittleFS mount failed in WorkoutStorage; formatting...");
    LittleFS.begin(true, "/littlefs", 10, "spiffs");
  }
  if (!LittleFS.exists("/workouts")) {
    LittleFS.mkdir("/workouts");
  }
  return true;
}

static String path_for(const String& id) {
  return "/workouts/" + id + ".json";
}



// Serialize a single swim as {speed,dur,note} (the format the UI has always used).
static void write_swim(JsonObject o, const SwimStep &s) {
  o["speed"] = s.pace100s;   // pace100s -> "speed"
  o["dur"]   = s.durSec;
  o["note"]  = s.note;
}

String WorkoutStorage::to_json(const Workout &w) {
  // Use heap allocation to avoid stack overflow; size based on content
  size_t swimCount = 0;
  for (const auto &it : w.items) swimCount += it.isSet ? it.swims.size() : 1;
  size_t cap = 512 + (swimCount * 96);
  for (const auto &it : w.items) {
    cap += it.note.length();
    if (it.isSet) { for (const auto &s : it.swims) cap += s.note.length(); }
    else            cap += it.swim.note.length();
  }
  if (cap < 1024) cap = 1024;
  if (cap > 8192) cap = 8192;
  DynamicJsonDocument doc(cap);

  doc["id"] = w.id;                // string id, preserved verbatim
  doc["title"] = w.name;           // use "title" not "name"
  JsonArray arr = doc.createNestedArray("swims");   // "swims" key
  for (const auto &it : w.items) {
    JsonObject o = arr.createNestedObject();
    if (it.isSet) {
      o["type"]   = "set";
      o["repeat"] = it.repeat;
      o["note"]   = it.note;
      JsonArray sub = o.createNestedArray("swims");
      for (const auto &s : it.swims) write_swim(sub.createNestedObject(), s);
    } else {
      write_swim(o, it.swim);      // plain swim: unchanged {speed,dur,note}
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool WorkoutStorage::from_json(const uint8_t *data, size_t len, Workout &w) {
  // Allocate document on heap sized to input length (clamped)
  size_t cap = len + 512;
  if (cap < 1024) cap = 1024;
  if (cap > 8192) cap = 8192;
  DynamicJsonDocument doc(cap);

  auto err = deserializeJson(doc, data, len);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }
  w.id   = String(doc["id"] | "");            // preserve string id verbatim
  w.name = String(doc["title"] | "Unnamed");  // use "title"

  auto read_swim = [](JsonObjectConst o) {
    SwimStep s;
    s.pace100s = o["speed"] | 0U;             // "speed" -> pace100s
    s.durSec   = o["dur"]   | 0UL;
    s.note     = String(o["note"] | "");
    return s;
  };

  w.items.clear();
  // Each element of "swims" is either a plain swim {speed,dur,note} (legacy &
  // current) or a set {type:"set", repeat, note, swims:[...]}. Elements without
  // type:"set" parse as plain swims, so old workout files still load unchanged.
  for (JsonObjectConst elem : doc["swims"].as<JsonArrayConst>()) {
    const char* type = elem["type"] | "";
    WorkoutItem it;
    if (strcmp(type, "set") == 0) {
      it.isSet = true;
      uint32_t rep = elem["repeat"] | 1U;
      if (rep < 1)   rep = 1;
      if (rep > 100) rep = 100;               // sanity clamp
      it.repeat = (uint16_t)rep;
      it.note = String(elem["note"] | "");
      for (JsonObjectConst s : elem["swims"].as<JsonArrayConst>())
        it.swims.push_back(read_swim(s));
    } else {
      it.isSet = false;
      it.swim  = read_swim(elem);
    }
    w.items.push_back(std::move(it));
  }
  return true;
}

std::vector<FlatStep> WorkoutStorage::flatten(const Workout &w) {
  std::vector<FlatStep> out;
  int16_t setIdx = -1;
  for (const auto &it : w.items) {
    if (it.isSet) {
      ++setIdx;
      uint16_t reps = it.repeat < 1 ? 1 : it.repeat;
      for (uint16_t r = 1; r <= reps; ++r) {
        for (const auto &s : it.swims) {
          FlatStep fs;
          fs.swim = s;
          fs.setIndex = setIdx;
          fs.setRep   = r;
          fs.setReps  = reps;
          out.push_back(fs);
        }
      }
    } else {
      FlatStep fs;
      fs.swim = it.swim;   // setIndex stays -1
      out.push_back(fs);
    }
  }
  return out;
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
