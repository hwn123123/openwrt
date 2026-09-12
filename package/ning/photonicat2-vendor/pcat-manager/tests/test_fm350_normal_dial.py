#!/usr/bin/env python3
"""Exercise the real C dial/watch functions against an emulated AT socket.
Usage: python3 test_fm350_normal_dial.py /path/to/patched/src/fm350-mm.c
No modem, network access, or device configuration is needed.
"""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

SOURCE = Path(sys.argv.pop(1)).resolve()
HARNESS = r'''
#define main fm350_program_main
#define sleep test_sleep
#define system test_system
#include "dialer.c"
#undef main
#undef sleep
#undef system
unsigned int test_sleep(unsigned int seconds) { (void)seconds; return 0; }
int test_system(const char *cmd) { fprintf(stderr, "HOST_CMD:%s\n", cmd); return 0; }
int main(int argc, char **argv) {
    if(argc != 4) return 99;
    int fd = atoi(argv[1]);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    g_modem_pref = atoi(argv[3]);
    if(!strcmp(argv[2], "dial")) {
        int result = at_dial(fd, 0, "test-apn");
        printf("RESULT:%d\n", result);
    } else {
        watch_loop(fd, "test0", 0);
        puts("WATCH_RETURNED");
    }
    return 0;
}
'''


