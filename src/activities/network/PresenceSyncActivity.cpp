#include "PresenceSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/WifiUtils.h"
#include "presence/PresenceStore.h"

void PresenceSyncActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);
  requestUpdate();
}

void PresenceSyncActivity::onExit() {
  Activity::onExit();
  if (wifiActivated) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
    silentRestart();
  }
}

void PresenceSyncActivity::loop() {
  if (started) return;
  started = true;
  // Show "Syncing Presence…" before the blocking network work.
  requestUpdateAndWait();
  wifiActivated = true;
  if (hasActiveStationWifiConnection()) {
    onWifiReady(true);
    return;
  }
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true,
                                              /*useReaderButtonHints=*/false, /*unattended=*/true),
      [this](const ActivityResult& result) { onWifiReady(!result.isCancelled); });
}

void PresenceSyncActivity::onWifiReady(const bool connected) {
  if (connected) {
    PRESENCE.markWifiOk();
    WiFi.setSleep(false);
    sdFontSystem.releaseForNetwork(renderer);
    if (!PRESENCE.syncNow()) LOG_ERR("PRES", "sync did not finish; Home shows the last copy");
  } else {
    LOG_INF("PRES", "no saved Wi-Fi in range; skipping Presence sync");
  }
  done = true;
  // onExit tears Wi-Fi down and reboots Home, which clears network heap use.
  activityManager.goHome();
}

void PresenceSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_PRESENCE_SYNCING));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
