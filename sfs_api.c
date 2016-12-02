#include "sfs_api.h"
#include "disk_emu.h"


void mksfs(int fresh){
	/*====================Initialize disk====================*/
	printf("Initialing disk......\n");
	int init_flag;
	if (fresh == 1){
		init_flag = init_fresh_disk(FILENAME, BLOCK_SIZE, BLOCK_NUMBER);
	}
	else{
		init_flag = init_disk(FILENAME, BLOCK_SIZE, BLOCK_NUM);
	}

	if (init_flag == -1){
		printf("Initializing failed....\n");
		printf("Exiting.....\n");
		return;
	}

	/*====================Partition====================*/
	
}
int sfs_get_next_file_name(char *fname){
  return 0;
}
int sfs_get_file_size(char* path){
  return 0;
}
int sfs_fopen(char *name){
  return 0;
}
int sfs_fclose(int fileID){
  return 0;
}
int sfs_frseek(int fileID, int loc){
  return 0;
}
int sfs_fwseek(int fileID, int loc){
  return 0;
}
int sfs_fwrite(int fileID, char *buf, int length){
  return 0;
}
int sfs_fread(int fileID, char *buf, int length){
  return 0;
}
int sfs_remove(char *file){
  return 0;
}
