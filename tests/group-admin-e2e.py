#!/usr/bin/env python3
"""Three-helper native regression for admin invite, accept, and member removal."""

from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable


class Peer:
    def __init__(self, binary: Path, root: Path, name: str) -> None:
        self.name = name
        self.home = root / f"home-{name}"
        self.state = root / f"state-{name}"
        self.download = root / f"download-{name}"
        for path in (self.home, self.state, self.download):
            path.mkdir(mode=0o700)
        env = os.environ.copy()
        env.update(
            OMAQ_HOME=str(self.home),
            OMAQ_STATE=str(self.state),
            OMAQ_DOWNLOAD_DIR=str(self.download),
        )
        self.process = subprocess.Popen(
            [str(binary)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
            env=env,
        )
        self.events: list[dict] = []
        self.condition = threading.Condition()
        self.stderr: list[str] = []
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            with self.condition:
                self.events.append(event)
                self.condition.notify_all()

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            self.stderr.append(line.rstrip())

    def send(self, operation: dict) -> None:
        if self.process.poll() is not None:
            raise RuntimeError(f"{self.name}: helper exited {self.process.returncode}")
        assert self.process.stdin is not None
        self.process.stdin.write(json.dumps(operation, separators=(",", ":")) + "\n")
        self.process.stdin.flush()

    def mark(self) -> int:
        with self.condition:
            return len(self.events)

    def wait(
        self,
        predicate: Callable[[dict], bool],
        timeout: float,
        start: int = 0,
        description: str = "event",
    ) -> dict:
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for event in self.events[start:]:
                    if predicate(event):
                        return event
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    tail = self.events[-12:]
                    errors = self.stderr[-8:]
                    raise RuntimeError(
                        f"{self.name}: timed out waiting for {description}; "
                        f"events={tail!r}; stderr={errors!r}"
                    )
                self.condition.wait(min(remaining, 0.5))

    def stop(self) -> None:
        if self.process.poll() is not None:
            return
        self.process.send_signal(signal.SIGTERM)
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=2)


def event_is(name: str, **fields: object) -> Callable[[dict], bool]:
    return lambda event: event.get("event") == name and all(
        event.get(key) == value for key, value in fields.items()
    )


def start_identity(peer: Peer) -> str:
    request = f"admin-e2e-status-{peer.name}"
    peer.send({"op": "status", "id": request})
    snapshot = peer.wait(
        lambda event: event.get("event") == "snapshot"
        and event.get("request") == request
        and isinstance(event.get("addr"), str),
        15,
        description="identity snapshot",
    )
    address = str(snapshot["addr"])
    if len(address) != 76:
        raise RuntimeError(f"{peer.name}: invalid Tox address")
    return address[:64]


def pair(inviter: Peer, recipient: Peer, recipient_key: str, label: str) -> dict:
    invite_start = inviter.mark()
    request_start = inviter.mark()
    inviter.send(
        {
            "op": "invite.create",
            "kind": "direct",
            "ttlSec": 86400,
            "request": f"{label}-create",
        }
    )
    invitation = inviter.wait(
        lambda event: event.get("event") == "invite"
        and isinstance(event.get("url"), str)
        and "k=direct" in event["url"],
        10,
        invite_start,
        "direct invitation",
    )
    recipient.send(
        {
            "op": "invite.redeem",
            "payload": invitation["url"],
            "request": f"{label}-redeem",
        }
    )
    inviter.wait(event_is("request", kind="direct"), 100, request_start, "direct request")
    inviter.send({"op": "contact.decide", "id": label, "accept": True})
    friend = inviter.wait(
        lambda event: event.get("event") == "friend.info"
        and event.get("key") == recipient_key
        and event.get("online") is True,
        100,
        description="online stable friend projection",
    )
    recipient.wait(
        lambda event: event.get("event") == "friend.info"
        and event.get("online") is True,
        100,
        description="reciprocal online friend projection",
    )
    return friend


