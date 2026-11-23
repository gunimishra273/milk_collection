// exact.cpp - exact solver for the Milk Collection Problem with Incompatibility Constraints
// (HMCHF: heterogeneous milk types, heterogeneous fleet of pure trucks and truck+trailer
// "complete vehicles"; Caramia & Guerriero, Interfaces 40(2), 2010).
// Objective, lexicographic as in the paper: 1) fewest vehicles used, 2) least total time
// (preparation + travel), subject to compartment/milk-type, trailer-access and shift limits.
// Build: g++ -std=c++17 -O2 -o exact exact.cpp        Run: ./exact < instance.txt

#include "milk_io.h"

static const double INF = 1e18, EPS = 1e-7;

// ---------------------------------------------------------------- routing: Held-Karp
struct HK {  // best cycle root -> (every subset of nodes) -> root
    int root, k; vector<int> nodes; vector<vector<double>> dp; vector<vector<int>> par;
    HK(int r, vector<int> nd) : root(r), k(nd.size()), nodes(move(nd)),
        dp(1u << k, vector<double>(k, INF)), par(1u << k, vector<int>(k, -1)) {
        for (int a = 0; a < k; a++) dp[1u << a][a] = tt[root][nodes[a]];
        for (unsigned mask = 1; mask < (1u << k); mask++)
            for (int a = 0; a < k; a++) if ((mask >> a & 1) && dp[mask][a] < INF)
                for (int b = 0; b < k; b++) if (!(mask >> b & 1)) {
                    double c = dp[mask][a] + tt[nodes[a]][nodes[b]];
                    if (c < dp[mask | 1u << b][b]) dp[mask | 1u << b][b] = c, par[mask | 1u << b][b] = a;
                }
    }
    int last(unsigned mask) const {
        int best = -1;
        for (int a = 0; a < k; a++) if ((mask >> a & 1) &&
            (best < 0 || dp[mask][a] + tt[nodes[a]][root] < dp[mask][best] + tt[nodes[best]][root])) best = a;
        return best;
    }
    double cycle(unsigned mask) const { if (!mask) return 0; int a = last(mask); return dp[mask][a] + tt[nodes[a]][root]; }
    vector<int> order(unsigned mask) const {
        vector<int> seq;
        for (int a = mask ? last(mask) : -1; mask; ) { seq.push_back(nodes[a]); int p = par[mask][a]; mask ^= 1u << a; a = p; }
        reverse(seq.begin(), seq.end());
        return seq;
    }
};

// ---------------------------------------------------------------- global model data
vector<int> act;                 // farmers with positive demand; bit b <-> node act[b]
int na; unsigned FULL = 0, NEED = 0, strM = 0;  // NEED: farmers with milk (the rest are only parking stops)
vector<int> swtNodes;            // S_wt farmers (local index w)
vector<unsigned> expandW;        // local S_wt mask -> act mask
vector<unsigned> has;            // has[j]: act mask of farmers with milk type j
vector<double> D;                // total demand per type
vector<int> rootNode;            // subtour roots: depot, then every S_tr farmer
vector<int> rootOfBit;           // act bit -> index in rootNode (S_tr only)
vector<vector<double>> cycR;     // cycR[r][X]: cycle rootNode[r] -> X -> rootNode[r], X local S_wt mask
vector<HK> subHK;

// Cheapest way to cover S_wt set B with subtours parked at the depot or at S_tr farmers of A.
vector<double> partitionCost(unsigned A, vector<unsigned>* choice) {
    int w = swtNodes.size(); unsigned W = 1u << w;
    vector<int> roots = {0};
    for (int b = 0; b < na; b++) if (A >> b & 1) roots.push_back(rootOfBit[b]);
    vector<double> g(W, INF), P(W, INF);
    for (unsigned X = 1; X < W; X++) for (int r : roots) g[X] = min(g[X], cycR[r][X]);
    if (choice) choice->assign(W, 0);
    P[0] = 0;
    for (unsigned B = 1; B < W; B++) {
        unsigned low = B & -B;
        for (unsigned X = B; X; X = (X - 1) & B) if ((X & low) && g[X] + P[B ^ X] < P[B]) {
            P[B] = g[X] + P[B ^ X];
            if (choice) (*choice)[B] = X;
        }
    }
    return P;
}

