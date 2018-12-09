"TorMB": a Shadow plug-in
=========================

This plug-in provides middlebox functionality to Tor.
The most important features of the code that enable this are:
 + no process forking
 + no busy loops


last known working version
--------------------------

This plug-in was last tested and known to work with

`Shadow v1.12.0-15-g9502fca 2017-10-23 (built 2017-10-26) running GLib v2.46.2 and IGraph v0.7.1`

compiling
---------
`bash
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=`readlink -f ~`/.shadow
make
make install
`


usage
-----

Please see the `shadowtor.config.xml`, which may be run in Shadow

```bash
shadow shadowtor.config.xml > shadow.log
```

After running the above, check the following directories for process output:

  + `shadow.data/hosts/cover/stdout-cover-1000.log`
  + `shadow.data/hosts/cover/stdout-cover-1000.log`

A binary version of the code is available for usage outside of Shadow.
Run the program `cover` with no arguments to start the server:

```bash
cover
```

Run the program `cover` with the IP address or hostname of the listening
server to run client mode:

```bash
cover localhost
```
