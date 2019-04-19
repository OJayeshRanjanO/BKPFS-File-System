#/bin/sh
echo "Testing ioctl -r for valid version and more than PAGE_SIZE"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
tr -dc A-Za-z0-9 </dev/urandom | head -c 4100 > testG
echo "Hello World 2" >> testG
echo "Hello World 3" >> testG
cat testG
/usr/src/hw2-jranjan/CSE-506/ioctl -r 2 testG
cat testG
cd -
cd /test/bkpfs_test
cd -
