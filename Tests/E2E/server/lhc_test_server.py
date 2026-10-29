#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation
# Licensed under the MIT license. See LICENSE file in the project root for full license information.
"""Loopback HTTP / HTTPS / WebSocket server for the libHttpClient end-to-end tests.

Python 3.8+ standard library only (no pip installs), so it runs on any CI agent that has Python.

The server binds 127.0.0.1 on free ports and prints a single JSON line to stdout once it is ready:

    {"http": 50001, "http2": 50002, "https": 50003, "https_error": null, "pid": 1234}

  http   plain HTTP + WebSocket (ws://) on the same port
  http2  a second plain HTTP origin, used as the target of cross-origin redirects
  https  TLS with a throwaway self-signed certificate for 127.0.0.1 that is generated at startup
         with the `openssl` CLI (PATH, then Git for Windows) into a temp dir and deleted once
         loaded. No key material is stored in the repo. If openssl can't be found or fails,
         "https" is null and "https_error" says why.

It then serves until it is killed, or until --timeout seconds have passed. See Tests/E2E/README.md
for the route contract.
"""

import argparse
import base64
import faulthandler
import gzip
import hashlib
import http.server
import json
import os
import shutil
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from urllib.parse import parse_qs, urlsplit

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_WS_MESSAGE = 64 * 1024 * 1024
MAX_REQUEST_BODY = 256 * 1024 * 1024

PORTS = {"http": 0, "http2": 0, "https": 0}
VERBOSE = False


def log(msg):
    if VERBOSE:
        sys.stderr.write("[lhc_test_server] %s\n" % msg)
        sys.stderr.flush()


def pattern_bytes(offset, length):
    """Deterministic body content: byte i of a body is (i % 251). Clients verify against this."""
    start = offset % 251
    period = bytes(range(251))
    reps = (start + length) // 251 + 1
    return (period * reps)[start:start + length]


# ---------------------------------------------------------------------------------------------
# Shared state (guarded by STATE_LOCK)
# ---------------------------------------------------------------------------------------------

STATE_LOCK = threading.Lock()
RETRY_COUNTERS = {}      # id -> number of requests seen by /retry-after
CONCURRENCY = {}         # group -> {"active": n, "max": n, "total": n}
WS_LOG = {}              # id -> dict describing what the server saw on that WebSocket


def ws_log_update(ws_id, **kwargs):
    if not ws_id:
        return
    with STATE_LOCK:
        entry = WS_LOG.setdefault(ws_id, {})
        entry.update(kwargs)


# ---------------------------------------------------------------------------------------------
# WebSocket (RFC 6455 + RFC 7692 permessage-deflate)
# ---------------------------------------------------------------------------------------------

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WebSocketClosed(Exception):
    pass


