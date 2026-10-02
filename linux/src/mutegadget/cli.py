# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""``mutegadget`` command line."""

from __future__ import annotations

import argparse
import logging
import os
import sys

from mutegadget import __version__, config, identity, mute_api

log = logging.getLogger("mutegadget")


def cmd_pair(args: argparse.Namespace) -> int:
    if config.load_json(config.PAIRING_FILE) and not args.force:
        print("Already paired. Run `mutegadget unpair` first, or pass --force.", file=sys.stderr)
        return 1
    server = args.server.rstrip("/")
    if not server.startswith(("https://", "http://")):
        print("--server must be an http:// or https:// URL.", file=sys.stderr)
        return 1
    if server.startswith("http://"):
        log.warning("plain http: the device token travels unencrypted until the Noise handshake")
    vms, status = mute_api.fetch_vms_with_status(args.token, server)
    if not vms:
        print(f"The server refused the token (HTTP {status}).", file=sys.stderr)
        return 1
    config.save_json(config.PAIRING_FILE, {"server": server, "access_token": args.token})
    print(f"Paired with {server}.")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    from mutegadget.executor import Account, Executor
    from mutegadget.service import run_service

    if os.geteuid() == 0:
        if not args.run_as:
            print("Pass --run-as ACCOUNT (the account whose permissions commands get).",
                  file=sys.stderr)
            return 1
        try:
            account = Account.lookup(args.run_as)
        except KeyError:
            print(f"Account {args.run_as!r} does not exist. Create it, or pass --run-as.",
                  file=sys.stderr)
            return 1
    else:
        account = Account.current()
    log.info("mutegadget %s: commands run as %s", __version__, account.name)
    run_service(identity.load_or_create(), Executor(account))
    return 0


def cmd_send_user_msg(args: argparse.Namespace) -> int:
    import json
    import socket

    message = sys.stdin.read() if args.message == ["-"] else " ".join(args.message)
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(90)
            sock.connect(str(config.socket_path()))
            request = {"message": message}
            if args.session_id:
                request["session_id"] = args.session_id
            sock.sendall(json.dumps(request).encode() + b"\n")
            reply = json.loads(sock.makefile("rb").readline())
    except (OSError, ValueError) as exc:
        print(f"Could not reach the mutegadget service: {exc}", file=sys.stderr)
        return 1
    if not reply.get("ok"):
        print(f"Not delivered: {reply.get('error') or reply}", file=sys.stderr)
        return 1
    response = reply.get("response")
    print(response.get("text", "") if isinstance(response, dict) else "Sent to your Mute.")
    return 0


def cmd_info(args: argparse.Namespace) -> int:
    ident = identity.load_or_create()
    pairing = config.load_json(config.PAIRING_FILE) or {}
    print(f"version:   {__version__}")
    print(f"node id:   {ident.node_id}")
    print(f"server:    {pairing.get('server') or 'not paired'}")
    return 0


def cmd_unpair(args: argparse.Namespace) -> int:
    config.delete_json(config.PAIRING_FILE)
    print("Pairing removed. The device identity is kept.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="mutegadget")
    parser.add_argument("-v", "--verbose", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)

    pair = sub.add_parser("pair", help="connect this device to a Mute server")
    pair.add_argument("--server", required=True, help="server URL, e.g. https://mute.example.com")
    pair.add_argument("--token", required=True, help="the server's device token")
    pair.add_argument("--force", action="store_true", help="pair even if already paired")
    pair.set_defaults(func=cmd_pair)

    run = sub.add_parser("run", help="stay connected to the Mute and serve its commands")
    run.add_argument(
        "--run-as",
        default=os.environ.get("MUTEGADGET_RUN_AS") or os.environ.get("SUDO_USER"),
        help="account whose permissions commands run with (default: the account that ran sudo)",
    )
    run.set_defaults(func=cmd_run)

    send = sub.add_parser("send-user-msg", help="send a message to your Mute from this device")
    send.add_argument("message", nargs="+", help="the message, or - to read it from stdin")
    send.add_argument("--session-id",
                     help="send to this side chat (a new id starts one) instead of the main chat")
    send.set_defaults(func=cmd_send_user_msg)

    sub.add_parser("info", help="show device identity").set_defaults(func=cmd_info)
    sub.add_parser("unpair", help="forget the saved pairing").set_defaults(func=cmd_unpair)

    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    return args.func(args)
