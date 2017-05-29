# raspijpgs

[![Build Status](https://travis-ci.org/fhunleth/raspijpgs.svg?branch=master)](https://travis-ci.org/fhunleth/raspijpgs) [![Coverity Scan Build Status](https://scan.coverity.com/projects/4338/badge.svg)](https://scan.coverity.com/projects/4338)

Simple commandline driven MotionJPEG streamer for the Raspberry Pi

See [Silvan Melchior's branch of the Raspberry Pi userland project](https://github.com/silvanmelchior/userland/tree/master/host_applications/linux/apps/raspicam)
for `raspimjpeg` and `raspistill`. These are programs that I wish that I could have
used, but they either didn't have a convenient interface for my application or had
way too poor performance. This program copies a little more code from `raspimjpeg`.
I've been getting 15 fps and better performance with `raspijpgs`. I have no
reason to believe that it wouldn't be able to get to the max frame rate assuming
a lower exposure time (for me) and enough bandwidth for the JPEGS.

NOTE: Many, but not all of the `raspistill` camera configuration options have
been implemented. Each one is pretty easy to add, so if you find one that's not
implemented, please consider adding it and sending a pull request. I'll get
to all of the eventually!

## Installation and Demo

Run the following on the Raspberry Pi:

    # Assumes Raspian - see Makefile if VideoCore headers aren't in /opt/vc

    git clone https://github.com/fhunleth/raspijpgs.git
    cd raspijpgs
    make
    sudo make install

The following demo runs a Python webserver to stream video from the Pi Camera:

    # Start the raspijpgs server
    raspijpgs &

    # Start up the python web server - python will call raspijpgs via a cgi script
    cd pyserver
    ./run.sh

Open a browser up on a PC and point it to http://your.pi.ip.address:8000/.

`raspijpgs` takes many of the same options as `raspistill`. You can modify the camera's
settings on the fly by using the `--send` option. For example:

    # Flip video
    raspijpgs --send vflip=on

    # Experiment with image effects
    raspijpgs --send imxfx=sketch

    raspijpgs --send imxfx=cartoon

    raspijpgs --send imxfx=none

More than one --send option can be specified at a time.

While the server is running, you can capture an image to a file at any time. Do this
while streaming to a web browser to see that it doesn't interrupt the stream. On the
Raspberry Pi, run:

    raspijpgs --count 1 --output test.jpg

When you're done, stop the Python webserver. Then, you can either kill the `raspijpgs`
server process or tell it to quit:

    raspijpgs --send quit

## MotionJPEG Framing

By default, `raspijpgs` concatenates each JPEG image to make one big file.
Other options are available, though. Here's the list:

  1. `cat` - concatenate each frame together
  2. `replace` - save each frame to a file. Each frame replaces the contents of the previous file.
  3. `mime` - output a multipart MIME stream with each JPEG in its own part
  4. `http` - this is similar to MIME except that the client will wait for an HTTP GET request before serving the JPEGs
  4. `header` - output the number of bytes in the JPEG and then the JPEG

The `replace` option makes `raspijpgs` work similar to `raspimjpeg` and `raspistill`. Many
programs that serve Motion JPEG streams expect this kind of operation. The `mime` option
makes it possible to run `raspijpgs` in a CGI script so that you can stream the browser
without additional smarts. The `http` option is an extension to `mime` to support running
`raspijpgs` clients from xinetd or netcat. The `header` option is convenient if your program reads the JPEGs
from stdout and doesn't want to scan for JPEG SOI markers to separate the
images. The header is a 4 byte integer (big endian) that specifies the length of
the JPEG data to follow. When enabling the header option, commands sent via
stdin must also have length headers, so that the protocol is symetric.

Framing is specified on the invocation of `raspijpgs`, so you can have different
framing options running at the same time.

## Configuration

Configuration can be specified using configuration file, environment variables, or via
commandline arguments. Commandline arguments have precedence over environment variables
which have precedence over the configuration file. Each way of specifying a configuration
has it's own use. For the most part, commandline arguments and configuration files are
the way to go. However, sometimes it is inconvenient to access those arguments since
`raspijpgs` is being used deep in some other program. In this case, using environment
variables is a good way of passing configuration down. (E.g., you may want to set the
camera to have a cartoon image effect, so you set RASPIJPGS_IMXFX=cartoon in the
environment.

A configuration file is just a list of `key=value` pairs and comments. The keys are
the commandline option names without the "--" part. For example,

    # configuration the camera like we want
    contrast=20
    brightness=40
    saturation=-10

`raspijpgs` uses many of the same commandline arguments as `raspistill`. The following
table summarizes the options:

Option          | Environment Var | Description
----------------|-----------------|------------
width           | RASPIJPG_WIDTH | Set the image width
annotation      | RASPIJPG_ANNOTATION | Annotate the video frames with this text
anno_background | RASPIJPG_ANNO_BACKGROUND | Enable a black background behind the annotated text
sharpness       | RASPIJPG_SHARPNESS | 	 Set image sharpness (-100 to 100)
contrast        | RASPIJPG_CONTRAST | 	 Set image contrast (-100 to 100)
brightness      | RASPIJPG_BRIGHTNESS |  Set image brightness (0 to 100)
saturation      | RASPIJPG_SATURATION |  Set image saturation (-100 to 100)
ISO             | RASPIJPG_ISO | 	 Set capture ISO (100 to 800)
vstab           | RASPIJPG_VSTAB | 	 Turn on video stabilisation
ev              | RASPIJPG_EV | 	 Set EV compensation (-10 to 10)
exposure        | RASPIJPG_EXPOSURE | 	 Set exposure mode
awb             | RASPIJPG_AWB | 	 Set Automatic White Balance (AWB) mode
imxfx           | RASPIJPG_IMXFX | 	 Set image effect
colfx           | RASPIJPG_COLFX | 	 Set colour effect <U:V>
metering        | RASPIJPG_METERING | 	 Set metering mode
rotation        | RASPIJPG_ROTATION | 	 Set image rotation (0-359)
hflip           | RASPIJPG_HFLIP | 	 Set horizontal flip
vflip           | RASPIJPG_VFLIP | 	 Set vertical flip
roi             | RASPIJPG_ROI | 	 Set sensor region of interest
shutter         | RASPIJPG_SHUTTER | 	 Set shutter speed
quality         | RASPIJPG_QUALITY | 	 Set the JPEG quality (0-100)
socket          | RASPIJPG_SOCKET | 	 Specify the socket filename for communication
output          | RASPIJPG_OUTPUT | 	 Specify an output filename or '-' for stdout
count           | RASPIJPG_COUNT |      	 How many frames to capture before quiting (-1 = no limit)
lockfile        | RASPIJPG_LOCKFILE |      	 Specify a lock filename to prevent multiple runs
config          | | 	 Specify a config file to read for options
framing         | | 	 Specify the output framing (cat, mime, http, header, replace)
send            | |      	 Set this parameter on the server (e.g. --send shutter=1000)
server          | |      	 Run as a server
client          | |      	 Run as a client
quit            | |      	 Tell a server to quit
help            | | 	 Print a help message


## Client/Server Protocol

When integrating your program with `raspijpgs`, you have a few options for
communicating with the `raspijpgs` server. The first is just to call
`raspijpgs` like in the example above to send commands and receive frames.

A second approach is to specify `--output -` to the server so that it sends
frames out stdout. Use the `--framing` option so that the frames are output in
a convenient form. For control, the server reads stdin for commands. Each
command is of the form 'key=value\n' or just 'key\n' to enable boolean options. 
To exit the server, send `quit`.

A third option is to communicate with a `raspijpgs` server using a Unix Domain
socket in datagram mode.  By default, the server creates a socket file named
`/tmp/raspijpgs`. Configuration commands are sent to the socket. An empty
command is ok. Once the server receives a command, it starts forwarding JPEG
frames to the client's Unix Domain socket. Make sure that you bind the client
socket to a file for it to receive frames. Numerous examples of how to do this
exist on the web. Each datagram from the server is one JPEG frame.
Configuration commands may be sent to the server at any time. The format is
identical to the file format.  E.g., to change the contrast, send a packet
containing the string "contrast=70". It is ok to change multiple configuration
parameters at a time by separating them with '\n' characters.

You can almost use `nc` to interact with `raspijpgs` with the exception that it
cannot receive the large Unix Domain socket packets containing JPEG images (the
buffer size is hardcoded to 2K bytes.) Sending configurations using `nc` works
fine, but make sure that you tell `nc` to quit or it will accumulate a lot of
2K JPEG fragments.

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