class WebSocketSession:
    def __init__(self, rfile, sock, deflate, server_no_ctx, client_no_ctx, ws_id, headers):
        self.rfile = rfile
        self.sock = sock
        self.send_lock = threading.Lock()
        self.deflate = deflate
        self.server_no_ctx = server_no_ctx
        self.client_no_ctx = client_no_ctx
        self.ws_id = ws_id
        self.headers = headers
        self.closed = threading.Event()
        self.close_sent = False
        self.stats = {
            "deflate": deflate,
            "messages_received": 0,
            "compressed_messages_received": 0,
            "fragmented_messages_received": 0,
            "pings_received": 0,
            "pongs_received": 0,
            "pings_sent": 0,
        }
        self._decompressor = zlib.decompressobj(-15) if deflate else None
        self._compressor = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION, zlib.DEFLATED, -15) if deflate else None

    # -- framing -------------------------------------------------------------------------------

    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                raise WebSocketClosed("connection closed by peer")
            data += chunk
        return data

    def read_frame(self):
        b0, b1 = self._read_exact(2)
        fin = bool(b0 & 0x80)
        rsv1 = bool(b0 & 0x40)
        opcode = b0 & 0x0F
        masked = bool(b1 & 0x80)
        length = b1 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._read_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._read_exact(8))[0]
        if length > MAX_WS_MESSAGE:
            raise WebSocketClosed("frame too large")
        mask = self._read_exact(4) if masked else None
        payload = self._read_exact(length) if length else b""
        if mask and payload:
            payload = _unmask_fast(payload, mask)
        return fin, rsv1, opcode, payload

    def send_frame(self, opcode, payload, fin=True, rsv1=False):
        header = bytearray()
        header.append((0x80 if fin else 0) | (0x40 if rsv1 else 0) | opcode)
        n = len(payload)
        if n < 126:
            header.append(n)
        elif n < 65536:
            header.append(126)
            header += struct.pack("!H", n)
        else:
            header.append(127)
            header += struct.pack("!Q", n)
        with self.send_lock:
            if self.close_sent and opcode != OP_CLOSE:
                return
            if opcode == OP_CLOSE:
                self.close_sent = True
            self.sock.sendall(bytes(header) + payload)

    def _compress(self, data):
        if self.server_no_ctx:
            comp = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION, zlib.DEFLATED, -15)
        else:
            comp = self._compressor
        out = comp.compress(data) + comp.flush(zlib.Z_SYNC_FLUSH)
        if out.endswith(b"\x00\x00\xff\xff"):
            out = out[:-4]
        return out

    def _decompress(self, data):
        if self.client_no_ctx:
            self._decompressor = zlib.decompressobj(-15)
        return self._decompressor.decompress(data + b"\x00\x00\xff\xff")

    def send_message(self, opcode, data, fragments=1):
        """Send a data message, optionally split into `fragments` frames, compressed if negotiated."""
        compressed = self.deflate
        payload = self._compress(data) if compressed else data
        fragments = max(1, min(fragments, max(1, len(payload))))
        if fragments == 1:
            self.send_frame(opcode, payload, fin=True, rsv1=compressed)
            return
        step = (len(payload) + fragments - 1) // fragments
        pieces = [payload[i:i + step] for i in range(0, len(payload), step)]
        for i, piece in enumerate(pieces):
            self.send_frame(opcode if i == 0 else OP_CONT, piece, fin=(i == len(pieces) - 1), rsv1=(compressed and i == 0))

    def send_close(self, code=None, reason=b""):
        payload = b"" if code is None else struct.pack("!H", code) + reason
        try:
            self.send_frame(OP_CLOSE, payload)
        except OSError:
            pass

    def send_ping(self, data=b"lhc-ping"):
        self.stats["pings_sent"] += 1
        self.send_frame(OP_PING, data)

    # -- message loop --------------------------------------------------------------------------

    def read_message(self):
        """Returns (opcode, data) for the next complete data message. Handles control frames inline."""
        msg_opcode = None
        msg_compressed = False
        parts = []
        frame_count = 0
        while True:
            fin, rsv1, opcode, payload = self.read_frame()
            if opcode == OP_PING:
                self.stats["pings_received"] += 1
                self.send_frame(OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                self.stats["pongs_received"] += 1
                continue
            if opcode == OP_CLOSE:
                code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else None
                ws_log_update(self.ws_id, client_close_code=code, client_close_reason=payload[2:].decode("utf-8", "replace"))
                log("ws %s: client close %s" % (self.ws_id, code))
                # Echo the close (RFC 6455 5.5.1) unless we initiated it.
                if not self.close_sent:
                    self.send_close(code if code is not None else None)
                raise WebSocketClosed("close frame")
            if opcode in (OP_TEXT, OP_BINARY):
                if msg_opcode is not None:
                    raise WebSocketClosed("protocol error: new message before previous finished")
                msg_opcode = opcode
                msg_compressed = rsv1
            elif opcode == OP_CONT:
                if msg_opcode is None:
                    raise WebSocketClosed("protocol error: continuation without a message")
            else:
                raise WebSocketClosed("protocol error: unknown opcode %d" % opcode)
            parts.append(payload)
            frame_count += 1
            if sum(len(p) for p in parts) > MAX_WS_MESSAGE:
                raise WebSocketClosed("message too large")
            if fin:
                break
        data = b"".join(parts)
        if msg_compressed:
            if not self.deflate:
                raise WebSocketClosed("protocol error: RSV1 without permessage-deflate")
            data = self._decompress(data)
            self.stats["compressed_messages_received"] += 1
        self.stats["messages_received"] += 1
        if frame_count > 1:
            self.stats["fragmented_messages_received"] += 1
        return msg_opcode, data


def _unmask_fast(payload, mask):
    n = len(payload)
    key = int.from_bytes((mask * ((n // 4) + 1))[:n], "little")
    return (int.from_bytes(payload, "little") ^ key).to_bytes(n, "little")


def parse_deflate_offer(header_value):
    """Returns (accepted, server_no_ctx, client_no_ctx) for the first permessage-deflate offer."""
    if not header_value:
        return False, False, False
    for offer in header_value.split(","):
        params = [p.strip() for p in offer.split(";")]
        if params[0].lower() != "permessage-deflate":
            continue
        names = {p.split("=")[0].strip().lower() for p in params[1:]}
        return True, "server_no_context_takeover" in names, "client_no_context_takeover" in names
    return False, False, False


# ---------------------------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------------------------

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "lhc_test_server/1.0"

    def log_message(self, fmt, *args):
        log("%s:%d %s" % (self.server.name, self.server.server_address[1], fmt % args))

    # Every method goes through the same router.
    def do_GET(self):
        self.route()

    do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = do_HEAD = do_GET

    # -- helpers -------------------------------------------------------------------------------

    def read_body(self):
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            body = b""
            while True:
                size_line = self.rfile.readline()
                size = int(size_line.split(b";")[0].strip() or b"0", 16)
                if size == 0:
                    # Trailers until blank line
                    while self.rfile.readline() not in (b"\r\n", b"\n", b""):
                        pass
                    return body
                body += self.rfile.read(size)
                self.rfile.readline()
                if len(body) > MAX_REQUEST_BODY:
                    raise ValueError("request body too large")
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length > MAX_REQUEST_BODY:
            raise ValueError("request body too large")
        return self.rfile.read(length) if length else b""

    def send_body(self, status, body, content_type="application/octet-stream", headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or []):
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)

    def send_json(self, status, obj, headers=None):
        body = json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        self.send_body(status, body, "application/json; charset=utf-8", headers)

    def header_dict(self):
        result = {}
        for name, value in self.headers.items():
            # http.server decodes header bytes as latin-1; recover the raw bytes and prefer UTF-8.
            raw = value.encode("latin-1", "replace")
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                text = value
            key = name.lower()
            result[key] = text if key not in result else result[key] + ", " + text
        return result

    def non_ascii_header_bytes(self):
        """Hex of the raw bytes of every header value that isn't plain ASCII."""
        result = {}
        for name, value in self.headers.items():
            raw = value.encode("latin-1", "replace")
            if any(b >= 0x80 for b in raw):
                result[name.lower()] = raw.hex()
        return result

    # -- router --------------------------------------------------------------------------------

    def route(self):
        try:
            url = urlsplit(self.path)
            self.query = {k: v[-1] for k, v in parse_qs(url.query).items()}
            parts = [p for p in url.path.split("/") if p]
            if self.headers.get("Upgrade", "").lower() == "websocket" and parts and parts[0] == "ws":
                self.handle_websocket(parts)
                return
            body = self.read_body()
            handler = ROUTES.get(parts[0] if parts else "")
            if handler is None:
                self.send_json(404, {"error": "no such route", "path": self.path})
                return
            handler(self, parts[1:], body)
        except (ConnectionError, BrokenPipeError, ssl.SSLError, socket.timeout) as e:
            log("connection error on %s: %r" % (self.path, e))
            self.close_connection = True
        except Exception as e:  # report handler bugs to the client instead of dropping the socket
            log("handler error on %s: %r" % (self.path, e))
            try:
                self.send_json(500, {"error": "server exception", "detail": repr(e)})
            except OSError:
                pass
            self.close_connection = True

    # -- HTTP routes ---------------------------------------------------------------------------

    def r_echo(self, args, body):
        encoding = self.headers.get("Content-Encoding", "")
        decoded = body
        if encoding.lower() == "gzip" and body:
            decoded = gzip.decompress(body)
        result = {
            "method": self.command,
            "path": self.path,
            "port": self.server.server_address[1],
            "server": self.server.name,
            "headers": self.header_dict(),
            "non_ascii_header_bytes": self.non_ascii_header_bytes(),
            "body_length": len(decoded),
            "body_crc32": zlib.crc32(decoded) & 0xFFFFFFFF,
            "wire_body_length": len(body),
            "content_encoding": encoding,
        }
        if len(decoded) <= 4096:
            result["body"] = decoded.decode("utf-8", "replace")
        self.send_json(200, result, headers=[("X-Lhc-Echo", "1"), ("X-Lhc-Multi", "a"), ("X-Lhc-Multi", "b")])

    def r_status(self, args, body):
        code = int(args[0]) if args else 200
        if code in (204, 304) or code < 200:
            self.send_response(code)
            self.end_headers()
            return
        self.send_body(code, ("status %d" % code).encode(), "text/plain")

    def r_retry_after(self, args, body):
        failures = int(args[0]) if args else 1
        status = int(self.query.get("status", "503"))
        after = self.query.get("after", "1")
        key = self.query.get("id", "default")
        with STATE_LOCK:
            RETRY_COUNTERS[key] = RETRY_COUNTERS.get(key, 0) + 1
            attempt = RETRY_COUNTERS[key]
        if attempt <= failures:
            self.send_body(status, ("attempt %d" % attempt).encode(), "text/plain", headers=[("Retry-After", after)])
        else:
            self.send_json(200, {"attempts": attempt})

    def r_retry_count(self, args, body):
        key = self.query.get("id", "default")
        with STATE_LOCK:
            count = RETRY_COUNTERS.get(key, 0)
        self.send_json(200, {"count": count})

    def r_delay(self, args, body):
        ms = int(args[0]) if args else 1000
        time.sleep(ms / 1000.0)
        self.send_body(200, ("delayed %d" % ms).encode(), "text/plain")

    def r_bytes(self, args, body):
        n = int(args[0]) if args else 0
        chunk = max(1, int(self.query.get("chunk", str(256 * 1024))))
        delay_ms = int(self.query.get("delay_ms", "0"))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(n))
        self.end_headers()
        if self.command == "HEAD":
            return
        offset = 0
        while offset < n:
            step = min(chunk, n - offset)
            self.wfile.write(pattern_bytes(offset, step))
            offset += step
            if delay_ms and offset < n:
                self.wfile.flush()
                time.sleep(delay_ms / 1000.0)

    def r_stream(self, args, body):
        n = int(args[0]) if args else 0
        chunk = max(1, int(self.query.get("chunk", "4096")))
        delay_ms = int(self.query.get("delay_ms", "0"))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        if self.command == "HEAD":
            return
        offset = 0
        while offset < n:
            step = min(chunk, n - offset)
            self.wfile.write(b"%x\r\n" % step + pattern_bytes(offset, step) + b"\r\n")
            offset += step
            if delay_ms:
                self.wfile.flush()
                time.sleep(delay_ms / 1000.0)
        self.wfile.write(b"0\r\n\r\n")

    def r_gzip(self, args, body):
        plain = b"".join(b"libHttpClient gzip response line %d\n" % i for i in range(1000))
        self.send_body(200, gzip.compress(plain), "text/plain", headers=[("Content-Encoding", "gzip"), ("X-Lhc-Plain-Length", str(len(plain))), ("X-Lhc-Plain-Crc32", str(zlib.crc32(plain) & 0xFFFFFFFF))])

    def r_redirect(self, args, body):
        kind = args[0] if args else "same"
        code = int(self.query.get("code", "302"))
        port = PORTS["http"] if kind == "same" else PORTS["http2"]
        self.send_response(code)
        self.send_header("Location", "http://127.0.0.1:%d/echo" % port)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def r_concurrency(self, args, body):
        ms = int(args[0]) if args else 200
        group = self.query.get("group", "default")
        with STATE_LOCK:
            g = CONCURRENCY.setdefault(group, {"active": 0, "max": 0, "total": 0})
            g["active"] += 1
            g["total"] += 1
            g["max"] = max(g["max"], g["active"])
        try:
            time.sleep(ms / 1000.0)
        finally:
            with STATE_LOCK:
                g["active"] -= 1
        self.send_json(200, {"group": group})

    def r_concurrency_stats(self, args, body):
        group = self.query.get("group", "default")
        with STATE_LOCK:
            g = dict(CONCURRENCY.get(group, {"active": 0, "max": 0, "total": 0}))
        self.send_json(200, g)

    def r_ws_log(self, args, body):
        ws_id = self.query.get("id", "")
        with STATE_LOCK:
            entry = dict(WS_LOG.get(ws_id, {}))
        self.send_json(200, entry)

    # -- WebSocket -----------------------------------------------------------------------------

    def handle_websocket(self, parts):
        route = parts[1] if len(parts) > 1 else ""
        key = self.headers.get("Sec-WebSocket-Key", "")
        ws_id = self.query.get("id", "")
        self.close_connection = True

        if route == "bad-upgrade":
            # 101 with a Sec-WebSocket-Accept that cannot match the client's key, so the client must
            # reject the handshake. Keep the socket open until the client gives up (issue #909).
            self.wfile.write(
                b"HTTP/1.1 101 Switching Protocols\r\n"
                b"Upgrade: websocket\r\n"
                b"Connection: Upgrade\r\n"
                b"Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n")
            self.wfile.flush()
            try:
                self.connection.settimeout(30)
                while self.connection.recv(4096):
                    pass
            except OSError:
                pass
            return

        if route not in ("echo", "close", "stall") or not key:
            self.send_json(404, {"error": "no such websocket route", "path": self.path})
            return

        # Hold the 101 back so a test can act while the client's connect is still in flight.
        upgrade_delay_ms = int(self.query.get("upgrade_delay_ms", "0"))
        if upgrade_delay_ms > 0:
            time.sleep(upgrade_delay_ms / 1000.0)

        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        deflate, server_no_ctx, client_no_ctx = parse_deflate_offer(self.headers.get("Sec-WebSocket-Extensions"))
        lines = [
            "HTTP/1.1 101 Switching Protocols",
            "Upgrade: websocket",
            "Connection: Upgrade",
            "Sec-WebSocket-Accept: " + accept,
            "X-Lhc-Ws: 1",
        ]
        protocols = self.headers.get("Sec-WebSocket-Protocol")
        if protocols:
            lines.append("Sec-WebSocket-Protocol: " + protocols.split(",")[0].strip())
        if deflate:
            ext = "permessage-deflate"
            if server_no_ctx:
                ext += "; server_no_context_takeover"
            if client_no_ctx:
                ext += "; client_no_context_takeover"
            lines.append("Sec-WebSocket-Extensions: " + ext)
        self.wfile.write(("\r\n".join(lines) + "\r\n\r\n").encode())
        self.wfile.flush()

        session = WebSocketSession(self.rfile, self.connection, deflate, server_no_ctx, client_no_ctx, ws_id, self.header_dict())
        ws_log_update(ws_id, connected=True, deflate=deflate, offered_extensions=self.headers.get("Sec-WebSocket-Extensions", ""))
        log("ws %s connected route=%s deflate=%s" % (ws_id, route, deflate))

        ping_ms = int(self.query.get("ping_ms", "0"))
        if ping_ms > 0:
            def pinger():
                while not session.closed.wait(ping_ms / 1000.0):
                    try:
                        session.send_ping()
                    except OSError:
                        return
            threading.Thread(target=pinger, daemon=True).start()

        try:
            if route == "stall":
                # Never read, so the client's sends back up in the socket buffers and stay in
                # flight; then drop the connection with a reset instead of a close handshake.
                time.sleep(int(self.query.get("after_ms", "1000")) / 1000.0)
                linger = struct.pack("HH", 1, 0) if os.name == "nt" else struct.pack("ii", 1, 0)
                self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
                self.connection.close()
                return
            if route == "close":
                code = int(parts[2]) if len(parts) > 2 else 1000
                after_ms = int(self.query.get("after_ms", "0"))
                if after_ms > 0:
                    time.sleep(after_ms / 1000.0)
                session.send_close(code, b"server close")
                # Wait for the client's close reply (read_message raises when it arrives).
                self.connection.settimeout(10)
                while True:
                    session.read_message()
            self.echo_loop(session)
        except (WebSocketClosed, OSError, ValueError) as e:
            log("ws %s ended: %r" % (ws_id, e))
        finally:
            session.closed.set()
            ws_log_update(ws_id, closed=True, stats=dict(session.stats))

    def echo_loop(self, session):
        while True:
            opcode, data = session.read_message()
            ws_log_update(session.ws_id, stats=dict(session.stats))
            if opcode == OP_TEXT and data.startswith(b"__"):
                if self.ws_command(session, data.decode("utf-8", "replace")):
                    continue
            session.send_message(opcode, data)

    def ws_command(self, session, text):
        """Control commands sent as text messages. Returns True if handled."""
        if text == "__stats__":
            stats = dict(session.stats)
            stats["request_headers"] = session.headers
            session.send_message(OP_TEXT, json.dumps(stats, sort_keys=True, separators=(",", ":")).encode())
            return True
        if text == "__ping__":
            session.send_ping()
            return True
        if text.startswith("__cmd:"):
            # __cmd:<name>:<arg>
            _, name, arg = (text.split(":", 2) + ["", ""])[:3]
            if name == "close":
                session.send_close(int(arg or "1000"), b"server close")
                return True
            if name == "binary":
                session.send_message(OP_BINARY, pattern_bytes(0, int(arg or "0")))
                return True
            if name == "text":
                session.send_message(OP_TEXT, (b"abcdefghij" * (int(arg or "0") // 10 + 1))[:int(arg or "0")])
                return True
            if name == "fragmented":
                # arg = "<fragments>,<size>": a text message of <size> bytes split into <fragments> frames
                frags, size = (arg.split(",") + ["4", "1000"])[:2]
                session.send_message(OP_TEXT, (b"0123456789" * (int(size) // 10 + 1))[:int(size)], fragments=int(frags))
                return True
        return False


ROUTES = {
    "echo": Handler.r_echo,
    "status": Handler.r_status,
    "retry-after": Handler.r_retry_after,
    "retry-count": Handler.r_retry_count,
    "delay": Handler.r_delay,
    "bytes": Handler.r_bytes,
    "stream": Handler.r_stream,
    "gzip": Handler.r_gzip,
    "redirect": Handler.r_redirect,
    "concurrency": Handler.r_concurrency,
    "concurrency-stats": Handler.r_concurrency_stats,
    "ws-log": Handler.r_ws_log,
}


# ---------------------------------------------------------------------------------------------
# Servers
# ---------------------------------------------------------------------------------------------

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False
    request_queue_size = 128

    def __init__(self, name, ssl_context=None):
        self.name = name
        self.ssl_context = ssl_context
        super().__init__(("127.0.0.1", 0), Handler)

    def server_bind(self):
        # HTTPServer.server_bind also sets server_name from socket.getfqdn(), a reverse DNS lookup
        # that can stall for a long time on some hosts (e.g. macOS build agents) before the ready
        # line is printed. Nothing here uses server_name, so skip the lookup.
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]

    def finish_request(self, request, client_address):
        # Runs on the per-connection thread, so a slow or failing TLS handshake never blocks accept().
        if self.ssl_context is None:
            super().finish_request(request, client_address)
            return
        try:
            tls = self.ssl_context.wrap_socket(request, server_side=True)
        except (ssl.SSLError, OSError) as e:
            log("%s: TLS handshake failed from %s: %r" % (self.name, client_address, e))
            return
        try:
            super().finish_request(tls, client_address)
        finally:
            try:
                tls.close()
            except OSError:
                pass

    def handle_error(self, request, client_address):
        log("%s: unhandled error from %s: %r" % (self.name, client_address, sys.exc_info()[1]))


# ---------------------------------------------------------------------------------------------
# Throwaway TLS certificate
# ---------------------------------------------------------------------------------------------

OPENSSL_CONFIG = """\
[req]
distinguished_name = dn
x509_extensions = v3
prompt = no

[dn]
CN = 127.0.0.1

[v3]
subjectAltName = IP:127.0.0.1, DNS:localhost
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
"""


def find_openssl():
    """openssl on PATH, else the copy that ships with Git for Windows. Returns None if not found."""
    found = shutil.which("openssl")
    if found:
        return found
    if os.name != "nt":
        return None

    roots = []
    git = shutil.which("git")
    if git:
        roots.append(os.path.dirname(os.path.realpath(git)))
        try:
            exec_path = subprocess.run([git, "--exec-path"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.DEVNULL, timeout=30, universal_newlines=True).stdout.strip()
            if exec_path:
                roots.append(os.path.normpath(exec_path))
        except (OSError, subprocess.SubprocessError):
            pass
    for env in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)", "LOCALAPPDATA"):
        base = os.environ.get(env)
        if base:
            roots.append(os.path.join(base, "Git"))
            roots.append(os.path.join(base, "Programs", "Git"))

    # Walk up from each git location (e.g. <git>\cmd or <git>\mingw64\libexec\git-core) to the
    # install root, which has usr\bin\openssl.exe (and usually mingw64\bin\openssl.exe).
    for root in roots:
        directory = root
        for _ in range(5):
            for relative in (("usr", "bin", "openssl.exe"), ("mingw64", "bin", "openssl.exe")):
                candidate = os.path.join(directory, *relative)
                if os.path.isfile(candidate):
                    return candidate
            parent = os.path.dirname(directory)
            if parent == directory:
                break
            directory = parent
    return None


def create_tls_context(openssl):
    """Returns (ssl_context, None) or (None, error message)."""
    if not openssl:
        return None, "openssl was not found on PATH or in a Git for Windows install"
    workdir = tempfile.mkdtemp(prefix="lhc_e2e_tls_")
    try:
        config = os.path.join(workdir, "openssl.cnf")
        cert = os.path.join(workdir, "cert.pem")
        key = os.path.join(workdir, "key.pem")
        with open(config, "w") as f:
            f.write(OPENSSL_CONFIG)
        env = dict(os.environ)
        env["OPENSSL_CONF"] = config
        # Git for Windows' usr\bin\openssl.exe is an MSYS program; keep it from rewriting arguments.
        env["MSYS_NO_PATHCONV"] = "1"
        env["MSYS2_ARG_CONV_EXCL"] = "*"
        command = [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "2",
                   "-config", config, "-keyout", key, "-out", cert]
        try:
            # Well under the test exe's 30 s wait for the ready line, so a hung openssl costs HTTPS
            # coverage (reported via https_error) rather than the whole run.
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    env=env, timeout=15, universal_newlines=True)
        except (OSError, subprocess.SubprocessError) as e:
            return None, "running %s failed: %r" % (openssl, e)
        if result.returncode != 0 or not os.path.isfile(cert) or not os.path.isfile(key):
            return None, "%s req exited with %d: %s" % (openssl, result.returncode, result.stdout.strip()[-500:])
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        try:
            ctx.load_cert_chain(cert, key)
        except (ssl.SSLError, OSError) as e:
            return None, "could not load the generated certificate: %r" % (e,)
        log("generated a throwaway TLS certificate with %s" % openssl)
        return ctx, None
    finally:
        # The context holds the key in memory; don't leave it on disk.
        shutil.rmtree(workdir, ignore_errors=True)


def main():
    global VERBOSE
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=float, default=0, help="exit after this many seconds (0 = run until killed)")
    parser.add_argument("--openssl", default=None, help="openssl executable to use (default: search PATH, then Git for Windows)")
    parser.add_argument("--no-https", action="store_true", help="don't serve HTTPS (simulates openssl being unavailable)")
    parser.add_argument("--verbose", action="store_true", help="log requests to stderr")
    parser.add_argument("--parent-pid", type=int, default=0,
                        help="POSIX only: exit when this process (our parent) exits")
    args = parser.parse_args()
    VERBOSE = args.verbose

    # The test exe waits 30 s for the ready line and then reports only a timeout. If startup stalls,
    # dump every thread's stack to stderr first so the log shows where.
    faulthandler.dump_traceback_later(25, exit=False)

    if args.no_https:
        ctx, https_error = None, "HTTPS disabled with --no-https"
    else:
        ctx, https_error = create_tls_context(args.openssl or find_openssl())
    if https_error:
        sys.stderr.write("[lhc_test_server] HTTPS unavailable: %s\n" % https_error)
        sys.stderr.flush()

    servers = {"http": Server("http"), "http2": Server("http2")}
    if ctx is not None:
        servers["https"] = Server("https", ctx)
    for name, srv in servers.items():
        PORTS[name] = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, name=name, daemon=True).start()

    ready = {
        "http": PORTS["http"],
        "http2": PORTS["http2"],
        "https": PORTS["https"] if ctx is not None else None,
        "https_error": https_error,
        "pid": os.getpid(),
    }
    sys.stdout.write(json.dumps(ready) + "\n")
    sys.stdout.flush()
    faulthandler.cancel_dump_traceback_later()

    # Never read stdin: when launched in the background it may be closed.
    stop = threading.Event()
    if args.parent_pid:
        threading.Thread(target=watch_parent, args=(args.parent_pid, stop), daemon=True).start()
    try:
        stop.wait(args.timeout if args.timeout > 0 else None)
    except KeyboardInterrupt:
        pass
    for srv in servers.values():
        srv.shutdown()


def watch_parent(parent_pid, stop):
    """POSIX: exit when the test runner that started us goes away (it may be killed without
    cleaning up; on Windows it uses a kill-on-close job object instead)."""
    while not stop.wait(0.5):
        if os.getppid() != parent_pid:
            log("parent %d exited; shutting down" % parent_pid)
            os._exit(0)


if __name__ == "__main__":
    main()
