#!/usr/bin/env bash
# Runs every CampusCompute feature with the repeatable inputs in data/.
# Usage: ./scripts/run_tests.sh      (from the project directory, after make)

cd "$(dirname "$0")/.." || exit 1
BIN=./campuscompute
[[ -x $BIN ]] || { echo "Build first: make"; exit 1; }

pass=0; fail=0
check() {   # check <expected-exit-code> <description> <command...>
    local want=$1 desc=$2; shift 2
    echo; echo ">>> $desc"; echo "\$ $*"
    "$@"; local got=$?
    if [[ $got -eq $want ]]; then
        echo "[TEST PASS] $desc (exit $got)"; pass=$((pass+1))
    else
        echo "[TEST FAIL] $desc (expected exit $want, got $got)"; fail=$((fail+1))
    fi
}

check 0 "help"                         $BIN help
check 0 "FR1 load valid jobs"          $BIN jobs data/jobs.txt
check 0 "FR2 FCFS"                     $BIN fcfs data/jobs.txt
check 0 "FR2 Round Robin q=3 + trace"  $BIN rr data/jobs.txt 3 -v
check 0 "FR2 Priority"                 $BIN priority data/jobs.txt
check 0 "FR2 idle CPU handling"        $BIN fcfs data/jobs_idle.txt
check 0 "FR2 comparison"               $BIN compare data/jobs.txt 3
check 0 "FR3 producer-consumer logger" $BIN logger 3 2 5 4
check 0 "FR4 shared-memory IPC"        $BIN ipc 12
check 0 "FR5 Banker safe"              $BIN banker data/banker_safe.txt
check 0 "FR5 Banker unsafe"            $BIN banker data/banker_unsafe.txt
check 0 "FR6 normal workers"           $BIN workers 3
check 0 "FR6 simulated failure (exit)" $BIN workers 4 3 exit
check 0 "FR6 simulated failure (signal)" $BIN workers 4 2 signal
check 0 "FR7 resource limits"          $BIN rlimit all
check 1 "FR8 malformed job file"       $BIN jobs data/jobs_invalid.txt
check 1 "FR8 invalid Banker state"     $BIN banker data/banker_invalid.txt
check 2 "FR8 unknown command"          $BIN fly
check 2 "FR8 bad quantum"              $BIN rr data/jobs.txt zero
check 0 "FR7 cleanup verification"     $BIN cleanup-check

echo
echo "=============================================="
echo " Test summary: $pass passed, $fail failed"
echo "=============================================="
[[ $fail -eq 0 ]]
