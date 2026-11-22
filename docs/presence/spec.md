# Presence on the Xteink (CrossInk fork)

Brief: Raven wants Xteink sync to happen on its own when he opens the reader, and a
home screen that carries a few Presence things (briefing, today's Bible reading,
notes sent to paper) next to the book he is reading. (Raven, 2026-11-21.)

## Scope (v1)

1. **Sync on its own.**
   - Wake: if Presence sync is set up and the last sync is older than 3 minutes (and no failed try in the last 10),
     the reader boots into a short network step first: join saved Wi-Fi, fetch the
     Presence home data, then run the existing KOReader smart sync for the open
     book, then open the book (or Home). A missing network gives up after ~8 s and
     opens as normal. Nothing waits on the user.
   - Opening a book from Home: smart sync runs for that book first (same rules).
   - Sleep: if the page moved since the last upload, the position is uploaded
     before deep sleep.
   - Smart sync rules are CrossInk's own: pull if remote is ahead, push if local is
     ahead, nothing if equal. No prompts.
2. **Presence Home** (new Home screen option, Settings > Display > Home screen).
   One calm list, top to bottom:
   - Date and "Synced 9:41 PM" (or "Not synced yet").
   - Continue: current book and percent.
   - Bible: today's chapter ("Luke 6 · Blessings and Woes").
   - Briefing: today's briefing, first lines shown.
   - For paper: up to 5 Presence notes Raven sends to paper.
   - Library, Settings.
   Confirm opens the row. Items are plain text files under `/Presence/` on the SD
   card, opened in the normal TXT reader, so they read offline too.
3. **Server** (`murmur-server`, `ink.py`): `GET /ink/home` returns a small JSON
   manifest; `GET /ink/file/<name>` returns plain text. Read-only token
   `INK_TOKEN`, separate from `MURMUR_TOKEN`.

## Device config

`/Presence/config.txt` on the SD card (or the web portal later):

```
server=http://192.168.86.25:8787
token=<INK_TOKEN>
```

KOReader sync uses the existing CrossInk KOReader settings (BookOrbit's KOSync).

## Out of scope for v1

Highlights back into Presence, marking the Bible chapter read, Kindle, OTA from
Presence. Each is a small follow-up once v1 feels right.

## Manifest

```json
{"v":1,"date":"2026-11-21","generated":"2026-11-21T21:40:00-05:00",
 "items":[{"kind":"bible","title":"Luke 6","sub":"Blessings and Woes","file":"bible.txt","rev":"…"},
          {"kind":"briefing","title":"Evening briefing","sub":"first line…","file":"briefing.txt","rev":"…"},
          {"kind":"note","title":"…","sub":"","file":"note-1.txt","rev":"…"}]}
```

`rev` is a short content hash; the device only downloads files whose rev changed.