def main() -> int:
    repository = Path(__file__).resolve().parent.parent
    binary = repository / "helper" / "omaq"
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError("group-admin-e2e: helper/omaq is not executable")
    temporary = Path(tempfile.mkdtemp(prefix="omaq-group-admin-", dir="/tmp"))
    peers: list[Peer] = []
    try:
        owner = Peer(binary, temporary, "owner")
        admin = Peer(binary, temporary, "admin")
        member = Peer(binary, temporary, "member")
        peers.extend((owner, admin, member))
        owner_key = start_identity(owner)
        admin_key = start_identity(admin)
        member_key = start_identity(member)

        owner_admin_friend = pair(owner, admin, admin_key, "owner-admin")
        owner.send({"op": "group.create", "title": "Admin regression"})
        created = owner.wait(
            lambda event: event.get("event") == "group.changed"
            and event.get("action") == "create",
            10,
            description="group creation",
        )
        group_id = str(created.get("group", ""))
        if len(group_id) != 66:
            raise RuntimeError("owner: missing stable group id")

        admin_request_start = admin.mark()
        owner.send(
            {
                "op": "invite.create",
                "kind": "group",
                "group": group_id,
                "role": "member",
                "id": str(owner_admin_friend["id"]),
                "key": admin_key,
                "request": "gi-admin-e2e-owner-invites-admin",
                "ttlSec": 86400,
            }
        )
        admin.wait(event_is("request", kind="group"), 45, admin_request_start, "owner group invite")
        admin.send({"op": "contact.decide", "accept": True})
        joined_admin = owner.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("friendKey") == admin_key,
            100,
            description="admin member binding",
        )
        admin_member_key = str(joined_admin.get("key", ""))
        owner.send(
            {
                "op": "group.member.setRole",
                "group": group_id,
                "member": admin_member_key,
                "role": "admin",
            }
        )
        admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("self") is True
            and event.get("role") == "admin",
            30,
            description="native admin role",
        )

        admin_member_friend = pair(admin, member, member_key, "admin-member")
        member_request_start = member.mark()
        admin.send(
            {
                "op": "invite.create",
                "kind": "group",
                "group": group_id,
                "role": "member",
                "id": str(admin_member_friend["id"]),
                "key": member_key,
                "request": "gi-admin-e2e-admin-invites-member",
                "ttlSec": 86400,
            }
        )
        member.wait(
            event_is("request", kind="group"),
            45,
            member_request_start,
            "admin-initiated group invite",
        )
        member.send({"op": "contact.decide", "accept": True})
        joined_member = admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("friendKey") == member_key
            and event.get("role") == "member",
            100,
            description="accepted admin invitation",
        )
        group_member_key = str(joined_member.get("key", ""))
        if len(group_member_key) != 64:
            raise RuntimeError("admin: missing member stable key")

        owner.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("key") == group_member_key,
            30,
            description="owner projection of admin-invited member",
        )
        owner.send(
            {"op": "group.member.setRole", "group": group_id,
             "member": group_member_key, "role": "admin"}
        )
        admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("key") == group_member_key
            and event.get("role") == "admin",
            30,
            description="second admin projection",
        )
        equal_role_start = admin.mark()
        admin.send(
            {"op": "group.member.remove", "group": group_id,
             "member": group_member_key}
        )
        admin.wait(event_is("error", code="forbidden"), 15, equal_role_start,
                   "admin removal rejection")
        demotion_start = admin.mark()
        owner.send(
            {"op": "group.member.setRole", "group": group_id,
             "member": group_member_key, "role": "member"}
        )
        admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("key") == group_member_key
            and event.get("role") == "member",
            30,
            demotion_start,
            description="ordinary member restoration",
        )

        removed_start = member.mark()
        admin.send(
            {
                "op": "group.member.remove",
                "group": group_id,
                "member": group_member_key,
            }
        )
        member.wait(
            lambda event: event.get("event") == "group.changed"
            and event.get("group") == group_id
            and event.get("action") == "leave",
            45,
            removed_start,
            "admin member removal",
        )

        owner_projection = admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("group") == group_id
            and event.get("role") == "owner"
            and event.get("self") is False,
            20,
            description="owner projection",
        )
        owner_member_key = str(owner_projection.get("key", ""))
        forbidden_start = admin.mark()
        admin.send(
            {
                "op": "group.member.remove",
                "group": group_id,
                "member": owner_member_key,
            }
        )
        admin.wait(event_is("error", code="forbidden"), 15, forbidden_start, "owner removal rejection")
        admin.send({"op": "group.list", "id": "admin-e2e-final-list"})
        admin.wait(
            lambda event: event.get("event") == "group.member"
            and event.get("request") == "admin-e2e-final-list"
            and event.get("key") == owner_member_key
            and event.get("role") == "owner",
            15,
            description="owner retained after forbidden removal",
        )
        print("group-admin-e2e: ok")
        return 0
    finally:
        for peer in reversed(peers):
            peer.stop()
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001 - test harness reports full context.
        print(f"group-admin-e2e: {error}", file=sys.stderr)
        raise SystemExit(1)
