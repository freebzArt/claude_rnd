# Quire

A quiet, offline-first place to write a story: the manuscript, characters, places
and plot in one file you own.

- Runs in **Chrome or Edge on Windows and Mac**, straight from a local file. Nothing
  to install, no account, no internet needed.
- Your whole story is **one `.quire` file** (plain JSON). Keep it on a USB stick or
  in OneDrive, Dropbox or iCloud. Quire saves to it as you type.
- [ROADMAP.md](ROADMAP.md): where this is going (installable app, Cloudflare sync).
- [docs/file-format.md](docs/file-format.md): what's inside a `.quire` file.

## Try it

1. Download [`dist/Quire.html`](dist/Quire.html). It's a single file with everything inside.
2. Open it in **Chrome or Edge** (double-click, or right-click > Open with).
3. Click **Explore a sample** to look around, or **New story** to start your own. New
   stories ask where to save the `.quire` file first.

Tip: bookmark the page, or pin it to the taskbar or Dock (Chrome: ⋮ > Cast, save and
share > Create shortcut). On each laptop, keep `Quire.html` next to your stories.

Firefox and Safari can't write straight to files yet. Quire still works there, but
you save by downloading a copy.

## What's in the prototype

| Area | What you can do |
| --- | --- |
| **Manuscript** | Chapters and scenes in a drag-and-drop binder. Write one scene at a time, or a whole chapter or the whole book as one continuous page. |
| **Writing** | Clean serif page, bold and italic only, smart quotes, `--` to em dash, `...` to ellipsis. Pasting from Word or Google Docs keeps italics and drops the clutter. |
| **Focus mode** | `Ctrl+Shift+F`: full screen, just the page. The paragraph you're on stays bright, the rest dims, and the line you're typing stays mid-screen. |
| **Scene details** | Status (idea, draft, revised, final), synopsis, POV, characters, setting, plot threads, when, notes. Quire spots characters and places named in the text and offers to tag them. |
| **Board** | Index cards per chapter. Drag to reorder. Card edges take the POV character's colour. |
| **Story map** | A grid of every scene against your plot threads, characters (in scene or POV) and places. Click to mark. Shows at a glance where a thread goes quiet or a character vanishes. |
| **Characters & places** | A sheet for each, with custom fields (goal, motivation, voice…), aliases, notes, and an automatic "appears in" list. |
| **Notes** | Research, world rules, stray lines. |
| **Search** | `Ctrl+K` finds any scene, character, place, note or line of prose, and runs any command. |
| **Progress** | Word counts per scene, chapter and book; book and daily goals; a 14-day chart (click the story title). |
| **Safety** | Autosaves to your file. Backs up to the browser too, with a snapshot every 10 minutes (Menu > Snapshots). If the browser holds newer work than the file, Quire offers to recover it. |
| **Export** | Markdown, plain text, or print / PDF with manuscript formatting. |
| **Themes** | Paper, Sepia, Night, or follow the system. Choice of typeface, text size, line length and paragraph style. |

### Keyboard

| Keys | Action |
| --- | --- |
| `Ctrl+K` | Find anything, run any command |
| `Ctrl+S` / `Ctrl+Shift+S` | Save now / Save as |
| `Ctrl+O` | Open a story |
| `Ctrl+Enter` | New scene after this one |
| `Ctrl+Shift+F` | Focus mode (`Esc` to leave) |
| `Ctrl+.` / `Ctrl+\` | Scene details panel / sidebar |
| `Alt+1` … `Alt+6` | Manuscript, Board, Story map, Characters, Places, Notes |

On a Mac, `Cmd` works wherever `Ctrl` is listed.

## Known limits of this prototype

- Deleting a scene or chapter can't be undone with `Ctrl+Z`. It asks first, and
  Snapshots hold earlier versions.
- If the same file is open in two windows or on two laptops at once, the last save wins.
  Close it on one machine before writing on the other. Real sync comes in Phase 3.
- When you reopen a recent story, Chrome may ask once per session to let Quire edit the file.
- No images yet.

## Developing

The source is plain HTML, CSS and JavaScript in [`app/`](app/), with no framework and no
dependencies. `dist/Quire.html` is those files inlined into one.

```bash
node tools/build.mjs           # app/ -> dist/Quire.html
```

```bash
node tools/smoke.mjs           # headless Chromium end-to-end checks from file://
```

```bash
node tools/make-sample.mjs     # regenerate samples/The Lantern Keeper.quire
```

`app/` is also a ready-to-host Progressive Web App (manifest, service worker, `.quire`
file handler) for Phase 2. Opening `app/index.html` directly also works.