// ---------------------------------------------------------------- capacity: exact compartment typing
struct Comp { int veh, idx; bool trailer; double cap; unsigned adj; };

double maxflow(vector<vector<double>>& cap, int s, int t) {
    int N = cap.size(); double total = 0;
    while (true) {
        vector<int> par(N, -1); par[s] = s; queue<int> q; q.push(s);
        while (!q.empty() && par[t] < 0) {
            int u = q.front(); q.pop();
            for (int v = 0; v < N; v++) if (par[v] < 0 && cap[u][v] > EPS) par[v] = u, q.push(v);
        }
        if (par[t] < 0) return total;
        double f = INF;
        for (int v = t; v != s; v = par[v]) f = min(f, cap[par[v]][v]);
        for (int v = t; v != s; v = par[v]) cap[par[v]][v] -= f, cap[v][par[v]] += f;
        total += f;
    }
}

struct CapacityCheck {
    vector<Comp> comps; vector<int> type; int C = 0;
    // Max flow of type j when compartments typed j, or not yet typed (index >= from), are available.
    double typeFlow(int j, int from, vector<vector<double>>* flows = nullptr) {
        int N = na + C + 2, src = na + C, snk = na + C + 1;
        vector<vector<double>> cap(N, vector<double>(N, 0));
        for (int b = 0; b < na; b++) cap[src][b] = Q[act[b]][j];
        for (int c = 0; c < C; c++) if (type[c] == j || c >= from) {
            cap[na + c][snk] = comps[c].cap;
            for (int b = 0; b < na; b++) if (comps[c].adj >> b & 1) cap[b][na + c] = Q[act[b]][j];
        }
        auto orig = cap;
        double f = maxflow(cap, src, snk);
        if (flows) for (int b = 0; b < na; b++) for (int c = 0; c < C; c++)
            (*flows)[b][c] = max(0.0, orig[b][na + c] - cap[b][na + c]);
        return f;
    }
    bool same(int a, int b) { return comps[a].veh == comps[b].veh && comps[a].trailer == comps[b].trailer && comps[a].cap == comps[b].cap; }
    bool assign(int ci) {
        if (ci == C) return true;
        bool useful = false;
        for (int t = 0; t < m; t++) {
            if (D[t] <= EPS || !(comps[ci].adj & has[t])) continue;
            useful = true;
            if (ci > 0 && same(ci - 1, ci) && t < type[ci - 1]) continue;  // identical compartments: symmetry break
            type[ci] = t;
            bool ok = true;  // compartment ci is now lost for every other type it could have served
            for (int j = 0; j < m && ok; j++)
                if (j != t && D[j] > EPS && (comps[ci].adj & has[j])) ok = typeFlow(j, ci + 1) >= D[j] - 1e-6;
            if (ok && assign(ci + 1)) return true;
        }
        if (useful) { type[ci] = -1; return false; }
        type[ci] = -2;  // can't receive any demanded milk: leave empty
        return assign(ci + 1);
    }
    bool run(const vector<unsigned>& S) {
        comps.clear();
        for (int i = 0; i < V; i++) if (S[i]) {
            for (int k = 0; k < (int)fleet[i].truck.size(); k++) comps.push_back({i, k, false, fleet[i].truck[k], S[i]});
            for (int k = 0; k < (int)fleet[i].trailer.size(); k++) comps.push_back({i, k, true, fleet[i].trailer[k], S[i] & strM});
        }
        sort(comps.begin(), comps.end(), [](const Comp& a, const Comp& b) {
            return tie(a.veh, a.trailer, a.cap, a.idx) < tie(b.veh, b.trailer, b.cap, b.idx); });
        C = comps.size(); type.assign(C, -1);
        double cap = 0, dem = 0;
        for (auto& c : comps) cap += c.cap;
        for (double x : D) dem += x;
        if (cap < dem - 1e-6) return false;
        for (int j = 0; j < m; j++) if (D[j] > EPS && typeFlow(j, 0) < D[j] - 1e-6) return false;
        return assign(0);
    }
};

