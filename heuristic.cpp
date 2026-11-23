// heuristic.cpp - ALNS heuristic for the Milk Collection Problem with Incompatibility Constraints
// (HMCHF, Caramia & Guerriero, Interfaces 40(2), 2010). Same model and I/O as exact.cpp.
// Objective, lexicographic: 1) fewest vehicles used, 2) least total time (preparation + travel).
// Build: g++ -std=c++17 -O2 -o heuristic heuristic.cpp
// Run:   ./heuristic [time_limit_seconds=2] [seed=12345] < instance.txt

#include "milk_io.h"
#include <cassert>

static const double INF = 1e18, EPS = 1e-7;
// ---------------------------------------------------------------- compartment typings
// A typing gives every compartment one milk type. Inside one vehicle a load is feasible iff for every
// type j:  S_wt milk of j <= truck capacity typed j  and  all milk of j <= truck+trailer capacity typed j
// (Hall's condition: S_wt milk can't enter the trailer). Only Pareto-maximal typings are kept.
struct Typing { vector<double> capT, capTot; vector<int> truckType, trailerType; };
vector<vector<Typing>> typings;

void buildTypings(int i) {
    const Vehicle& v = fleet[i];
    struct C { bool tr; int idx; double cap; };
    vector<C> cs;
    for (int k = 0; k < (int)v.truck.size(); k++) cs.push_back({false, k, v.truck[k]});
    for (int k = 0; k < (int)v.trailer.size(); k++) cs.push_back({true, k, v.trailer[k]});
    sort(cs.begin(), cs.end(), [](const C& a, const C& b) { return tie(a.tr, a.cap, a.idx) < tie(b.tr, b.cap, b.idx); });
    vector<int> ty(cs.size());
    vector<Typing> all;
    function<void(int)> rec = [&](int c) {
        if (c == (int)cs.size()) {
            Typing t{vector<double>(m, 0), vector<double>(m, 0), vector<int>(v.truck.size()), vector<int>(v.trailer.size())};
            for (size_t k = 0; k < cs.size(); k++) {
                t.capTot[ty[k]] += cs[k].cap;
                if (cs[k].tr) t.trailerType[cs[k].idx] = ty[k];
                else t.capT[ty[k]] += cs[k].cap, t.truckType[cs[k].idx] = ty[k];
            }
            all.push_back(t);
            return;
        }
        bool twin = c > 0 && cs[c - 1].tr == cs[c].tr && cs[c - 1].cap == cs[c].cap;  // symmetry break
        for (int j = twin ? ty[c - 1] : 0; j < m; j++) ty[c] = j, rec(c + 1);
    };
    rec(0);
    auto dom = [&](const Typing& a, const Typing& b) {
        for (int j = 0; j < m; j++) if (a.capT[j] < b.capT[j] || a.capTot[j] < b.capTot[j]) return false;
        return true;
    };
    // shortcut: Pareto filter is O(L^2); skipped above 4000 typings (still correct, just slower)
    if (all.size() > 4000) { typings[i] = all; return; }
    for (size_t a = 0; a < all.size(); a++) {
        bool d = false;
        for (size_t b = 0; b < all.size() && !d; b++) d = b != a && dom(all[b], all[a]) && (!dom(all[a], all[b]) || b < a);
        if (!d) typings[i].push_back(all[a]);
    }
}

// ---------------------------------------------------------------- solution representation
struct Sub { int root; vector<int> seq; };   // trailer parked at root; truck-only loop over S_wt farmers
struct VS {
    vector<int> main;                        // tour from/to depot (with trailer for complete vehicles)
    vector<Sub> subs;                        // complete vehicles only
    map<int, vector<double>> load;           // farmer -> milk collected per type
    vector<double> tot, swt;                 // per type: all milk, milk from S_wt farmers
    double travel = 0, prepSum = 0;
    bool dirty = false;
    double dur() const { return travel + prepSum; }
};
struct Sol { vector<VS> v; vector<int> unassigned; };
struct Obj { int un, used; double time; };
bool operator<(const Obj& a, const Obj& b) {
    if (a.un != b.un) return a.un < b.un;
    if (a.used != b.used) return a.used < b.used;
    return a.time < b.time - 1e-9;
}
Obj evaluate(const Sol& s) {
    Obj o{(int)s.unassigned.size(), 0, 0};
    for (auto& v : s.v) if (!v.load.empty()) o.used++, o.time += v.dur();
    return o;
}

