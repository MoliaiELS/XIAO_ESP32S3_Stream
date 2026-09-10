#!/usr/bin/env python3
"""Minimal standard-library client for the XIAO sensor/command TCP channel."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import struct
import time
import wave
import zlib

MAGIC = 0x4F414958
VERSION = 2
TYPE_IMU = 0x10
TYPE_TOF = 0x11
TYPE_AUDIO_PCM = 0x12
TYPE_STATUS = 0x13
TYPE_COMMAND = 0x20
TYPE_COMMAND_ACK = 0x21
FLAG_STREAM_START = 1 << 2
FLAG_STREAM_END = 1 << 3
HEADER = struct.Struct("<IBBHIQII")
AUDIO_PREFIX = struct.Struct("<IIIHBB")


class AudioSink:
    def __init__(self, output_dir: str) -> None:
        self.output_dir = Path(output_dir)
        self.streams: dict[int, tuple[wave.Wave_write, Path]] = {}

    def consume(self, flags: int, payload: bytes) -> str:
        if len(payload) < AUDIO_PREFIX.size:
            raise ValueError("audio payload is shorter than its prefix")
        stream_id, chunk_index, rate, count, channels, bits = AUDIO_PREFIX.unpack_from(payload)
        pcm = payload[AUDIO_PREFIX.size :]
        expected = count * channels * (bits // 8)
        if bits % 8 or len(pcm) != expected:
            raise ValueError(f"invalid PCM size for audio stream {stream_id}")

        if flags & FLAG_STREAM_START or stream_id not in self.streams:
            self._close_stream(stream_id)
            self.output_dir.mkdir(parents=True, exist_ok=True)
            stamp = time.strftime("%Y%m%d-%H%M%S")
            path = self.output_dir / f"voice-{stamp}-{stream_id}.wav"
            writer = wave.open(str(path), "wb")
            writer.setnchannels(channels)
            writer.setsampwidth(bits // 8)
            writer.setframerate(rate)
            self.streams[stream_id] = (writer, path)

        writer, path = self.streams[stream_id]
        if pcm:
            writer.writeframesraw(pcm)

        ended = bool(flags & FLAG_STREAM_END)
        if ended:
            self._close_stream(stream_id)
        state = "end" if ended else "data"
        return (
            f"audio stream={stream_id} chunk={chunk_index} {state} "
            f"rate={rate}Hz samples={count} file={path}"
        )

    def _close_stream(self, stream_id: int) -> None:
        entry = self.streams.pop(stream_id, None)
        if entry:
            entry[0].close()

    def close(self) -> None:
        for stream_id in list(self.streams):
            self._close_stream(stream_id)


def recv_exact(sock: socket.socket, count: int) -> bytes:
    chunks: list[bytes] = []
    remaining = count
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("device closed the TCP connection")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def send_command(sock: socket.socket, command: dict, sequence: int = 0) -> None:
    payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
    header = HEADER.pack(
        MAGIC,
        VERSION,
        TYPE_COMMAND,
        0,
        sequence,
        time.monotonic_ns() // 1000,
        len(payload),
        zlib.crc32(payload),
    )
    sock.sendall(header + payload)


def describe(message_type: int, payload: bytes) -> str:
    if message_type == TYPE_STATUS and len(payload) == 12:
        uptime, heap, rssi, flags, _ = struct.unpack("<IIbBH", payload)
        return f"status uptime={uptime}ms heap={heap}B rssi={rssi}dBm sensors=0b{flags:03b}"
    if message_type == TYPE_IMU and len(payload) == 44:
        values = struct.unpack("<11f", payload)
        return f"imu accel={values[0:3]} gyro={values[3:6]} quat={values[6:10]} temp={values[10]:.1f}C"
    if message_type == TYPE_TOF and len(payload) == 8:
        distance, signal, ambient, status, _ = struct.unpack("<HHHBB", payload)
        return f"tof distance={distance}mm signal={signal} ambient={ambient} status={status}"
    if message_type in (TYPE_COMMAND, TYPE_COMMAND_ACK):
        return payload.decode("utf-8", errors="replace")
    return f"type=0x{message_type:02x} payload={len(payload)} bytes"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", help="XIAO IP address")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--audio-dir", default="recordings", help="directory for received WAV files")
    parser.add_argument(
        "--command",
        help='JSON command sent once after connecting, e.g. \'{"id":"1","name":"ping","args":{}}\'',
    )
    args = parser.parse_args()

    audio_sink = AudioSink(args.audio_dir)
    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.settimeout(None)
        print(f"connected to {args.host}:{args.port}")
        if args.command:
            value = json.loads(args.command)
            if not isinstance(value, dict):
                raise ValueError("--command must be a JSON object")
            send_command(sock, value)

        try:
            while True:
                raw_header = recv_exact(sock, HEADER.size)
                magic, version, kind, flags, sequence, timestamp, length, crc = HEADER.unpack(raw_header)
                if magic != MAGIC or version != VERSION:
                    raise ValueError("invalid protocol header")
                payload = recv_exact(sock, length)
                if zlib.crc32(payload) != crc:
                    raise ValueError(f"CRC mismatch on frame {sequence}")
                detail = (
                    audio_sink.consume(flags, payload)
                    if kind == TYPE_AUDIO_PCM
                    else describe(kind, payload)
                )
                print(f"#{sequence} t={timestamp}us flags=0x{flags:04x} {detail}")
        except KeyboardInterrupt:
            print("\nstopped")
        finally:
            audio_sink.close()


if __name__ == "__main__":
    main()
