#!/bin/bash

aclocal -I m4
autoheader
libtoolize --copy --force
autoconf
automake --add-missing --copy