double pathCost(int root, const vector<int>& seq) {
    if (seq.empty()) return 0;
    double c = tt[root][seq[0]] + tt[seq.back()][root];
    for (size_t k = 1; k < seq.size(); k++) c += tt[seq[k - 1]][seq[k]];
    return c;
}
double travelOf(const VS& v) {
    double c = pathCost(0, v.main);
    for (auto& s : v.subs) c += pathCost(s.root, s.seq);
    return c;
}

// ---------------------------------------------------------------- insertion
// kind 0: main[a]; 1: subs[a][b]; 2: new sub at node a;
// 3: empty complete vehicle - visit S_tr farmer a with no pickup (A4 anchor), park at b, subtour to s;
// 4: s is already visited (as an anchor), just load
struct Pos { int kind = -1, a = 0, b = 0; double delta = INF, extraPrep = 0; };
vector<int> strActive;  // S_tr farmers, with or without milk (possible anchors)

void scanPath(int root, const vector<int>& seq, int s, int kind, int a, Pos& best) {
    for (size_t q = 0; q <= seq.size(); q++) {
        int x = q ? seq[q - 1] : root, y = q < seq.size() ? seq[q] : root;
        double d = tt[x][s] + tt[s][y] - tt[x][y];
        if (d < best.delta) best = {kind, a, (int)q, d};
    }
}
Pos bestPos(int i, const VS& v, int s) {
    Pos p;
    if (fleet[i].type == 'P' || reach[s]) { scanPath(0, v.main, s, 0, 0, p); if (p.kind == 0) p.a = p.b; return p; }
    if (v.main.empty()) {  // (A4) needs an S_tr farmer on the main tour: add a zero-pickup anchor
        for (int a : strActive) {
            int root = tt[a][s] < tt[0][s] ? a : 0;
            double d = 2 * tt[0][a] + 2 * tt[root][s];
            if (d + prep[a] < p.delta + p.extraPrep) p = {3, a, root, d, prep[a]};
        }
        return p;
    }
    for (size_t k = 0; k < v.subs.size(); k++) scanPath(v.subs[k].root, v.subs[k].seq, s, 1, k, p);
    for (int r = -1; r < (int)v.main.size(); r++) {
        int node = r < 0 ? 0 : v.main[r];
        if (2 * tt[node][s] < p.delta) p = {2, node, 0, 2 * tt[node][s]};
    }
    return p;
}
void applyPos(VS& v, int s, const Pos& p) {
    if (p.kind == 0) v.main.insert(v.main.begin() + p.a, s);
    else if (p.kind == 1) v.subs[p.a].seq.insert(v.subs[p.a].seq.begin() + p.b, s);
    else if (p.kind == 2) v.subs.push_back({p.a, {s}});
    else if (p.kind == 3) v.main = {p.a}, v.subs.push_back({p.b, {s}});
    v.travel += p.delta;
}

// Most milk of `rem` vehicle i can take from farmer s on top of its current load.
double absorb(int i, const VS& v, int s, const vector<double>& rem, vector<double>& out) {
    double best = -1; vector<double> a(m);
    bool wt = !reach[s];
    for (auto& t : typings[i]) {
        bool ok = true;
        for (int j = 0; j < m && ok; j++) ok = t.capT[j] >= v.swt[j] - EPS && t.capTot[j] >= v.tot[j] - EPS;
        if (!ok) continue;
        double sum = 0;
        for (int j = 0; j < m; j++) {
            double sl = t.capTot[j] - v.tot[j];
            if (wt) sl = min(sl, t.capT[j] - v.swt[j]);
            a[j] = max(0.0, min(rem[j], sl)); sum += a[j];
        }
        if (sum > best) best = sum, out = a;
    }
    return best;
}

mt19937_64 rng;
double noise = 0, OPEN;
double urand() { return uniform_real_distribution<double>(0, 1)(rng); }

