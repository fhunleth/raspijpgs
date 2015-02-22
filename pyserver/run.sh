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

python -m CGIHTTPServer 8000
