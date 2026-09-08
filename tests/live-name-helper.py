#!/usr/bin/env python3
"""Phase-3 rename regression on its two already-connected, isolated helpers.

FDs 3/4 are their command pipes. Observe unsolicited projections only: no status,
group.list, message, reopen, or restart may make a stale name look current.
"""
import json
import os
from pathlib import Path
import secrets
import sys
import time


class Events:
    def __init__(self, path):
        # Read complete framing from startup, not from the middle of a snapshot.
        # Every trial uses a new name absent from the startup projections.
        self.stream = Path(path).open('rb')
        self.buffer = b''
        self.bytes = 0
        self.group_snapshot = None

    def read(self):
        data = self.stream.read(65536)
        self.bytes += len(data)
        if self.bytes > 4 * 1024 * 1024:
            raise RuntimeError('rename observation exceeded its byte bound')
        self.buffer += data
        result = []
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            if len(line) > 16384:
                raise RuntimeError('oversized helper event')
            event = json.loads(line)
            if not isinstance(event, dict):
                raise RuntimeError('non-object helper event')
            result.append(event)
        if len(self.buffer) > 16384:
            raise RuntimeError('unterminated helper event')
        return result

    def matching_group_snapshot(self, event, groups, name, own):
        kind = event.get('event')
        if kind == 'group.list.begin':
            if self.group_snapshot is not None:
                raise RuntimeError('overlapping group snapshots')
            self.group_snapshot = {'begin': event, 'groups': {}, 'members': []}
        elif kind in {'group.info', 'group.member', 'group.list.end'}:
            snapshot = self.group_snapshot
            if snapshot is None:
                raise RuntimeError('group snapshot has no beginning')
            begin = snapshot['begin']
            for field in ('instance', 'generation', 'request'):
                if event.get(field) != begin.get(field):
                    raise RuntimeError('group snapshot correlation mismatch')
            if kind == 'group.info':
                group = event['group']
                if group in snapshot['groups']:
                    raise RuntimeError('duplicate group')
                snapshot['groups'][group] = event['members']
            elif kind == 'group.member':
                snapshot['members'].append(event)
            else:
                self.group_snapshot = None
                members = snapshot['members']
                if event['groups'] != begin['groups'] or event['members'] != begin['members'] or \
                        len(snapshot['groups']) != event['groups'] or len(members) != event['members']:
                    raise RuntimeError('incomplete group snapshot')
                keys = set()
                for member in members:
                    key = (member['group'], member['key'])
                    if key in keys:
                        raise RuntimeError('duplicate group member')
                    keys.add(key)
                for group, count in snapshot['groups'].items():
                    if sum(m['group'] == group for m in members) != count:
                        raise RuntimeError('group member count mismatch')
                return all(any(m['group'] == group and m['self'] is own and m['name'] == name
                               for m in members) for group in groups)
        return False

    def close(self):
        self.stream.close()


def rename(source_path, peer_path, command_fd, name, source_groups, shared_group):
    source, peer = Events(source_path), Events(peer_path)
    try:
        request = 'name-' + secrets.token_hex(8)
        command = {'op': 'nickname.set', 'nickname': name, 'id': request}
        # Match QML's UTF-8 wire strings; helper IPC does not accept \\u escapes.
        os.write(command_fd, json.dumps(command, ensure_ascii=False).encode('utf-8') + b'\n')
        deadline = time.monotonic() + 30
        acknowledged = direct = local_groups = peer_group = False
        while time.monotonic() < deadline:
            for event in source.read():
                if event.get('request') == request:
                    if event.get('event') == 'error':
                        raise RuntimeError('nickname operation failed')
                    if event.get('event') == 'nickname':
                        acknowledged = event.get('value') == name
                local_groups |= source.matching_group_snapshot(event, source_groups, name, True)
            for event in peer.read():
                if event.get('event') == 'friend.info' and event.get('id') == '0':
                    direct |= event.get('name') == name
                peer_group |= peer.matching_group_snapshot(event, {shared_group}, name, False)
            if acknowledged and direct and local_groups and peer_group:
                return
            time.sleep(0.05)
        raise RuntimeError(f'unsolicited rename timed out: ack={acknowledged}, direct={direct}, '
                           f'local_groups={local_groups}, peer_group={peer_group}')
    finally:
        source.close()
        peer.close()


if __name__ == '__main__':
    a, b, shared, preexisting = sys.argv[1:]
    rename(a, b, 3, 'Live name A', {shared}, shared)
    rename(b, a, 4, 'Live name B', {shared, preexisting}, shared)
    rename(a, b, 3, 'Älice renamed', {shared}, shared)
    rename(b, a, 4, '123456789012345678', {shared, preexisting}, shared)
    print('live-name-helper: ok (both directions, repeat/Unicode, all existing groups, no refresh)')
