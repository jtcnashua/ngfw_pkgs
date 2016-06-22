#! /bin/bash

set -e

# constants

ASUS_ROOTFS="/var/lib/asus-ac88u-rootfs"
WL_BIN="/usr/sbin/wl"
PY_SCRIPT="/usr/lib/python2.7/brcm-wifi.py"

# functions

usage() {
  echo "Usage:"
  echo "  $0 <interface> (start|stop)"
}

interfaceUp() {
  chroot $ASUS_ROOTFS $WL_BIN -i $1 radio on
  chroot $ASUS_ROOTFS $WL_BIN -i $1 up
  chroot $ASUS_ROOTFS $WL_BIN -i $1 ap on
}

interfaceDown() {
  chroot $ASUS_ROOTFS $WL_BIN -i $1 ap off
  chroot $ASUS_ROOTFS $WL_BIN -i $1 down
  chroot $ASUS_ROOTFS $WL_BIN -i $1 radio off
}

startAp() {
  python $PY_SCRIPT $nic /etc/hostapd/hostapd.conf-$nic | while read line ; do
    chroot $ASUS_ROOTFS $line
  done
}

# main

if [ $# != 2 ] ; then
  usage
  exit 1
fi

nic=$1
action=$2

case $action in
  start)
    interfaceUp $nic
    startAp $nic
    ;;
  stop)
    pkill nas || true
    interfaceDown $nic ;;
esac
