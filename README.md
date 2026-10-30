# CampusCompute — Intelligent Job Scheduling and Resource Management

OS Level-3 mini-project (UE25CS242A). A command-line prototype in C for Linux/Ubuntu
that demonstrates CPU scheduling, thread synchronisation, shared-memory IPC with
semaphores, the Banker's algorithm, worker failure detection and resource limits.
All output is produced by the algorithms at run time; nothing is hard-coded.

## Prerequisites (Ubuntu 20.04 / 22.04 / 24.04 or WSL Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential zip unzip
```

## Build

```bash
make            # builds ./campuscompute  (gcc -Wall -Wextra, links -pthread -lrt)
make clean      # removes build/ and the executable
```

Without make: `gcc -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -Iinclude src/*.c -o campuscompute -pthread -lrt`

## Commands

| Command | Requirement | What it shows |
|---|---|---|
| `./campuscompute help` | FR8 | all commands and examples |
| `./campuscompute jobs data/jobs.txt` | FR1 | job loading, validation, lifecycle state |
| `./campuscompute fcfs data/jobs.txt [-v]` | FR2 | FCFS order, Gantt chart, CT/TAT/WT/RT, averages |
| `./campuscompute rr data/jobs.txt 3 [-v]` | FR2 | Round Robin with quantum 3 |
| `./campuscompute priority data/jobs.txt [-v]` | FR2 | non-preemptive Priority |
| `./campuscompute compare data/jobs.txt 3` | FR2 | all three policies on the same job set |
| `./campuscompute logger 3 2 5 4` | FR3 | 3 producer + 2 consumer threads, queue capacity 4 |
| `./campuscompute ipc 12` | FR4 | producer/consumer processes via shared memory + named semaphores |
| `./campuscompute banker data/banker_safe.txt` | FR5 | safe state + safe sequence |
| `./campuscompute banker data/banker_unsafe.txt` | FR5 | unsafe state detected |
| `./campuscompute workers 3` | FR6 | fork + waitpid, all workers succeed |
| `./campuscompute workers 4 3 exit` | FR6 | worker 3 **simulates** failure via `_exit(42)` |
| `./campuscompute workers 4 2 signal` | FR6 | worker 2 **simulates** failure via `abort()` (SIGABRT) |
| `./campuscompute rlimit [cpu\|nofile\|mem\|all]` | FR7 | setrlimit effects (SIGXCPU, EMFILE, ENOMEM) |
| `./campuscompute cleanup-check [--force]` | FR7 | no leftover threads, children or IPC objects |
| `./campuscompute demo` | all | integrated run + PASS/FAIL summary |
| `make test` | all | runs 20 checks including invalid inputs |

`-v` prints the job lifecycle trace (NEW → READY → RUNNING → COMPLETED).
Exit codes: 0 success, 1 runtime/input-file error, 2 usage error.

## Input formats

**Job file** — one job per line, `#` starts a comment:
```
# id  arrival  burst  priority     (lower priority number = higher priority)
1     0        6      3
```
Validation: id 1–9999 and unique, arrival 0–100000, burst 1–1000, priority 0–99,
exactly 4 integer fields. Any bad line rejects the whole file with line numbers.

**Banker state file** — `n m`, then Available (m values), Allocation (n×m), Maximum (n×m).
Need = Maximum − Allocation is computed. Allocation > Maximum is rejected.

## Design notes

- **Scheduling tie-breaks.** FCFS: earlier arrival, then lower job ID.
  RR (default quantum 3): FIFO queue by (arrival, ID); a job arriving at the instant a
  quantum expires is queued before the pre-empted job. Priority: lower number wins,
  ties → earlier arrival → lower ID; non-preemptive. Idle CPU gaps appear as `IDLE`.
- **Logger (FR3).** Bounded circular queue guarded by one `pthread_mutex_t`;
  `not_full` / `not_empty` condition variables with while-loop waits. Shutdown: main
  joins producers, sets `producers_done`, broadcasts `not_empty`; consumers exit only when
  the queue is empty and producers are done, so no event is lost. Events go to
  `logs/campuscompute.log`.
- **IPC (FR4).** `shm_open`+`mmap` ring buffer and three named semaphores (`empty`,
  `full`, `mutex`). Both child processes re-open them **by name**. Object names contain
  the parent PID (`/campuscompute_shm_<pid>`), so cleanup only touches this program's objects.
- **Workers (FR6).** Failures are clearly labelled *SIMULATED* — they are controlled
  test cases, not real OS faults. Status decoded with `WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG`.
- **Limits (FR7).** Applied inside forked children so the parent shell is unaffected:
  `RLIMIT_CPU` 1 s (child receives SIGXCPU), `RLIMIT_NOFILE` 10 (open() → EMFILE),
  `RLIMIT_AS` current+64 MiB (128 MiB `mmap` fails with ENOMEM, 16 MiB succeeds). `mmap` is
  used instead of `malloc` because optimising compilers may delete an unused malloc/free pair.

## Cleanup

Every command frees its own threads, processes, memory, shared memory and semaphores.
Verify with:
```bash
./campuscompute cleanup-check
ls /dev/shm | grep campuscompute     # prints nothing
ps -e | grep campuscompute           # prints nothing
```
If a run is killed mid-IPC (e.g. Ctrl+C), remove leftovers with
`./campuscompute cleanup-check --force` (it only removes objects named `campuscompute_*`
owned by you). `make distclean` also deletes `logs/` and zip files.

## SRN terminal prompt

```bash
source scripts/set_srn_prompt.sh YOUR_FULL_SRN --persist
```
Changes the prompt to `[YOUR_FULL_SRN] ~/CampusCompute$ `. `--remove` undoes it.

## Project layout

```
Makefile  README.md  SCREENSHOTS.md
include/  common.h jobs.h scheduler.h logger.h ipc.h banker.h workers.h
src/      main.c jobs.c scheduler.c logger.c ipc.c banker.c workers.c rlimit.c util.c
data/     jobs.txt jobs_idle.txt jobs_invalid.txt banker_safe.txt banker_unsafe.txt banker_invalid.txt
scripts/  set_srn_prompt.sh run_tests.sh
```
