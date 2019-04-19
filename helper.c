#include "bkpfs.h"

int max_versions = 0;
int bytes_written = 0;
int backup_on = 0;

int get_nth_line(struct file * file, int version, char **buffer)
{
	int bytes_left = 0;
	mm_segment_t old_fs;
	int filename_length = 0;
	int err = 0;
	UDBG;
 
	filename_length = strlen(file->f_path.dentry->d_name.name); // bkpfs.FILENAME.meta
	filename_length -= strlen("meta"); // bkpfs.FILENAME.
	filename_length += 16; // bkpfs.FILENAME.1234567890123456
	filename_length += 1; // For the null byte
	*buffer = kzalloc(filename_length, GFP_KERNEL);
	
	if (!*buffer) {
		err = -ENOMEM;
		goto out;
	}
	memset(*buffer,'\0',filename_length);
	bytes_left = file->f_path.dentry->d_inode->i_size;

	if (version == -1) // Get the last file
		version = bytes_left / filename_length;
	
	// CHECK if version > number of lines in file
	if (((version - 1) * filename_length) >= bytes_left) {
		err = -ENOENT;
		goto out;
	}
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	
	file->f_pos += (version - 1) * filename_length;
	err = vfs_read(file, *buffer, filename_length, &(file->f_pos));
	if (err < 0)
		goto out;
	err = 0; // If everything goes well
	(*buffer)[filename_length - 1] = '\0';
		
	set_fs(old_fs);

out:
	return err;
}

int get_backup_size(struct file * meta_file){		
	char * filename = NULL;
	int filename_length = 0;
	struct path filename_path;
	int bytes_left = 0;
	int err = 0;
	int total_backup_size = 0;
	int count = 1;
	UDBG;

	filename_length = strlen(meta_file->f_path.dentry->d_name.name); // bkpfs.FILENAME.meta
	filename_length -= strlen("meta"); // bkpfs.FILENAME.
	filename_length += 16; // bkpfs.FILENAME.1234567890123456
	filename_length += 1; // For the null byte

	bytes_left = meta_file->f_path.dentry->d_inode->i_size;
	
	while(bytes_left){
		err = get_nth_line(meta_file, count, &filename);
		if (err < 0)
			goto out;

		filename[filename_length-1] = '\0';
		err = vfs_path_lookup(meta_file->f_path.dentry->d_parent, 
					meta_file->f_path.mnt, filename, 0, &filename_path);
		if (err < 0)
			goto out;

		total_backup_size += filename_path.dentry->d_inode->i_size;

		meta_file->f_pos = 0;//Reset and calculate again
		kfree(filename);//We have to free the filename buffer allocated by get_nth_line
		path_put(&filename_path);
		count++;
		bytes_left -= filename_length;
	}
out:
	meta_file->f_pos = 0;//Reset and calculate again
	if (err == 0)
		return total_backup_size;
	return err;
}

int negate_version(struct file * meta_file, int version, char ** buffer){	
	//char * buffer = NULL;
	char * buffer_pointer = NULL;
	int buffer_size = 0;
	char * filename = NULL;
	int count = 1;
	int filename_length = 0;
	int bytes_left = 0;
	int err = 0;
	UDBG;

 
	filename_length = strlen(meta_file->f_path.dentry->d_name.name); // bkpfs.FILENAME.meta
	filename_length -= strlen("meta"); // bkpfs.FILENAME.
	filename_length += 16; // bkpfs.FILENAME.1234567890123456
	filename_length += 1; // For the null byte

	bytes_left = meta_file->f_path.dentry->d_inode->i_size;
	
	if (version == -1) // Get the last file
		version = bytes_left / filename_length;

	if ( (version -1)*filename_length >= bytes_left ){ // CHECK if version > number of lines in file
		err = -ENOENT;
		goto out;
	}

	buffer_size = (bytes_left+1); // Making sure to add null at the end
	buffer_size -= filename_length;

	*buffer = kzalloc(buffer_size,GFP_KERNEL);
	if (!*buffer) {
		err = -ENOMEM;
		goto out;
	}
	memset(*buffer,'\0',buffer_size);
	buffer_pointer = *buffer;	
	
	while(bytes_left){
		if (version != count){

			err = get_nth_line(meta_file, count, &filename);
			if (err < 0)
				goto out;


			filename[filename_length-1] = '\n'; // The newline was replaced with 0, now replace it with \n

			strcpy(buffer_pointer,filename);

			meta_file->f_pos = 0; // Reset and calculate again
			buffer_pointer += filename_length;
			kfree(filename); // We have to free the filename buffer allocated by get_nth_line
		}
		count++;
		bytes_left -= filename_length;
	}
out:
	return err;
}



