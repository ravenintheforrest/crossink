#include "PresenceHomeActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "RecentBookProgress.h"
#include "SilentRestart.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "presence/PresenceAuto.h"
#include "presence/PresenceStore.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;

bool isPresenceFile(const std::string& path) {
  const size_t n = strlen(PresenceStore::DIR);
  return path.compare(0, n, PresenceStore::DIR) == 0 && path.size() > n && path[n] == '/';
}
}  // namespace

PresenceHomeActivity::PresenceHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Home", renderer, mappedInput), uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

void PresenceHomeActivity::buildRows() {
  rows.clear();
  rows.reserve(12);

  // The book being read. Presence items opened as text also land in the
  // recent list; they have their own rows, so skip them here.
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (isPresenceFile(book.path)) continue;
    const float pct = RecentBookProgress::loadPercent(book);
    std::string sub = book.author;
    const std::string pctLabel = RecentBookProgress::formatPercent(pct);
    if (!pctLabel.empty()) sub = sub.empty() ? pctLabel : pctLabel + " · " + sub;
    rows.push_back({RowAction::ContinueBook, book.title.empty() ? book.path : book.title, sub, book.path});
    break;
  }

  PRESENCE.loadHome();
  for (const auto& item : PRESENCE.items()) {
    rows.push_back({RowAction::PresenceItem, item.title, item.sub, PRESENCE.itemPath(item)});
  }

  rows.push_back({RowAction::Library, tr(STR_LIBRARY), "", ""});
  rows.push_back({RowAction::SyncNow, tr(STR_PRESENCE_SYNC_NOW), "", ""});
  rows.push_back({RowAction::Settings, tr(STR_SETTINGS_TITLE), "", ""});

  // "Saturday · Synced 9:41 PM": the date when the clock knows it, then sync state.
  char date[32] = {};
  headerTitle.clear();
  if (halClock.formatDate(date, sizeof(date), SETTINGS.clockUtcOffsetQ, HalClock::MONTH_DAY_LONG)) {
    headerTitle = date;
  }
  if (headerTitle.empty()) headerTitle = tr(STR_PRESENCE);
  headerTitle += " · ";
  if (PRESENCE.syncedLabel().empty()) {
    headerTitle += tr(STR_PRESENCE_NOT_SYNCED);
  } else {
    headerTitle += tr(STR_PRESENCE_SYNCED);
    headerTitle += " ";
    headerTitle += PRESENCE.syncedLabel();
  }

  if (selectedIndex >= static_cast<int>(rows.size())) selectedIndex = 0;
}

void PresenceHomeActivity::onEnter() {
  Activity::onEnter();
  PRESENCE.loadConfig();
  selectedIndex = 0;
  topIndex = 0;
  buildRows();
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &PresenceHomeActivity::onRowEvent, this);
  app.setScreen(&PresenceHomeActivity::listScreen, this);
  requestUpdate();
}

void PresenceHomeActivity::onExit() {
  rows.clear();
  Activity::onExit();
}

void PresenceHomeActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size())) return;
  const Row row = rows[index];
  switch (row.action) {
    case RowAction::ContinueBook:
      // Pull the newest page from the phone first when it has been a while;
      // the quiet sync boot opens the book itself when it is done.
      if (FsHelpers::hasEpubExtension(row.path) && KOREADER_STORE.hasCredentials() &&
          PRESENCE.autoSyncDue(PresenceAuto::WAKE_SYNC_MIN_MINUTES)) {
        APP_STATE.openEpubPath = row.path;
        APP_STATE.saveToFile();
        PRESENCE.markAttempt();
        silentRestartToNetwork(NetworkBootTarget::KOREADER_SYNC, PresenceAuto::PAYLOAD_AUTO);
        break;
      }
      activityManager.goToReader(row.path);
      break;
    case RowAction::PresenceItem:
      activityManager.goToReader(row.path);
      break;
    case RowAction::Library:
      activityManager.goToLibrary();
      break;
    case RowAction::SyncNow:
      silentRestartToNetwork(NetworkBootTarget::PRESENCE_SYNC);
      break;
    case RowAction::Settings:
      activityManager.goToSettings();
      break;
  }
}

void PresenceHomeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selectedIndex);
    return;
  }

  const int listSize = static_cast<int>(rows.size());
  if (listSize == 0) return;

  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const auto moveSelection = [this, listSize](const int next) {
    selectedIndex = next;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, listSize);
    requestUpdate();
  };
  buttonNavigator.onNextRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, listSize)); });
  buttonNavigator.onPreviousRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, listSize)); });
}

void PresenceHomeActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PresenceHomeActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->rows.size())) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->activate(self->selectedIndex);
}

void PresenceHomeActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<PresenceHomeActivity*>(user)->buildListScreen(screen);
}

void PresenceHomeActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(CompactHeader::contentTop(metrics)), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  std::vector<fui::ListItem> items;
  items.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    fui::ListItem item;
    item.label = rows[i].title.c_str();
    if (!rows[i].subtitle.empty()) item.subtitle = rows[i].subtitle.c_str();
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }
  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  const fui::Rect bounds = screen.body();
  const auto shown = configureUiList(props, screen.theme(), bounds, UiListRowType::WithSubtitle);
  visibleRows = shown > 0 ? shown : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(rows.size()));
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void PresenceHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, headerTitle.c_str());
  uiReady = false;
  app.render();
  uiReady = true;
  const auto labels = mappedInput.mapLabels("", tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
