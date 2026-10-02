#!/usr/bin/env bash
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

# Install the Linux Device SDK.
#
#   curl -fsSL <url>/install.sh | bash
#   bash install.sh --server URL --token TOKEN [--from SOURCE] [--run-as USER] [--yes]
#   bash install.sh --uninstall [--purge]
#
# Installs system packages, a pinned uv, and mutegadget into /opt/mutegadget;
# pairs it with your Mute server; then installs and starts the mutegadget
# service.

set -euo pipefail

UV_VERSION="0.9.9"
PREFIX="/opt/mutegadget"
VENV="$PREFIX/venv"
UNIT="/etc/systemd/system/mutegadget.service"
STATE_DIR="/var/lib/mutegadget"
DEFAULT_SOURCE="git+https://github.com/mfiumara/mute-gadget-sdk@main#subdirectory=linux"
APT_PACKAGES=(python3 python3-cryptography curl ca-certificates)

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: install.sh [options]

  --from SOURCE     Install mutegadget from SOURCE: a local linux/ checkout, a
                    wheel, or a pip/uv URL. Default: $DEFAULT_SOURCE
  --server URL      Your Mute server, e.g. https://mute.example.com.
  --token TOKEN     The server's device token (MUTE_DEVICE_TOKEN on the server).
  --run-as USER     Account whose permissions your Mute's commands run with.
                    Default: the account running this installer.
  --yes             Don't ask for confirmation.
  --no-pair         Install without pairing; run 'sudo mutegadget pair' later.
  --uninstall       Remove mutegadget. Keeps the device identity and pairing
                    in $STATE_DIR unless --purge is also given.
  -h, --help        Show this help.
EOF
}

# Prompts read the terminal, not stdin, so `curl ... | bash` still works.
ask() {
    local prompt="$1" answer
    if [ "$ASSUME_YES" = 1 ]; then return 0; fi
    if [ ! -r /dev/tty ]; then die "no terminal to confirm on; rerun with --yes"; fi
    printf '%s [Y/n] ' "$prompt" >/dev/tty
    read -r answer </dev/tty || true
    case "$answer" in ""|y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

as_root() { if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi; }

systemd_running() { [ -d /run/systemd/system ]; }

# --- Checks ------------------------------------------------------------------

check_system() {
    [ "$(uname -s)" = Linux ] || die "The Linux Device SDK runs on Linux."
    [ -r /etc/os-release ] || die "cannot identify this Linux distribution (/etc/os-release missing)."
    # shellcheck disable=SC1091
    . /etc/os-release
    local id="${ID:-}" version="${VERSION_ID:-0}" major="${VERSION_ID%%.*}"
    case "$id" in
        debian|raspbian)
            [ "${major:-0}" -ge 11 ] 2>/dev/null ||
                die "$PRETTY_NAME is too old. Upgrade to Debian 11 (Raspberry Pi OS Bullseye) or later." ;;
        ubuntu)
            [ "$(printf '%s\n22.04\n' "$version" | sort -V | head -1)" = 22.04 ] ||
                die "$PRETTY_NAME is too old. Upgrade to Ubuntu 22.04 or later." ;;
        *)
            case " ${ID_LIKE:-} " in
                *" debian "*|*" ubuntu "*) warn "$PRETTY_NAME is untested; continuing because it is Debian-based." ;;
                *) die "$PRETTY_NAME is not supported. This installer needs a Debian or Ubuntu based system." ;;
            esac ;;
    esac
    command -v apt-get >/dev/null || die "apt-get not found."
    # Many 32-bit Raspberry Pi installs run a 64-bit kernel, so ask dpkg,
    # which reports the userland architecture.
    ARCH="$(dpkg --print-architecture)"
    case "$ARCH" in armhf|arm64|amd64) ;; *) die "unsupported architecture: $ARCH" ;; esac
    if ! systemd_running; then
        warn "systemd is not running; the service will be installed but not started."
    fi
}

choose_account() {
    if [ -z "$RUN_AS" ]; then
        if [ "$(id -u)" -ne 0 ]; then RUN_AS="$(id -un)"; else RUN_AS="${SUDO_USER:-}"; fi
    fi
    if [ -z "$RUN_AS" ] || [ "$RUN_AS" = root ]; then
        die "choose the account your Mute's commands run as with --run-as USER (running them as root is not supported)."
    fi
    id "$RUN_AS" >/dev/null 2>&1 || die "account '$RUN_AS' does not exist."

    local admin=""
    if as_root sudo -n -l -U "$RUN_AS" 2>/dev/null | grep -qE '\(ALL( : ALL)?\) (NOPASSWD: )?ALL'; then
        admin=" This account has administrator (sudo) rights, so your Mute will be able to do anything on this machine, including reading the device's own credentials."
    fi
    say "Your Mute will be able to run any command on this machine as '$RUN_AS', with that account's permissions.$admin"
    ask "Continue?" || die "cancelled. Rerun with --run-as to choose a different account."
}

# --- Install -----------------------------------------------------------------

install_packages() {
    local missing=() pkg
    for pkg in "${APT_PACKAGES[@]}"; do
        dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'install ok installed' || missing+=("$pkg")
    done
    case "$SOURCE" in git+*) command -v git >/dev/null || missing+=(git) ;; esac
    if [ "${#missing[@]}" -eq 0 ]; then return; fi
    say "Installing system packages: ${missing[*]}"
    as_root env DEBIAN_FRONTEND=noninteractive apt-get update -qq
    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${missing[@]}"
}

