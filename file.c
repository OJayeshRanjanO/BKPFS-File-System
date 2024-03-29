/*
 * Copyright (c) 1998-2017 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2017 Stony Brook University
 * Copyright (c) 2003-2017 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bkpfs.h"

static ssize_t bkpfs_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	int err;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	lower_file = bkpfs_lower_file(file);
	err = vfs_read(lower_file, buf, count, ppos);
	/* update our inode atime upon a successful lower read */
	if (err >= 0)
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));

	return err;
}

static ssize_t bkpfs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int err;

	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	lower_file = bkpfs_lower_file(file);
	err = vfs_write(lower_file, buf, count, ppos);
	/* update our inode times+sizes upon a successful lower write */
	if (err >= 0) {
		fsstack_copy_inode_size(d_inode(dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(dentry),
					file_inode(lower_file));
	}
	bytes_written+=count; // Count number of bytes written
	return err;
}


static int (*old_ctx)(struct dir_context * ctx, const char * name, int i, loff_t offset , u64 ino ,unsigned int d_dype);
static int exclude_files(struct dir_context * ctx, const char * name, int i, loff_t offset , u64 ino ,unsigned int d_dype){
	if ( strlen(name) >= strlen("bkpfs") ){
		if ( memcmp(name,"bkpfs",5) == 0 ){
			return 0;
		}
	}


	return old_ctx(ctx, name, i, offset , ino ,d_dype);
}

static int bkpfs_readdir(struct file *file, struct dir_context *ctx)
{
	int err;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;

	//Slight modification to use some other callback
	old_ctx = ctx->actor;
	ctx->actor = &exclude_files;
		
	lower_file = bkpfs_lower_file(file);
	err = iterate_dir(lower_file, ctx);


	
	file->f_pos = lower_file->f_pos;
	if (err >= 0)		/* copy the atime */
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));

	return err;
}

