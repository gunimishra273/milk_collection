#!/usr/bin/env bash
# Edge cases: both solvers must agree with the expected status; every plan they print is validated by ./checker.
cd "$(dirname "$0")"
for f in exact heuristic checker; do g++ -std=c++17 -O2 -o $f $f.cpp || exit 1; done
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
pass=0; fail=0
t() {  # name expected(FEASIBLE|INFEASIBLE|ERROR) instance
    printf "%s\n" "$3" > "$tmp/in"
    for b in exact heuristic; do
        if ! ./$b 0.2 < "$tmp/in" > "$tmp/out" 2>/dev/null; then got=ERROR
        else case $(./checker "$tmp/in" "$tmp/out") in OK*) got=FEASIBLE;; INFEASIBLE) got=INFEASIBLE;; *) got=CHECKER-FAIL;; esac
        fi
        if [ "$got" = "$2" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL [$1] $b: expected $2 got $got"; fi
    done
}
H="SPEED_KM_PER_HOUR 60"
t "no farmers" FEASIBLE "SHIFT_MINUTES 100
$H
MILK_TYPES A B
FARMS
DISTANCES_KM
Depot 0
FLEET
V1 truck 10 trailer 10"
t "no vehicles" INFEASIBLE "SHIFT_MINUTES 100
$H
MILK_TYPES A
FARMS
F1 trailer_ok 3 10
DISTANCES_KM
Depot 0 5
F1    5 0
FLEET"
t "zero-demand farmers only" FEASIBLE "SHIFT_MINUTES 100
$H
MILK_TYPES A
FARMS
F1 trailer_ok 3 0
F2 truck_only 3 0
DISTANCES_KM
Depot 0 5 5
F1    5 0 5
F2    5 5 0
FLEET
V1 truck 10"
t "farm beyond the shift" INFEASIBLE "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 30
F1    30 0
FLEET
V1 truck 10"
t "two types, one compartment" INFEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A B
FARMS
F1 trailer_ok 0 5 5
DISTANCES_KM
Depot 0 5
F1    5 0
FLEET
V1 truck 100"
t "two types, two vehicles (split by type)" FEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A B
FARMS
F1 trailer_ok 0 5 5
DISTANCES_KM
Depot 0 5
F1    5 0
FLEET
V1 truck 100
V2 truck 100"
t "demand > any vehicle (split pickup)" FEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A
FARMS
F1 trailer_ok 2 150
DISTANCES_KM
Depot 0 5
F1    5 0
FLEET
V1 truck 100
V2 truck 100"
t "truck_only farm, truck+trailer only" INFEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A
FARMS
F1 truck_only 0 5
DISTANCES_KM
Depot 0 5
F1    5 0
FLEET
V1 truck 10 trailer 10"
t "truck_only farm via truck-only trip (A4 anchor)" FEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A
FARMS
F1 truck_only 0 5
F2 trailer_ok 0 1
DISTANCES_KM
Depot 0 5 5
F1    5 0 5
F2    5 5 0
FLEET
V1 truck 10 trailer 10"
t "truck_only milk can't use trailer (A8)" INFEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A
FARMS
F1 truck_only 0 15
F2 trailer_ok 0 1
DISTANCES_KM
Depot 0 5 5
F1    5 0 5
F2    5 5 0
FLEET
V1 truck 10 trailer 10"
t "non-metric table (shortest roads used)" FEASIBLE "SHIFT_MINUTES 30
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
F2 trailer_ok 0 5
DISTANCES_KM
Depot 0 100 5
F1    100 0 5
F2    5 5 0
FLEET
V1 truck 20"
t "milk-less trailer_ok farm as parking stop" FEASIBLE "SHIFT_MINUTES 500
$H
MILK_TYPES A
FARMS
F1 truck_only 0 5
F2 trailer_ok 0 0
DISTANCES_KM
Depot 0 5 5
F1    5 0 5
F2    5 5 0
FLEET
V1 truck 10 trailer 10"
t "asymmetric table" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 3
F1    4 0
FLEET
V1 truck 10"
t "not a number" ERROR "SHIFT_MINUTES fifty"
t "missing section" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5"
t "wrong number of milk amounts" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A B
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
V1 truck 10"
t "distance rows out of order" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
F1    3 0
Depot 0 3
FLEET
V1 truck 10"
t "unknown road word" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 dirt_road 0 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
V1 truck 10"
t "same name twice" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
F1 truck 10"
t "same milk type name twice" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A A
FARMS
F1 trailer_ok 0 5 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
V1 truck 10 10"
t "setting given twice" ERROR "SHIFT_MINUTES 50
SHIFT_MINUTES 60
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
V1 truck 10"
t "keyword used as a name" ERROR "SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
Fleet trailer_ok 0 5
DISTANCES_KM
Depot 0 3
Fleet 3 0
FLEET
V1 truck 10"
t "file starting with a UTF-8 mark (Windows editors)" FEASIBLE "$(printf '\xEF\xBB\xBF')SHIFT_MINUTES 50
$H
MILK_TYPES A
FARMS
F1 trailer_ok 0 5
DISTANCES_KM
Depot 0 3
F1    3 0
FLEET
V1 truck 10"

# The checker is the judge for every test above, so it must reject broken plans.
./exact < sample.txt > "$tmp/good" 2>/dev/null
c() {  # name sed-edit: the edited sample plan must be rejected
    sed "$2" "$tmp/good" > "$tmp/bad"
    if cmp -s "$tmp/good" "$tmp/bad"; then fail=$((fail+1)); echo "FAIL [checker: $1] the edit changed nothing"; return; fi
    if ./checker sample.txt "$tmp/bad" | grep -q '^FAIL'; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL [checker: $1] accepted"; fi
}
./checker sample.txt "$tmp/good" | grep -q '^OK 2 499' && pass=$((pass+1)) || { fail=$((fail+1)); echo "FAIL [checker: good plan] rejected"; }
c "milk left behind" 's/30 from Rossi/29 from Rossi/'
c "wrong duration" 's/232.00 minutes/230.00 minutes/'
c "farm dropped from route" 's/Depot -> Romano -> Russo/Depot -> Russo/'
c "wrong total" 's/Total time    : 499.00/Total time    : 450.00/'
c "over capacity" 's/35 from Russo   (35 of 46.7 q)/35 from Russo, 12 from Greco   (47 of 46.7 q)/'
c "compartment listed twice (two milk types)" '/Trailer compartment 2/{p;s/Standard - 17.5 from Ferrari/High_quality - 0 from Rossi/}'
c "damaged line" 's/Truck compartment 1 (37.5 q)   :/Truck compartment :/'
c "truck-only farm on the trailer route" 's/Depot -> Ferrari -> Romano/Depot -> Greco -> Ferrari -> Romano/'
echo "edge tests: pass=$pass fail=$fail"
[ $fail -eq 0 ]
