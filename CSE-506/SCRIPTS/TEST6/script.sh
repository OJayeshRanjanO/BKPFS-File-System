#/bin/sh
echo "Testing ioctl -r for a valid version"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" > testF
echo "Hello World 2" > testF
echo "Hello World 3" > testF
cat testF
/usr/src/hw2-jranjan/CSE-506/ioctl -r 2 testF
cat testF
cd -
cd /test/bkpfs_test
cd -