static long bkpfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file = NULL;
	struct args *kern_struct = NULL;
	struct path op_lower_path;
	struct path meta_path;
	struct file *meta_file = NULL;
	char *meta_filename = NULL;
	mm_segment_t old_fs;
	int count = 1;

	lower_file = bkpfs_lower_file(file);


	/* XXX: use vfs_ioctl if/when VFS exports it */

	if (!lower_file || !lower_file->f_op)
		goto out;

	if (lower_file->f_op->unlocked_ioctl)
		err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);

	/* some ioctls can change inode attributes (EXT2_IOC_SETFLAGS) */
	if (!err)
		fsstack_copy_attr_all(file_inode(file),
				      file_inode(lower_file));
	

	//Allocate for struct
	kern_struct = kzalloc(sizeof(struct args), GFP_KERNEL);
	if (!kern_struct) {
		err = -ENOMEM;
		goto out;
	}

	if (copy_from_user(kern_struct, (struct args *)arg, sizeof(struct args))) {
		err = -EFAULT;
		goto out;
	}

	//Allocate for filename
	kern_struct->filename = kzalloc(strlen(file->f_path.dentry->d_name.name), GFP_KERNEL) ;
	strcpy(kern_struct->filename, file->f_path.dentry->d_name.name);

	//Generating name for metafile
	meta_filename = kzalloc(strlen("bkpfs.") + strlen(kern_struct->filename) + strlen(".meta")+1,GFP_KERNEL);//free
	if(!meta_filename){
		err = -ENOMEM;
		goto out;
	}
	snprintf(meta_filename, strlen("bkpfs.") + strlen(kern_struct->filename) + strlen(".meta")+1,"bkpfs.%s.meta",
		kern_struct->filename);

	
	switch(cmd) {
		case RESTORE_BKPFS:
			//Getting version number
			kern_struct->version = ((struct args *)arg)->version;
		
			//OPEN THE META FILE HERE
			bkpfs_get_lower_path(file->f_path.dentry, &op_lower_path);//put it back
			err = vfs_path_lookup(op_lower_path.dentry->d_parent,op_lower_path.mnt,meta_filename, 0, &meta_path);
      			if (err < 0)
        			goto out;
			
      			meta_file = dentry_open(&meta_path, O_RDWR , current_cred());
			if (IS_ERR(meta_file)){

				err = PTR_ERR(meta_file);	
				meta_file = NULL;
				goto out;
			}

			err = restore_version(meta_file, file, kern_struct->version);
			if (err < 0)
				goto out;
			path_put(&op_lower_path);
			path_put(&meta_path);
			break;
 
		case DELETE_BKPFS:
			//Getting version number
			kern_struct->version = ((struct args *)arg)->version;

			// OPEN THE META FILE HERE
			bkpfs_get_lower_path(file->f_path.dentry, &op_lower_path); // put it back
			
			err = vfs_path_lookup(op_lower_path.dentry->d_parent,op_lower_path.mnt,meta_filename,0,&meta_path);	
			if (err < 0)
				goto out;

			meta_file = dentry_open(&meta_path, O_RDWR ,current_cred());
			if (IS_ERR(meta_file)){
				err = PTR_ERR(meta_file);
				meta_file = NULL;
				goto out;
			}

			if (kern_struct->version == -2){ // Removing all files
				while (err == 0){
					err = delete_version(meta_file, count);
					meta_file->f_pos = 0;
					count++;
				}

				vfs_truncate(&meta_path,0); // Set it to 0 bytes
				path_put(&op_lower_path);
				path_put(&meta_path);	
				err = 0;
				break;
			}
			else // For any other case
				err = delete_version(meta_file, kern_struct->version);
			if (err < 0)
				goto out;

			// Get all by 1 version
			err = negate_version(meta_file, kern_struct->version, &kern_struct->buffer);
			if (err < 0)
				goto out;
			err = 0; // If everything goes well 
			path_put(&meta_path);	
			filp_close(meta_file,0);

			// Update the meta-data file // Reopen the meta file
			err = vfs_path_lookup(op_lower_path.dentry->d_parent,op_lower_path.mnt,meta_filename,0,&meta_path);	
			if (err < 0)
				goto out;
			meta_file = dentry_open(&meta_path, O_RDONLY|O_WRONLY|O_TRUNC, current_cred());
			if (IS_ERR(meta_file)) {
				err = PTR_ERR(meta_file);
				meta_file = NULL;
				goto out;
			}
			vfs_truncate(&meta_path,0); // Set it to 0 bytes

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = vfs_write(meta_file, kern_struct->buffer, strlen(kern_struct->buffer), &(meta_file->f_pos));
			if (err < 0)
				goto out;
			set_fs(old_fs);

			path_put(&op_lower_path);
			path_put(&meta_path);
			break;
                case VIEW_BKPFS:
			//Getting version number
			kern_struct->version = ((struct args *)arg)->version;

			//OPEN THE META FILE HERE
			bkpfs_get_lower_path(file->f_path.dentry, &op_lower_path);//put it back
			err = vfs_path_lookup(op_lower_path.dentry->d_parent, op_lower_path.mnt, meta_filename, 0, &meta_path);	
			if (err < 0)
				goto out;
			meta_file = dentry_open(&meta_path,O_RDONLY, current_cred());
			if (IS_ERR(meta_file)){
				err = PTR_ERR(meta_file);
				meta_file = NULL;
				goto out;
			}

			// Getting contents of current version
			err = view_version(meta_file, kern_struct->version, &(kern_struct->buffer));
			if (err < 0)
				goto out;
 
			//Copying back the result to user buffer
			err = copy_to_user( ((struct args *)arg)->buffer, kern_struct->buffer, strlen( kern_struct->buffer ) );	
			if (err != 0)
				goto out;
			path_put(&op_lower_path);
			path_put(&meta_path);
                        break;
		case LIST_BKPFS:
			//OPEN THE META FILE HERE
			bkpfs_get_lower_path(file->f_path.dentry, &op_lower_path);//put it back
			err = vfs_path_lookup(op_lower_path.dentry->d_parent, op_lower_path.mnt, meta_filename, 0, &meta_path);
			if (err < 0)
				goto out;

			meta_file = dentry_open(&meta_path,O_RDONLY, current_cred());
			if (IS_ERR(meta_file)){
				err = PTR_ERR(meta_file);	
				meta_file = NULL;
				goto out;
			}
		
			//Getting all the files
			err = list_files(meta_file, &(kern_struct->buffer));
			if (err < 0)
				goto out;

			//Copying back the result to user buffer
			err = copy_to_user(((struct args *)arg)->buffer, kern_struct->buffer, strlen(kern_struct->buffer));
			if (err != 0)
				goto out;
			path_put(&op_lower_path);
			path_put(&meta_path);
			break;
        }

out:
	if (meta_file)
		filp_close(meta_file, 0);
	if(kern_struct)
		kfree(kern_struct);
	return err;
}



#ifdef CONFIG_COMPAT
static long bkpfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;
	lower_file = bkpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);
out:
	return err;
}
#endif

