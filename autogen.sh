#!/bin/sh
set -e

echo 'Running gtkdocize'
gtkdocize

echo 'Running autoreconf -i --force'
autoreconf -i --force

# Honor NOCONFIGURE for compatibility with gnome-autogen.sh
if test x"$NOCONFIGURE" = x; then
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
else
    run_configure=false
fi

default_args="--enable-gtk-doc --enable-mcd-plugins --enable-gnome-keyring=auto"

if test $run_configure = true; then
    echo "Running ./configure $default_args $*"
    ./configure $default_args "$@"
fi
