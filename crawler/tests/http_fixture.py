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
        if self.path.startswith("/robots-case/"):
            self.server.robots_case = self.path.removeprefix("/robots-case/")
            self.send_response(204)
            self.end_headers()
            return
        if self.path == "/must-not-follow":
            self.server.followed = True
        if self.path.startswith("/private"):
            self.server.blocked_page_requested = True
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
            "/hop": (302, "/ok"),
            "/chain": (301, "/hop"),
            "/loop-a": (302, "/loop-b"),
            "/loop-b": (307, "/loop-a#same-page"),
            "/to-partial": (302, "/partial"),
            "/to-private": (302, "/private/page"),
            "/page-cross-origin": (302, f"http://localhost:{self.server.server_port}/private/page"),
            "/page-cross-public": (302, f"http://localhost:{self.server.server_port}/public"),
            "/crawl/alias": (302, "/crawl/nested/page"),
            "/crawl/alias-two": (301, "/crawl/nested/page"),
            "/crawl/export-alias": (302, "/crawl/export"),
        }
        default_status = {"/missing": 404, "/empty": 204}.get(self.path, 200)
        status, location = redirects.get(self.path, (default_status, None))
        body = b"x" * 128 if self.path == "/large" else b"<p>Hello</p>"
        crawl_pages = {
            "/crawl/start": b'<title>Start</title><p>Start body</p>'
                            b'<a href="a#first">A</a><a href="a#again">Duplicate</a>'
                            b'<a href="b">B</a><a href="/private/page">Private</a>'
                            b'<a href="http://example.invalid/outside">Outside</a>',
            "/crawl/a": b'<title>A</title><p>Alpha &amp; beta</p>'
                        b'<a href="start">Back</a><a href="deep">Deep</a>',
            "/crawl/b": b'<title>B</title><p>Beta</p>',
            "/crawl/deep": b'<title>Deep</title><p>Deep body</p>',
            "/crawl/nested/page": b'<title>Redirected</title><a href="child">Child</a>',
            "/crawl/nested/child": b'<title>Child</title><p>Child body</p>',
            "/crawl/export": b'<title>Export &quot;title&quot; \\ caf\xc3\xa9</title>'
                             b'<p>First &quot;line&quot; \\ path</p><p>Second caf\xc3\xa9 &#1;</p>',
        }
        body = crawl_pages.get(self.path, body)
        if self.path in ("/crawl/error", "/crawl/plain", "/crawl/untyped", "/crawl/htmlish"):
            body = b'<a href="/must-not-follow">Do not discover</a>'
        if self.path == "/crawl/error":
            status = 404
        encoding_cases = {
            "/encoding/utf8": ("text/html; charset=\"UTF-8\"", b"<title>Caf\xc3\xa9</title><p>UTF-8</p>"),
            "/encoding/undeclared": ("text/html", b"<title>Undeclared</title><p>UTF-8</p>"),
            "/encoding/bom": ("text/html; charset=windows-1252", b"\xef\xbb\xbf<title>BOM</title><p>Clean body</p>"),
            "/encoding/meta": ("text/html", b"<meta charset=windows-1252>"),
            "/encoding/legacy": ("text/html; charset=windows-1252", b"<p>Caf\xe9</p>"),
            "/encoding/broken": ("text/html; charset=utf-8", b"<p>Broken \xff</p>"),
            "/encoding/ambiguous": ("text/html; charset=utf-8; charset=windows-1252", b"<p>Text</p>"),
        }
        encoding_type = None
        if self.path in encoding_cases:
            encoding_type, body = encoding_cases[self.path]
            # Rejected input must never contribute links to the frontier.
            body += b'<a href="/must-not-follow">Do not discover</a>' if self.path not in (
                "/encoding/utf8", "/encoding/undeclared", "/encoding/bom") else b""
        if self.path in ("/robots.txt", "/robots-rules"):
            scenario = self.server.robots_case
            body = b"User-agent: HumanFirstSearch\nDisallow: /private\n"
            status = {"empty": 200, "no-content": 204, "missing": 404, "gone": 410,
                      "forbidden": 403, "rate-limit": 429, "server-error": 503,
                      "not-modified": 304}.get(scenario, 200)
            if scenario in ("empty", "no-content"):
                body = b""
            if self.path == "/robots.txt":
                if scenario == "redirect":
                    status, location = 302, "/robots-rules"
                elif scenario == "cross-origin":
                    status, location = 302, f"http://localhost:{self.server.server_port}/robots-rules"
                elif scenario == "loop":
                    status, location = 302, "/robots.txt"
                elif scenario == "bad-redirect":
                    status, location = 302, "file:///unused"
            if scenario == "slow":
                time.sleep(0.5)
            if scenario == "large":
                body = b"x" * (500 * 1024 + 1)
        if self.path == "/agent":
            body = self.headers.get("User-Agent", "").encode("utf-8")
        if self.path == "/empty":
            body = b""
        self.send_response(status)
        if self.path not in ("/empty", "/crawl/untyped"):
            media_type = "text/plain" if self.path in ("/robots.txt", "/robots-rules") else "text/html"
            media_type = {"/crawl/plain": "text/plain", "/crawl/htmlish": "text/htmlish",
                          "/crawl/nested/page": "Text/HTML"}.get(self.path, media_type)
            self.send_header("Content-Type", encoding_type or media_type + "; charset=utf-8")
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
    server.robots_case = "rules"
    server.blocked_page_requested = False
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        env = dict(os.environ, HFS_TEST_BASE_URL=f"http://127.0.0.1:{server.server_port}")
        result = subprocess.run(sys.argv[1:], env=env, timeout=15)
        if server.followed:
            raise RuntimeError("Client followed a redirect despite the single-request policy")
        if server.blocked_page_requested:
            raise RuntimeError("Client requested a page forbidden by robots.txt")
        exit_code = result.returncode
    finally:
        server.shutdown()
        thread.join()
sys.exit(exit_code)
