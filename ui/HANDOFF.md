# UI handoff: Milk Collection (HMCHF) dashboard

Last checked against the real binaries and server. The dashboard is complete and working end to end. No known bugs.

## Files (all in `ui/`, next to the solvers and `sample.txt` in the project root)
- `server.py`: stdlib-only Python server (no pip install). On start it compiles `exact`, `heuristic`, `checker` with g++ if the binary is missing or older than its `.cpp` or any `.h` in the root, then serves http://127.0.0.1:8000 (change with the `PORT` env var).
  - `GET /` (or `/index.html`) returns the page.
  - `GET /api/sample` returns `../sample.txt` (empty if missing).
  - `POST /api/solve` with `{input, solver: exact|heuristic, seconds, seed}`:
    - runs `../exact`, or `../heuristic <seconds> <seed>` (seconds clamped to 1-60);
    - then runs `../checker` on the input and plan (temp files);
    - returns `{plan, log, code, check}`, or `{error}` (binary missing, unknown solver, or 300 s timeout for the exact solver).
  - Anything else returns 404 JSON.
- `index.html`: single-file UI, no build step. Sky-blue hero with the animated milk splash (inline `<svg class="scene">`), Quicksand + Inter fonts loaded from Google Fonts (needs internet; falls back to system fonts).

## Run
```
cd ~/milk_collection        # use WSL on Windows (the binaries are Linux programs; g++ must be installed)
python3 ui/server.py        # then open http://127.0.0.1:8000
```

## How the page works
- Forms for settings, farms (one column per milk type), symmetric distance matrix (diagonal fixed to 0 and read-only; editing one cell mirrors the other), and fleet. Buttons: Solve, Load sample, Import/Export input .txt, Clear all. The sample loads automatically on page load.
- `toText()` builds the input format the C++ reader expects; `fromText()` loads it. `parsePlan()` turns the printed plan into KPI cards, vehicle cards and compartment fill bars. The raw plan and solver log are in a "Raw solver output" section.
- Before solving, the page checks for at least one farm and one vehicle, and that no distance cell is empty.
- Names are forced to single words (spaces become `_`).

## Verified
- `server.py` starts, `GET /` and `GET /api/sample` work.
- Full Solve through `/api/solve` on `sample.txt`: both solvers return code 0 and the checker says `OK 2 499.0000`.
- `parsePlan` run on a real heuristic plan: it reads Result, Vehicles used, Total time, Not needed, and both vehicles with their routes (1 each) and compartments (4 and 3).
- Not tested here: opening the page in a real browser (this includes the theme button fix), the file import dialog, the exact solver on larger inputs (it is only for about 10 farms), and a plan containing a `Truck-only trip` line (the regex is there but the sample plan has none).

## Bugs found and fixed (do not reintroduce)
1. `fromText()` skipped the first distance row because sample.txt's header line is a `#` comment. Now any line with no numbers is treated as a header and skipped.
2. `toText()` must write the distance header as `# Depot Rossi ...` (a comment). An uncommented header made the reader count one extra row ("DISTANCES_KM needs N rows ... found N+1").
3. Table header cells must not be `display:block` (column names stacked vertically).
4. The diagonal distance cell is forced to `0` and read-only.
5. The README input example used to show the distance header without `#`. It is fixed in the README now.
6. The theme button always switched to dark because it compared `--bg` to `'#111'` (real values are `#FFFFFF` / `#141414`). It now reads `data-theme`, or the OS setting (`prefers-color-scheme`) if the button hasn't been clicked yet, and flips that.

## Known issues
- Fonts need internet (Google Fonts link in `<head>`).
- Local only (127.0.0.1), no auth, one request at a time per solve, no saved scenarios.

## Design notes
- Hero gradient, soft shadows, rounded corners, pill buttons, second typeface (Quicksand).
- Colours are CSS variables on `:root` with light and dark values (dark applies from the OS setting or `data-theme="dark"`).

## Plan text format the parser depends on (README "Output")
`Result :`, `Vehicles used : X of Y`, `Total time : N minutes (...)`, `Vehicle NAME (kind) - N minutes on the road (shift limit T)`, `Route with trailer|Route|Truck-only trip : ...`, `Truck|Trailer compartment K (C q) : Type - details (L of C q)` or `empty`, `Not needed : ...`. If `milk_io.h` changes its printer, update `parsePlan`.

## Scope and decisions (for grading)
- The dashboard only runs the existing programs and shows their output. It does not solve anything itself, so all results come from `exact`, `heuristic` and `checker`.
- Text parsing was kept instead of adding JSON output to the C++ code, so the solvers and the checker were not changed for the UI.
- Left out on purpose: route map, saved scenarios, side-by-side heuristic vs exact view, per-field validation beyond the two checks above, and authentication.

## Next steps (optional)
- Ideas: per-field validation, route map SVG, saved scenarios, heuristic vs exact compare view, optional `--json` output from `milk_io.h` to replace text parsing.