BKPFS_VERSION="0.1"

EXTRA_CFLAGS += -DBKPFS_VERSION=\"$(BKPFS_VERSION)\"

obj-$(CONFIG_BKP_FS) += bkpfs.o

bkpfs-y := dentry.o file.o inode.o main.o super.o lookup.o mmap.o backup.o helper.o