struct Opt { int veh = -1; Pos pos; vector<double> amt; double got = 0, cost = INF; };
vector<Opt> options(const Sol& sol, int s, const vector<double>& rem, const vector<char>* skip = nullptr) {
    vector<Opt> res;
    for (int i = 0; i < V; i++) {
        const VS& v = sol.v[i];
        auto it = v.load.find(s);
        bool visited = it != v.load.end();
        if ((visited && accumulate(it->second.begin(), it->second.end(), 0.0) > EPS) || (skip && (*skip)[i])) continue;
        Pos p = visited ? Pos{4, 0, 0, 0, 0} : bestPos(i, v, s);
        double addPrep = visited ? 0 : prep[s];
        if (p.kind < 0 || v.dur() + p.delta + p.extraPrep + addPrep > T + EPS) continue;
        Opt o; o.veh = i; o.pos = p;
        o.got = absorb(i, v, s, rem, o.amt);
        if (o.got <= EPS) continue;
        o.cost = (p.delta + p.extraPrep + addPrep) * (1 + noise * (urand() - 0.5)) + (v.load.empty() ? OPEN : 0);
        res.push_back(o);
    }
    return res;
}
void addLoad(VS& v, int s, const vector<double>& amt) {
    auto [it, fresh] = v.load.try_emplace(s, vector<double>(m, 0));
    if (fresh) v.prepSum += prep[s];
    for (int j = 0; j < m; j++) { it->second[j] += amt[j]; v.tot[j] += amt[j]; if (!reach[s]) v.swt[j] += amt[j]; }
    v.dirty = true;
}
void dropLoad(VS& v, int s) {
    auto it = v.load.find(s);
    if (it == v.load.end()) return;
    for (int j = 0; j < m; j++) {
        v.tot[j] = max(0.0, v.tot[j] - it->second[j]);
        if (!reach[s]) v.swt[j] = max(0.0, v.swt[j] - it->second[j]);
    }
    v.prepSum -= prep[s];
    v.load.erase(it);
    v.dirty = true;
}
// Removes s from the route; S_wt farmers that lose their only parking spot are returned in orphans.
void removeFromRoute(VS& v, int s, vector<int>& orphans) {
    auto it = find(v.main.begin(), v.main.end(), s);
    if (it != v.main.end()) {
        v.main.erase(it);
        for (auto& sub : v.subs) if (sub.root == s) {  // re-park at the cheapest remaining spot
            int best = 0;
            for (int r : v.main)
                if (tt[r][sub.seq[0]] + tt[sub.seq.back()][r] < tt[best][sub.seq[0]] + tt[sub.seq.back()][best]) best = r;
            sub.root = best;
        }
        if (v.main.empty()) {
            for (auto& sub : v.subs) orphans.insert(orphans.end(), sub.seq.begin(), sub.seq.end());
            v.subs.clear();
        }
    } else {
        for (size_t k = 0; k < v.subs.size(); k++) {
            auto& q = v.subs[k].seq;
            auto jt = find(q.begin(), q.end(), s);
            if (jt == q.end()) continue;
            q.erase(jt);
            if (q.empty()) v.subs.erase(v.subs.begin() + k);
            break;
        }
    }
    v.travel = travelOf(v);
    v.dirty = true;
}
// Fully unassigns s (from every vehicle serving it) plus any farmer orphaned by that.
void unassign(Sol& sol, int s, vector<int>& pending) {
    vector<int> work = {s};
    while (!work.empty()) {
        int x = work.back(); work.pop_back();
        if (find(pending.begin(), pending.end(), x) != pending.end()) continue;
        pending.push_back(x);
        for (auto& v : sol.v) if (v.load.count(x)) { removeFromRoute(v, x, work); dropLoad(v, x); }
    }
}

