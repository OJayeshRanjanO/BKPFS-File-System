#/bin/sh
rm -f /test/bkpfs_test/test* /test/bkpfs_test/TEST*  /test/bkpfs_test/bkpfs*
cd ..;
make -j2;
cd "CSE-506";
umount /mnt/bkpfs/;
rmmod bkpfs;
insmod /usr/src/hw2-jranjan/fs/bkpfs/bkpfs.ko;
mount -t bkpfs /test/bkpfs_test /mnt/bkpfs -o maxver=5;

