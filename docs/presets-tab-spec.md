# Presets tab spec (from the user via raptor-coder, 2026-11-05)

Status: not started. Raptor relayed a go "after the SysEx/dump fix works"; confirm with the user directly before starting.

Third tab "Presets" (after Connection, Panel). English UI only.

Prerequisite: SysEx dumps work. Find the name inside the unpacked 1024-byte program (request a preset with a known name, search the bytes; Sequential usually keeps ~20 ASCII chars). Confirm bank/program counts on this unit.

1. List: bank selector + table of programs (number, name, "updated"/"cached" status). Click = select; double-click or "Load" = Program Change (+ Bank Select if needed). Loading is ok without asking; anything that writes into the synth's memory is not.
2. "Fetch all banks": program dump requests (F0 01 <id> 05 bank prog F7) one by one, throttled (wait for each reply, timeout + retry once), progress bar + Cancel, never blocks UI or audio thread. Errors per program.
3. "Save all to disk": choose a folder; one .syx per program (received bytes unchanged) + index (bank, program, name), e.g. "B1-042 Name.syx". Default Documents\Trigon6 Control\Banks\<date>.
4. "Save preset as...": selected program (or current edit buffer) to .syx with a custom name in a user-picked folder; remember the last folder.
5. Local cache on disk (names + dumps), shown at start without asking the synth; "Refresh all", "Re-read this preset".
6. Staleness: WRITE on the synth probably sends nothing (verify). On a Program Change from the synth, quietly re-request that program; if it differs from the cache, update the cache and mark "updated", no modal prompt. If the synth sends something on WRITE, update that program at once. Never overwrite the user's "Save preset as" files.
7. Core logic (name extraction, file naming, cache diff) in src/core with tests in tests/CoreTests.cpp; JUCE only for UI/IO.
8. Out of scope: sending presets back to the synth (separate yes needed: overwrites memory), sequencer import/export.
