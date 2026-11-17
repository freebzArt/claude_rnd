# Cloud sync and offline use

## The short version

**Quire never opens your story *from* the cloud. It opens it from your computer.** The cloud is a
courier that carries changes between your computers whenever there's a connection. So Quire opens
and saves the same way with or without one: no spinner, no "you're offline", no blank page.

## What happens on a plane

You write on your Windows laptop at home, then take your Mac on a flight.

1. **At home (online):** each change saves to the story on the Windows laptop first, then syncs to
   your Cloudflare account a few seconds later.
2. **Before the flight:** the Mac needs to have had Quire open with a connection at some point
   *after* you finished writing on Windows. Whenever Quire is open and online, it quietly downloads
   every cloud story that has changed, not just the one you're working on. The welcome screen shows
   "In your cloud · checked 5 min ago", and each story says **on this computer**.
3. **On the plane (offline):** open Quire (`Quire.html`, or the installed app) and click the story.
   It opens straight away from what's on the Mac, including what you wrote on Windows. The status
   bar says **Offline · saved on this computer**, and every word you type is saved on the Mac.
4. **After landing:** as soon as there's a connection, Quire syncs. Edits made on the two laptops
   are merged.

The one thing no app can do: if you wrote on Windows and then never had Quire open on the Mac while
online, the Mac can't have those words. **Tip:** open Quire on the travel laptop for a moment
before you leave, and check that the story says "on this computer".

### How merging works

Every chapter, scene, character and note has a permanent id, so Quire merges piece by piece:

- Different scenes edited on each laptop: both edits are kept.
- A scene renamed on one laptop and rewritten on the other: both changes are kept.
- Tags (who's in a scene, which plot threads it carries): additions and removals from both are kept.
- Something deleted on one laptop but edited on the other: the edited copy stays.
- **The same scene's text changed on both laptops:** Quire can't know which you want, so it keeps
  **both**. The other version appears right after it as "(Scene name) (other version)" with a note
  explaining why. Keep one, combine them, delete the other.

The cloud also refuses to let one laptop overwrite another's newer save, which uses standard HTTP
`If-Match` revisions. The laptop that loses the race fetches, merges and tries again.

### Why this won't fail like Notion or Google Docs offline

| | Notion / Google Docs | Quire |
| --- | --- | --- |
| Where the app comes from | Downloaded from their servers each time; offline relies on a cache that may or may not be complete | A file on your disk (`Quire.html`), or an installed app that keeps every file it needs before it says "Ready to work offline" |
| Where your story comes from | Their servers; an offline copy only if you remembered to enable it per document | Your computer, always. The cloud is only a backup and courier |
| Bad Wi-Fi (hotel or plane portals) | Can hang on load | Never waits for the network to open or save; sync times out in the background |

## Setting up your Cloudflare sync server (about 10 minutes)

You need a free Cloudflare account and Node.js (version 18 or newer) on the computer you set it up from.

1. Get the code and open a terminal in `quire/cloud`.
2. Log in to Cloudflare:

   ```bash
   npx wrangler login
   ```

3. Create the storage bucket (R2; the free tier is plenty for writing):

   ```bash
   npx wrangler r2 bucket create quire-stories
   ```

4. Choose a long random **sync key**. It's the password your laptops use, so keep it in your password
   manager. Store it on Cloudflare:

   ```bash
   npx wrangler secret put QUIRE_TOKEN
   ```

5. Deploy. This builds the app and uploads it with the sync API:

   ```bash
   npx wrangler deploy
   ```

   It prints your address, like `https://quire.your-name.workers.dev`.

6. On **each laptop**, in Quire: **Menu › Cloud sync…**, enter the address and the sync key, and
   click **Connect**. Then open a story and switch on **Sync this story**.

Updating Quire later is the same `npx wrangler deploy`. Installed copies offer an **Update**
button; nothing reloads underneath you.

### Two ways to run Quire on each laptop

- **`Quire.html` from a file** (the most robust): download it from your site (Menu › Download
  Quire.html) or from `dist/`. Keep it in Documents and pin it. It's a file on disk, so it can't
  fail to load.
- **Installed app**: visit your Quire address in Chrome or Edge and choose **Install** in the
  address bar. You get Quire in its own window, in the Start menu or Dock, and `.quire` files open
  in it by double-click. Wait for the welcome screen to say **✓ Ready to work offline** once.

Both talk to the same sync server, and you can use either on either laptop.

### Try it locally first (optional)

```bash
node tools/build.mjs && node tools/dev-server.mjs --data ./cloud-data
```

This runs the same Worker code on your machine at `http://localhost:8787` with sync key `dev-token`.

## Security notes

- The sync key travels only to your Worker over HTTPS. On each laptop it's stored in that browser's
  storage, so anyone who can use your logged-in laptop can sync. **Disconnect this computer** removes it.
- Stories are stored in your own R2 bucket. They're private to your account but not end-to-end
  encrypted, so Cloudflare could technically read them. End-to-end encryption is on the roadmap.
- Want an extra lock on the web app itself? Put **Cloudflare Access** (Zero Trust, free for one
  person) in front of your Quire address. Exclude the `/api/*` path from Access, since the sync key
  already protects it, and that keeps `Quire.html`-from-a-file syncing.
- Deleted stories go to `trash/` in the bucket. The cloud keeps up to 200 earlier versions per story
  (at most one per 10 minutes), which you can restore from **Menu › Cloud sync › Cloud history**.

## The API (for the curious)

`cloud/worker.js`: `GET /api/stories`, then `GET|PUT|DELETE /api/stories/:id` (ETag/If-Match),
then `GET /api/stories/:id/history[/:version]`. Every request needs `Authorization: Bearer <sync key>`.