static int bkpfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;
	/* this might be deferred to mmap's writepage */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * File systems which do not implement ->writepage may use
	 * generic_file_readonly_mmap as their ->mmap op.  If you call
	 * generic_file_readonly_mmap with VM_WRITE, you'd get an -EINVAL.
	 * But we cannot call the lower ->mmap op, so we can't tell that
	 * writeable mappings won't work.  Therefore, our only choice is to
	 * check if the lower file system supports the ->writepage, and if
	 * not, return EINVAL (the same error that
	 * generic_file_readonly_mmap returns in that case).
	 */
	lower_file = bkpfs_lower_file(file);
	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "bkpfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}

	/*
	 * find and save lower vm_ops.
	 *
	 * XXX: the VFS should have a cleaner way of finding the lower vm_ops
	 */
	if (!BKPFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "bkpfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
	}

	/*
	 * Next 3 lines are all I need from generic_file_mmap.  I definitely
	 * don't want its test for ->readpage which returns -ENOEXEC.
	 */
	file_accessed(file);
	vma->vm_ops = &bkpfs_vm_ops;

	file->f_mapping->a_ops = &bkpfs_aops; /* set our aops */
	if (!BKPFS_F(file)->lower_vm_ops) /* save for our ->fault */
		BKPFS_F(file)->lower_vm_ops = saved_vm_ops;
out:
	return err;
}


static int bkpfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct path lower_path;
	
	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) {
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct bkpfs_file_info), GFP_KERNEL);
	if (!BKPFS_F(file)) {
		err = -ENOMEM;
		goto out_err;
	}
	
	/* open lower object and link bkpfs's file struct to lower's */
	bkpfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());

	path_put(&lower_path);
	if (IS_ERR(lower_file)) {
		err = PTR_ERR(lower_file);
		lower_file = bkpfs_lower_file(file);
		if (lower_file) {
			bkpfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	} else {
		bkpfs_set_lower_file(file, lower_file);
	}

	if (err)
		kfree(BKPFS_F(file));
	else
		fsstack_copy_attr_all(inode, bkpfs_lower_inode(inode));
out_err:
	bytes_written = 0; // Initialize bytes written	
	return err;
}

static int bkpfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = bkpfs_lower_file(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->flush) {
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}
	
	return err;
}

/* release all lower object references & free the file info structure */
static int bkpfs_file_release(struct inode *inode, struct file *file)
{
	struct file *lower_file;
	lower_file = bkpfs_lower_file(file);
	if (lower_file) {
		bkpfs_set_lower_file(file, NULL);
		fput(lower_file);
	}
	// MY CODE
	if ( (file->f_flags & O_WRONLY) && S_ISREG(file->f_mode) ){
		if ( memcmp(file->f_path.dentry->d_name.name,"bkpfs",5)!=0 ){
			if (bytes_written > backup_on){
				backup_helper(inode, file);
			}
		}

	}
	kfree(BKPFS_F(file));

	return 0;
}

static int bkpfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;
	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = bkpfs_lower_file(file);
	bkpfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	bkpfs_put_lower_path(dentry, &lower_path);
	
out:
	return err;
}

static int bkpfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;
	lower_file = bkpfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);
	
	return err;
}

/*
 * Bkpfs cannot use generic_file_llseek as ->llseek, because it would
 * only set the offset of the upper file.  So we have to implement our
 * own method to set both the upper and lower file offsets
 * consistently.
 */
static loff_t bkpfs_file_llseek(struct file *file, loff_t offset, int whence)
{
	int err;
	struct file *lower_file;

	err = generic_file_llseek(file, offset, whence);
	if (err < 0)
		goto out;

	lower_file = bkpfs_lower_file(file);
	err = generic_file_llseek(lower_file, offset, whence);
	
out:
	return err;
}

/*
 * Bkpfs read_iter, redirect modified iocb to lower read_iter
 */
ssize_t
bkpfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->read_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode atime as needed */
	if (err >= 0 || err == -EIOCBQUEUED)
		fsstack_copy_attr_atime(d_inode(file->f_path.dentry),
					file_inode(lower_file));
out:
	return err;
}

/*
 * Bkpfs write_iter, redirect modified iocb to lower write_iter
 */
ssize_t
bkpfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->write_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->write_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode times/sizes as needed */
	if (err >= 0 || err == -EIOCBQUEUED) {
		fsstack_copy_inode_size(d_inode(file->f_path.dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(file->f_path.dentry),
					file_inode(lower_file));
	}
	
out:
	return err;
}


const struct file_operations bkpfs_main_fops = {
	.llseek		= generic_file_llseek,
	.read		= bkpfs_read,
	.write		= bkpfs_write,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.mmap		= bkpfs_mmap,
	.open		= bkpfs_open,
	.flush		= bkpfs_flush,
	.release	= bkpfs_file_release,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
	.read_iter	= bkpfs_read_iter,
	.write_iter	= bkpfs_write_iter,
};

/* trimmed directory options */
const struct file_operations bkpfs_dir_fops = {
	.llseek		= bkpfs_file_llseek,
	.read		= generic_read_dir,
	.iterate	= bkpfs_readdir,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.open		= bkpfs_open,
	.release	= bkpfs_file_release,
	.flush		= bkpfs_flush,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
};

