#!/bin/bash
# Elvrek: install QNX SDP 8.0 on a Linux or WSL host with the QNX Software
# Center command line tool. Asks for your myQNX login and licence key without
# showing them; they go only to qnxsoftwarecenter_clt and are never saved by
# this script.
#
#   bash tools/install-qnx-sdp.sh
#
# Your licence key is in myQNX License Manager: click the serial number of
# "QNX Software Development Platform 8.0" and copy "License Key".
set -e

CLT="$HOME/qnxinstall/qnxsoftwarecenter/qnxsoftwarecenter_clt"
DEST="$HOME/qnx800"
BASE="com.qnx.qnx800"   # package to install
MIRROR="qnx800"         # catalogue name the mirror step expects
[ -x "$CLT" ] || { echo "QNX Software Center not found at $CLT"; exit 1; }

read -r -p "myQNX email: " QUSER
read -r -s -p "myQNX password (hidden): " QPASS; echo
read -r -s -p "Licence key from License Manager (hidden, paste it): " QKEY; echo
AUTH=(-myqnx.user "$QUSER" -myqnx.password "$QPASS")

echo
echo "1/5 Adding and activating your licence key..."
"$CLT" "${AUTH[@]}" -addLicenseKey "$QKEY"
"$CLT" "${AUTH[@]}" -syncLicenseKeys || true
"$CLT" "${AUTH[@]}" -activateAll || true
unset QKEY

echo "2/5 Licence keys now on this machine:"
"$CLT" -listLicenseKeys

echo "3/5 Testing the connection to QNX..."
"$CLT" "${AUTH[@]}" -testConnections || true

echo "4/5 Downloading the SDP 8.0 package catalogue..."
"$CLT" "${AUTH[@]}" -mirrorBaseline "$MIRROR"

echo "5/5 Installing QNX SDP 8.0 into $DEST (this takes a while)..."
"$CLT" "${AUTH[@]}" -installBaseline "$BASE" -destination "$DEST"

unset QPASS AUTH
echo
echo "Done. Use it with:  source $DEST/qnxsdp-env.sh"
ls "$DEST"
