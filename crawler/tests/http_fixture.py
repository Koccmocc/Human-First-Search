"""Run HTTP client tests against an ephemeral loopback server, with no internet."""

import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path == "/must-not-follow":
            self.server.followed = True
        if self.path == "/slow":
            time.sleep(0.5)
        redirects = {
            "/redirect": (302, "/must-not-follow"),
            "/relative": (301, "../must-not-follow?x=1#part"),
            "/absolute": (303, "https://example.invalid/not-fetched"),
            "/other-scheme": (307, "file:///unused"),
            "/credentials": (308, "https://user:pass@example.invalid/"),
            "/no-location": (302, None),
            "/ok-location": (200, "/must-not-follow"),
            "/not-modified": (304, "/must-not-follow"),
        }
        default_status = {"/missing": 404, "/empty": 204}.get(self.path, 200)
        status, location = redirects.get(self.path, (default_status, None))
        body = b"x" * 128 if self.path == "/large" else b"<p>Hello</p>"
        if self.path == "/empty":
            body = b""
        self.send_response(status)
        if self.path != "/empty":
            self.send_header("Content-Type", "text/html; charset=utf-8")
        if location is not None:
            self.send_header("Location", location)
        # /large intentionally has no Content-Length to exercise the write limit.
        if self.path != "/large":
            self.send_header("Content-Length", str(100 if self.path == "/partial" else len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # The timeout test intentionally closes the connection early.


with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
    server.followed = False
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        env = dict(os.environ, HFS_TEST_BASE_URL=f"http://127.0.0.1:{server.server_port}")
        result = subprocess.run(sys.argv[1:], env=env, timeout=15)
        if server.followed:
            raise RuntimeError("Client followed a redirect despite the single-request policy")
        exit_code = result.returncode
    finally:
        server.shutdown()
        thread.join()
sys.exit(exit_code)
