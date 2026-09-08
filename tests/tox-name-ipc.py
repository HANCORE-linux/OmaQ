#!/usr/bin/env python3
"""Real helper IPC with private homes and link-time native name/save faults."""
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import time


class Helper:
    def __init__(self, binary, root):
        env = os.environ.copy()
        env.update(HOME=str(root), OMAQ_NAME_TEST_ROOT=str(root))
        for variable, name in (('OMAQ_HOME', 'data'), ('OMAQ_STATE', 'state'),
                               ('OMAQ_DOWNLOAD_DIR', 'downloads'),
                               ('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'),
                               ('XDG_DATA_HOME', 'share'), ('XDG_RUNTIME_DIR', 'runtime')):
            path = root / name
            path.mkdir(mode=0o700)
            env[variable] = str(path)
        self.process = subprocess.Popen([str(binary)], env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ, 'stdout')
        self.selector.register(self.process.stderr, selectors.EVENT_READ, 'stderr')
        self.events = []
        self.buffer = b''
        self.bytes = 0
        self.instance = ''
        self.serial = 0

    def send(self, operation):
        self.process.stdin.write(json.dumps(operation).encode() + b'\n')
        self.process.stdin.flush()

    def wait(self, predicate, start=0):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            for event in self.events[start:]:
                if predicate(event):
                    return event
            for key, _ in self.selector.select(0.05):
                data = os.read(key.fileobj.fileno(), 65536)
                if not data:
                    self.selector.unregister(key.fileobj)
                    continue
                self.bytes += len(data)
                if self.bytes > 1048576:
                    raise RuntimeError('nickname IPC output exceeded byte bound')
                if key.data != 'stdout':
                    continue
                self.buffer += data
                while b'\n' in self.buffer:
                    line, self.buffer = self.buffer.split(b'\n', 1)
                    if len(line) > 16384 or len(self.events) >= 1024:
                        raise RuntimeError('nickname IPC event bound exceeded')
                    event = json.loads(line)
                    if not isinstance(event, dict):
                        raise RuntimeError('nickname IPC event is not an object')
                    self.events.append(event)
                if len(self.buffer) > 16384:
                    raise RuntimeError('nickname IPC line bound exceeded')
            if not self.selector.get_map():
                raise RuntimeError(f'nickname helper exited early ({self.process.poll()})')
        raise RuntimeError('nickname IPC event timed out')

    def boundary(self):
        self.serial += 1
        request = f'boundary-{self.serial}'
        self.send({'op': 'helper.probe', 'id': self.instance, 'request': request})
        self.wait(lambda e: e.get('event') == 'helper.probe' and
                  e.get('request') == request and e.get('instance') == self.instance)

    def rename(self, name, request, result, calls, terminal=False):
        self.boundary()
        start = len(self.events)
        self.send({'op': 'nickname.set', 'nickname': name, 'id': request})
        self.wait(lambda e: e.get('request') == request and e.get('event') in {'error', 'nickname'}, start)
        if terminal:
            self.wait(lambda e: e.get('event') == 'identity.primary' and e.get('uncertain') is True, start)
            assert self.process.wait(timeout=3) == 0, 'uncertain helper did not stop cleanly'
        else:
            self.boundary()
        events = self.events[start:]
        audit = [e for e in events if e.get('event') == 'test.nickname.audit']
        assert len(audit) == 1 and audit[0]['result'] == result, 'wrong native result'
        assert audit[0]['groupCalls'] == calls and audit[0]['storedMatches'], 'wrong native/published state'
        correlated = [e for e in events if e.get('request') == request]
        names = [e for e in events if e.get('event') == 'nickname']
        if result == 0:
            assert len(correlated) == 1 and correlated[0]['event'] == 'nickname', 'missing success acknowledgement'
            assert correlated[0]['value'] == name
        else:
            code = 'nickname_group_sync_failed' if result == 1 else 'nickname_update_failed'
            assert len(correlated) == 1 and correlated[0]['event'] == 'error' and correlated[0]['code'] == code
            if result == 1:
                assert len(names) == 1 and names[0]['value'] == name and not names[0].get('request')
                assert events.index(names[0]) < events.index(correlated[0]), 'wrong partial-result ordering'
            else:
                assert not names, 'failed primary save projected nickname success'

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.selector.close()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='omaq-name-ipc-', dir='/tmp') as directory:
        helper = Helper(binary, Path(directory))
        try:
            helper.send({'op': 'status', 'id': 'initial'})
            snapshot = helper.wait(lambda e: e.get('event') == 'snapshot' and e.get('request') == 'initial')
            helper.instance = snapshot['instance']
            helper.rename('Before', 'initial-name', 0, 0)
            for index in range(2):
                start = len(helper.events)
                helper.send({'op': 'group.create', 'title': f'Name fixture {index}'})
                helper.wait(lambda e: e.get('event') == 'group.changed' and e.get('action') == 'create', start)
            helper.rename('First save fault', 'first-save', -2, 0)
            helper.rename('Partial name', 'partial-send', 1, 2)
            helper.rename('Partial name', 'send-retry', 0, 2)
            helper.rename('Second save fault', 'second-save', 1, 2)
            helper.rename('Second save fault', 'save-retry', 0, 2)
            helper.rename('Primary uncertain', 'uncertain-save', -2, 0, terminal=True)
            marker = Path(directory) / 'state' / 'identity-primary-uncertain'
            assert marker.stat().st_size < 128
            assert marker.read_bytes() == b'primary durability uncertain\n', 'persistent identity guard missing'
        finally:
            helper.close()
    print('tox-name-ipc: ok (native faults, published state, correlation, ordering, retries, identity guard)')


if __name__ == '__main__':
    main()