class NormalDial(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fm350-normal-test-")
        cls.root = Path(cls.temp.name)
        # Make preference reads independent of the machine running the test.
        source = SOURCE.read_text().replace(
            '"/etc/pcat-modem-pref.json"', '"' + str(cls.root / 'pref.json') + '"')
        (cls.root / 'dialer.c').write_text(source)
        (cls.root / 'pref.json').write_text('{"modem_tech_pref":"AUTO"}')
        (cls.root / 'harness.c').write_text(HARNESS)
        cls.binary = cls.root / 'harness'
        subprocess.run(['gcc', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-Wno-format-truncation', str(cls.root / 'harness.c'),
                        '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_modem(self, mode, respond, pref=0):
        host, modem = socket.socketpair()
        commands, errors = [], []

        def emulate():
            pending = b''
            try:
                while True:
                    data = modem.recv(8192)
                    if not data:
                        return
                    pending += data
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        cmd = line.decode().strip()
                        if cmd:
                            commands.append(cmd)
                            response = respond(cmd)
                            modem.sendall(('\r\n' + response + '\r\n').encode())
            except Exception as exc:
                errors.append(exc)
            finally:
                modem.close()

        thread = threading.Thread(target=emulate, daemon=True)
        thread.start()
        proc = subprocess.Popen([str(self.binary), str(host.fileno()), mode, str(pref)],
                                pass_fds=[host.fileno()], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        host.close()
        try:
            stdout, stderr = proc.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            self.fail('Dialer stalled; commands: ' + repr(commands[-15:]))
        thread.join(timeout=1)
        self.assertFalse(errors, repr(errors))
        self.assertEqual(proc.returncode, 0, stderr)
        return commands, stdout, stderr

    @staticmethod
    def registered(cmd):
        if cmd in ('AT+CEREG?', 'AT+CGREG?'):
            return cmd[2:-1] + ': 2,1\r\nOK'
        if cmd == 'AT+COPS?':
            return '+COPS: 0,2,"46000",11\r\nOK'
        return 'OK'

    def test_success_configured_rat_without_probe(self):
        for pref in (0, 1, 2):
            with self.subTest(pref=pref):
                commands, out, err = self.run_modem('dial', self.registered, pref)
                self.assertIn('RESULT:0', out)
                self.assertFalse(any(c.startswith(('AT+GTACT=', 'AT+CFUN='))
                                     for c in commands), commands)
                self.assertNotIn('HOST_CMD:', err)

    def test_activation_failure_never_switches_rat(self):
        def respond(cmd):
            if cmd.startswith('AT+CGACT=1,'):
                return '+CME ERROR: 5847'
            return self.registered(cmd)
        for pref in (0, 1, 2):
            with self.subTest(pref=pref):
                commands, out, err = self.run_modem('dial', respond, pref)
                self.assertIn('RESULT:-1', out)
                self.assertFalse(any(c.startswith(('AT+GTACT=', 'AT+CFUN='))
                                     for c in commands), commands)
                self.assertEqual(commands.count('AT+CGACT=1,0'), 3)
                self.assertFalse(any(c.startswith(('AT+CGACT=1,1',
                                                   'AT+CGACT=1,3'))
                                     for c in commands), commands)
                self.assertNotIn('HOST_CMD:', err)

    def test_cold_boot_5702_cycles_radio_once_and_retries(self):
        activations = 0

        def respond(cmd):
            nonlocal activations
            if cmd == 'AT+CGACT=1,0':
                activations += 1
                if activations == 1:
                    return '+CME ERROR: 5702'
            return self.registered(cmd)

        commands, out, err = self.run_modem('dial', respond)
        self.assertIn('RESULT:0', out)
        self.assertEqual(commands.count('AT+CGACT=1,0'), 2)
        self.assertEqual(commands.count('AT+CFUN=0'), 1)
        self.assertEqual(commands.count('AT+CFUN=1'), 1)
        self.assertLess(commands.index('AT+CFUN=0'), commands.index('AT+CFUN=1'))
        self.assertNotIn('AT+GTACT=2', commands)
        self.assertIn('cold-boot PDP state detected', err)
        self.assertNotIn('HOST_CMD:', err)

    def test_many_read_errors_do_not_reset_active_bearer(self):
        polls = 0
        def respond(cmd):
            nonlocal polls
            if cmd == 'AT+CGPADDR=0':
                polls += 1
                return '+CME ERROR: 0'
            if cmd == 'AT+CGACT?':
                state = 1 if polls <= 15 else 0
                return f'+CGACT: 0,{state}\r\n+CGACT: 1,0\r\nOK'
            return 'ERROR'
        commands, out, err = self.run_modem('watch', respond)
        self.assertEqual(polls, 16)  # No twelve-miss reset, no other-context reset.
        self.assertIn('WATCH_RETURNED', out)
        self.assertIn('PDP context 0 is inactive', err)
        self.assertNotIn('HOST_CMD:', err)
        self.assertFalse(any(c.startswith(('AT+GTACT=', 'AT+CFUN=')) for c in commands))

    def test_confirmed_inactive_reconnects_without_miss_delay(self):
        def respond(cmd):
            if cmd == 'AT+CGACT?':
                return '+CGACT: 0,0\r\nOK'
            return 'ERROR'
        commands, out, err = self.run_modem('watch', respond)
        self.assertEqual(commands.count('AT+CGPADDR=0'), 1)
        self.assertIn('WATCH_RETURNED', out)
        self.assertNotIn('HOST_CMD:', err)

    def test_unknown_state_is_not_a_disconnect(self):
        polls = 0
        def respond(cmd):
            nonlocal polls
            if cmd == 'AT+CGPADDR=0':
                polls += 1
                return 'ERROR'
            if cmd == 'AT+CGACT?':
                if polls == 1:
                    return 'ERROR'
                if polls == 2:
                    return '+CGACT: 1,0\r\nOK'  # A different context.
                if polls == 3:
                    return '+CGACT: 0\r\nOK'  # Missing state.
                return '+CGACT: 0,0\r\nOK'
            return 'ERROR'
        _, out, _ = self.run_modem('watch', respond)
        self.assertEqual(polls, 4)
        self.assertIn('WATCH_RETURNED', out)

    def test_ip_is_configured_without_waiting_for_internet(self):
        polls = 0
        def respond(cmd):
            nonlocal polls
            if cmd == 'AT+CGPADDR=0':
                polls += 1
                if polls <= 4:
                    return '+CGPADDR: 0,"10.1.2.3"\r\nOK'
                return 'ERROR'
            if cmd == 'AT+GTDNS=0':
                return '+GTDNS: 0,"223.5.5.5","119.29.29.29"\r\nOK'
            if cmd == 'AT+CGACT?':
                return '+CGACT: 0,0\r\nOK'
            return 'ERROR'
        _, out, err = self.run_modem('watch', respond)
        self.assertIn('WATCH_RETURNED', out)
        self.assertEqual(err.count('HOST_CMD:ip addr add'), 1)
        self.assertEqual(err.count('HOST_CMD:ip route add default'), 1)
        self.assertNotIn('ping ', err)
        self.assertNotIn('curl ', err)
        self.assertNotIn('wget ', err)
        self.assertEqual(polls, 5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
