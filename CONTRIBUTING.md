# Contributing

Bug reports and port reports are the most useful contributions here; the engine
exists to be pointed at other games.

## Before a pull request

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1   # ~250 assertions, no console
python tools\check_repo.py                                     # no game data, no huge files
```

Both must pass. `check_repo.py` rejects a commit that adds `*.rgssad`, `*.ttma`,
`lang.dat`, `*.cia`, `*.3dsx`, `*.ttf`, `*.log` or a file over 5 MB — the repo ships
no game content, and CI runs the same check.

## Rules

1. **No game data**, in any form: archive, extracted files, fonts, music, text
   dumps, screenshots of the game's art, or a build that carries them.
2. **A fix comes with its test.** `engine/pc/` is the contract; add or extend a
   check that fails before the change and passes after (see `ending_check.rb`).
3. **Claims about hardware come from hardware.** Say what console, what build, and
   paste the relevant `log.txt` lines. "Runs fine" is not verifiable; a `perf:` line
   is.
4. **One change, one reason.** The `RUN*.md` records show the style: symptom, cause,
   fix, proof.
5. **Don't reformat unrelated code.** The documents quote line numbers and file
   sizes; churn makes them lie.

## Port reports

Use [the template](.github/ISSUE_TEMPLATE/port-report.md): game, RGSS version,
console model, what happens, and the tail of `log.txt`. If it boots on the PC
harness but not on hardware, say so — that narrows it enormously.

## Adding a game to the table

`docs/COMPATIBILITY.md` has a row per game tried. Add yours with the status
(`boots` / `plays` / `finishes`) and a one-line note about what it needed; that
table is the map for the next person.
