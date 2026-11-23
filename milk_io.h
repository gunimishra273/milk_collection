// milk_io.h - the plain-language input file and the readable plan output, shared by exact, heuristic
// and checker so all three read and write exactly the same format (see README "Input file").
#pragma once
#include <bits/stdc++.h>
using namespace std;

struct Vehicle { string name; char type; vector<double> truck, trailer; };  // type 'P' truck, 'C' truck+trailer

int n, m, V;
double T, speedV;            // shift (minutes), speed (km per minute)
vector<vector<double>> tt;   // travel minutes between nodes (0 = depot), shortest-path closed
vector<int> reach;           // 1: farmer in S_tr (reachable with trailer), 0: S_wt (truck only)
vector<double> prep;         // stop minutes per farmer visit
vector<vector<double>> Q;    // Q[s][j]: quintals of milk type j at farmer s
vector<Vehicle> fleet;
vector<string> nodeName;     // 0 = "Depot", then farm names
vector<string> typeName;     // milk type names

// ---------------------------------------------------------------- input
int curLine = 0;
[[noreturn]] void fail(const string& msg) {
    cerr << "input error" << (curLine ? " (line " + to_string(curLine) + ")" : "") << ": " << msg << "\n";
    exit(1);
}
static string upper(string s) { for (char& c : s) c = toupper((unsigned char)c); return s; }
double toNum(const string& s, const string& what) {
    try { size_t p; double v = stod(s, &p); if (p == s.size() && isfinite(v)) return v; } catch (...) {}
    fail(what + " must be a number, found '" + s + "'");
}
const set<string> KEYWORDS = {"SHIFT_MINUTES", "SPEED_KM_PER_HOUR", "MILK_TYPES", "FARMS", "DISTANCES_KM", "FLEET"};
static void checkName(const string& s) {
    if (s.find_first_of(":,()") != string::npos || s == "->" || s == "-" || upper(s) == "DEPOT" || KEYWORDS.count(upper(s)))
        fail("'" + s + "' can't be used as a name (no : , ( ), not 'Depot' and not a keyword)");
}

