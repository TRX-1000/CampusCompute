#!/usr/bin/env bash
# Place this file beside the CampusCompute Makefile, then run:
# bash campuscompute-demo.sh

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
if [[ -f "$script_dir/Makefile" && -d "$script_dir/src" ]]; then
    cd -- "$script_dir" || exit 1
elif [[ ! -f Makefile || ! -d src ]]; then
    printf 'Open the CampusCompute project folder containing Makefile and src.\n'
    printf 'Place this script there, then run: bash campuscompute-demo.sh\n'
    exit 1
fi

if [[ ! -t 0 ]]; then
    printf 'Run this script in an interactive Ubuntu or WSL terminal.\n' >&2
    exit 1
fi

step=0
run_step() {
    local speaker=$1 cue=$2 expected=$3
    shift 3
    local answer status
    step=$((step + 1))
    printf '\n------------------------------------------------------------\n'
    printf 'Step %d | %s\n' "$step" "$speaker"
    printf 'Next command: '
    printf '%q ' "$@"
    printf '\nExplain: %s\n' "$cue"
    printf 'Press Enter to run, or type q and Enter to quit: '
    IFS= read -r answer || exit 0
    if [[ "$answer" == q || "$answer" == Q ]]; then
        exit 0
    fi
    "$@"
    status=$?
    if [[ $status -ne $expected ]]; then
        printf '\nStopped: expected exit code %d, received %d.\n' "$expected" "$status" >&2
        printf 'Check the output above before restarting the demo.\n' >&2
        exit 1
    fi
    if [[ $expected -ne 0 ]]; then
        printf '\nExpected input rejection confirmed (exit code %d).\n' "$status"
    fi
}

printf 'CampusCompute guided demo\n'
printf 'Each command waits for Enter. Its output stays visible while you explain.\n'
printf 'The next prompt pauses the demo until you are ready to continue.\n'

run_step 'Setup' \
    'Build the project before showing its features.' 0 \
    make

run_step 'Teammate 1' \
    'Introduce CampusCompute and the OS concepts shown by these commands.' 0 \
    ./campuscompute help

run_step 'Teammate 1' \
    'The four columns are ID, arrival, burst and priority. Lower priority numbers win.' 0 \
    cat data/jobs.txt

run_step 'Teammate 1' \
    'The loader validates each row and rejects malformed values and duplicate IDs.' 0 \
    ./campuscompute jobs data/jobs.txt

run_step 'Teammate 1' \
    'Quantum 3: J1 runs from 0 to 3. J2, J3 and J4 queue before unfinished J1. Point to the trace and metrics.' 0 \
    ./campuscompute rr data/jobs.txt 3 -v

run_step 'Teammate 1' \
    'Compare FCFS, Round Robin and non-preemptive Priority on identical input. Read the actual average waiting times.' 0 \
    ./campuscompute compare data/jobs.txt 3

run_step 'Teammate 1' \
    'Need equals Maximum minus Allocation. A finishing process releases its allocation. Point to the safe sequence.' 0 \
    ./campuscompute banker data/banker_safe.txt

run_step 'Teammate 1' \
    'This valid state has no guaranteed completion sequence under its maximum demands. The verdict is UNSAFE.' 0 \
    ./campuscompute banker data/banker_unsafe.txt

run_step 'Teammate 1' \
    'This malformed file should be rejected. After explaining the error, hand over to Teammate 2.' 1 \
    ./campuscompute jobs data/jobs_invalid.txt

run_step 'Teammate 2' \
    'Three producers generate five events each: 15 total. A mutex and condition variables protect a four-slot queue.' 0 \
    ./campuscompute logger 3 2 5 4

run_step 'Teammate 2' \
    'Two processes exchange 12 records through shared memory. Explain empty, full and mutex, then show checksum and cleanup.' 0 \
    ./campuscompute ipc 12

run_step 'Teammate 2' \
    'Worker 3 deliberately exits with code 42. The parent reaps children with waitpid and detects the failure.' 0 \
    ./campuscompute workers 4 3 exit

run_step 'Teammate 2' \
    'Worker 2 deliberately triggers SIGABRT. Show the parent identifying signal termination.' 0 \
    ./campuscompute workers 4 2 signal

run_step 'Teammate 2' \
    'CPU time produces SIGXCPU, open descriptors produce EMFILE, and address space produces ENOMEM. Limits run inside children.' 0 \
    ./campuscompute rlimit all

run_step 'Teammate 2' \
    'Check current threads and children, plus leftover CampusCompute processes and IPC objects.' 0 \
    ./campuscompute cleanup-check

run_step 'Both teammates' \
    'Let the integrated run finish. Explain your own modules and point to the actual ten-check summary and cleanup result.' 0 \
    make demo

printf '\nDemo sequence finished. Review the displayed results with the examiner.\n'
