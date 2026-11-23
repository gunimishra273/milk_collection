// gen.cpp - random instance generator.
//   ./gen <seed> <n> <V> <m>      small random instance (random fleet)
//   ./gen <seed> <n> paper        case-study scale: 4 milk types, the paper's 13-vehicle fleet (Tables 2-3)
#include <bits/stdc++.h>
using namespace std;

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: gen seed n V m | gen seed n paper\n"); return 1; }
    mt19937 rng(atoi(argv[1]));
    auto U = [&](double a, double b) { return uniform_real_distribution<double>(a, b)(rng); };
    auto I = [&](int a, int b) { return uniform_int_distribution<int>(a, b)(rng); };
    int n = atoi(argv[2]);
    bool paper = string(argv[3]) == "paper";
    int m = paper ? 4 : atoi(argv[4]);
    vector<string> fleet;
    double T, speed, span;
    if (paper) {
        // complete vehicles: truck compartments | trailer compartments (quintals)
        fleet = {"truck 37.5 37.5 trailer 37.5 37.5", "truck 37.5 37.5 trailer 37.5 37.5", "truck 80 60 trailer 50 60 60",
                 "truck 130 trailer 130 50", "truck 37.5 37.5 trailer 37.5 37.5",
                 "truck 46.7 46.7 46.7", "truck 46.7 46.7 46.7", "truck 43.5 43.5 43.5", "truck 37 37 31",
                 "truck 20 20 20", "truck 20 20 20", "truck 13.35 13.35 13.35", "truck 37 37 36"};
        T = 480; speed = 0.8; span = 120;  // 8 h shift in minutes, 48 km/h, ~120 km region
    } else {
        int V = atoi(argv[3]);
        for (int i = 0; i < V; i++) {
            bool cv = I(0, 9) < 4;
            string s = "truck";
            int kt = I(1, cv ? 2 : 3);
            for (int k = 0; k < kt; k++) s += " " + to_string(10 * I(2, 5));
            if (cv) { int kr = I(1, 2); s += " trailer"; for (int k = 0; k < kr; k++) s += " " + to_string(10 * I(2, 5)); }
            fleet.push_back(s);
        }
        T = I(150, 400); speed = 1; span = 100;
    }
    vector<double> x(n + 1), y(n + 1);
    x[0] = span * 0.5; y[0] = paper ? span * 0.1 : span * 0.5;
    for (int s = 1; s <= n; s++) x[s] = U(0, span), y[s] = U(0, span * (paper ? 0.8 : 1));
    const char* names[] = {"High_quality", "Standard", "Sanitary", "Organic"};
    printf("# generated: seed=%s farms=%d vehicles=%d milk types=%d\n", argv[1], n, (int)fleet.size(), m);
    printf("SHIFT_MINUTES %.0f\nSPEED_KM_PER_HOUR %.0f\nMILK_TYPES", T, speed * 60);
    for (int j = 0; j < m; j++) printf(" %s", j < 4 ? names[j] : ("Type_" + to_string(j + 1)).c_str());
    printf("\n\nFARMS\n# name road stop_minutes milk_per_type...\n");
    double perFarmer = paper ? 0.6 * 1855 / max(1, n) : 25;
    for (int s = 1; s <= n; s++) {
        printf("F%d %s %d", s, I(0, 99) < (paper ? 75 : 65) ? "trailer_ok" : "truck_only", I(paper ? 10 : 5, paper ? 25 : 15));
        vector<double> q(m, 0);
        int k = I(1, min(m, 2));
        for (int c = 0; c < k; c++) q[I(0, m - 1)] += round(U(0.4, 1.6) * perFarmer / k * 2) / 2;
        for (double v : q) printf(" %g", v);
        printf("\n");
    }
    printf("\nDISTANCES_KM\n");
    for (int a = 0; a <= n; a++) {
        printf(a ? "F%d" : "Depot", a);
        for (int b = 0; b <= n; b++) printf(" %.0f", round(hypot(x[a] - x[b], y[a] - y[b])));
        printf("\n");
    }
    printf("\nFLEET\n");
    for (size_t i = 0; i < fleet.size(); i++) printf("V%d %s\n", (int)i + 1, fleet[i].c_str());
}
