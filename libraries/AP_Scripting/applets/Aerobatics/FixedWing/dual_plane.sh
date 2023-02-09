#!/bin/bash

# two planes flying together doing aerobatics

# assume we start the script from the root directory
ROOTDIR=$PWD
PLANE=$ROOTDIR/build/sitl/bin/arduplane

LOC1="-35.36326940,149.16517260,585,353.8"
LOC2="-35.36325764,149.16526957,585,353.8"

[ -x "$PLANE" ] || {
    ./waf configure --board sitl --debug
    ./waf plane
}

# setup multicast
UARTA="tcp:0"

PLANE_DEFAULTS="$ROOTDIR/Tools/autotest/models/plane-3d.parm"

(cd ArduPlane/AeroBatics1 && $PLANE --instance 1 --home $LOC1 --model plane-3d --uartA $UARTA --defaults $PLANE_DEFAULTS) &
(cd ArduPlane/AeroBatics2 && $PLANE --instance 2 --home $LOC2 --model plane-3d --uartA $UARTA --defaults $PLANE_DEFAULTS) &

wait