// Plan to place all milk of s on distinct vehicles (states untouched, so options stay valid).
// crit 0: one vehicle taking everything if possible, else the one taking most; crit 1: cheapest per quintal.
vector<Opt> plan(const Sol& sol, int s, int crit) {
    vector<double> rem = Q[s];
    vector<char> inPlan(V, 0);
    vector<Opt> res;
    for (double need; (need = accumulate(rem.begin(), rem.end(), 0.0)) > EPS;) {
        auto opts = options(sol, s, rem, &inPlan);
        const Opt* pick = nullptr;
        for (auto& o : opts) {
            bool full = o.got >= need - EPS, pfull = pick && pick->got >= need - EPS;
            if (!pick || (crit == 1 ? o.cost / o.got < pick->cost / pick->got
                         : (full && !pfull) || (full == pfull && (full ? o.cost < pick->cost
                         : (o.got > pick->got + EPS || (o.got > pick->got - EPS && o.cost < pick->cost)))))) pick = &o;
        }
        if (!pick) return {};
        res.push_back(*pick);
        inPlan[pick->veh] = 1;
        for (int j = 0; j < m; j++) rem[j] = max(0.0, rem[j] - pick->amt[j]);
    }
    return res;
}
// Inserts all milk of s, possibly split over several vehicles (split pickup is allowed).
bool insertFarmer(Sol& sol, int s) {
    vector<Opt> best; double bc = INF;
    for (int crit = 0; crit < 2; crit++) {
        auto p = plan(sol, s, crit);
        double c = 0; for (auto& o : p) c += o.cost;
        if (!p.empty() && c < bc) bc = c, best = p;
    }
    if (best.empty()) return false;
    for (auto& o : best) {
        applyPos(sol.v[o.veh], s, o.pos);
        if (o.pos.kind == 3) addLoad(sol.v[o.veh], o.pos.a, vector<double>(m, 0));
        addLoad(sol.v[o.veh], s, o.amt);
    }
    return true;
}

void recreate(Sol& sol, vector<int> pending, bool regret) {
    // a milk-less anchor that was removed has nothing to insert
    pending.erase(remove_if(pending.begin(), pending.end(), [](int s) { return accumulate(Q[s].begin(), Q[s].end(), 0.0) <= EPS; }), pending.end());
    while (!pending.empty()) {
        size_t pickIdx = pending.size() - 1;
        if (regret) {  // regret-2: farmer whose best single-vehicle option is most "at risk" goes first
            double bestReg = -INF;
            for (size_t k = 0; k < pending.size(); k++) {
                int s = pending[k];
                double need = accumulate(Q[s].begin(), Q[s].end(), 0.0), b1 = INF, b2 = INF;
                for (auto& o : options(sol, s, Q[s])) if (o.got >= need - EPS) {
                    if (o.cost < b1) b2 = b1, b1 = o.cost; else if (o.cost < b2) b2 = o.cost;
                }
                double reg = b1 >= INF ? INF : (b2 >= INF ? 1e15 : b2 - b1);
                if (reg > bestReg) bestReg = reg, pickIdx = k;
            }
        }
        int s = pending[pickIdx];
        pending.erase(pending.begin() + pickIdx);
        if (!insertFarmer(sol, s)) sol.unassigned.push_back(s);
    }
}

