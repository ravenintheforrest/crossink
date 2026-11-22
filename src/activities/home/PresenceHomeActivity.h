#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Presence fork: a calm Home. One list, top to bottom: the book being read,
// today's Bible chapter, the briefing, notes sent to paper, then Library,
// Sync now and Settings. Items are text files under /Presence/ (see
// PresenceStore), so this screen reads only the SD card and never the network.
class PresenceHomeActivity final : public Activity {
 public:
  PresenceHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  enum class RowAction : uint8_t { ContinueBook, PresenceItem, Library, SyncNow, Settings };
  struct Row {
    RowAction action;
    std::string title;
    std::string subtitle;
    std::string path;  // book or /Presence/<file>
  };

  std::vector<Row> rows;
  std::string headerTitle;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int topIndex = 0;
  int visibleRows = 1;
  using UiApp = freeink::ui::FreeInkApp<20, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void buildRows();
  void activate(int index);
};
