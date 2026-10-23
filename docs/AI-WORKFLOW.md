# How this was built with an AI agent

This is not a “generate a port” story. It is a loop that the console kept
arbitrating, and the loop is the reusable part if you want to extend this project
with an agent of your own.

## The loop

1. **A behaviour, stated as a test.** Not “add languages” but “the ZIP reader
   returns a deflated entry byte-identical to the extracted copy, a stored PNG, a
   folder, and ENOENT for a missing entry”. `engine/pc/*.rb` is that list, ~250
   assertions, runnable without a console.
2. **A failing run.** The PC harness fails, or the console logs an `ERROR` with a
   backtrace. That artefact — not an opinion — is the bug report.
3. **The smallest change that makes it pass.** One gem, one patch, one stub. The
   engine's C and the game's Ruby are kept apart so a fix stays in one of them.
4. **Hardware.** A build goes to the 3DS, the user plays, and `log.txt` (or a
   photo) says what happened. Numbers in the documents come from those runs:
   `map load: 89 -> 131 in 2596 ms`, `perf: 41/41 fps, rb 6.5, dr 1.7 ms`.
5. **The record.** `RUN14` … `RUN20` write down the symptom, the cause, the fix and
   the check that proves it, in the same voice as the code comments.

## What the agent did, and what it did not

| Agent | Human |
|---|---|
| wrote the engine's C, the Ruby compatibility layer and the patches | chose the design: mruby, citro2d, the bottom-screen menu, the 40 fps target |
| wrote every `pc/*.rb` check and the tools in `tools/` | ran every build on a real New 3DS XL and reported what broke |
| wrote the documentation, including the run records | decided what “done” meant, and what was acceptable to trade away |
| found the causes from logs (e.g. `NoMethodError` at the credits) | decided the legal line: no game data in this repo, ever |

There is no emulator in the loop, which is why the log is treated as the source of
truth and why several fixes exist in the “the console says X” form: the animated
HOME banner that the console refused, the minisodes' invisible text, the ending's
Steam call.

## Driving an agent on this codebase

- Point it at `engine/README.md` (architecture) and `3DS-COMPAT-MATRIX.md` (what a
  real game needed, with the reason).
- Give it the failing check, not the feature: “`pc/minisode_check.rb hs1` fails at
  the first message” is actionable; “the minisodes don't work” is not.
- Ask for the check first when a behaviour is new. The agent is good at writing the
  assertion and bad at knowing which behaviour matters.
- Make it record the console log in the fix. The `RUN*.md` files are that habit.
- Keep the no-game-data rule mechanical: `tools/check_repo.py`, CI, and the
  `.gitignore` list are the enforcement, not trust.
