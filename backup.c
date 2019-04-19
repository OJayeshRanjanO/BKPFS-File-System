#include "bkpfs.h"

// FIX - write what this does
int backup_helper(struct inode *inode, struct file *file){
	struct path lower_path; // Needs to be released here
	struct dentry *lower_dentry = NULL; // Need to release
	struct dentry *lower_parent_dentry = NULL;
	char *backup_name = NULL;
	int err = 0;
	int backup_name_length = 0;
	struct timeval t;
	UDBG;

	backup_name_length =  strlen("bkpfs."); // bkpfs.
	backup_name_length += strlen(file->f_path.dentry->d_name.name); // bkpfs.FILENAME
	backup_name_length += 1; // bkpfs.FILENAME.
	backup_name_length += 16; // bkpfs.FILENAME.TIMESTAMP
	backup_name_length += 1; // Adding the null byte

	backup_name = kzalloc( backup_name_length , GFP_KERNEL);
	if (!backup_name) {
		err = -ENOMEM;
		goto out;
	}

	do_gettimeofday(&t);
	snprintf(backup_name, backup_name_length, "bkpfs.%s.%10ld%06ld", file->f_path.dentry->d_name.name,t.tv_sec,t.tv_usec);

	// Create dentry for lowerfs
	bkpfs_get_lower_path(file->f_path.dentry, &lower_path); // put path
	
	lower_parent_dentry = lower_path.dentry->d_parent;
	
	err = create_negative_dentry(lower_parent_dentry ,backup_name, &lower_dentry); // dput
	if (err < 0)
		goto out;

	//meta file operations
	err = update_meta_file(backup_name, file->f_path.dentry->d_name.name, lower_path, O_RDWR|O_APPEND, 
		file->f_inode->i_mode);
	if (err < 0)
		goto out;
	
	err = create_file(lower_parent_dentry->d_inode, lower_path.mnt, lower_dentry, file->f_inode->i_mode, 0);
	if (err < 0)
		goto out;

	err = write_to_backup(file, lower_parent_dentry->d_inode, lower_path.mnt, lower_dentry);
	if (err < 0)
		goto out;

out:
	kfree(backup_name);
	if (lower_dentry)
		dput(lower_dentry);
	path_put(&lower_path);
	return err;
}


int restore_version(struct file *meta_file, struct file *original_file, int version)
{
	char *restore_filename = NULL;
	struct path restore_path; //ext4
	struct file *restore_file = NULL;
	char *buffer = NULL;
	int bytes_left = 0;
	int bytes_read = 0;
	mm_segment_t old_fs;
	int err = 0;
	char * meta_buffer = NULL;
	UDBG;

	err = get_nth_line(meta_file, version, &restore_filename);
	if (err < 0){
		goto out;
	}

	err = vfs_path_lookup(meta_file->f_path.dentry->d_parent, meta_file->f_path.mnt, restore_filename , 0, &restore_path);
	if (err < 0) {
		goto out; 
	}
	
	restore_file = dentry_open(&restore_path, O_RDONLY, current_cred());
	if (IS_ERR(restore_file)) {
		err = PTR_ERR(restore_file);
		goto out;
	}

	buffer = kzalloc(PAGE_SIZE, GFP_KERNEL); // Copy contents of file
	if (!buffer) {
		err = -ENOMEM;
		goto out;
	}
	
	memset(buffer, '\0', PAGE_SIZE);
	bytes_left = restore_path.dentry->d_inode->i_size;
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	// Write to original file
	while (bytes_left) {
		bytes_read = (bytes_left < PAGE_SIZE) ? bytes_left : PAGE_SIZE;
		err = vfs_read(restore_file, buffer, bytes_read, &(restore_file->f_pos));
		if (err < 0)
			break;
		err = vfs_write(original_file, buffer, bytes_read, &(original_file->f_pos));
		if (err < 0)
			break;
		bytes_left -= bytes_read;
		memset(buffer, '\0', PAGE_SIZE);
	}
	err = 0; // If everything goes well
	set_fs(old_fs);
	
out:
	if (meta_buffer)
		kfree(meta_buffer);
	if (restore_filename)
		kfree(restore_filename);
	if (restore_file)
		fput(restore_file);
	if (buffer)
		kfree(buffer);
	path_put(&restore_path);
	return err;
}


