#pragma once

#include "activities/Activity.h"

// Presence fork: one quiet network step. Joins saved Wi-Fi without asking,
// fetches Presence items into /Presence/, then reboots back to Home. Runs on
// wake (when no book is open) and from "Sync now" on the Presence Home.
// Never waits for a button: a missing network just returns Home.
class PresenceSyncActivity final : public Activity {
 public:
  explicit PresenceSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PresenceSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return !done; }

 private:
  bool started = false;
  bool done = false;
  bool wifiActivated = false;
  void onWifiReady(bool connected);
};
