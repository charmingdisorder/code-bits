#!/bin/bash

# Compile-time SLKQ_SPOOL_COLLAPSE_LIMIT must be set accordingly
# for collapse part of code to be executed

SCRIPT=$(readlink -f "$0")
CPATH=$(dirname "$SCRIPT")
KMODULE=$CPATH/../build/kernel/slkq.ko

rmmod slkq
insmod $KMODULE

cat /proc/slkq_status

dd if=/dev/urandom of=$CPATH/test2_1.in count=1 bs=1k

DUMBSTR=`head -c 100 < /dev/zero | tr '\0' '\141'`

for i in {1..1024}
do
    echo "test $i $DUMBSTR" > /dev/slkq
done

echo "FIFO filled, stats:"

cat /proc/slkq_status

for i in {1025..1027}
do
    echo "test $i" > /dev/slkq
done

echo "Stats (after spooling):"

cat /proc/slkq_status

dd if=/dev/slkq of=$CPATH/test2_1.out count=1 bs=120
dd if=/dev/slkq of=$CPATH/test2_2.out count=1 bs=120

echo "First element:"
cat $CPATH/test2_1.out
echo "Second element:"
cat $CPATH/test2_2.out

echo "Stats afterwards:"
cat /proc/slkq_status

echo "Going to empty (for collapse)"

ls -l /var/spool/slkq.dat

# cat /proc/slkq_status

cat /dev/slkq > $CPATH/test2_collapse.out  &
CAT_PID=$!

while true
do
    cat /proc/slkq_status

    V1=`cat /proc/slkq_status | cut -f1 -d' '`
    if (( $V1 == 0)); then
	kill $CAT_PID
	break
    fi

done

echo "Done"
cat /proc/slkq_status
