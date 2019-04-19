#/bin/sh
echo "Testing ioctl -v for a INVALID version"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" > testE
/usr/src/hw2-jranjan/CSE-506/ioctl -v 2 testE
cd -
cd /test/bkpfs_test
ls
cd -
