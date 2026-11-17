# The `.quire` file format (version 1)

A story is one UTF-8 JSON file, pretty-printed with two-space indentation so it reads
and diffs well. The extension is `.quire`; Quire also opens `.json`.

```jsonc
{
  "format": "quire",          // always "quire"
  "version": 1,               // format version; Quire fills in missing fields from older versions
  "app": "Quire 0.1.0",       // the app version that last wrote the file (informational)
  "meta": {
    "id": "p…",               // stable story ID (links browser backups and snapshots)
    "title": "The Lantern Keeper",
    "author": "",
    "created": "ISO-8601", "modified": "ISO-8601",
    "wordGoal": 80000, "dailyGoal": 1000,
    "cloud": true             // optional: this story syncs through Quire cloud (docs/cloud-sync.md)
  },
  "manuscript": [             // chapters, in order
    {
      "id": "c…", "title": "Chapter One", "synopsis": "", "notes": "",
      "children": [           // scenes, in order
        {
          "id": "s…", "title": "Opening",
          "status": "idea | draft | revised | final",
          "synopsis": "", "when": "Day 3, dusk", "notes": "",
          "pov": "<character id or empty>",
          "characters": ["<character id>", …],
          "location": "<place id or empty>",
          "threads": ["<thread id>", …],
          "content": "<p>Prose with <em>italic</em> and <strong>bold</strong>.</p>"
        }
      ]
    }
  ],
  "characters": [ { "id": "ch…", "name": "", "role": "", "color": "#b0643c", "aliases": "comma, separated",
                    "summary": "", "fields": [ { "k": "Goal", "v": "…" } ], "notes": "" } ],
  "locations":  [ /* same shape as characters; "role" is shown as "Kind" */ ],
  "threads":    [ { "id": "t…", "name": "", "color": "#…", "summary": "" } ],
  "notes":      [ { "id": "n…", "title": "", "content": "<p>…</p>" } ],
  "stats":      { "daily": { "2026-11-17": { "start": 41200, "end": 42310 } } }
}
```

## Rules

- **Prose** (`content`) is a sequence of `<p>` elements that may contain only `<em>`,
  `<strong>` and text, with HTML entities escaped. `<p><br></p>` is an empty paragraph.
  Quire sanitises on every save and paste, so other tools can rely on this.
- **IDs** are opaque strings, unique within the file and never reused. References
  (`pov`, `characters`, `location`, `threads`) point at them. A dangling reference is
  ignored.
- **Unknown fields are kept.** Tools may add their own keys; Quire passes them through untouched.
- **Daily stats** record the book's total word count at the start and end of each day
  the story was open, so `end - start` is net words written that day.
- Things that belong to one device (theme, font size, recent files, snapshots) are not in
  the file. They live in the browser.

## Reading a story with other tools

```bash
# Every scene title, in order
jq -r '.manuscript[].children[].title' "My Story.quire"
```

```bash
# Total words, roughly
jq -r '.manuscript[].children[].content' "My Story.quire" | sed 's/<[^>]*>/ /g' | wc -w
```
