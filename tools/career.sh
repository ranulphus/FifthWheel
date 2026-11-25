#!/bin/sh
# career.sh - a new career played through by the autopilot, headless (make
# career; run in the dev container): the progression from $250 and box
# trailers, never stuck. Three runs, one line each in out/career/results.txt:
#   plain     8 depot jobs; licences bought as soon as it can pay
#   spender   5 depot jobs, the first 6 cosmetics bought whenever it can:
#             no trailer it may take, then shunts and a parcel round to earn
#             its way back to a licence
#   rounds    25 parcel rounds, five in each town, from the depots in turn:
#             the towns' streets, never touching anything solid
# Fails if any run is not "HX-TEST career PASS".
#
#   tools/career.sh
set -u
out=out/career
exe=build/headless/fwheel-hl
mkdir -p $out
fail=0
for run in "plain:-autocareer 8" "spender:-autocareer 5 -spender 6" "rounds:-autocareer 0 -rounds 25"; do
    name=${run%%:*}
    $exe -test -fixed -nosound -nodraw ${run#*:} -frames 8000000 > $out/$name.log 2>&1
    line=$(grep -ao 'HX-TEST career .*' $out/$name.log)
    echo "career: $name: ${line:-no result}"
    echo "$line" | grep -q 'career PASS' || fail=1
done > $out/results.txt
cat $out/results.txt
exit $fail
