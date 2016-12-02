#include "sfs_api.h"
#include "disk_emu.h"


void partition(){
	/*====================Partition====================*/
	printf("Partition starts.....\n");

	//Initialize super block
	super_block sb;
	void *buffer;
	
	sb->magic = (int)strtol("0*ACBD0005", NULL, 0)
	sb->block_size = BLOCK_SIZE;
	sb->file_system_size = BLOCK_NUM;
	sb->inode_table_length = BLOCK_NUM;

	buffer = &sb;
	//we need the inode pointing to root directory here,but data block is not initialized
	write_blocks(SUPER_BLOCK_START_ADDRESS,SUPER_BLOCK_SIZE,buffer);


	//Initialize inode table and write it to block
	for (int i = 0; i < INODE_TABLE_SIZE; i++){
		buffer = &it->inodes[i];
		write_blocks(INODE_TABLE_START_ADDRESS+i,1,buffer);
	}
	printf("I-Node table intilization complete\n");

	//Initialize data blocks
	void* db_buffer = (void*) malloc(BLOCK_SIZE);
	for (int i = 0; i < DATA_BLOCK_SIZE; i++){
		
		write_blocks(DATA_BLOCK_START_ADDRESS,1,db_buffer);
	}
	free(db_buffer);

	printf("Data blocks initilization complete");

	//Initialize free bit map


}

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
	if (fresh !=1){
		partition();
	}
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
