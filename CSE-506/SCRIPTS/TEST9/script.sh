#/bin/sh
echo "Testing ioctl -d for invalid version"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" >> testI
echo "Hello World 2" >> testI
echo "Hello World 3" >> testI
/usr/src/hw2-jranjan/CSE-506/ioctl -d 5 testI
cd -
cd /test/bkpfs_test
cat bkpfs.testI.meta
ls -l
cd -