/**************************************************Functions to create backup file************************************************/
/**
 * update_meta_file - This function is mainly used to keep track of meta for a file and for the directory
 *
 *@backup_name: Name of the file to be appended to the meta of the file
 *@filename: Name of the original file
 *@lower_path: path to the original file on ext4 
 *
 *This function is called by backup_helper.
 *The function creates a new meta file in the following format: bkpfs.FILENAME.meta
 *
 */
int update_meta_file(char * backup_name, const char * filename, struct path lower_path, int flags, int mode){
	char *meta_name;
	struct path meta_path;
	struct dentry *meta_dentry = NULL;
	struct file *meta_file = NULL;
	char * meta_buffer = NULL;
	int meta_size = 0;
	int meta_found;
	mm_segment_t old_fs;
	int err = 0;
	UDBG;

	meta_name = kzalloc(strlen("bkpfs.") + strlen(filename) + strlen(".meta")+1, GFP_KERNEL); // free
	if (!meta_name) {
		err = -ENOMEM;
		goto out;
	}
	err = snprintf(meta_name, strlen("bkpfs.") + strlen(filename) + strlen(".meta")+1, "bkpfs.%s.meta", filename);
	if (err < 0)
		goto out;
	meta_found = vfs_path_lookup(lower_path.dentry->d_parent, lower_path.mnt, meta_name,0, &meta_path); // put path
	
	if (meta_found != 0){ // Create the meta file
		err = create_negative_dentry(lower_path.dentry->d_parent, meta_name, &meta_dentry); // dput
		if (err < 0)
			goto out;

		err = create_file(lower_path.dentry->d_parent->d_inode, lower_path.mnt ,meta_dentry, mode, 0); 
		if (err < 0)
			goto out;

		err = vfs_path_lookup(lower_path.dentry->d_parent, lower_path.mnt, meta_name, 0, &meta_path);
		if (err < 0)
			goto out;
		
		meta_file = dentry_open(&meta_path, O_WRONLY, current_cred());
		if (IS_ERR(meta_file)){
			err = PTR_ERR(meta_file); 
			goto out;
		}

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = vfs_write(meta_file, backup_name, strlen(backup_name), &(meta_file->f_pos)); // Write the backup name
		set_fs(old_fs);	
		if (err < 0)
			goto out;
		err = 0;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = vfs_write(meta_file,"\n",strlen("\n"),&(meta_file->f_pos)); // Add a new line
		set_fs(old_fs);
		if (err < 0)
			goto out;
		err = 0;

		path_put(&meta_path);
	}else{ // If the file already exists
			
		meta_file = dentry_open(&meta_path, flags, current_cred());
		if (IS_ERR(meta_file)){
			err = PTR_ERR(meta_file); 
			goto out;
		}
		// Check if meta file already has max_versions
		if (  (meta_file->f_path.dentry->d_inode->i_size / (strlen(backup_name)+1) ) >= max_versions ){ // if max, remove	
			err = delete_version(meta_file, 1);

			if (err < 0)
				goto out;
			meta_file->f_pos = 0;	

			//oldest version is removed 	
			meta_size = meta_file->f_path.dentry->d_inode->i_size - (strlen(backup_name)+1);	

			meta_buffer = kmalloc( meta_size, GFP_KERNEL ); 
			err = negate_version(meta_file,1,&meta_buffer);
			vfs_truncate(&meta_file->f_path,0);

			meta_file->f_pos = 0;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = vfs_write(meta_file, meta_buffer, meta_size, &(meta_file->f_pos));
			set_fs(old_fs);	
			if (err < 0)
				goto out;
			err = 0;


			if (meta_buffer)
				kfree(meta_buffer);			
			
		}

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = vfs_write(meta_file,backup_name,strlen(backup_name),&(meta_file->f_pos));//Write the backup name
		set_fs(old_fs);		
		if (err < 0)
			goto out;
		err = 0;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = vfs_write(meta_file,"\n",strlen("\n"),&(meta_file->f_pos));//Add a new line
		set_fs(old_fs);		
		if (err < 0)
			goto out;
		err = 0;

		path_put(&meta_path);
	}
out:
	if (meta_file)
		fput(meta_file);
	if (meta_name)
		kfree(meta_name);
	if (meta_dentry)
		dput(meta_dentry);
	return err;
}

