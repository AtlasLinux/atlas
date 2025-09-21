#!/usr/bin/env bash
set -e
make clean
make modules MODULES="e1000 rtl8xxxu"
make
make run