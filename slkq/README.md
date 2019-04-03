# SLKQ: Simple Linux Kernel Queue

## Building

``` bash
mkdir build && cd build
cmake ..
make
```

## Testing

### Kernel

Example of tests are provided in `tests/`.

Some example scenarios:
```

``` bash
build# insmod kernel/slkq.ko
build# cat /proc/slkq_status
0 1024 1024 0
build# echo "test1" > /dev/slkq
build# cat /dev/slkq
test1
^C
build# ./slkq_write test_write
OK
root@sap1:/mnt_code/code-bits/slkq.export/build# cat /dev/slkq
test_write^C
```

For `reader` and `writer`

``` bash

build# ./slkq_reader &
build# ./slkq_write test_reader
OK
build# find /var/db/slkq/ -mmin -1
/var/db/slkq/
/var/db/slkq/20190403_0113.bin
build# od -cd /var/db/slkq/20190403_0113.bin
0000000  \f  \0   t   e   s   t   _   r   e   a   d   e   r  \0
             12   25972   29811   29279   24933   25956     114
0000016
```