/**
 * write_to_file - This function is mainly used for write to backup file in ext4 (lower) fs
 *
 *@file: FILE * (original) to dentry in bkpfs (upper) fs
 *@dir: Inode to directory in ext4 (lower) fs
 *@mnt: mount point of ext4
 *@dst_dentry: Dentry for backup file in ext4 (lower) fs
 *
 *This function is called by backup_helper.
 *This function is called after create_file in backup_helper
 *
 */
int write_to_backup(struct file * file, struct inode *dir, struct vfsmount *mnt, struct dentry *dst_dentry)
{
	int err = 0;
	
	struct file * dst_file = NULL;
	struct file * src_file = NULL;
	struct path dst_path;
	struct path src_path; //Derieved from file * file;
	
	mm_segment_t old_fs;
	char * buffer = NULL;
	size_t bytes_left = 0;
	size_t bytes_read = 0;
	/* Use vfs_path_lookup to check if the dentry exists or not */
	err = vfs_path_lookup(dst_dentry->d_parent, mnt, dst_dentry->d_name.name , 0,&dst_path);
	if (err < 0)
		goto out;

	dst_file = dentry_open(&dst_path, O_WRONLY, current_cred());
	if (IS_ERR(dst_file)){
		err = PTR_ERR(dst_file); 
		goto out;
	}

	bkpfs_get_lower_path(file->f_path.dentry, &src_path);
	src_file = dentry_open(&src_path, O_RDONLY, current_cred());
	if (IS_ERR(src_file)){
		err = PTR_ERR(src_file); 
		goto out;
	}
	buffer = kzalloc(PAGE_SIZE,GFP_KERNEL);
	if(!buffer){
		err = -ENOMEM;
		goto out;
	}
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	bytes_left = src_file->f_path.dentry->d_inode->i_size;
	while (bytes_left != 0){
		bytes_read = bytes_left < PAGE_SIZE ? bytes_left : PAGE_SIZE;
		err = vfs_read(src_file, buffer, bytes_read, &(src_file->f_pos) );
		if (err < 0)
			break;
		err = vfs_write(dst_file,buffer,bytes_read,&(dst_file->f_pos));
		if (err < 0)
			break;
		bytes_left -= bytes_read;
	} 
	err = 0; // If everything goes well
	set_fs(old_fs);
out:

	if (dst_file)
		fput(dst_file);

	if (src_file)
		fput(src_file);

	if (buffer)
		kfree(buffer);

	path_put(&src_path);

	path_put(&dst_path);
	return err;
}




/**
 * create_file - This function is used to create a backup file in ext4 (lower) fs
 *
 *@dir: Inode to directory in ext4 (lower) fs
 *@mnt: mount point of ext4
 *@lower_dentry: Dentry for backup file in ext4 (lower) fs
 *@mode: mode of lower_dentry
 *@want_excl: 0
 *
 *This function is called by backup_helper.
 *This function is called after a negative dentry has been successfully created by create_negative_dentry 
 *
 */
int create_file(struct inode *dir, struct vfsmount *mnt, struct dentry *lower_dentry,umode_t mode, bool want_excl)
{
	int err;
	//struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;

	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_create(d_inode(lower_parent_dentry), lower_dentry, mode, want_excl);

	if (err)
		goto out;
out:
	unlock_dir(lower_parent_dentry);

	return err;
}


/**
 * create_negative_dentry - first function to be called in the creation of backup file in ext4 (lower) fs
 *
 *@dir_dentry: dentry of the directory in ext4 (lower) fs where we want our inode to reside
 *@name: name of the file we want the dentry for.
 *
 *this function is called by backup_helper.
 *entry point for creation if inode and file.
 *the format of backup file is usually bkpfs.filename.xxx where xxx is any number between 0 - 999
 *
 */
int create_negative_dentry (struct dentry * dir_dentry, char * name, struct dentry ** negative_dentry){
	struct qstr this;
	int err = 0;
	/* instatiate a new negative dentry */
	this.name = name;
	this.len = strlen(name);
	this.hash = full_name_hash(dir_dentry, this.name, this.len);
	*negative_dentry = d_lookup(dir_dentry, &this);
	*negative_dentry = d_alloc(dir_dentry, &this);
	d_add(*negative_dentry, NULL); /* instantiate and hash */
	
	return err;

}



