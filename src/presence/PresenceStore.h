#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Presence on the Xteink: a few items from Raven's Presence server (today's
// Bible chapter, the briefing, notes sent to paper), kept as plain text files
// under /Presence/ so they read offline in the normal TXT reader.
//
// SD layout:
//   /Presence/config.txt  server=http://host:8787, token=<INK_TOKEN>, home=presence|classic
//   /Presence/home.json   last manifest from GET /ink/home
//   /Presence/state.txt   last sync minute + "9:41 PM" label, last try, last Wi-Fi ok
//   /Presence/<file>.txt  one item each
struct PresenceItem {
  std::string kind;   // bible | briefing | note
  std::string title;  // "Luke 6"
  std::string sub;    // "Blessings and Woes"
  std::string file;   // "bible.txt"
  std::string rev;    // content hash; a changed rev means download again
};

class PresenceStore {
 public:
  static PresenceStore& instance();

  // Reads /Presence/config.txt. Cheap; safe to call on every Home entry.
  bool loadConfig();
  bool configured() const { return !server.empty() && !token.empty(); }
  bool presenceHomeEnabled() const { return configured() && !classicHome; }
  const std::string& serverUrl() const { return server; }
  const std::string& inkToken() const { return token; }

  // Reads the cached manifest. Items stay valid until the next load.
  bool loadHome();
  const std::vector<PresenceItem>& items() const { return homeItems; }
  std::string itemPath(const PresenceItem& item) const;

  // "9:41 PM" style label for the last successful sync, empty if never.
  const std::string& syncedLabel() const { return syncedAt; }

  // True when the wake-time auto sync should run: configured, and the last
  // sync is older than minMinutes (or unknown because the clock is not set).
  bool autoSyncDue(int minMinutes) const;

  // Automatic syncs back off after a failed try (for example away from home
  // Wi-Fi), so wake and sleep don't scan for a network every time.
  void markAttempt();
  void markWifiOk();
  bool recentlyFailed(int withinMinutes) const;

  // Network step. Wi-Fi must already be connected. Fetches the manifest,
  // downloads changed items, removes items that left the manifest.
  bool syncNow();

  static constexpr const char* DIR = "/Presence";

 private:
  PresenceStore() = default;
  bool parseManifest(const std::string& json, std::vector<PresenceItem>& out) const;
  void loadState();
  void saveState();

  std::string server;
  std::string token;
  bool classicHome = false;
  std::vector<PresenceItem> homeItems;
  std::string syncedAt;      // display label
  int32_t syncedMinute = -1;   // minutes since 2000-01-01 (RTC), -1 unknown
  int32_t attemptMinute = -1;  // last automatic try
  int32_t wifiOkMinute = -1;   // last time Wi-Fi joined for a sync
};

#define PRESENCE PresenceStore::instance()
