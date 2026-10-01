#!/bin/sh
# jobsweep.sh - the job autopilot on every job in the world: each depot's
# trailer to each other depot's three bays, headless, in parallel. Run in
# the dev container (make jobsweep). One line per job in out/jobsweep/results.txt;
# fails if any job is not delivered at grade OK or better.
#
#   tools/jobsweep.sh [JOBS]    JOBS parallel runs (default 8)
set -u
export LP_NUM_THREADS=${LP_NUM_THREADS:-1}    # llvmpipe: one thread a run
out=out/jobsweep
exe=build/headless/fwheel-hl
par=${1:-8}
mkdir -p $out

if [ "${JOBSWEEP_ONE:-}" ]; then        # one job: "D T B" (the worker xargs runs)
    set -- $JOBSWEEP_ONE
    log=$out/j$1-$2-$3.log
    $exe -test -fixed -autojob -job $1:$2:$3 -nosound -nodraw -frames 72000 > $log 2>&1
    echo "$1:$2:$3 | $(grep -o 'HX-TEST job .*' $log) | $(grep -o 'FW-PILOT staged.*' $log)"
    exit 0
fi
rm -f $out/j*.log $out/results.txt

depots=$($exe -test -fixed -autojob -job 0:1:0 -nosound -nodraw -frames 1 2>&1 | sed -n 's/.* \([0-9]*\) depots.*/\1/p')
[ -n "$depots" ] || { echo "jobsweep: cannot read the world"; exit 1; }
d=0
while [ $d -lt $depots ]; do
    t=0
    while [ $t -lt $depots ]; do
        [ $d -ne $t ] && for b in 0 1 2; do echo "$d $t $b"; done
        t=$((t + 1))
    done
    d=$((d + 1))
done | xargs -P "$par" -I{} env JOBSWEEP_ONE="{}" "$0" > $out/results.txt.tmp
sort -t: -k1,1n -k2,2n -k3,3n $out/results.txt.tmp > $out/results.txt
rm -f $out/results.txt.tmp
total=$(wc -l < $out/results.txt)
pass=$(grep -c "job PASS" $out/results.txt)
grep -o "PASS delivered, [A-Z]*" $out/results.txt | sort | uniq -c | sed 's/^/jobsweep: /'
grep -v "job PASS" $out/results.txt | sed 's/^/jobsweep: FAIL /'
echo "jobsweep: $pass of $total jobs delivered"
[ "$pass" -eq "$total" ]
