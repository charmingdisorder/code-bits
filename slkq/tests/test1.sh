#!/bin/bash

SCRIPT=$(readlink -f "$0")
CPATH=$(dirname "$SCRIPT")
KMODULE=$CPATH/../build/kernel/slkq.ko

rmmod slkq
insmod $KMODULE

cat /dev/slkq > $CPATH/test1.out &
CAT_PID=$!

dd if=/dev/urandom of=$CPATH/test1.in bs=1k count=1

cat $CPATH/test1.in > /dev/slkq

sleep 5
kill $CAT_PID

rmmod slkq

(cmp --silent $CPATH/test1.out $CPATH/test1.in && echo "Test passed") || echo "Test FAILED"
