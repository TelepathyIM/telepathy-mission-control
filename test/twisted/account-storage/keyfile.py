#!/usr/bin/env python

def keyfile_read(fname):
    groups = { None: {} }
    group = None
    for line in open(fname):
        line = line[:-1].decode('utf-8').strip()
        if not line or line.startswith('#'):
            continue

        if line.startswith('[') and line.endswith(']'):
            group = line[1:-1]
            groups[group] = {}
            continue

        if '=' in line:
            k, v = line.split('=', 1)
        else:
            k = line
            v = None

        groups[group][k] = v
    return groups

