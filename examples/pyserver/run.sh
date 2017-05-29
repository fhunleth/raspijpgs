#!/bin/sh

# Start a barebones Python webserver going that will
# call raspijpgs. Make sure that you build raspijpgs
# first. Then open up a browser and point it to
# http://ipaddress:8000/
#
# NOTE: Python appears to only allow one cgi session
#       at a time, so streaming is limited to one
#       browser. If you know how to make this multi-thread,
#       could you let me know.
#
# Environment variables can be used to modify the startup
# behavior. For example:
#
# RASPIJPGS_WIDTH=640 ./run.sh
#
# Also, once a stream is going, you can modify parameters:
#
# ./raspijpgs --client --send imxfx=sketch
# ./raspijpgs --client --send imxfx=none
# ./raspijpgs --client --send quality=20
# ./raspijpgs --client --send hflip
# ./raspijpgs --client --send hflip=off
# ./raspijpgs --client --send vflip
#

python -m CGIHTTPServer 8000
