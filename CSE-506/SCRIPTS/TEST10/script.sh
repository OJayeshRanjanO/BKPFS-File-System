#/bin/sh
echo "Testing ioctl -d for 'all'"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" >> testJ
echo "Hello World 2" >> testJ
echo "Hello World 3" >> testJ
/usr/src/hw2-jranjan/CSE-506/ioctl -d all testJ
cd -
cd /test/bkpfs_test
cat bkpfs.testJ.meta
ls -l
cd -