/**
 * delete_version - deletes given version and updates the file's meta 
 * data and folder's meta data
 *
 * @file: FILE * to meta data file (bkpfs.FILENAME.meta) on ext4 
 *			 (lower) fs 
 * @version: Version of the file based on newest to oldest
 *
 * This function is called by ioctl - DELETE
 *
 */
int delete_version(struct file *file, int version)
{
	char *delete_filename = NULL;
	struct path delete_path;
	struct dentry *dir_dentry = NULL;
	int err = 0;
	UDBG;

	// GET THE VERSION NAME
	err = get_nth_line(file, version, &delete_filename);
	if (err < 0) {
		goto out;
	}

	err = vfs_path_lookup(file->f_path.dentry->d_parent, file->f_path.mnt, delete_filename , 0,&delete_path);
	if (err < 0) {
		goto out; 
	}

	dir_dentry = lock_parent(delete_path.dentry);
	err = vfs_unlink(delete_path.dentry->d_parent->d_inode, delete_path.dentry, NULL);
	unlock_dir(dir_dentry);

	if (err < 0)
		goto out;
out:
	if (delete_filename)
		kfree(delete_filename);
	path_put(&delete_path);
	file->f_pos = 0; // Reset it to beginning of time
	return err;
}


/**
 * view_version - Read from the meta file and get the correct version
 *								based on line number
 *
 * @file: FILE * to meta data file (bkpfs.FILENAME.meta) on ext4 (lower) fs 
 * @version: Version of the file based on newest to oldest
 *
 * This function is called by ioctl - DELETE
 *
 */
int view_version(struct file *file, int version, char **buffer)
{
	char *view_filename = NULL;
	struct file *view_file = NULL;
	int bytes_left = 0;
	int bytes_read = 0;
	int err = 0;
	mm_segment_t old_fs;
	struct path view_path;
	UDBG;
	
	// get the version name
	err = get_nth_line(file, version, &view_filename);//Check for errors
	if (err < 0)
		goto out;
	 
	// GET THE CONTENTS OF THE VERSION FILE 
	err = vfs_path_lookup(file->f_path.dentry->d_parent, file->f_path.mnt, view_filename, 0, &view_path);
	if (err < 0)
		goto out;

	view_file = dentry_open(&view_path,O_RDONLY, current_cred());
	path_put(&view_path);
	if (IS_ERR(view_file)){
		err = PTR_ERR(view_file); 
		goto out;
	}
	*buffer = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!*buffer) {
		err = -ENOMEM;
		goto out;
	}
	memset( *buffer, '\0', PAGE_SIZE);
	bytes_left = view_file->f_path.dentry->d_inode->i_size;
	bytes_read = (bytes_left < PAGE_SIZE) ? bytes_left : PAGE_SIZE;
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = vfs_read(view_file, *buffer, bytes_read, &(view_file->f_pos) );
	set_fs(old_fs); 
	if (err < 0)
		goto out;
	err = 0;
out:
	kfree(view_filename);
	if (view_file)
		fput(view_file);
	return err;
}

int list_files(struct file *file, char **buffer)
{
	int bytes_read = 0;
	int bytes_left = 0;
	int err = 0;
	mm_segment_t old_fs;
	UDBG;
	
	*buffer = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer) {
		err = -ENOMEM;
		goto out;
	}
	
	bytes_left = file->f_path.dentry->d_inode->i_size;	
	bytes_read = (bytes_left < PAGE_SIZE) ? bytes_left : PAGE_SIZE;
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = vfs_read(file, *buffer, bytes_read, &(file->f_pos));
	set_fs(old_fs);
	if (err < 0)
		goto out;
	err = 0; // If everything goes well and we have positive result

out:
	return err;
}



