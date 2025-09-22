#!/usr/bin/env bash
set -e
make clean
make modules MODULES="e1000 rtl8xxxu mac80211 libarc4 cfg80211 rfkill"
make
make run