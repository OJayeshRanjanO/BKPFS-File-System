#/bin/sh
echo "Testing ioctl -v for a valid version"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" > testD
echo "Hello World 2" >> testD
echo "Hello World 3" >> testD
/usr/src/hw2-jranjan/CSE-506/ioctl -v 2 testD
cd -
cd /test/bkpfs_test
ls
cd -
