# Quire

A quiet, offline-first place to write a story: the manuscript, characters, places
and plot in one file you own, synced between your computers through your own
Cloudflare account if you want.

- Runs in **Chrome or Edge on Windows and Mac**, straight from a local file, or installed as an app.
- Your whole story is **one `.quire` file** (plain JSON). Quire saves to it as you type.
- **Offline is normal.** Quire opens and saves from your computer. Cloud sync carries changes
  between laptops in the background and merges them scene by scene.

| Read | |
| --- | --- |
| [docs/cloud-sync.md](docs/cloud-sync.md) | How offline and sync work (the plane scenario), and setting up your Cloudflare server |
| [ROADMAP.md](ROADMAP.md) | What's done and what's next |
| [docs/file-format.md](docs/file-format.md) | What's inside a `.quire` file |

## Try it

1. Download [`dist/Quire.html`](dist/Quire.html). It's one file with everything inside.
2. Open it in **Chrome or Edge**.
3. **Explore a sample**, start a **New story**, or **Import a draft** from Word, Markdown or text.

For sync between laptops, follow [docs/cloud-sync.md](docs/cloud-sync.md): about 10 minutes, on
Cloudflare's free tier.

## Features

| Area | What you can do |
| --- | --- |
| **Manuscript** | Chapters and scenes in a drag-and-drop binder. Write one scene, a chapter, or the whole book as one page. Split a scene at the cursor (`Ctrl+Shift+Enter`) or merge it with the next. |
| **Writing** | Clean serif page; bold and italic; smart quotes, `--` to em dash, `...` to ellipsis. Pasting from Word or Google Docs keeps italics and drops the clutter. |
| **Focus mode** | `Ctrl+Shift+F`: full screen, the current paragraph bright, typewriter scrolling. |
| **Scene details** | Status, synopsis, POV, characters, setting, plot threads, when, notes. Spots characters and places named in your text. |
| **Board** | Index cards per chapter; drag to reorder. |
| **Story map** | Every scene against your plot threads, characters (in scene / POV) and places. |
| **Characters & places** | Sheets with custom fields, aliases, notes, and an automatic "appears in" list. |
| **Search & replace** | `Ctrl+K` finds anything and runs any command. `Ctrl+H` replaces across the manuscript (match case, whole words). |
| **Undo** | `Ctrl+Z` undoes deletes, moves, splits, merges, replace-all and imports, not just typing. |
| **Import / export** | Import Word (`.docx`), Markdown and text, split into chapters and scenes. Export Word in standard manuscript format, Markdown, text, or print/PDF. |
| **Safety** | Autosaves to your file. Browser backups, 10-minute snapshots, crash recovery. If the file was changed by another computer (e.g. via Dropbox), Quire merges rather than overwriting. |
| **Cloud sync** | Your own Cloudflare Worker + R2. Background sync, a three-way merge per scene, both versions kept on real conflicts, every cloud story downloaded for offline, cloud version history. |
| **Installable** | Hosted on your Cloudflare address it installs as an app: cached for offline, opens `.quire` files, offers updates. |
| **Progress & looks** | Word counts, book and daily goals, 14-day chart; Paper, Sepia and Night themes; typeface, size, line length. |

### Keyboard

| Keys | Action |
| --- | --- |
| `Ctrl+K` | Find anything, run any command |
| `Ctrl+S` / `Ctrl+Shift+S` | Save now / Save as |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo (typing while in text; otherwise structure) |
| `Ctrl+H` | Find and replace |
| `Ctrl+Enter` / `Ctrl+Shift+Enter` | New scene / split scene at cursor |
| `Ctrl+Shift+F` | Focus mode (`Esc` to leave) |
| `Ctrl+.` / `Ctrl+\` | Scene details / sidebar |
| `Alt+1` … `Alt+6` | Manuscript, Board, Story map, Characters, Places, Notes |

On a Mac, `Cmd` works wherever `Ctrl` is listed. (`Cmd+H` hides the window on a Mac, so
use the menu for find and replace there.)

## Layout

```
app/         the app: index.html, styles.css, app.js, merge.js (sync merge), docx.js (Word),
             sw.js + manifest (installable app)
cloud/       worker.js (sync API + serves the app) and wrangler.toml (Cloudflare deploy)
dist/        Quire.html: the single-file build (dist/web/ is built on deploy, not committed)
docs/        cloud sync, file format
samples/     The Lantern Keeper.quire
tools/       build, tests, local dev server
```

## Developing and testing

No framework and no dependencies. Node 18+ and the Playwright browser for tests.

```bash
node tools/build.mjs              # app/ -> dist/Quire.html and dist/web/
```

```bash
node tools/test-merge.mjs         # three-way merge: 14 scenarios
```

```bash
node tools/test-worker.mjs        # sync API against an in-memory R2
```

```bash
node tools/smoke.mjs              # the app end to end in Chromium, from file://
```

```bash
node tools/sync-e2e.mjs           # two laptops, a flight with no network, merge on landing,
                                  # and the installed app opening with the server switched off
```

`sync-e2e.mjs --server http://127.0.0.1:8787 --token KEY` runs the same test against
`npx wrangler dev`, Cloudflare's own runtime.
