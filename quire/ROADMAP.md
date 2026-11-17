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
when sync arrives. Details: [docs/file-format.md](docs/file-format.md).

## Phases

### Phase 0: prototype ✅ (this commit)

- Manuscript binder (chapters and scenes), drag to reorder; scene, chapter and whole-book
  writing views.
- Distraction-free editor: smart typography, clean paste, focus mode with typewriter
  scrolling and dimmed paragraphs.
- Scene details: status, synopsis, POV, cast, setting, plot threads, timeline label,
  notes; mentions found automatically.
- Board (index cards), story map (threads, characters and places against scenes),
  character and place sheets, notes.
- `Ctrl+K` search and command palette.
- Autosave to the `.quire` file, browser backups, 10-minute snapshots, crash recovery,
  recent files.
- Word counts, goals, 14-day history; Markdown, text and print/PDF export; themes and
  typography preferences.
- Headless end-to-end test from `file://`.

### Phase 1: daily driver (next)

Make it something you trust with a novel.

- [ ] Undo and redo for structure (delete, move, merge), plus a trash for deleted scenes.
- [ ] Split a scene at the cursor; merge scenes.
- [ ] Find and replace across the manuscript (whole word, match case).
- [ ] Import: `.docx`, `.md` and `.txt`, split into chapters and scenes by headings or `***`.
- [ ] Export: `.docx` (standard manuscript format) and `.epub`.
- [ ] Inline comments and highlights on prose.
- [ ] Timeline view: scenes ordered by story time (the "When" field) rather than
      manuscript order.
- [ ] Character relationships (a simple map), arcs per act.
- [ ] Per-chapter word targets; writing-session timer; streaks.
- [ ] Spellcheck language choice; optional British/American quote styles.
- [ ] Resizable sidebar and details panel; better phone layout.
- [ ] Warn when the file changed on disk since it was opened (edited on another laptop).

### Phase 2: installable app on Cloudflare Pages

Still local files, but with a real app feel.

- [ ] Deploy `app/` to **Cloudflare Pages** (free) on a domain of your choice.
- [ ] "Install Quire" in Chrome or Edge gives it its own window, taskbar or Dock icon,
      and Start-menu entry, on both Windows and Mac.
- [ ] Service worker caches the app, so it opens offline once installed (already in `app/sw.js`).
- [ ] File association: double-click a `.quire` file to open it in Quire (already
      declared in the manifest via `file_handlers`).
- [ ] Automatic updates whenever a new version is pushed; `dist/Quire.html` stays
      available as the zero-install fallback.

### Phase 3: optional sync through your Cloudflare account

For when local files plus OneDrive or Dropbox aren't enough.

- [ ] **R2 bucket** stores one object per story, with **object versioning** for history.
- [ ] A small **Worker** API: `GET/PUT /stories/:id` with ETag preconditions.
- [ ] **Cloudflare Access** (Zero Trust, free tier) for login: no passwords or user
      code in Quire itself.
- [ ] Sync model: the local file stays the source of truth while offline; on reconnect,
      push with `If-Match`. On conflict, merge **per scene** by ID (each scene is a unit,
      so most edits on two machines don't collide). A real collision keeps both versions
      side by side.
- [ ] Optional end-to-end encryption (key never leaves your devices).
- [ ] Optional **D1** index for cross-story search and stats if you end up with many projects.

Cost estimate: a single writer stays well inside Cloudflare's free tiers.

### Phase 4: only if needed

- Tauri desktop wrapper (about 10 MB) if a native menu bar, OS-level file locking, or
  offline without any browser becomes important.
- Opt-in writing assistant, off by default: continuity checks ("what colour did I say
  her eyes were?"), timeline consistency, a summary of each scene for the board.

## Using it across Windows and Mac today

Keep `Quire.html` and your `.quire` stories in a synced folder (OneDrive, Dropbox or
iCloud Drive). Open the story on one machine at a time; Quire saves to the file
continuously, so the sync client picks up each change within seconds.
