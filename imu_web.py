#!/usr/bin/env python3
"""Forward XIAO TCP or USB serial quaternion data to a localhost viewer."""
import argparse
import json
import math
from pathlib import Path
import re
import socket
import sys
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import zlib
from laptop_client import HEADER, MAGIC, VERSION, TYPE_IMU, TYPE_AUDIO_PCM, AudioSink, recv_exact


def load_serial():
    try:
        import serial
        return serial
    except ModuleNotFoundError:
        base = Path(__file__).resolve().parent
        for folder in (base / 'serial-deps', base / 'build' / 'serial-deps'):
            if (folder / 'serial' / '__init__.py').is_file():
                sys.path.insert(0, str(folder))
                import serial
                return serial
        raise ImportError('Serial mode requires pyserial')


def normalize(q):
    if len(q) != 4 or not all(math.isfinite(v) for v in q):
        raise ValueError('invalid quaternion')
    norm = math.sqrt(sum(v*v for v in q))
    if not 0.5 < norm < 1.5:
        raise ValueError('invalid quaternion norm')
    return [v/norm for v in q]


def serial_quaternion(line):
    match = re.search(r'Q\(wxyz\)[:=]\s*([^|]+)', line)
    if match:
        return normalize([float(v) for v in match[1].split()])
    match = re.search(r'q_wxyz=([^\s]+)', line)
    if match:
        return normalize([float(v) for v in match[1].split(',')])
    return None


class State:
    def __init__(self, source):
        self.lock = threading.Lock()
        self.source = source
        self.connected = False
        self.error = 'Waiting for connection'
        self.q = None
        self.received = None
        self.count = 0

    def status(self, connected, error=''):
        with self.lock:
            self.connected, self.error = connected, error

    def pose(self, q):
        q = normalize(q)
        with self.lock:
            self.q, self.received = q, time.monotonic()
            self.count += 1

    def snapshot(self):
        with self.lock:
            age = None if self.received is None else time.monotonic()-self.received
            return dict(source=self.source, connected=self.connected, error=self.error,
                        q=self.q, age=age, count=self.count,
                        fresh=self.connected and age is not None and age < 0.7)


def decode_imu(payload):
    if len(payload) == 28:  # GitHub integrated firmware, protocol v3
        return normalize(struct.unpack('<7f', payload)[:4])
    if len(payload) == 44:  # Local legacy protocol v2
        return normalize(struct.unpack('<11f', payload)[6:10])
    raise ValueError('invalid IMU payload length')


def read_frame(sock):
    magic, version, kind, flags, seq, stamp, length, crc = HEADER.unpack(recv_exact(sock, HEADER.size))
    if magic != MAGIC or version not in (2, 3) or length > 65536:
        raise ValueError('invalid TCP header / oversized payload')
    payload = recv_exact(sock, length)
    if zlib.crc32(payload) != crc:
        raise ValueError('TCP CRC mismatch')
    return kind, flags, payload


def receive(args, state, stop):
    while not stop.is_set():
        try:
            if args.host:
                sink = AudioSink(args.audio_dir)
                try:
                    with socket.create_connection((args.host, args.device_port), timeout=6) as sock:
                        sock.settimeout(10)
                        state.status(True)
                        while not stop.is_set():
                            kind, flags, payload = read_frame(sock)
                            if kind == TYPE_IMU:
                                state.pose(decode_imu(payload))
                            elif kind == TYPE_AUDIO_PCM:
                                sink.consume(flags, payload)
                finally:
                    sink.close()
            else:
                serial = load_serial()
                with serial.Serial(args.serial, args.baud, timeout=0.2) as port:
                    state.status(True)
                    pending = b''
                    while not stop.is_set():
                        pending += port.read(max(1, min(port.in_waiting, 4096)))
                        while b'\n' in pending:
                            line, pending = pending.split(b'\n', 1)
                            try:
                                q = serial_quaternion(line.decode('utf-8', errors='replace'))
                                if q is not None:
                                    state.pose(q)
                            except ValueError:
                                pass
                        if len(pending) > 8192:
                            pending = b''
        except (OSError, ValueError, ConnectionError) as exc:
            state.status(False, str(exc))
            print('Source disconnected:', exc, flush=True)
        stop.wait(2)


def handler(state):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/api/state':
                data = json.dumps(state.snapshot(), ensure_ascii=False, allow_nan=False).encode()
                content_type = 'application/json; charset=utf-8'
            elif self.path in ('/', '/index.html'):
                data = Path(__file__).with_name('imu_viewer.html').read_bytes()
                content_type = 'text/html; charset=utf-8'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *_):
            pass
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--host', help='XIAO IP, root firmware TCP stream')
    source.add_argument('--serial', help='USB port, e.g. COM3, integrated firmware quaternion output')
    parser.add_argument('--device-port', type=int, default=9000)
    parser.add_argument('--port', type=int, default=8765, help='local web port')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--audio-dir', default='recordings')
    args = parser.parse_args()
    if args.serial:
        try:
            load_serial()
        except ImportError:
            parser.error('Serial mode requires: python -m pip install pyserial')
    state = State(args.host or args.serial)
    stop = threading.Event()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), handler(state))
    threading.Thread(target=receive, args=(args, state, stop), daemon=True).start()
    print(f'IMU viewer: http://127.0.0.1:{args.port} (Ctrl+C to stop)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()


if __name__ == '__main__':
    main()
