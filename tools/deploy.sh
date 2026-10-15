#!/usr/bin/env bash
#
# deploy.sh -- push the built .deb to the jailbroken devices and install it.
#
# Deliberately does NOT respring. SpringBoard is HUP'd by the old inline version
# of this, and respringing drops every running app, dismisses the on-screen
# keyboard, and takes a few seconds of black screen for no benefit here: the
# uicache refresh below is enough for a same-bundle-identifier app to pick up a
# new binary. The user launches manually anyway, since there is no working way to
# launch a GUI app from the command line on these devices.
#
# Usage:  tools/deploy.sh [host ...]
# Defaults to both known devices.

set -u

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEB="$PROJECT_DIR/packages/com.superyosh23.veryoriginalblockgame_1.0_iphoneos-arm.deb"
APP="/Applications/VeryOriginalBlockGame.app"
PKG="com.superyosh23.veryoriginalblockgame"

# iOS 6 ships an OpenSSH old enough to need RSA re-enabled on the client side.
SSH_OPTS=(
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o ConnectTimeout=10
    -o HostKeyAlgorithms=+ssh-rsa
    -o PubkeyAcceptedAlgorithms=+ssh-rsa
    -o LogLevel=ERROR
)
# scp and ssh spell the port flag differently: scp's -p means "preserve times",
# so passing "-p 22" to scp silently consumes 22 as a filename. Keep them apart.
SCP_PORT=(-P 22)
SSH_PORT=(-p 22)

if [ ! -f "$DEB" ]; then
    echo "no deb at $DEB -- run 'make package FOR_RELEASE=1' first" >&2
    exit 1
fi

LOCAL_MD5=$(md5sum "$PROJECT_DIR/.theos/obj/VeryOriginalBlockGame.app/VeryOriginalBlockGame" | cut -d' ' -f1)

hosts=("$@")
if [ ${#hosts[@]} -eq 0 ]; then
    hosts=(10.42.0.122 10.42.0.105)
fi

status=0
for host in "${hosts[@]}"; do
    printf '%-14s ' "$host"
    if ! sshpass -e scp "${SSH_OPTS[@]}" "${SCP_PORT[@]}" "$DEB" "root@$host:/var/tmp/" >/dev/null 2>&1; then
        echo "PUSH FAILED"
        status=1
        continue
    fi

    result=$(sshpass -e ssh "${SSH_OPTS[@]}" "${SSH_PORT[@]}" "root@$host" "
        DEB=/var/tmp/com.superyosh23.veryoriginalblockgame_1.0_iphoneos-arm.deb
        APP=$APP
        dpkg -r $PKG >/dev/null 2>&1
        rm -rf \"\$APP\"
        dpkg -i \"\$DEB\" >/dev/null 2>&1
        chown -R root:wheel \"\$APP\"
        find \"\$APP\" -type d -exec chmod 755 {} \;
        find \"\$APP\" -type f -exec chmod 644 {} \;
        chmod 755 \"\$APP/VeryOriginalBlockGame\"
        # The toolchain does not sign, so the kernel would refuse to exec it.
        ldid -S\"\$APP/VeryOriginalBlockGame\"
        ldid -S\"\$APP/Info.plist\"
        # Refresh SpringBoard's icon cache without respringing. Must run as
        # mobile: root writing to the cache trips ownership checks.
        su mobile -c \"uicache -p \\\$APP\" >/dev/null 2>&1
        echo \"\$(dpkg -l $PKG 2>/dev/null | tail -1 | cut -c1-4) \$(md5sum \"\$APP/VeryOriginalBlockGame\" | cut -d' ' -f1)\"
    " 2>&1 | grep -v "WARNING: connection")

    set_status=$(echo "$result" | awk '{print $1}')
    remote_md5=$(echo "$result" | awk '{print $2}')

    if [ "$set_status" != "ii" ]; then
        echo "INSTALL FAILED (dpkg status '${set_status:-none}')"
        status=1
    elif [ "$remote_md5" != "$LOCAL_MD5" ]; then
        echo "MISMATCH  on-device=$remote_md5 local=$LOCAL_MD5"
        status=1
    else
        echo "ok        $set_status  $remote_md5"
    fi
done

exit $status