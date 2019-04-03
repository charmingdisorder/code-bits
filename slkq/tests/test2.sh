#!/bin/bash

# XXX: SLKQ_SPOOL_COLLAPSE_LIMIT must be set accordingly

SCRIPT=$(readlink -f "$0")
CPATH=$(dirname "$SCRIPT")
KMODULE=$CPATH/../build/kernel/slkq.ko

rmmod slkq
insmod $KMODULE

cat /proc/slkq_status

dd if=/dev/urandom of=$CPATH/test2_1.in count=1 bs=1k

echo "test2_1" > /dev/slkq
echo "test2_2" > /dev/slkq

for i in {3..1024}
do
    cat $CPATH/test2_1.in > /dev/slkq
done

echo "FIFO filled, stats:"

cat /proc/slkq_status

echo "test2_1025" > /dev/slkq
echo "test2_1026" > /dev/slkq
echo "test2_1027" > /dev/slkq

echo "Stats (after spooling):"

cat /proc/slkq_status

dd if=/dev/slkq of=$CPATH/test2_1.out count=1 bs=20
dd if=/dev/slkq of=$CPATH/test2_2.out count=1 bs=20

echo "First element:"
cat $CPATH/test2_1.out
echo "Second element:"
cat $CPATH/test2_2.out

echo "Stats afterwards:"
cat /proc/slkq_status

echo "Going to empty (for collapse)"

ls -l /var/spool/slkq.dat

cat /proc/slkq_status

cat /dev/slkq > $CPATH/test2_collapse.out  &
CAT_PID=$!

while true
do
      V1=`cat /proc/slkq_status | cut -f1 -d' '`
      if (( $V1 == 0)); then
	  kill $CAT_PID
	  break
      fi

done

cat /proc/slkq_status
echo "Done"
