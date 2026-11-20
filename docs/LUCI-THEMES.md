# LuCI under any theme: the rules we keep

Source: the footstrap theme's guide for app authors, `docs/luci-app-styling-guide_ru.md` in
https://github.com/VizzleTF/luci-theme-footstrap (read 2026-11-02; behaviour only, nothing copied). The owner tests
on Bootstrap and footstrap; Aurora was checked in r129. footstrap swaps views without reloading the page (SPA), so
what a view leaves behind lives on into the next one.

## Rules

1. **CSS lifetime.** A stylesheet that names only our own classes may sit in `<head>` (the status include's
   `dashboard.css`, the bot pages' `#mmb-css`). Never a rule on a stock selector (`.cbi-*`, `table`, `th`, `pre`,
   `h4`) outside our own subtree: under an SPA theme it would restyle every later page.
2. **Names.** Every class and id starts with `mm-` (pages, the status include) or `mmb-` (the bot pages). Custom
   properties: `--mm-*` only, never a bare `:root { … }` beyond our own `--mm-*`. No `* { … }` reset, no framework.
3. **Colours.** The theme contract is 26 names: `--background-color-{high,medium,low}`, `--border-color-{high,medium,low}`,
   `--text-color-{highest,high,medium,low}`, and `--{primary,success,warn,error}-color-{high,medium,low}` with
   `--on-{primary,success,warn,error}-color`. Always with a literal fallback: `var(--warn-color-medium, #9a6700)`.
   The family is `warn`, never `warning`; there is no `--text-color` without a level. Text on a coloured fill takes
   its ink from `--on-*-color`, not `#fff`. Never read `--fs-*` (footstrap's private tier).
4. **Dark mode.** Ask the theme: `:root[data-darkmode="true"]` (Bootstrap, footstrap) beside
   `@media (prefers-color-scheme: dark)` guarded by `:root:where(:not([data-darkmode="false"]))`, as dashboard.css
   does; footstrap also stamps `data-theme` and `data-bs-theme`. Best: colours from the contract, no detection.
5. **Layout.** The content column is narrower than the viewport (a sidebar of ~224 px): size by `@container`
   (`.mm-root` is an inline-size container) rather than `@media (max-width)`; no fixed px widths on containers.
6. **SPA.** Work in `render()`, never `window.onload`; pollers through `L.Poll`; nothing long-lived on `<body>` (our
   toasts sit in `#view` since 2026-11-02, never inside `.mm-root`: a container-query root holds a fixed box); no
   `popstate` handlers; stock Save/Apply hidden by `handleSaveApply = null`, not by CSS.
7. **`!important`** only inside our own subtree, never to beat a theme's stock rule elsewhere.

## Our state (2026-11-20, r14)

| Rule | State |
|---|---|
| 1 Lifetime | ok: dashboard.css and #mmb-css name only `mm-`/`mmb-` (two dead `.fx-*` rules removed) |
| 2 Names | ok |
| 3 Colours | ok: frames, buttons, borders, tabs and owner tags read the contract with a fallback (r14). Kept fixed on purpose, light and dark pairs: the backup, warn and paused pills and the USB chips (bench 2026-10-15: a theme's bright yellow on white could not be read), the filled blue button (Bootstrap's light theme made an outlined one vanish), the chart palette (the contract has no categorical colours) |
| 4 Dark mode | ok by `data-darkmode` and the guarded media query |
| 5 Layout | ok: every width query is `@container` (r14); the status include on «Обзор» is its own container. `@media` stays only for the pointer, dark mode and reduced motion |
| 6 SPA | ok: no onload/popstate/intervals; toasts moved into `#view` |
| 7 !important | ok: 8, all inside `.mm-root`/`.mm-modem`/`.mm-status` |

Left: the remaining literal colours to the contract; the status include's phone layout by container, not viewport.
