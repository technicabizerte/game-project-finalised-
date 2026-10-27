# The Tailies — landing page

Static landing page (plain HTML + CSS + a little vanilla JS, no build step) for
**The Tailies**, ready to deploy on Vercel.

## Folder structure

```
landing/                 <- push THIS folder to Vercel (it is the project root)
├── index.html           landing page
├── style.css            all styling
├── vercel.json          COOP/COEP headers for /play/ (WASM cross-origin isolation)
├── assets/
│   └── logo.png         <-- DROP YOUR LOGO HERE (hero image)
├── screenshots/
│   ├── 1.png ... 5.png  <-- DROP YOUR 5 SCREENSHOTS HERE
└── play/
    └── index.html       placeholder "coming soon" page
                         <-- DROP YOUR GAME BUILD HERE (overwrite this folder)
```

## Where exactly to drop your files

| What | Where | Notes |
|------|-------|-------|
| **Logo** | `landing/assets/logo.png` | PNG, transparent bg, ~512px. |
| **Screenshots** | `landing/screenshots/1.png` … `5.png` | `1.png` is the big featured tile. |
| **Game build** | `landing/play/` | Copy the **contents** of your Emscripten output (`build/web/index.html`, `index.js`, `index.wasm`, `index.data`) into `landing/play/`, replacing the placeholder `index.html`. The result must serve at `/play/`. |

Missing images don't break anything — each shows a labelled placeholder until you
add the real file with the matching name.

## Deploy to Vercel

**Option A — drag & drop / CLI**
```bash
cd landing
npx vercel        # first run links the project; follow prompts
npx vercel --prod # production deploy
```

**Option B — Git import**
Push the repo to GitHub and import it in the Vercel dashboard. If `landing/` is a
subfolder of a larger repo, set **Root Directory = `landing`** in the Vercel
project settings. No framework, no build command, no install command — it's pure
static output.

## Notes

- **PLAY NOW** links to `/play/`. On click it does a quick check: if `/play/` is
  live it navigates there; if not, it shows an inline "not deployed yet" note
  instead of dumping you on a 404.
- `vercel.json` sets `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` on `/play/(.*)` so the WASM build
  runs cross-origin isolated (needed if it ever uses SharedArrayBuffer / threads).
  Because of those headers, the game's assets under `/play/` must be same-origin
  (they are) — don't hot-link cross-origin resources from inside `/play/`.
- Fonts (Press Start 2P + Inter) load from Google Fonts on the landing page only.
- Local preview: `cd landing && python3 -m http.server 8000` → http://localhost:8000
  (the PLAY check will show the "not deployed" note locally unless `/play/` exists).
```
