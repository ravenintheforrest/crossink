#pragma once
#include <cstdint>

// Flags for the Presence fork's automatic sync. They live for one network
// boot: main.cpp sets them from the silent-restart payload, and the KOReader
// sync and Wi-Fi screens read them so an automatic run never waits for a
// button press.
namespace PresenceAuto {
// Payload bits OR-ed into the KOREADER_SYNC network-boot payload, above the
// one-based reader orientation in the low byte.
constexpr uint32_t PAYLOAD_AUTO = 1U << 16;         // started on wake/sleep, not by Raven
constexpr uint32_t PAYLOAD_SLEEP_AFTER = 1U << 17;  // deep sleep when done (pre-sleep push)
constexpr uint32_t PAYLOAD_FLAGS = PAYLOAD_AUTO | PAYLOAD_SLEEP_AFTER;
// Wake syncs are skipped when the last one is newer than this.
constexpr int WAKE_SYNC_MIN_MINUTES = 3;

extern bool unattended;      // fail quietly back to the book instead of showing a screen
extern bool sleepAfterSync;  // return path goes to deep sleep instead of the reader
// Set just before the reader hands off to KOReaderSyncActivity on sleep.
extern uint32_t pendingHandoffFlags;
}  // namespace PresenceAuto
