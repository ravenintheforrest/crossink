#include "PresenceStore.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"

namespace {
constexpr const char* CONFIG_PATH = "/Presence/config.txt";
constexpr const char* HOME_PATH = "/Presence/home.json";
constexpr const char* STATE_PATH = "/Presence/state.txt";
constexpr size_t MAX_MANIFEST_BYTES = 8 * 1024;  // a handful of short items
constexpr size_t MAX_ITEMS = 8;
constexpr const char* INK_USER = "ink";

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' || s[a] == '"')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == '"'))
    --b;
  return s.substr(a, b - a);
}

// Only plain names like "note-2.txt" are accepted from the server, so a bad
// manifest can never write outside /Presence/.
bool safeFileName(const std::string& name) {
  if (name.empty() || name.size() > 40) return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.';
    if (!ok) return false;
  }
  return name.find("..") == std::string::npos && name.rfind(".txt") == name.size() - 4;
}

// Minutes since 2000-01-01 (UTC, from the RTC). Month lengths are approximate
// (31 days); only differences of a few minutes or hours matter here.
int32_t clockMinute() {
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  if (!halClock.isAvailable() || !halClock.getDateTime(y, mo, d, h, mi) || y < 2024) return -1;
  return ((((static_cast<int32_t>(y) - 2000) * 12 + (mo - 1)) * 31 + (d - 1)) * 24 + h) * 60 + mi;
}
}  // namespace

PresenceStore& PresenceStore::instance() {
  static PresenceStore store;
  return store;
}

bool PresenceStore::loadConfig() {
  server.clear();
  token.clear();
  classicHome = false;
  if (!Storage.exists(CONFIG_PATH)) return false;
  const String raw = Storage.readFile(CONFIG_PATH);
  const std::string text(raw.c_str());
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = trim(text.substr(start, end - start));
    start = end + 1;
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "server") {
      server = value;
      while (!server.empty() && server.back() == '/') server.pop_back();
    } else if (key == "token") {
      token = value;
    } else if (key == "home") {
      classicHome = value == "classic";
    }
  }
  if (!configured()) LOG_ERR("PRES", "config.txt needs server= and token=");
  loadState();
  return configured();
}

bool PresenceStore::parseManifest(const std::string& json, std::vector<PresenceItem>& out) const {
  JsonDocument doc;
  const auto err = deserializeJson(doc, json);
  if (err) {
    LOG_ERR("PRES", "manifest parse failed: %s", err.c_str());
    return false;
  }
  out.clear();
  const JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  out.reserve(items.size() < MAX_ITEMS ? items.size() : MAX_ITEMS);
  for (const JsonObjectConst it : items) {
    if (out.size() >= MAX_ITEMS) break;
    PresenceItem item;
    item.kind = it["kind"] | "";
    item.title = it["title"] | "";
    item.sub = it["sub"] | "";
    item.file = it["file"] | "";
    item.rev = it["rev"] | "";
    if (item.title.empty() || !safeFileName(item.file)) continue;
    out.push_back(std::move(item));
  }
  return true;
}

bool PresenceStore::loadHome() {
  homeItems.clear();
  if (!Storage.exists(HOME_PATH)) return false;
  const String raw = Storage.readFile(HOME_PATH);
  return parseManifest(std::string(raw.c_str()), homeItems);
}

std::string PresenceStore::itemPath(const PresenceItem& item) const { return std::string(DIR) + "/" + item.file; }

void PresenceStore::loadState() {
  syncedAt.clear();
  syncedMinute = attemptMinute = wifiOkMinute = -1;
  if (!Storage.exists(STATE_PATH)) return;
  const String raw = Storage.readFile(STATE_PATH);
  int synced = -1, attempt = -1, wifiOk = -1;
  char label[24] = {};
  // state.txt: "<synced>\n<attempt>\n<wifiOk>\n<label>\n"
  const int n = sscanf(raw.c_str(), "%d\n%d\n%d\n%23[^\n]", &synced, &attempt, &wifiOk, label);
  if (n >= 3) {
    syncedMinute = synced;
    attemptMinute = attempt;
    wifiOkMinute = wifiOk;
    if (n == 4) syncedAt = label;
  }
}

