# Quire roadmap

## Goals

1. **Offline is normal, not a fallback.** Writing on a plane is no different from writing at home.
2. **One file per story.** Easy to move, back up, email to yourself, or keep in a synced folder.
3. **Same app on Windows and Mac.** You travel with both.
4. **Calm by default.** The page comes first. Structure (characters, places, plot) is one
   click away and never in the way.
5. **Cloud is optional and yours.** Sync through your own Cloudflare account, never required.

## Key decisions

### Runtime: a web app that runs from a local file

Chrome and Edge let a page opened from `file://` read and write a file you pick, using
the File System Access API. So Quire is ordinary HTML/JS that:

- runs without a server, an install or an internet connection;
- behaves the same on Windows and Mac, because it's the same browser engine;
- can later be hosted on Cloudflare Pages and installed as an app (Phase 2) without a rewrite.

A purpose-built desktop app (Electron or Tauri) would add an installer, updates and code
signing for each OS, and buy little the browser doesn't already provide. It stays an
option for Phase 4 if a real need appears.

### File format: one JSON file, `*.quire`

| Option | Verdict |
| --- | --- |
| **JSON** (chosen) | Native to the browser, one file, readable in any text editor, diffs cleanly, easy for other tools and AI to read. |
| XML | Same single-file benefit but more verbose and needs a parser. No advantage here. |
| Zip container (like .docx or Scrivener) | Needed only for images or attachments. Planned as an optional `.quirez` if that becomes necessary. |
| Folder of Markdown files | Nice for git, but you said one file is easier to move, and it is. |

Prose is stored as a tiny HTML subset (`<p>`, `<em>`, `<strong>`), so formatting survives
without a heavy editor. Everything has a stable ID, which makes per-scene merge possible
for sync. Details: [docs/file-format.md](docs/file-format.md).

## Phases

### Phase 0: prototype ✅

Binder, scene/chapter/book writing, focus mode, scene details, board, story map, character and place
sheets, notes, `Ctrl+K`, autosave to one file, browser backups and snapshots, goals, exports, themes.

### Phase 1: daily driver ✅ (the parts that protect your work)

- [x] Undo and redo for structure (delete, move, split, merge, replace-all, import, restores), with
      an Undo button on every delete.
- [x] Split a scene at the cursor; merge a scene with the next.
- [x] Find and replace across the manuscript (match case, whole words, per-scene match list).
- [x] Import `.docx`, `.md` and `.txt`, split into chapters and scenes by headings and `***` / `#`
      breaks; "Import a draft" starts a new story from one.
- [x] Export `.docx` in standard manuscript format (Times 12, double-spaced, chapters on new pages).
- [x] Notice when the file was changed on another computer (e.g. via a synced folder) and merge it
      in rather than overwriting it.

Still open, now Phase 4 below:

- [ ] Inline comments and highlights on prose.
- [ ] Timeline view ordered by story time.
- [ ] Character relationship map; arcs per act.
- [ ] Per-chapter word targets; session timer; streaks.
- [ ] `.epub` export.
- [ ] Resizable panels; better phone layout.

### Phase 2: installable app on Cloudflare ✅

- [x] `cloud/wrangler.toml` deploys the app and the sync API as one Cloudflare Worker (static assets
      plus API) with a single `npx wrangler deploy`.
- [x] Installable in Chrome and Edge on Windows and Mac: own window, Start menu or Dock, opens
      `.quire` files.
- [x] **Cache-first** service worker: once the welcome screen says "Ready to work offline", the app
      opens with no network at all (tested with the server switched off). Handles hosts that
      redirect `/index.html`.
- [x] Updates download in the background and are offered with an **Update** button, never forced
      mid-sentence.
- [x] `Quire.html` downloadable from the site as the zero-install fallback.

### Phase 3: sync through your Cloudflare account ✅

- [x] R2 bucket, one object per story; Worker API with ETag/`If-Match` revisions so two laptops
      can't overwrite each other.
- [x] Sync key (bearer token) auth, so it also works from `Quire.html` opened as a file; optional
      Cloudflare Access in front of the web app.
- [x] **Local-first:** opening and saving never wait for the network. Sync runs in the background
      on edit, every minute, on reconnect, and when you come back to the window.
- [x] Three-way merge per item (scene, chapter, character, place, thread, note) and per field; tag
      lists merge as sets; reorders and moves between chapters merge; a scene rewritten on both
      laptops keeps **both** versions side by side.
- [x] Every cloud story is downloaded in the background, so the newest version opens on a plane
      (see [docs/cloud-sync.md](docs/cloud-sync.md)).
- [x] Cloud version history (one version per 10 minutes, newest 200), restorable from the app;
      deleted stories go to a trash folder.
- [x] Tested end to end: two simulated laptops, a flight, simultaneous saves, against both a local
      server and Cloudflare's own runtime (`wrangler dev`).
- [ ] End-to-end encryption (key never leaves your devices). Planned; needs a passphrase flow.
- [ ] Optional D1 index for cross-story search, if you end up with many projects.

### Phase 4: writing tools

The Phase 1 items still open (comments, timeline, relationships, targets, `.epub`, panels), plus:

- An opt-in writing assistant, off by default: continuity checks ("what colour did I say her eyes
  were?"), timeline consistency, a summary of each scene for the board.
- A Tauri desktop wrapper, only if a native menu bar or OS file locking becomes important.

## Using it across Windows and Mac today

- **Cloudflare sync (recommended):** see [docs/cloud-sync.md](docs/cloud-sync.md). Both laptops
  keep a full local copy, sync in the background, and merge edits scene by scene.
- **A synced folder (OneDrive, Dropbox, iCloud Drive):** keep your `.quire` file there. If the file
  changes underneath Quire (because you wrote on the other laptop), Quire now merges that version
  in instead of overwriting it. But folder sync clients can still create "conflicted copy" files
  if both laptops save while offline, so Cloudflare sync is the safer choice for writing on planes.