void readInstance(istream& in) {
    struct Row { int line; vector<string> w; };
    vector<Row> farms, dist, vehicles;
    bool haveShift = false, haveSpeed = false, haveTypes = false;
    set<string> seen;
    vector<Row>* section = nullptr;
    string line;
    for (int ln = 1; getline(in, line); ln++) {
        curLine = ln;
        if (ln == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // UTF-8 mark some Windows editors add
        line = line.substr(0, line.find('#'));  // '#' starts a note up to end of line
        istringstream ss(line);
        vector<string> w; for (string t; ss >> t;) w.push_back(t);
        if (w.empty()) continue;
        string key = upper(w[0]);
        auto one = [&](const string& what) { if (w.size() != 2) fail(key + " needs exactly one value (a keyword can't be used as a name)"); return toNum(w[1], what); };
        if (KEYWORDS.count(key) && !seen.insert(key).second) fail(key + " appears twice");
        if (key == "SHIFT_MINUTES") { T = one("SHIFT_MINUTES"); haveShift = true; if (T < 0) fail("SHIFT_MINUTES can't be negative"); }
        else if (key == "SPEED_KM_PER_HOUR") { speedV = one("SPEED_KM_PER_HOUR") / 60; haveSpeed = true; if (speedV <= 0) fail("SPEED_KM_PER_HOUR must be above 0"); }
        else if (key == "MILK_TYPES") {
            typeName.assign(w.begin() + 1, w.end()); haveTypes = true;
            if (typeName.empty()) fail("MILK_TYPES needs at least one milk type name");
            for (auto& t : typeName) checkName(t);
            if (set<string>(typeName.begin(), typeName.end()).size() != typeName.size()) fail("a milk type name is used twice");
        }
        else if (key == "FARMS" || key == "DISTANCES_KM" || key == "FLEET") {
            if (w.size() != 1) fail(key + " must be alone on its line, with its rows below it (a keyword can't be used as a name)");
            section = key == "FARMS" ? &farms : key == "DISTANCES_KM" ? &dist : &vehicles;
        }
        else if (!section) fail("unknown setting '" + w[0] + "' (expected SHIFT_MINUTES, SPEED_KM_PER_HOUR, MILK_TYPES, FARMS, DISTANCES_KM or FLEET)");
        else section->push_back({ln, w});
    }
    curLine = 0;
    if (!haveShift) fail("SHIFT_MINUTES is missing");
    if (!haveSpeed) fail("SPEED_KM_PER_HOUR is missing");
    if (!haveTypes) fail("MILK_TYPES is missing");
    for (const char* s : {"FARMS", "DISTANCES_KM", "FLEET"}) if (!seen.count(s)) fail(string(s) + " section is missing");
    m = typeName.size(); n = farms.size(); V = vehicles.size();

    set<string> names = {"DEPOT"};
    nodeName = {"Depot"};
    reach.assign(n + 1, 1); prep.assign(n + 1, 0); Q.assign(n + 1, vector<double>(m, 0));
    for (int s = 1; s <= n; s++) {
        auto& [ln, w] = farms[s - 1]; curLine = ln;
        if ((int)w.size() != 3 + m) fail("a farm row is: name road stop_minutes, then " + to_string(m) + " milk amounts (one per milk type); found " + to_string(w.size()) + " values");
        checkName(w[0]);
        if (!names.insert(upper(w[0])).second) fail("name '" + w[0] + "' is used twice");
        nodeName.push_back(w[0]);
        string road = upper(w[1]);
        if (road != "TRAILER_OK" && road != "TRUCK_ONLY") fail("road must be trailer_ok or truck_only, found '" + w[1] + "'");
        reach[s] = road == "TRAILER_OK";
        if ((prep[s] = toNum(w[2], "stop minutes")) < 0) fail("stop minutes can't be negative");
        for (int j = 0; j < m; j++) if ((Q[s][j] = toNum(w[3 + j], typeName[j] + " amount")) < 0) fail("milk amount can't be negative");
    }

    curLine = 0;
    if ((int)dist.size() != n + 1) fail("DISTANCES_KM needs " + to_string(n + 1) + " rows (Depot and every farm), found " + to_string(dist.size()));
    vector<vector<double>> d(n + 1, vector<double>(n + 1));
    for (int a = 0; a <= n; a++) {
        auto& [ln, w] = dist[a]; curLine = ln;
        if (upper(w[0]) != upper(nodeName[a])) fail("distance row " + to_string(a + 1) + " should start with '" + nodeName[a] + "' (same order as FARMS, Depot first), found '" + w[0] + "'");
        if ((int)w.size() != n + 2) fail("row " + nodeName[a] + " needs " + to_string(n + 1) + " distances (Depot and every farm), found " + to_string(w.size() - 1));
        for (int b = 0; b <= n; b++) if ((d[a][b] = toNum(w[b + 1], "distance")) < 0) fail("distance can't be negative");
        d[a][a] = 0;
        for (int b = 0; b < a; b++) if (fabs(d[a][b] - d[b][a]) > 1e-9)
            fail("distance " + nodeName[a] + " to " + nodeName[b] + " is " + w[b + 1] + " but " + nodeName[b] + " to " + nodeName[a] + " is " + dist[b].w[a + 1] + "; the table must be the same both ways");
    }
    for (int k = 0; k <= n; k++) for (int a = 0; a <= n; a++) for (int b = 0; b <= n; b++)
        d[a][b] = min(d[a][b], d[a][k] + d[k][b]);
    tt = d;
    for (auto& row : tt) for (auto& x : row) x /= speedV;

    fleet.resize(V);
    for (int i = 0; i < V; i++) {
        auto& [ln, w] = vehicles[i]; curLine = ln;
        Vehicle& v = fleet[i];
        checkName(w[0]);
        if (!names.insert(upper(w[0])).second) fail("name '" + w[0] + "' is used twice");
        v.name = w[0];
        if (w.size() < 2 || upper(w[1]) != "TRUCK") fail("a vehicle row is: name truck <capacities> [trailer <capacities>]");
        vector<double>* part = &v.truck;
        v.type = 'P';
        for (size_t k = 2; k < w.size(); k++) {
            if (upper(w[k]) == "TRAILER") {
                if (v.type == 'C') fail("'trailer' appears twice");
                v.type = 'C'; part = &v.trailer; continue;
            }
            double c = toNum(w[k], "compartment capacity");
            if (c < 0) fail("compartment capacity can't be negative");
            part->push_back(c);
        }
    }
    curLine = 0;
}
void readInstance() { readInstance(cin); }

// ---------------------------------------------------------------- output (the readable plan)
struct VehSol {
    int id; double dur; vector<int> tour; vector<vector<int>> subs;            // tour 0..0, subtour r..r
    vector<tuple<char, int, int, int, double>> loads;  // part T/R, compartment, type, farmer, qty (1-based)
};
string fmtQ(double x) {  // at most 4 decimals, no trailing zeros
    char b[32]; snprintf(b, sizeof b, "%.4f", x);
    string s = b; s.erase(s.find_last_not_of('0') + 1); if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}
string hoursMin(double t) { int x = (int)lround(t); return to_string(x / 60) + " h " + to_string(x % 60) + " min"; }
string path(const vector<int>& p) {
    string s; for (size_t k = 0; k < p.size(); k++) s += (k ? " -> " : "") + nodeName[p[k]];
    return s;
}

void printInfeasible() {
    printf("MILK COLLECTION PLAN\n====================\n");
    printf("Result        : NOT POSSIBLE - these vehicles cannot collect all the milk within the shift\n");
}

void printPlan(const vector<VehSol>& sol) {
    double total = 0; for (auto& v : sol) total += v.dur;
    printf("MILK COLLECTION PLAN\n====================\n");
    printf("Result        : all milk can be collected\n");
    printf("Vehicles used : %d of %d\n", (int)sol.size(), V);
    printf("Total time    : %.2f minutes (%s)\n", total, hoursMin(total).c_str());
    vector<char> used(V + 1, 0);
    for (auto& v : sol) {
        const Vehicle& veh = fleet[v.id - 1];
        used[v.id] = 1;
        printf("\nVehicle %s (%s) - %.2f minutes on the road (shift limit %s)\n", veh.name.c_str(),
               veh.type == 'C' ? "truck + trailer" : "truck", v.dur, fmtQ(T).c_str());
        printf("  %-30s : %s\n", veh.type == 'C' ? "Route with trailer" : "Route", path(v.tour).c_str());
        for (auto& s : v.subs)
            printf("  %-30s : %s   (trailer waits at %s)\n", "Truck-only trip", path(s).c_str(), nodeName[s[0]].c_str());
        for (char part : {'T', 'R'}) {
            const vector<double>& caps = part == 'T' ? veh.truck : veh.trailer;
            for (size_t k = 1; k <= caps.size(); k++) {
                string what, label = string(part == 'T' ? "Truck" : "Trailer") + " compartment " + to_string(k) + " (" + fmtQ(caps[k - 1]) + " q)";
                double fill = 0; int type = -1;
                for (auto& [p, kk, j, s, q] : v.loads) if (p == part && kk == (int)k) {
                    if (type >= 0 && j != type) { fprintf(stderr, "internal error: two milk types in one compartment\n"); exit(2); }
                    what += (type < 0 ? typeName[j - 1] + " - " : ", ") + fmtQ(q) + " from " + nodeName[s];
                    type = j; fill += q;
                }
                if (type < 0) printf("  %-30s : empty\n", label.c_str());
                else printf("  %-30s : %s   (%s of %s q)\n", label.c_str(), what.c_str(), fmtQ(fill).c_str(), fmtQ(caps[k - 1]).c_str());
            }
        }
    }
    string idle;
    for (int i = 1; i <= V; i++) if (!used[i]) idle += " " + fleet[i - 1].name;
    if (!idle.empty()) printf("\nNot needed    :%s\n", idle.c_str());
}
