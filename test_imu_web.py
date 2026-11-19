import json, math, socket, struct, threading, unittest, urllib.request, zlib
from http.server import ThreadingHTTPServer
import imu_web as w

class Tests(unittest.TestCase):

    def test_serial(self):
        self.assertEqual(w.serial_quaternion('Q(wxyz): 1 0 0 0 | age=0ms'), [1, 0, 0, 0])
        self.assertEqual(w.serial_quaternion('IMU ms=5 q_wxyz=1,0,0,0 rpy_deg=0,0,0'), [1, 0, 0, 0])
        self.assertEqual(w.serial_quaternion('[IMU] Q(wxyz)=1 0 0 0 | RPY=0 0 0'), [1, 0, 0, 0])
        self.assertIsNone(w.serial_quaternion('[IMU] bytes=100'))
        for line in ['Q(wxyz): nan 0 0 0', 'Q(wxyz): 0 0 0 0']:
            with self.assertRaises(ValueError):
                w.serial_quaternion(line)

    def test_v3(self):
        payload = struct.pack('<7f', 1, 0, 0, 0, 0, 0, 0)
        self.assertEqual(w.decode_imu(payload), [1, 0, 0, 0])
        a, b = socket.socketpair()
        with a, b:
            a.sendall(w.HEADER.pack(w.MAGIC, 3, w.TYPE_IMU, 0, 1, 100, len(payload), zlib.crc32(payload)) + payload)
            self.assertEqual(w.decode_imu(w.read_frame(b)[2]), [1, 0, 0, 0])

    def test_tcp(self):
        payload = struct.pack('<11f', *[math.nan] * 6 + [1, 0, 0, 0, math.nan])
        frame = w.HEADER.pack(w.MAGIC, 2, w.TYPE_IMU, 0, 1, 100, len(payload), zlib.crc32(payload)) + payload
        a, b = socket.socketpair()

        def send():
            with a:
                for v in frame:
                    a.sendall(bytes([v]))
        t = threading.Thread(target=send)
        t.start()
        with b:
            self.assertEqual(w.read_frame(b)[2], payload)
        t.join()
        a, b = socket.socketpair()
        with a, b:
            a.sendall(frame[:-1] + bytes([frame[-1] ^ 1]))
            with self.assertRaisesRegex(ValueError, 'CRC'):
                w.read_frame(b)

    def test_http_stale(self):
        s = w.State('test')
        s.status(True)
        s.pose([1, 0, 0, 0])
        self.assertTrue(s.snapshot()['fresh'])
        s.received -= 1
        self.assertFalse(s.snapshot()['fresh'])
        s.pose([1, 0, 0, 0])
        s.status(False)
        self.assertFalse(s.snapshot()['fresh'])
        server = ThreadingHTTPServer(('127.0.0.1', 0), w.handler(s))
        t = threading.Thread(target=server.serve_forever)
        t.start()
        try:
            base = 'http://127.0.0.1:' + str(server.server_port)
            with urllib.request.urlopen(base + '/api/state') as r:
                self.assertEqual(json.load(r)['q'], [1, 0, 0, 0])
            with urllib.request.urlopen(base) as r:
                self.assertIn(b'<canvas', r.read())
        finally:
            server.shutdown()
            server.server_close()
            t.join()
if __name__ == '__main__':
    unittest.main()
