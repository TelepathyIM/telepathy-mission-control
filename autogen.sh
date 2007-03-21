#!/bin/sh
echo 'Running gtkdocize'; gtkdocize
echo 'Running autoreconf -i --force'; autoreconf -i --force
echo "Running ./configure --enable-maintainer-mode $*"; ./configure --enable-maintainer-mode $*