void PresenceStore::saveState() {
  char buf[64];
  snprintf(buf, sizeof(buf), "%ld\n%ld\n%ld\n%s\n", static_cast<long>(syncedMinute), static_cast<long>(attemptMinute),
           static_cast<long>(wifiOkMinute), syncedAt.c_str());
  Storage.mkdir(DIR);
  FsFile f;
  if (!Storage.openFileForWrite("PRES", STATE_PATH, f)) {
    LOG_ERR("PRES", "cannot write state");
    return;
  }
  f.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  f.close();
}

void PresenceStore::markAttempt() {
  attemptMinute = clockMinute();
  saveState();
}

void PresenceStore::markWifiOk() {
  wifiOkMinute = clockMinute();
  saveState();
}

bool PresenceStore::recentlyFailed(const int withinMinutes) const {
  const int32_t now = clockMinute();
  if (now < 0 || attemptMinute < 0) return false;
  const int32_t age = now - attemptMinute;
  return age >= 0 && age < withinMinutes && wifiOkMinute < attemptMinute;
}

bool PresenceStore::autoSyncDue(const int minMinutes) const {
  if (!configured()) return false;
  const int32_t now = clockMinute();
  if (recentlyFailed(10)) return false;
  if (now < 0 || syncedMinute < 0) return true;
  const int32_t age = now - syncedMinute;
  return age < 0 || age >= minMinutes;
}

bool PresenceStore::syncNow() {
  if (!configured() && !loadConfig()) return false;
  Storage.mkdir(DIR);

  std::string manifest;
  if (!HttpDownloader::fetchUrl(server + "/ink/home", manifest, INK_USER, token) || manifest.empty() ||
      manifest.size() > MAX_MANIFEST_BYTES) {
    LOG_ERR("PRES", "manifest fetch failed (%u bytes)", static_cast<unsigned>(manifest.size()));
    return false;
  }
  std::vector<PresenceItem> fresh;
  if (!parseManifest(manifest, fresh)) return false;

  // Revs already on the card, so unchanged items are not downloaded again.
  std::vector<PresenceItem> previous;
  if (Storage.exists(HOME_PATH)) {
    const String raw = Storage.readFile(HOME_PATH);
    parseManifest(std::string(raw.c_str()), previous);
  }

  bool allOk = true;
  for (const auto& item : fresh) {
    const std::string path = itemPath(item);
    bool unchanged = false;
    for (const auto& old : previous) {
      if (old.file == item.file && old.rev == item.rev && !item.rev.empty()) unchanged = Storage.exists(path.c_str());
    }
    if (unchanged) continue;
    HttpDownloader::DownloadOptions opts;
    opts.stageAsPart = true;  // never leave a half-written item behind
    const auto res =
        HttpDownloader::downloadToFile(server + "/ink/file/" + item.file, path, nullptr, nullptr, INK_USER, token, opts);
    if (res != HttpDownloader::OK) {
      LOG_ERR("PRES", "download %s failed (%d)", item.file.c_str(), static_cast<int>(res));
      allOk = false;
    }
  }

  // Notes taken off paper in Presence leave the reader too.
  for (const auto& old : previous) {
    bool kept = false;
    for (const auto& item : fresh) kept = kept || item.file == old.file;
    if (!kept) Storage.remove(itemPath(old).c_str());
  }

  FsFile f;
  if (Storage.openFileForWrite("PRES", HOME_PATH, f)) {
    f.write(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size());
    f.close();
  } else {
    LOG_ERR("PRES", "cannot write home.json");
    return false;
  }
  homeItems = std::move(fresh);
  syncedMinute = clockMinute();
  char label[24] = {};
  if (!halClock.formatTime(label, sizeof(label), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) label[0] = '\0';
  syncedAt = label;
  wifiOkMinute = syncedMinute;
  saveState();
  LOG_INF("PRES", "synced %u items%s", static_cast<unsigned>(homeItems.size()), allOk ? "" : " (some failed)");
  return allOk;
}
