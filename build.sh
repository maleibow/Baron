#!/bin/sh
# Requires SGDK installed and the GDK environment variable pointing at it.
make -f "$GDK/makefile.gen"
echo "ROM: out/rom.bin"
