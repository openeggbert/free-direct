# CI matrix note (not yet implemented)

**This document is a note about what a future CI setup should test. No `.github/workflows/*.yml`
file exists in this repository today, and this document does not change that.** Adding real CI
infrastructure is a separate, ask-first decision outside the scope of the task that produced this
note (`plan.md` TASK-24H-0012). Every command below has been run manually in this repository's
development environment and is known to work as described; none of it runs automatically yet.

## Why this matters

FreeDirect has several independent build-time options (`FREE_API_USE_SYSTEM_SDL3`,
`FREE_DIRECT_ENABLE_ENET`, `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`,
`FREE_DIRECT_DIAGNOSTICS`) that change what gets compiled and, in ENet's case, which tests are even
valid to run together. A single "build once, test once" CI job would miss real bugs that only
surface in one specific combination - the ASan/UBSan build in particular already caught one real
bug this session (a null-pointer-to-`memcpy` UB in `DirectPlayMessageQueue.hpp`, see `plan.md`
TASK-24H-0010) that the default build's tests never exercised.

## Recommended matrix

| # | Configuration | Configure command | Test command | Notes |
|---|---|---|---|---|
| 1 | Default (loopback only) | `cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON` | `ctest --test-dir build` | The baseline. Already headless - `directdraw_tests`/`directsound_tests` set `SDL_VIDEODRIVER=dummy`/`SDL_AUDIODRIVER=dummy` via CTest's own `ENVIRONMENT` test property, not the invoking shell, so no extra CI setup is needed for this. |
| 2 | ENet-enabled | Same as #1 plus `-DFREE_DIRECT_ENABLE_ENET=ON` | `ctest --test-dir build -L enet` **only** - not the unfiltered suite | Must be a **separate** CI job/step from #1, not an extra flag on the same job: the loopback-scoped `directplay_tests` suite fails under an ENet-enabled build by design (`EnetDirectPlayTransport`'s real async model breaks `directplay_tests.cpp`'s synchronous assumptions - see that file's own header comment). A CI job that ran the unfiltered `ctest` here would show a false-looking failure every time. |
| 3 | Sanitizer (ASan+UBSan) | Same as #1 plus `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` | `ctest --test-dir build` (unfiltered - loopback-only, same scoping as #1) | Catches memory-safety/UB bugs the default build's plain pass/fail assertions cannot. Can be combined with #2's flag too (`-DFREE_DIRECT_ENABLE_ENET=ON` also set) if a single sanitizer job should cover both; in that combined case the same `-L enet` vs. unfiltered split from row 2 still applies. |
| 4 | Header hygiene | (part of #1's build) | `ctest --test-dir build -R header_hygiene` | Cheap, fast, high-value: fails if any `SDL_`/`SDL3_net`/`ENet` identifier leaks into `include/*.h` (CLAUDE.md's Internal Backend Policy). Already included in #1's unfiltered run; called out separately here because it is a natural "run this first, fail fast" gate. |
| 5 | Integration: build through `free-eggbert` | `cmake -B build -S ../free-eggbert && cmake --build build -j$(nproc)` | (no test suite - a real game build has no CTest of its own) | Requires the `../free-eggbert` sibling checkout, so only runnable in a CI environment that checks out both repositories. Verifies the "diamond dependency" (`free-eggbert` → `free-direct` → `free-api`) still resolves and links, which #1-#4 alone cannot catch (they never build a real game). |
| 6 | Integration: build through `planetblupi` | `cmake -B build -S ../planetblupi && cmake --build build -j$(nproc)` | (no test suite) | Same rationale as #5, for the other target game. |

## Explicitly not in scope for this note

- Real `.github/workflows/*.yml` authoring (ask-first, separate task).
- `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` (system libenet) as a distinct matrix row - not exercised
  successfully in this development environment (no system `libenet` package installed here); the
  vendored `third_party/enet` path (row 2 above) is the one actually verified working.
- Windows/macOS runners - every command above has only been verified on Linux in this environment.