// ---------------------------------------------------------------- intra-route improvement
void localOpt(int root, vector<int>& seq) {
    for (bool improved = true; improved;) {
        improved = false;
        vector<int> p = {root}; p.insert(p.end(), seq.begin(), seq.end()); p.push_back(root);
        int k = seq.size();
        for (int i = 1; i < k && !improved; i++)  // 2-opt
            for (int j = i + 1; j <= k && !improved; j++)
                if (tt[p[i - 1]][p[j]] + tt[p[i]][p[j + 1]] < tt[p[i - 1]][p[i]] + tt[p[j]][p[j + 1]] - 1e-9) {
                    reverse(seq.begin() + i - 1, seq.begin() + j); improved = true;
                }
        for (int L = 1; L <= 3 && !improved; L++)  // or-opt: move a segment of 1..3 farmers
            for (int i = 1; i + L - 1 <= k && !improved; i++) {
                int j = i + L - 1;
                double gain = tt[p[i - 1]][p[i]] + tt[p[j]][p[j + 1]] - tt[p[i - 1]][p[j + 1]];
                for (int q = 0; q <= k && !improved; q++) {
                    if (q >= i - 1 && q <= j) continue;
                    double fwd = tt[p[q]][p[i]] + tt[p[j]][p[q + 1]] - tt[p[q]][p[q + 1]];
                    double rev = tt[p[q]][p[j]] + tt[p[i]][p[q + 1]] - tt[p[q]][p[q + 1]];
                    if (min(fwd, rev) < gain - 1e-9) {
                        vector<int> seg(p.begin() + i, p.begin() + j + 1), np;
                        if (rev < fwd) reverse(seg.begin(), seg.end());
                        for (int x = 0; x <= k + 1; x++) {
                            if (x >= i && x <= j) continue;
                            np.push_back(p[x]);
                            if (x == q) np.insert(np.end(), seg.begin(), seg.end());
                        }
                        seq.assign(np.begin() + 1, np.end() - 1); improved = true;
                    }
                }
            }
    }
}
void optimize(VS& v) {
    for (bool again = true; again;) {  // drop zero-pickup visits unless one is the A4 anchor of a subtour
        again = false;
        for (auto& [s, amt] : v.load)
            if (accumulate(amt.begin(), amt.end(), 0.0) <= EPS && (v.subs.empty() || v.main.size() > 1 || v.main[0] != s)) {
                vector<int> none; removeFromRoute(v, s, none); dropLoad(v, s); again = true; break;
            }
    }
    localOpt(0, v.main);
    for (auto& s : v.subs) localOpt(s.root, s.seq);
    if (!v.subs.empty()) {  // rebuild subtours greedily against the (possibly new) parking spots
        vector<int> wt;
        for (auto& s : v.subs) wt.insert(wt.end(), s.seq.begin(), s.seq.end());
        auto nearest = [&](int s) { double b = tt[0][s]; for (int r : v.main) b = min(b, tt[r][s]); return b; };
        sort(wt.begin(), wt.end(), [&](int a, int b) { return nearest(a) > nearest(b); });
        VS alt; alt.main = v.main;
        for (int s : wt) { Pos p; p.kind = -1;
            for (size_t k = 0; k < alt.subs.size(); k++) scanPath(alt.subs[k].root, alt.subs[k].seq, s, 1, k, p);
            for (int r = -1; r < (int)alt.main.size(); r++) {
                int node = r < 0 ? 0 : alt.main[r];
                if (2 * tt[node][s] < p.delta) p = {2, node, 0, 2 * tt[node][s]};
            }
            applyPos(alt, s, p);
        }
        for (auto& s : alt.subs) localOpt(s.root, s.seq);
        if (travelOf(alt) < travelOf(v) - 1e-9) v.subs = alt.subs;
    }
    v.travel = travelOf(v);
    v.dirty = false;
}

// ---------------------------------------------------------------- ALNS
vector<int> active;

vector<int> ruin(Sol& sol) {
    vector<int> pending;
    vector<int> served;
    for (int s : active) if (find(sol.unassigned.begin(), sol.unassigned.end(), s) == sol.unassigned.end()) served.push_back(s);
    pending = sol.unassigned; sol.unassigned.clear();
    if (served.empty()) return pending;
    int kmax = max(1, min((int)served.size(), 2 + (int)served.size() / 3));
    int k = urand() < 0.05 ? served.size() : 1 + rng() % kmax, op = rng() % 3;
    if (op == 2) {  // route removal: empty a vehicle (drives fleet reduction)
        vector<int> used;
        for (int i = 0; i < V; i++) if (!sol.v[i].load.empty()) used.push_back(i);
        int i = used[rng() % used.size()];
        vector<int> fs; for (auto& [s, _] : sol.v[i].load) fs.push_back(s);
        for (int s : fs) unassign(sol, s, pending);
    } else {
        int seed = served[rng() % served.size()];
        if (op == 1) sort(served.begin(), served.end(), [&](int a, int b) { return tt[seed][a] < tt[seed][b]; });  // related
        else shuffle(served.begin(), served.end(), rng);
        for (int x = 0; x < k; x++) unassign(sol, served[x], pending);
    }
    for (auto& v : sol.v)  // re-parking can (rarely) lengthen a tour past the shift
        while (!v.load.empty() && v.dur() > T + EPS) unassign(sol, v.load.begin()->first, pending);
    return pending;
}

