#!/usr/bin/env python3
"""Tiny local server: serves index.html and runs ./exact, ./heuristic, ./checker.
Usage (from the project root, after building the binaries):  python3 ui/server.py
Then open http://127.0.0.1:8000   (standard library only, no pip install)."""
import json, os, subprocess, tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # project root holding the binaries
PORT = int(os.environ.get("PORT", 8000))

def read(path):
    with open(path, "rb") as f:
        return f.read()

def solve(req):
    solver = req.get("solver", "heuristic")
    if solver not in ("exact", "heuristic"):
        return {"error": "unknown solver"}
    exe = os.path.join(ROOT, solver)
    if not os.path.exists(exe):
        return {"error": f"{solver} binary not found in {ROOT}. Build it first: g++ -std=c++17 -O2 -o {solver} {solver}.cpp"}
    text = req.get("input", "")
    cmd = [exe]
    if solver == "heuristic":
        secs = max(1.0, min(60.0, float(req.get("seconds", 2))))
        cmd += [str(secs), str(int(req.get("seed", 1)))]
    try:
        p = subprocess.run(cmd, input=text, capture_output=True, text=True, timeout=300, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return {"error": "solver timed out (exact is for small inputs, about 10 farms)"}
    out = {"plan": p.stdout, "log": p.stderr.strip(), "code": p.returncode, "check": ""}
    chk = os.path.join(ROOT, "checker")
    if p.returncode == 0 and os.path.exists(chk):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "in.txt"), os.path.join(d, "plan.txt")
            open(a, "w").write(text); open(b, "w").write(p.stdout)
            c = subprocess.run([chk, a, b], capture_output=True, text=True, timeout=60)
            out["check"] = (c.stdout + c.stderr).strip()
    return out

class H(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._send(200, read(os.path.join(HERE, "index.html")), "text/html")
        elif self.path == "/api/sample":
            p = os.path.join(ROOT, "sample.txt")
            self._send(200, read(p) if os.path.exists(p) else b"", "text/plain")
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/api/solve":
            return self._send(404, {"error": "not found"})
        try:
            n = int(self.headers.get("Content-Length", 0))
            self._send(200, solve(json.loads(self.rfile.read(n))))
        except Exception as e:
            self._send(500, {"error": str(e)})

def ensure_built():
    """Compile exact/heuristic/checker with g++ if the binary is missing or older than its sources."""
    srcs = [os.path.join(ROOT, f) for f in os.listdir(ROOT) if f.endswith(".h")]
    for name in ("exact", "heuristic", "checker"):
        cpp, exe = os.path.join(ROOT, name + ".cpp"), os.path.join(ROOT, name)
        if not os.path.exists(cpp):
            continue
        newest = max([os.path.getmtime(cpp)] + [os.path.getmtime(s) for s in srcs])
        if not os.path.exists(exe) or os.path.getmtime(exe) < newest:
            print(f"building {name} ...")
            r = subprocess.run(["g++", "-std=c++17", "-O2", "-o", exe, cpp], cwd=ROOT)
            if r.returncode:
                print(f"WARNING: {name} failed to build (is g++ installed?)")

if __name__ == "__main__":
    ensure_built()
    print(f"Milk collection dashboard: http://127.0.0.1:{PORT}  (binaries expected in {ROOT})")
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()