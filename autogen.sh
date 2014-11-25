#!/bin/sh
bs_dir=$(cd "$(dirname "$0")"; pwd)

#Some versions of libtoolize don't like there being no ltmain.sh file already
touch "${bs_dir}"/ltmain.sh
autoreconf -fi "${bs_dir}"

if test -n "$1" && test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
