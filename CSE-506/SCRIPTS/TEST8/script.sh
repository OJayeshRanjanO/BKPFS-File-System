#/bin/sh
echo "Testing ioctl -d for valid version"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
echo "Hello World 1" >> testH
echo "Hello World 2" >> testH
echo "Hello World 3" >> testH
/usr/src/hw2-jranjan/CSE-506/ioctl -d 2 testH
cd -
cd /test/bkpfs_test
cat bkpfs.testH.meta
ls -l
cd -
