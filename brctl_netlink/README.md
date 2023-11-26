# brctl_netlink: implementation of 'brctl' utility using Netlink sockets

## Summary

`brctl_netlink` aims to implement limited set of original `brctl` utility functionality
using Netlink sockets to communicate with kernel

## Building

``` bash
mkdir build && cd build
cmake ..
make
```

## Using

``` bash

Usage: brctl_netlink [commands]

Commands:
	addbr       <bridge>            add bridge
	delbr       <bridge>            delete bridge
	addif       <bridge> <device>   add interface to bridge
	delif       <bridge> <device>   delete interface from bridge
        show        [ <bridge> ]        show a list of bridges
```

## Test
