# raspijpgs

Dirt simple MotionJPEG streamer for the Raspberry Pi

See
https://github.com/silvanmelchior/userland/tree/master/host_applications/linux/apps/raspicam
for `raspimjpeg` and `raspistill` which are programs that I wish that I could have
used. This one is based on raspimjpeg.

## Installation

If you're using Raspian, run the following:

    sudo apt-get install cmake
    cmake .
    make
    sudo make install


## Examples


## Client/Server Protocol

When integrating your program with `raspijpgs`, you may want
to communicate directly with the `raspijpgs` server. By default, the
server creates a Unix Domain socket file called `/tmp/raspijpgs`. Configuration
commands are sent to the socket. An empty command is ok. Once the
server receives a command, it starts forwarding JPEG frames to the
client. The Unix Domain socket is run in datagram mode, so you'll
need to bind the client to a file for it to receive frames. Numerous
examples of how to do this exist on the web. Each datagram from
the server is one JPEG frame. Configuration commands may be sent
to the server at any time. The format is identical to the file format.
E.g., to change the contrast, send a packet containing the string
"contrast=70". It is ok to change multiple configuration parameters
at a time. Each request is separated by a new line character ('\n').

You can almost use `nc` to interact with `raspijpgs` with the exception that it
cannot receive the large Unix Domain socket packets containing JPEG images (the buffer
size is hardcoded to 2K bytes.) Sending configurations using `nc` works
fine, but make sure that you tell `nc` to quit or it will accumulate a lot
of 2K JPEG fragments.

# License

```
Copyright (c) 2013, Broadcom Europe Ltd
Copyright (c) 2013, Silvan Melchior
Copyright (c) 2013, James Hughes
Copyright (c) 2015, Frank Hunleth
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
