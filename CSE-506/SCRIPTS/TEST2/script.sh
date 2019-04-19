#/bin/sh
echo "Dealing with more than 1 page"
cd /test/bkpfs_test
rm -f *
cd -
cd /mnt/bkpfs
tr -dc A-Za-z0-9 </dev/urandom | head -c 4096 > testB
echo "Hello World" >> testB
echo "Hello World" >> testB
cd -
cd /test/bkpfs_test
ls
cd -