install_uv() {
    if [ -x "$PREFIX/bin/uv" ] && [ "$("$PREFIX/bin/uv" --version | awk '{print $2}')" = "$UV_VERSION" ]; then
        return
    fi
    say "Installing uv $UV_VERSION"
    as_root mkdir -p "$PREFIX/bin"
    curl -fsSL "https://astral.sh/uv/$UV_VERSION/install.sh" |
        as_root env UV_INSTALL_DIR="$PREFIX/bin" UV_NO_MODIFY_PATH=1 sh -s -- --quiet
}

install_mutegadget() {
    local uv="$PREFIX/bin/uv"
    # The system interpreter is required: cryptography comes from apt and is
    # built for it.
    if [ ! -x "$VENV/bin/python" ]; then
        say "Creating $VENV"
        as_root "$uv" venv --quiet --python /usr/bin/python3 --system-site-packages "$VENV"
    fi
    say "Installing mutegadget from $SOURCE"
    as_root "$uv" pip install --quiet --python "$VENV/bin/python" --no-deps --reinstall "$SOURCE"
    local data
    data="$("$VENV/bin/python" -c 'import mutegadget, os; print(os.path.join(os.path.dirname(mutegadget.__file__), "data"))')"
    as_root "$uv" pip install --quiet --python "$VENV/bin/python" --require-hashes \
        -r "$data/requirements.lock"
    as_root ln -sf "$VENV/bin/mutegadget" /usr/local/bin/mutegadget
    DATA_DIR="$data"
}

install_service() {
    say "Installing the mutegadget service"
    sed "s/@RUN_AS@/$RUN_AS/" "$DATA_DIR/mutegadget.service" | as_root tee "$UNIT" >/dev/null
    if systemd_running; then
        as_root systemctl daemon-reload
        as_root systemctl enable --now mutegadget.service >/dev/null 2>&1
        as_root systemctl restart mutegadget.service
    fi
}

pair() {
    if as_root test -s "$STATE_DIR/pairing.json" && [ -z "$SERVER" ]; then
        say "Already paired; the service will reconnect to your Mute."
        return
    fi
    if [ "$NO_PAIR" = 1 ] || [ -z "$SERVER" ]; then
        say "Not paired. Run 'sudo mutegadget pair --server URL --token TOKEN' when you're ready."
        return
    fi
    as_root /usr/local/bin/mutegadget pair --force --server "$SERVER" --token "$TOKEN" ||
        warn "not paired. Run 'sudo mutegadget pair --server URL --token TOKEN' to try again."
}

summary() {
    echo
    as_root /usr/local/bin/mutegadget info
    cat <<EOF

Service:   sudo systemctl status mutegadget
Logs:      sudo journalctl -u mutegadget -f
Remove:    bash install.sh --uninstall
EOF
}

# --- Uninstall ---------------------------------------------------------------

uninstall() {
    say "Removing mutegadget"
    if systemd_running && [ -f "$UNIT" ]; then
        as_root systemctl disable --now mutegadget.service >/dev/null 2>&1 || true
    fi
    as_root rm -f "$UNIT" /usr/local/bin/mutegadget
    if systemd_running; then as_root systemctl daemon-reload; fi
    as_root rm -rf "$PREFIX"
    if [ "$PURGE" = 1 ]; then
        as_root rm -rf "$STATE_DIR"
        say "Removed the device identity and pairing too."
    else
        say "Kept the device identity and pairing in $STATE_DIR (use --purge to remove them)."
    fi
}

main() {
    SOURCE="$DEFAULT_SOURCE" RUN_AS="" SERVER="" TOKEN="" ASSUME_YES=0 NO_PAIR=0 UNINSTALL=0 PURGE=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --from) SOURCE="${2:?--from needs a value}"; shift 2 ;;
            --run-as) RUN_AS="${2:?--run-as needs a value}"; shift 2 ;;
            --server) SERVER="${2:?--server needs a value}"; shift 2 ;;
            --token) TOKEN="${2:?--token needs a value}"; shift 2 ;;
            --yes|-y) ASSUME_YES=1; shift ;;
            --no-pair) NO_PAIR=1; shift ;;
            --uninstall) UNINSTALL=1; shift ;;
            --purge) PURGE=1; shift ;;
            -h|--help) usage; exit 0 ;;
            *) usage >&2; die "unknown option: $1" ;;
        esac
    done
    if [ "$(id -u)" -ne 0 ]; then
        command -v sudo >/dev/null || die "run this as root, or install sudo."
        sudo true || die "this installer needs sudo."
    fi
    if [ "$UNINSTALL" = 1 ]; then uninstall; return; fi
    if [ -n "$SERVER" ] && [ -z "$TOKEN" ]; then die "--server needs --token too."; fi
    if [ -d "$SOURCE" ]; then SOURCE="$(cd "$SOURCE" && pwd)"; fi

    check_system
    choose_account
    install_packages
    install_uv
    install_mutegadget
    pair
    install_service
    summary
}

# Everything runs from main, so a truncated download never runs a partial script.
main "$@"