void printSolution(Sol& sol) {
    vector<VehSol> out;
    for (int i = 0; i < V; i++) {
        VS& v = sol.v[i];
        if (v.load.empty()) continue;
        VehSol o{i + 1, v.dur(), {0}, {}, {}};
        o.tour.insert(o.tour.end(), v.main.begin(), v.main.end()); o.tour.push_back(0);
        for (auto& s : v.subs) {
            vector<int> p = {s.root}; p.insert(p.end(), s.seq.begin(), s.seq.end()); p.push_back(s.root);
            o.subs.push_back(p);
        }
        const Typing* ty = nullptr;
        for (auto& t : typings[i]) {
            bool ok = true;
            for (int j = 0; j < m && ok; j++) ok = t.capT[j] >= v.swt[j] - EPS && t.capTot[j] >= v.tot[j] - EPS;
            if (ok) { ty = &t; break; }
        }
        assert(ty);
        vector<double> leftT = fleet[i].truck, leftR = fleet[i].trailer;
        auto& loads = o.loads;
        auto pour = [&](char part, vector<double>& left, const vector<int>& types, int j, int s, double& q) {
            for (size_t k = 0; k < left.size() && q > 1e-12; k++) if (types[k] == j && left[k] > 1e-12) {
                double x = min(q, left[k]); left[k] -= x; q -= x;
                loads.push_back({part, (int)k + 1, j + 1, s, x});
            }
        };
        for (int pass = 0; pass < 2; pass++)  // S_wt milk first (truck only), then S_tr milk (trailer first)
            for (auto& [s, amt] : v.load) if (reach[s] == pass)
                for (int j = 0; j < m; j++) {
                    double q = amt[j];
                    if (reach[s]) pour('R', leftR, ty->trailerType, j, s, q);
                    pour('T', leftT, ty->truckType, j, s, q);
                    if (q > 1e-6) { fprintf(stderr, "internal error: load does not fit\n"); exit(2); }
                }
        sort(loads.begin(), loads.end());
        out.push_back(o);
    }
    printPlan(out);
}

int main(int argc, char** argv) {
    double timeLimit = argc > 1 ? atof(argv[1]) : 2.0;
    rng.seed(argc > 2 ? atoll(argv[2]) : 12345);
    readInstance();
    auto start = chrono::steady_clock::now();
    auto elapsed = [&] { return chrono::duration<double>(chrono::steady_clock::now() - start).count(); };
    for (int s = 1; s <= n; s++) {
        if (accumulate(Q[s].begin(), Q[s].end(), 0.0) > EPS) active.push_back(s);
        if (reach[s]) strActive.push_back(s);
    }
    typings.resize(V);
    for (int i = 0; i < V; i++) buildTypings(i);
    OPEN = 10 * (V * T + 1) + 1000;  // opening a vehicle outweighs any time saving

    Sol cur; cur.v.assign(V, VS{});
    for (auto& v : cur.v) v.tot.assign(m, 0), v.swt.assign(m, 0);
    recreate(cur, active, true);
    for (auto& v : cur.v) if (v.dirty) optimize(v);
    Sol best = cur;
    Obj bo = evaluate(best), co = bo;
    double T0 = max(1e-6, 0.01 * bo.time);
    long long iter = 0, lastImp = 0;
    while (elapsed() < timeLimit && iter - lastImp < 30000 && !active.empty()) {
        iter++;
        Sol cand = cur;
        noise = urand() < 0.5 ? 0.2 : 0;
        vector<int> pending = ruin(cand);
        shuffle(pending.begin(), pending.end(), rng);
        recreate(cand, pending, urand() < 0.5);
        for (auto& v : cand.v) if (v.dirty) optimize(v);
        Obj o = evaluate(cand);
        double temp = T0 * pow(0.001, min(1.0, elapsed() / timeLimit));
        bool accept = o < co || (o.un == co.un && o.used == co.used && urand() < exp(-(o.time - co.time) / temp));
        if (accept) cur = move(cand), co = o;
        if (co < bo) best = cur, bo = co, lastImp = iter;
        if (iter - lastImp > 0 && (iter - lastImp) % 2000 == 0) cur = best, co = bo;  // restart from best
    }
    cerr << "heuristic: iterations=" << iter << " time=" << elapsed() << "s\n";
    if (bo.un > 0) { printInfeasible(); return 0; }
    printSolution(best);
}
