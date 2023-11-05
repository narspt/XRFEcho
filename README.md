# XRFEcho
This software connects to a D-Star reflector using DExtra protocol and provides echo/parrot functionality.

# Build
Program is a single C file, no makefile is required. To build, simply run gcc:
```
gcc -o xrfecho xrfecho.c
```
or for old gcc:
```
gcc -std=gnu99 -lrt -o xrfecho xrfecho.c
```

# Usage
```
./xrfecho [CALLSIGN] [XRFName:MOD:XRFHostIP:PORT]
```