// ---------------------------------------------------------------- branch and bound over who serves whom
vector<double> costP, costC;
vector<vector<pair<double, unsigned>>> cand;
vector<unsigned> chosen, bestChosen;
int bestUsed = INT_MAX; double bestCost = INF;
long long leaves = 0, capChecks = 0;
CapacityCheck checker;

bool better(int u, double c) { return u < bestUsed || (u == bestUsed && c < bestCost - 1e-9); }

void dfs(int i, unsigned cov, int used, double cost) {
    bool done = (cov & NEED) == NEED;
    if (!better(used + !done, cost)) return;
    if (i == V) {
        leaves++;
        if (done) {
            capChecks++;
            if (checker.run(chosen)) bestUsed = used, bestCost = cost, bestChosen = chosen;
        }
        return;
    }
    for (auto& [c, mask] : cand[i]) {
        if (!better(used + 1, cost + c)) break;
        chosen[i] = mask;
        dfs(i + 1, cov | mask, used + 1, cost + c);
    }
    chosen[i] = 0;
    dfs(i + 1, cov, used, cost);
}

int main() {
    readInstance();
    auto hasMilk = [](int s) { return accumulate(Q[s].begin(), Q[s].end(), 0.0) > EPS; };
    bool swtMilk = false;
    for (int s = 1; s <= n; s++) swtMilk |= !reach[s] && hasMilk(s);
    // a trailer_ok farm without milk is never needed, except as a parking stop for truck-only trips
    for (int s = 1; s <= n; s++) if (hasMilk(s) || (reach[s] && swtMilk)) act.push_back(s);
    na = act.size();
    if (na > 16) fail("the exact solver handles at most 16 farms; use ./heuristic");
    if (na > 10) cerr << "note: " << na << " farms, the exact solver may run for hours; ./heuristic answers in seconds\n";
    FULL = na ? (1u << na) - 1 : 0;
    for (int b = 0; b < na; b++) if (hasMilk(act[b])) NEED |= 1u << b;
    has.assign(m, 0); D.assign(m, 0);
    rootNode = {0}; rootOfBit.assign(na, -1);
    for (int b = 0; b < na; b++) {
        int s = act[b];
        for (int j = 0; j < m; j++) if (Q[s][j] > EPS) has[j] |= 1u << b, D[j] += Q[s][j];
        if (reach[s]) strM |= 1u << b, rootOfBit[b] = rootNode.size(), rootNode.push_back(s);
        else swtNodes.push_back(s);
    }
    int w = swtNodes.size();
    expandW.assign(1u << w, 0);
    for (unsigned X = 1; X < (1u << w); X++) {
        int lb = __builtin_ctz(X), b = find(act.begin(), act.end(), swtNodes[lb]) - act.begin();
        expandW[X] = expandW[X & (X - 1)] | 1u << b;
    }
    HK base(0, act);
    for (int r : rootNode) subHK.emplace_back(r, swtNodes);
    cycR.assign(rootNode.size(), vector<double>(1u << w));
    for (size_t r = 0; r < rootNode.size(); r++) for (unsigned X = 0; X < (1u << w); X++) cycR[r][X] = subHK[r].cycle(X);

    vector<double> prepSum(FULL + 1, 0);
    for (unsigned mask = 1; mask <= FULL; mask++) prepSum[mask] = prepSum[mask & (mask - 1)] + prep[act[__builtin_ctz(mask)]];
    costP.assign(FULL + 1, INF); costC.assign(FULL + 1, INF);
    for (unsigned mask = 0; mask <= FULL; mask++) {
        double c = base.cycle(mask) + prepSum[mask];
        costP[mask] = c <= T + EPS ? c : INF;
    }
    for (unsigned A = strM;; A = (A - 1) & strM) {  // every S_tr subset served with the trailer attached
        vector<double> P = partitionCost(A, nullptr);
        for (unsigned B = 0; B < (1u << w); B++) {
            unsigned mask = A | expandW[B];
            if (!A && B) continue;  // (A4) a complete vehicle must visit at least one S_tr farmer
            double c = base.cycle(A) + P[B] + prepSum[mask];
            costC[mask] = c <= T + EPS ? c : INF;
        }
        if (!A) break;
    }
    cand.resize(V);
    for (int i = 0; i < V; i++) {
        double capSum = 0;
        for (double c : fleet[i].truck) capSum += c;
        for (double c : fleet[i].trailer) capSum += c;
        if (capSum <= EPS) continue;  // a vehicle that can't load milk is never worth sending
        auto& cost = fleet[i].type == 'C' ? costC : costP;
        for (unsigned mask = 1; mask <= FULL; mask++)  // milk-less stops only help a truck + trailer
            if (cost[mask] < INF && (mask & NEED) && (fleet[i].type == 'C' || !(mask & ~NEED))) cand[i].push_back({cost[mask], mask});
        sort(cand[i].begin(), cand[i].end());
    }
    chosen.assign(V, 0);
    dfs(0, 0, 0, 0);
    cerr << "exact: leaves=" << leaves << " capacity checks=" << capChecks << "\n";
    if (bestUsed == INT_MAX) { printInfeasible(); return 0; }

    // rebuild the optimal plan: compartment types, flows (loads) and routes
    checker.run(bestChosen);
    int C = checker.C;
    map<int, VehSol> out;
    for (int j = 0; j < m; j++) {
        if (D[j] <= EPS) continue;
        vector<vector<double>> fl(na, vector<double>(C, 0));
        checker.typeFlow(j, C, &fl);
        for (int b = 0; b < na; b++) for (int c = 0; c < C; c++) if (fl[b][c] > 1e-9) {
            auto& cp = checker.comps[c];
            out[cp.veh].loads.push_back({cp.trailer ? 'R' : 'T', cp.idx + 1, j + 1, act[b], fl[b][c]});
        }
    }
    vector<VehSol> sol;
    for (int i = 0; i < V; i++) if (bestChosen[i]) {
        VehSol v = out[i];
        unsigned mask = bestChosen[i];
        v.id = i + 1;
        sort(v.loads.begin(), v.loads.end());
        if (fleet[i].type == 'P') {
            v.dur = costP[mask];
            v.tour = base.order(mask);
        } else {
            v.dur = costC[mask];
            unsigned A = mask & strM, B = 0;
            for (int x = 0; x < w; x++) if (mask & expandW[1u << x]) B |= 1u << x;
            v.tour = base.order(A);
            vector<unsigned> choice;
            partitionCost(A, &choice);
            for (unsigned rest = B; rest; rest ^= choice[rest]) {
                unsigned X = choice[rest]; int br = 0;  // cheapest parking spot among depot and A
                for (int b = 0; b < na; b++)
                    if ((A >> b & 1) && cycR[rootOfBit[b]][X] < cycR[br][X]) br = rootOfBit[b];
                vector<int> sub = {rootNode[br]};
                for (int x : subHK[br].order(X)) sub.push_back(x);
                sub.push_back(rootNode[br]);
                v.subs.push_back(sub);
            }
        }
        v.tour.insert(v.tour.begin(), 0); v.tour.push_back(0);
        sol.push_back(v);
    }
    printPlan(sol);
}
