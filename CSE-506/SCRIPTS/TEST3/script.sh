#/bin/sh
echo "Testing ioctl -l"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" > testC
echo "Hello World 2" >> testC
echo "Hello World 3" >> testC
/usr/src/hw2-jranjan/CSE-506/ioctl -l testC
cd -
cd /test/bkpfs_test
ls
cd -
