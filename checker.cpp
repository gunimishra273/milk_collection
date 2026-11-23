// checker.cpp - validates a solver's plan against an input file and recomputes its objective.
// Usage: ./checker input.txt plan.txt   -> prints "OK <used> <time>", "INFEASIBLE", or "FAIL: ..." (exit 1)
#include "milk_io.h"

static const double TOL = 1e-3;  // the plan prints quantities with 4 decimals and times with 2

int planLine = 0;
[[noreturn]] void bad(const string& msg) {
    printf("FAIL: %s%s\n", planLine ? ("plan line " + to_string(planLine) + ": ").c_str() : "", msg.c_str());
    exit(1);
}
vector<string> words(const string& s) { istringstream ss(s); vector<string> w; for (string t; ss >> t;) w.push_back(t); return w; }
string word(const string& s, size_t k) { auto w = words(s); if (k >= w.size()) bad("line is missing a value"); return w[k]; }
double number(const string& s) {
    try { size_t p; double v = stod(s, &p); if (p == s.size() && isfinite(v)) return v; } catch (...) {}
    bad("expected a number, found '" + s + "'");
}
string trim(const string& s) { size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' '); return a == string::npos ? "" : s.substr(a, b - a + 1); }
// "label : value" -> {label, value}; labels never contain ':'
pair<string, string> split(const string& s) {
    size_t c = s.find(" : ");
    if (c == string::npos) return {trim(s), ""};
    return {trim(s.substr(0, c)), trim(s.substr(c + 3))};
}
int nodeId(const string& name) {
    for (int x = 0; x <= n; x++) if (nodeName[x] == name) return x;
    bad("unknown place '" + name + "'");
}
vector<int> readPath(const string& s) {  // "A -> B -> C" (anything after the last name is ignored)
    vector<string> w = words(s);
    vector<int> p;
    for (size_t k = 0; k < w.size(); k += 2) {
        p.push_back(nodeId(w[k]));
        if (k + 1 >= w.size() || w[k + 1] != "->") break;
    }
    return p;
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: checker input.txt plan.txt\n"); return 2; }
    ifstream fi(argv[1]), fo(argv[2]);
    if (!fi || !fo) { printf("FAIL: cannot open files\n"); return 1; }
    readInstance(fi);

    vector<string> lines;
    for (string l; getline(fo, l);) lines.push_back(l);
    size_t li = 0;
    auto next = [&]() -> string {  // next non-blank line, "" at end
        while (li < lines.size()) { planLine = li + 1; string l = trim(lines[li++]); if (!l.empty()) return l; }
        planLine = lines.size(); return "";
    };
    if (next() != "MILK COLLECTION PLAN" || next().find_first_not_of('=') != string::npos) bad("plan must start with the MILK COLLECTION PLAN title");
    auto [rk, rv] = split(next());
    if (rk != "Result") bad("Result line expected");
    if (rv.rfind("NOT POSSIBLE", 0) == 0) { printf("INFEASIBLE\n"); return 0; }
    if (rv != "all milk can be collected") bad("unknown result '" + rv + "'");
    auto [uk, uv] = split(next());
    if (uk != "Vehicles used" || uv.empty()) bad("Vehicles used line expected");
    int claimedUsed = (int)number(word(uv, 0));
    auto [tk, tv] = split(next());
    if (tk != "Total time" || tv.empty()) bad("Total time line expected");
    double claimedTotal = number(word(tv, 0));

    vector<vector<double>> got(n + 1, vector<double>(m, 0));
    vector<char> seenVeh(V + 1, 0);
    int used = 0; double total = 0;
    string l = next();
    while (l.rfind("Vehicle ", 0) == 0) {
        // Vehicle <name> (truck + trailer) - <dur> minutes on the road ...
        string vname = word(l, 1);
        int id = 0;
        for (int i = 0; i < V; i++) if (fleet[i].name == vname) id = i + 1;
        if (!id || seenVeh[id]) bad("unknown or repeated vehicle '" + vname + "'");
        seenVeh[id] = 1;
        const Vehicle& veh = fleet[id - 1];
        bool saysTrailer = l.find("(truck + trailer)") != string::npos;
        if (saysTrailer != (veh.type == 'C') || (!saysTrailer && l.find("(truck)") == string::npos)) bad("vehicle kind does not match the FLEET");
        size_t dash = l.find(") - ");
        if (dash == string::npos) bad("vehicle duration missing");
        double claimedDur = number(word(l.substr(dash + 4), 0));

        auto [rlab, rpath] = split(next());
        if (rlab != (veh.type == 'C' ? "Route with trailer" : "Route")) bad("route line expected");
        vector<int> tour = readPath(rpath);
        if (tour.size() < 2 || tour.front() != 0 || tour.back() != 0) bad("route must start and end at the Depot");
        vector<char> served(n + 1, 0), onMain(n + 1, 0);
        double dur = 0;
        auto visit = [&](int x) { if (x == 0) bad("Depot inside a route"); if (served[x]) bad("farm visited twice by one vehicle"); served[x] = 1; dur += prep[x]; };
        for (size_t k = 0; k + 1 < tour.size(); k++) dur += tt[tour[k]][tour[k + 1]];
        for (size_t k = 1; k + 1 < tour.size(); k++) {
            visit(tour[k]); onMain[tour[k]] = 1;
            if (veh.type == 'C' && !reach[tour[k]]) bad("trailer taken to a truck_only farm");
        }
        if (veh.type == 'C' && tour.size() == 2) bad("a truck + trailer must visit at least one trailer_ok farm (A4)");
        vector<double> fill(veh.truck.size() + veh.trailer.size(), 0);
        vector<char> seenComp(fill.size(), 0);  // a second line for a compartment could hide a second milk type
        l = next();
        for (auto [lab, val] = split(l); lab == "Truck-only trip"; tie(lab, val) = split(l = next())) {
            if (veh.type != 'C') bad("truck-only trip on a vehicle without trailer");
            vector<int> p = readPath(val);
            if (p.size() < 3 || p.front() != p.back()) bad("truck-only trip must return to where the trailer waits");
            if (p[0] != 0 && !onMain[p[0]]) bad("trailer parked at a farm not on the route with trailer");
            for (size_t k = 0; k + 1 < p.size(); k++) dur += tt[p[k]][p[k + 1]];
            for (size_t k = 1; k + 1 < p.size(); k++) {
                if (reach[p[k]]) bad("truck-only trip visits a trailer_ok farm");
                visit(p[k]);
            }
        }
        while (l.rfind("Truck compartment", 0) == 0 || l.rfind("Trailer compartment", 0) == 0) {
            // <Truck|Trailer> compartment k (cap q) : <type> - q from <farm>, q from <farm>   (x of cap q)
            auto [lab, val] = split(l);
            bool trl = lab.rfind("Trailer", 0) == 0;
            int k = (int)number(word(lab, 2));
            int nk = trl ? veh.trailer.size() : veh.truck.size();
            if (k < 1 || k > nk) bad("no such compartment");
            int c = trl ? veh.truck.size() + k - 1 : k - 1;
            if (seenComp[c]++) bad("compartment listed twice");
            if (val != "empty") {
                vector<string> w = words(val);
                if (w.size() < 5 || w[1] != "-") bad("compartment line must be '<milk type> - <qty> from <farm>, ...'");
                int j = find(typeName.begin(), typeName.end(), w[0]) - typeName.begin();
                if (j == m) bad("unknown milk type '" + w[0] + "'");
                for (size_t t = 2; t < w.size() && w[t][0] != '('; t += 3) {
                    if (t + 2 >= w.size() || w[t + 1] != "from") bad("expected '<qty> from <farm>'");
                    double q = number(w[t]);
                    string farm = w[t + 2]; if (farm.back() == ',') farm.pop_back();
                    int s = nodeId(farm);
                    if (s == 0 || q < -TOL) bad("bad load");
                    if (!served[s]) bad("milk from a farm the vehicle does not visit");
                    if (trl && !onMain[s]) bad("trailer loaded while it was parked / from a truck_only farm (A8)");
                    fill[c] += q;
                    got[s][j] += q;
                }
                if (fill[c] > (trl ? veh.trailer[k - 1] : veh.truck[k - 1]) + TOL) bad("compartment over capacity");
            }
            l = next();
        }
        if (dur > T + 1e-4) bad("vehicle " + veh.name + " exceeds the shift");
        if (fabs(dur - claimedDur) > 0.01) bad("vehicle duration mismatch (plan says " + fmtQ(claimedDur) + ", route takes " + fmtQ(dur) + ")");
        used++; total += dur;
    }
    if (l.rfind("Not needed", 0) == 0) l = next();
    if (!l.empty()) bad("unexpected line '" + l + "'");
    planLine = 0;
    for (int s = 1; s <= n; s++) for (int j = 0; j < m; j++)
        if (fabs(got[s][j] - Q[s][j]) > TOL * max(1.0, Q[s][j])) bad("farm " + nodeName[s] + " " + typeName[j] + " milk not fully collected");
    if (used != claimedUsed || fabs(total - claimedTotal) > 0.01) bad("Vehicles used / Total time do not match the vehicles listed");
    printf("OK %d %.4f\n", used, total);
}
