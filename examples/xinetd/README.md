# xinetd

It's possible to run `raspijpgs` as a simple web server from `xinetd`. This lets
you access video from your Raspberry Pi by just adding an image element to your
web page that points to http://raspberrypi:8001/video. To make things easy to test,
if you point your web browser at http://raspberrypi:8001/, it will serve a minimal
test web page.

These instructions assume that you've already built and installed `raspijpgs`. If
you haven't, go back to the README.md in the main directory and following the
instructions.

To set up xinetd, first install the program:

    sudo apt-get install xinetd

The `xinetd` program listens on ports for connections and then forwards them to 
other programs to do the actual work. In this case, we want `raspijpgs` to
handle the web requests. To do this, add the following line to `/etc/services`:

    raspijpgs	8001/tcp

Next, copy the `raspijpgs` text file in this directory to `/etc/inetd.d`:

    sudo cp raspijpgs /etc/xinetd.d

Restart the `xinetd` daemon:

    /etc/init.d/xinetd restart

Make sure that a `raspijpgs` server has been started:

    raspijpgs &

Now you should be able to open http://raspberrypi:8001/ in your web browser. If
this doesn't work, check `/var/log/daemon` for error messages.
