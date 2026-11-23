#!/usr/bin/env bash
# Compares heuristic against exact on random small instances; every output is validated by ./checker.
# Usage: ./run_tests.sh [count=200] [heuristic_seconds=0.5]
set -u
cd "$(dirname "$0")"
N=${1:-200}; HT=${2:-0.5}
for f in exact heuristic checker gen; do g++ -std=c++17 -O2 -o $f $f.cpp || exit 1; done
mkdir -p tests
same=0; worse=0; bad=0; infeas=0; gapsum=0
for ((seed = 1; seed <= N; seed++)); do
    n=$((3 + seed % 6)); V=$((2 + seed % 3)); m=$((1 + seed % 3))   # n 3..8, V 2..4, m 1..3
    [ $V -eq 4 ] && [ $n -gt 6 ] && n=6                              # keep exact fast
    in=tests/t$seed.txt
    ./gen $seed $n $V $m > $in
    timeout 120 ./exact < $in > tests/t$seed.exact 2>/dev/null || { echo "seed $seed: exact failed/timeout"; bad=$((bad+1)); continue; }
    ./heuristic $HT $seed < $in > tests/t$seed.heur 2>/dev/null || { echo "seed $seed: heuristic crashed"; bad=$((bad+1)); continue; }
    e=$(./checker $in tests/t$seed.exact); h=$(./checker $in tests/t$seed.heur)
    case "$e$h" in *FAIL*) echo "seed $seed: CHECK FAILED exact=[$e] heur=[$h]"; bad=$((bad+1)); continue;; esac
    if [ "$e" = INFEASIBLE ]; then
        [ "$h" = INFEASIBLE ] && infeas=$((infeas+1)) || { echo "seed $seed: BUG exact infeasible, heuristic [$h]"; bad=$((bad+1)); }
        continue
    fi
    if [ "$h" = INFEASIBLE ]; then echo "seed $seed: heuristic found nothing, exact [$e]"; worse=$((worse+1)); continue; fi
    read -r _ eu et <<< "$e"; read -r _ hu ht <<< "$h"
    verdict=$(awk -v eu=$eu -v et=$et -v hu=$hu -v ht=$ht 'BEGIN {
        if (hu < eu || (hu == eu && ht < et - 1e-3)) print "BETTER";
        else if (hu == eu && ht <= et + 1e-3) print "SAME";
        else printf "WORSE %.2f", (hu == eu ? 100 * (ht - et) / et : -1) }')
    case $verdict in
        SAME) same=$((same+1));;
        BETTER) echo "seed $seed: BUG heuristic beats exact: exact [$e] heur [$h]"; bad=$((bad+1));;
        *) echo "seed $seed (n=$n V=$V m=$m): heuristic [$h] vs exact [$e] $verdict%"; worse=$((worse+1));;
    esac
done
echo "----"
echo "instances=$N  optimal-match=$same  heuristic-worse=$worse  both-infeasible=$infeas  errors/bugs=$bad"
