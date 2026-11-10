#!/usr/bin/env python3
"""A stand-in for the Supabase account server, for testing login and sync by hand.

Never test against the real project: this keeps one made-up account and its
progress row in memory (gone when it stops).

    python3 web/fake_supabase.py              # listens on http://localhost:8766
    python3 web/fake_supabase.py --seed FILE  # start with FILE as the online progress

Point a test build of the site at it with a settings file like
    {"url": "http://localhost:8766", "key": "test-public-key"}
passed as  -DSUPABASE_CONFIG=that-file.json  (see WEB.md).

The account is TEST_EMAIL / TEST_PASSWORD below. "Forgot password" prints the
reset link here instead of sending an email. GET /_state shows what's stored.
"""
import argparse, json, secrets, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

TEST_EMAIL = "cadet@example.test"
TEST_PASSWORD = "practice-only-1"
USER_ID = "00000000-0000-4000-8000-000000000001"
KEY = "test-public-key"

state = {"password": TEST_PASSWORD, "row": None, "access": set(), "refresh": set(), "puts": 0}


def tokens():
    a, r = "access-" + secrets.token_hex(8), "refresh-" + secrets.token_hex(8)
    state["access"].add(a)
    state["refresh"].add(r)
    return {"access_token": a, "refresh_token": r, "expires_in": 3600, "token_type": "bearer",
            "user": {"id": USER_ID, "email": TEST_EMAIL}}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def send(self, status, body=None):
        data = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "apikey, authorization, content-type, prefer")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return {}

    def authed(self):
        return self.headers.get("Authorization", "")[7:] in state["access"]

    def do_OPTIONS(self):
        self.send(204)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/_state":
            return self.send(200, {"row": state["row"], "puts": state["puts"], "password_changed": state["password"] != TEST_PASSWORD})
        if self.headers.get("apikey") != KEY:
            return self.send(401, {"message": "Invalid API key"})
        if url.path == "/rest/v1/progress":
            if not self.authed():
                return self.send(401, {"message": "JWT expired"})
            return self.send(200, [] if state["row"] is None else [{"data": state["row"]}])
        self.send(404, {"message": "Not found"})

    def do_POST(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        body = self.body()
        if self.headers.get("apikey") != KEY:
            return self.send(401, {"message": "Invalid API key"})
        if url.path == "/auth/v1/token":
            kind = query.get("grant_type", [""])[0]
            if kind == "password":
                if body.get("email") == TEST_EMAIL and body.get("password") == state["password"]:
                    return self.send(200, tokens())
                return self.send(400, {"error_code": "invalid_credentials", "msg": "Invalid login credentials"})
            if kind == "refresh_token" and body.get("refresh_token") in state["refresh"]:
                state["refresh"].discard(body["refresh_token"])
                return self.send(200, tokens())
            return self.send(400, {"msg": "Invalid Refresh Token"})
        if url.path == "/auth/v1/recover":
            if body.get("email") == TEST_EMAIL:
                t = tokens()["access_token"]
                back = query.get("redirect_to", ["http://localhost/reset.html"])[0]
                print("RESET LINK: %s#access_token=%s&type=recovery" % (back, t), flush=True)
            return self.send(200, {})
        if url.path == "/rest/v1/progress":
            if not self.authed():
                return self.send(401, {"message": "JWT expired"})
            if body.get("user_id") != USER_ID:
                return self.send(403, {"message": "new row violates row-level security policy"})
            state["row"] = body.get("data")
            state["puts"] += 1
            return self.send(201)
        self.send(404, {"message": "Not found"})

    def do_PUT(self):
        body = self.body()
        if urlparse(self.path).path == "/auth/v1/user" and self.authed():
            if len(body.get("password", "")) < 6:
                return self.send(422, {"msg": "Password should be at least 6 characters"})
            state["password"] = body["password"]
            return self.send(200, {"id": USER_ID, "email": TEST_EMAIL})
        self.send(401, {"msg": "Invalid token"})


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--seed", help="JSON file to start with as the online progress")
    args = ap.parse_args()
    if args.seed:
        with open(args.seed) as f:
            state["row"] = json.load(f)
    print("Fake account server on http://localhost:%d (account: %s)" % (args.port, TEST_EMAIL), flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
