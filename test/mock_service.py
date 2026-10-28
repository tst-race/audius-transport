#!/usr/bin/env python3
"""
Mock of the illustrative REST service that source/transport/AudiusClient.cpp talks to. It runs as a
docker-compose sidecar (see SIDECAR_SERVICES in adapter.py) so the stub's integration scenarios run
with no real service and no credentials. Delete it once AudiusClient targets a real service.

  PUT    /containers/<id>                      201 created / 409 exists
  GET    /containers/<id>                      200 / 404
  DELETE /containers/<id>                      204 / 404
  POST   /containers/<id>/messages             201 {"id": "<message id>"}      (404 if no container)
  GET    /containers/<id>/messages?after=<id>  200 {"ids": [...]} oldest first (404 if no container)
  GET    /containers/<id>/messages/<mid>       200 raw bytes
Every request needs an "Authorization: Bearer <non-empty token>" header (401 otherwise).
"""

import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

lock = threading.Lock()
containers = {}  # container id -> list of (message id, bytes), in post order
counter = 0


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("mock-service: " + (fmt % args) + "\n")

    def _reply(self, status, body=b"", content_type="application/octet-stream"):
        if isinstance(body, (dict, list)):
            body, content_type = json.dumps(body).encode(), "application/json"
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _route(self, method):
        global counter
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        if not self.headers.get("Authorization", "").removeprefix("Bearer ").strip():
            return self._reply(401)
        url = urlparse(self.path)
        parts = [unquote(p) for p in url.path.strip("/").split("/")]
        if len(parts) < 2 or parts[0] != "containers":
            return self._reply(404)
        cid, rest = parts[1], parts[2:]

        with lock:
            if not rest:
                if method == "PUT":
                    if cid in containers:
                        return self._reply(409)
                    containers[cid] = []
                    return self._reply(201)
                if method == "GET":
                    return self._reply(200 if cid in containers else 404)
                if method == "DELETE":
                    return self._reply(204 if containers.pop(cid, None) is not None else 404)
            elif rest == ["messages"] and cid in containers:
                if method == "POST":
                    counter += 1
                    mid = "%08d" % counter
                    containers[cid].append((mid, body))
                    return self._reply(201, {"id": mid})
                if method == "GET":
                    after = parse_qs(url.query).get("after", [""])[0]
                    return self._reply(200, {"ids": [m for m, _ in containers[cid] if m > after]})
            elif len(rest) == 2 and rest[0] == "messages" and cid in containers and method == "GET":
                for mid, data in containers[cid]:
                    if mid == rest[1]:
                        return self._reply(200, data)
            return self._reply(404)

    def do_GET(self):
        self._route("GET")

    def do_PUT(self):
        self._route("PUT")

    def do_POST(self):
        self._route("POST")

    def do_DELETE(self):
        self._route("DELETE")


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
