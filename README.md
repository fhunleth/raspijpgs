# raspijpgs

Simple commandline driven MotionJPEG streamer for the Raspberry Pi

See
https://github.com/silvanmelchior/userland/tree/master/host_applications/linux/apps/raspicam
for `raspimjpeg` and `raspistill` which are programs that I wish that I could have
used. This one is based on raspimjpeg.

## Installation and Demo

Run the following on the Raspberry Pi:

    # Assumes Raspian - modify accordingly
    sudo apt-get install cmake

    git clone https://github.com/fhunleth/raspimjpg.git
    cd raspimjpg
    cmake .
    make
    sudo make install

The following demo runs a Python webserver to stream video from the Pi Camera:

    # Start the raspijpgs server
    ./raspijpgs &

    # Start up the python web server - python will call raspijpgs via a cgi script
    cd pyserver
    ./run.sh

Open a browser up on a PC and point it to http://you.pi.ip.address:8000/.

`raspijpgs` takes many of the same options as `raspistill`. You can modify the camera's
settings on the fly by using the `--set` option. For example:

    # Flip video
    ./raspijpgs --set vflip=on

    # Experiment with image effects
    ./raspijpgs --set imxfx=sketch

    ./raspijpgs --set imxfx=cartoon

    ./raspijpgs --set imxfx=none

While the server is running, you can capture an image to a file at any time. Do this
while streaming to a web browser to see that it doesn't interrupt the stream. On the
Raspberry Pi, run:

    ./raspijpgs --count 1 --output test.jpg

More than one --set option can be specified at a time. When you're done, you can either
kill the server or tell it to quit:

    ./raspijpgs --set quit

## MotionJPEG Framing

By default, `raspijpgs` concatenates each JPEG image to make one big file.
Other options are available, though. Here's the list:

  1. `cat` - concatenate each frame together
  2. `replace` - save each frame to a file. Each frame replaces the contents of the previous file.
  3. `mime` - output a multipart MIME stream with each JPEG in its own part
  4. `header` - output the number of bytes in the JPEG and then the JPEG

The `replace` option makes `raspijpgs` work similar to `raspimjpeg` and `raspistill`. Many
programs that serve Motion JPEG streams expect this kind of operation. The `mime` option
makes it possible to run `raspijpgs` in a CGI script so that you can stream the browser
without additional smarts. The `header` option is convenient if you're writing your own
program to send the JPEGs. The byte count for the `header` option is 4 bytes and stored
in the native endian (e.g., little endian on the Raspberry Pi.)

Framing is specified on the invocation of `raspijpgs`, so you can have different
framing options running at the same time.



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
