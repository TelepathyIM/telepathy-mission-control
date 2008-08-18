#!/bin/sh
set -e

echo 'Running gtkdocize'
gtkdocize

if test -n "$AUTOMAKE"; then
    : # don't override an explicit user request
elif automake-1.9 --version >/dev/null 2>/dev/null && \
     aclocal-1.9 --version >/dev/null 2>/dev/null; then
    # If we have automake-1.9, use it. This helps to ensure that our build
    # system doesn't accidentally grow automake-1.10 dependencies.
    AUTOMAKE=automake-1.9
    export AUTOMAKE
    ACLOCAL=aclocal-1.9
    export ACLOCAL
fi

echo 'Running autoreconf -i --force'
autoreconf -i --force

run_configure=true
for arg in $*; do
    case $arg in
        --no-configure)
            run_configure=false
            ;;
        *)
            ;;
    esac
done

if test $run_configure = true; then
    echo "Running ./configure --enable-maintainer-mode --enable-gtk-doc $*"
    ./configure --enable-maintainer-mode --enable-gtk-doc "$@"
fi
