#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "bkpctl.h"
#include <linux/ioctl.h>
#define VIEW_BKPFS _IOWR('a','a',struct args*)
#define LIST_BKPFS _IOWR('a','b',struct args*)
#define DELETE_BKPFS _IOWR('a','c',struct args*)
#define RESTORE_BKPFS _IOWR('a','d',struct args*)



int main(int argc, char ** argv)
{
        int fd;
        int number;
	struct args * user_struct;
	int err = 0;

 
	user_struct = malloc(sizeof(struct args));
	if (argc == 3 ){
		

		// Allocate and initialize 
		user_struct->filename = malloc(strlen(argv[argc-1])+1);
		if(!user_struct->filename)
			goto out; 
		memset(user_struct->filename,'\0',strlen(argv[argc-1])+1);
		strcpy(user_struct->filename, argv[argc-1]);	

		//Plug in string length
		user_struct->filename_length = strlen(argv[argc-1])+1;

		//Allocate for buffer
		user_struct->buffer = malloc(4096);
		if (!user_struct)
			goto out;
		memset(user_struct->buffer,'\0',4096);
		if (strcmp(argv[1],"-l")==0){ 

			fd = open(user_struct->filename,O_RDWR);

			if(fd < 0) 
				goto out;
		
			err = ioctl(fd, LIST_BKPFS, user_struct);

			if (err!=0)
				goto out;
		}
		printf("Listing Backup Files: \n%s\n",user_struct->buffer);



	}
	else if (argc == 4){
		//Allocate and initialize filename
		user_struct->filename = malloc(strlen(argv[argc-1])+1);
		memset(user_struct->filename,'\0',strlen(argv[argc-1])+1);
		strcpy(user_struct->filename,argv[argc-1]);
		
		//Plug in length name
		user_struct->filename_length = strlen(argv[argc-1])+1;

		//Allocate buffer
		user_struct->buffer = malloc(4096);
		memset(user_struct->buffer,'\0',4096);
		
		//Get version
		sscanf (argv[argc-2],"%d",&user_struct->version);

		if (user_struct->version < 0 ){
			err = -2;
			goto out;
		}

		if (strcmp(argv[argc-2],"newest") == 0)
			user_struct->version = -1;
		else if (strcmp(argv[argc-2],"oldest") == 0)
			user_struct->version = 1;


		if (strcmp(argv[argc-3],"-v")==0){
			fd = open(user_struct->filename,O_RDWR);
			printf("FD : %d\n",fd);
			if(fd < 0) 
				goto out;

			err = ioctl(fd, VIEW_BKPFS, user_struct);

			if (err < 0)
				goto out;
			printf("View Contents: \n%s\n",user_struct->buffer);
		}else if (strcmp(argv[argc-3],"-d")==0){

			fd = open(user_struct->filename,O_RDWR);

			if(fd < 0) 
				goto out;

			if (strcmp(argv[argc-2],"all") == 0)
				user_struct->version = -2;
			err = ioctl(fd, DELETE_BKPFS, user_struct);

			if (err!=0)
				goto out;
		}else if (strcmp(argv[argc-3],"-r")==0){

			fd = open(user_struct->filename,O_RDWR);

			if(fd < 0) 
				goto out;

			err = ioctl(fd, RESTORE_BKPFS, user_struct);

			if (err!=0)
				goto out;
		}
	}else{
		err = -2;
		goto out;
	}

out:	

	if (err < 0){
		print_help();
	}

	if (fd > 0)
		close(fd);
	if (user_struct->buffer)
		free(user_struct->buffer);
	if (user_struct->filename)
		free(user_struct->filename);
	if (user_struct)
		free(user_struct);	

}

void print_help(){
	printf("Usage:\n");
	printf("./bkpctl -[ld:v:r:] FILE\n");
	printf("FILE: the file's name to operate on\n");
	printf("-l: option to 'list versions'\n");
	printf("-d ARG: option to 'delete' versions; ARG can be 'newest', 'oldest', or 'all'\n");
	printf("-v ARG: option to 'view' contents of versions (ARG: 'newest', 'oldest', or N)\n");
	printf("-r ARG: option to 'restore' file (ARG: 'newest' or N)\n");
	printf("(where N is a number such as 1, 2, 3, ...)\n");
}
