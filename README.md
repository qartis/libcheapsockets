libcheapsockets
=============

This is a simple networking library built around libcurl. It provides simulated socket support on cheap shared hosting web servers through port 80. It consists of a C API for connecting to a server and reading/writing data, as well as two sample CGI programs to be installed into the shared web host's cgi-bin.

The name libcheapsockets reflects the fact that this mechanism can be used to simulate a socket connection on a cheap shared web host with minimal features.

Usage - server
-----
The provided get.c and put.sh can be installed into your shared web host's cgi-bin. As an example server component, they create the file db.txt for storage and share lines of text.

The examples as given do not work with binary data, but they are protocol-agnostic with respect to what ASCII or UTF8 data they transfer. They work to accept and deliver individual lines of text from a file-based backend stored on the server. The provided get.c uses Linux's inotify subsystem to monitor the file for changes, but this could be replaced with a platform-independent library such as libev or libevent.

Usage - client
------
The client simply registers event handlers to handle events such as data arrival and unexpected timeouts or errors, and calls libcheapsockets_connect() on the server's URL. Incoming lines of text will be passed to the provided callback, and the user's application will be notified upon any unexpected situations.

License
----------
This project is released to the public domain.
