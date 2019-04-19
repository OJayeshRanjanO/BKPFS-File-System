#/bin/sh
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
touch testA
echo "Hello World" >> testA
echo "Hello World" >> testA
cd -
cd /test/bkpfs_test
ls
cd -
