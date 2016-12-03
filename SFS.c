#include "sfs_api.h"
#include "disk_emu.h"
#include <stdio.h>
#include <string.h> 
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
 	

/*================define top-level info=================*/
#define FILENAME "MYSFS"
#define BLOCK_SIZE 1024
#define BLOCK_NUM 3000

#define DIR_TABLE_SIZE 299
#define OPEN_TABLE_SIZE 299

#define FILE_NAME_LIMIT 16
#define FILE_EXT_LIMIT 3
#define DIR_BLOCK_SIZE sizeof(dir)/1024

/*================define block info==================*/
// the total num is 3000, 299 files alowed
#define SUPER_BLOCK_NUM 1
#define INODE_TABLE_SIZE 300
#define DATA_BLOCK_SIZE 2698
#define FREE_BIT_MAP_SIZE 1

#define DIRECT_PTR_SIZE 12
#define INDIRECT_PTR_SIZE BLOCK_SIZE/sizeof(int)
//length of free_bit_map, unit is byte
#define FREE_BIT_MAP_LENGTH (DATA_BLOCK_SIZE + 7)/8
#define SUPER_BLOCK_START_ADDRESS 0
#define INODE_TABLE_START_ADDRESS SUPER_BLOCK_NUM
#define DATA_BLOCK_START_ADDRESS (INODE_TABLE_SIZE + INODE_TABLE_START_ADDRESS)
#define FREE_BIT_MAP_START_ADDRESS (DATA_BLOCK_SIZE + DATA_BLOCK_START_ADDRESS)

/*================data structure================*/

//this indirect pointer has 1024/4 pointers
typedef struct indirect_ptr_block{
	int ptr[INDIRECT_PTR_SIZE];
}indirect_ptr_block;

//one inode per block, it has a pointer to the data block
typedef struct inode{
	int block_index;
	int initialized; 			//1 for initialized, uninitialized otherwise
	int size;
	int link_cnt;
	/* an inode has 12 direct pointer, each points to a data block
	 * it also has a indirect pointer, point to a block
	 */
	int direct_ptr[DIRECT_PTR_SIZE];		
	int indirect_ptr;
}inode;

typedef struct super_block{
	int magic;
	int block_size;
	int file_system_size;	
	int inode_table_length;
	int root_inode_index;
} super_block;



//inode table
typedef struct inode_table{
	inode inodes[INODE_TABLE_SIZE];
}inode_table;

//directory entry, it stores the file name and the i-Node(we use inode index here)
typedef struct dir_entry{
	int initialized; 			//1 for initialized, uninitialized otherwise
	char file_name[FILE_NAME_LIMIT+1];
	char file_ext[FILE_EXT_LIMIT+1];
	int inode_index;
} dir_entry;

/*
 *directory table, it keeps a copy of the directory block in memory
 */
typedef struct dir{
	int entry_cnt;    //number of entry
	dir_entry files[DIR_TABLE_SIZE];	
}dir;

typedef struct open_fd_table_entry{
	int inode_number;
	int readptr;
	int writeprt;	
}open_fd_table_entry;

typedef struct open_fd_table{
	int entry_cnt;  //number of entry
	open_fd_table_entry files[OPEN_TABLE_SIZE];
}open_fd_table;




/*================In-memory variable================*/
inode_table it;   //inode table cache
dir* dir_cache;			//directory table cache
open_fd_table oft;						//open file table
unsigned char free_bit_map[FREE_BIT_MAP_LENGTH];

/*===================Helper method====================*/

/*================Update disk part================*/
int update_disk_inode(int inode_index){
	void* buffer = malloc(BLOCK_SIZE);
	memcpy(buffer, &it.inodes[inode_index], sizeof(inode));
	
	if (write_blocks(INODE_TABLE_START_ADDRESS + inode_index, 1, buffer)<0){
		free(buffer);
		return -1;
	}	
	free(buffer);
	return 0;
}

int update_disk_fbm(){
	void* buffer = malloc(BLOCK_SIZE);
	memcpy(buffer, free_bit_map, sizeof(free_bit_map));
	write_blocks(FREE_BIT_MAP_START_ADDRESS, 1, buffer);
	free(buffer);
	return 0;
}

int update_disk_directory(int dir_index){
	return 0;
}


//allocate a block in free bit map
int alloate_fbm(int block_index){
	//this is the specific index of fbm
	unsigned char map = free_bit_map[block_index/8];
	unsigned char mask = 128;
	int index = block_index%8;
	mask = mask>>index;
	map = map | mask;
	free_bit_map[block_index/8] = map;
	return 0;
}

//dellocate a block in free bit map
int deallocate_fbm(int block_index){
	int index = block_index%8;
	unsigned char map = free_bit_map[block_index/8];	
	unsigned char mask = 256 -1 - (int)pow(2.0, 7.0-index);
	map = map & mask;
	free_bit_map[block_index/8] = map;
	return 0;
}


/*================Initialization method================*/
//Initialize free_bit_map
void init_free_bit_map(){
	void* buffer = malloc(BLOCK_SIZE);
	memcpy(free_bit_map, buffer, sizeof(unsigned char)*FREE_BIT_MAP_LENGTH);
	write_blocks(FREE_BIT_MAP_START_ADDRESS,FREE_BIT_MAP_SIZE,buffer);	
	free(buffer);
	printf("Free bit map initialization complete\n");
}

//Initialize data blocks
void init_data_blocks(){
	void* db_buffer = (void*) malloc(BLOCK_SIZE);

	for (int i = 0; i < DATA_BLOCK_SIZE; i++){
		
		write_blocks(DATA_BLOCK_START_ADDRESS,1,db_buffer);
	}
	free(db_buffer);

	printf("Data blocks initilization complete\n");
}


//Initialize root directory
int init_root_dir(){
	dir rootDir;
	void* rootD = &rootDir;
	void* buffer = malloc(BLOCK_SIZE);
	for (int i=0; i < DIR_BLOCK_SIZE; i++){	
		memcpy(buffer,rootD+BLOCK_SIZE*i,BLOCK_SIZE);	
		it.inodes[0].direct_ptr[i] = i;
		write_blocks(DATA_BLOCK_START_ADDRESS+i,1,buffer);
		alloate_fbm(i);
	}
	free(buffer);
	/* now we have initialized th root directory
	 *next step is to point the dir_node to it*/
	update_disk_inode(0);
	update_disk_fbm();
	printf("root_directory initialization complete\n");
	printf("inode on disk updated\n");
	return 0;
}

//Initialize root directory cache
int inti_directory_cache(){
	dir_cache = (dir*) malloc(sizeof(dir));
	void* buffer = malloc(BLOCK_SIZE);
	for (int i = 0; i < DIR_BLOCK_SIZE; i++){
		read_blocks(DATA_BLOCK_START_ADDRESS+i,1,buffer);
		memcpy((void*)dir_cache + i*BLOCK_SIZE, buffer, BLOCK_SIZE);
	}
	free(buffer);
	printf("directory_cache initilization finished\n");
	return 0;
}

//initialize open file table
void init_oft(){
	oft.entry_cnt = 0;
	for(int i=0; i < OPEN_TABLE_SIZE; i++){
		oft.files[i].inode_number = 0;
	}
	printf("Open file descriptor table initilization complete\n");
}

//initiallize dir 

void partition(){
	/*====================Partition====================*/
	printf("Partition starts.....\n");

	super_block sb;
	void* buffer = malloc(BLOCK_SIZE);
	
	//buffer = &sb;
	//we need the inode pointing to root directory here,but data block is not initialized
	//write_blocks(SUPER_BLOCK_START_ADDRESS,SUPER_BLOCK_NUM,buffer);


	//Initialize inode table and write it to block
	for (int i = 0; i < INODE_TABLE_SIZE; i++){
		memcpy(buffer, &(it.inodes[i]), sizeof(inode));
		write_blocks(INODE_TABLE_START_ADDRESS+i,1,buffer);
	}
	printf("I-Node table intilization complete\n");

	//Initialize data blocks
	init_data_blocks();

	//Initialize free bit map
	init_free_bit_map();

	//Initialize super block
	
	//sb.magic = 1;
	sb.magic = (int)strtol("0*ACBD0005", NULL, 0);
	sb.block_size = BLOCK_SIZE;
	sb.file_system_size = BLOCK_NUM;
	sb.inode_table_length = BLOCK_NUM;
	sb.root_inode_index = 0;
	memcpy(buffer, &sb,BLOCK_SIZE);
	write_blocks(SUPER_BLOCK_START_ADDRESS,1,buffer);
	printf("super_block inilization complete\n");

	//Initialize root directory
	init_root_dir();
	free(buffer);

}

	// update the inode on the disk
	


void mksfs(int fresh){
	/*====================Initialize disk====================*/
	printf("Initialing disk......\n");
	int init_flag;
	if (fresh == 1){
		init_flag = init_fresh_disk(FILENAME, BLOCK_SIZE, BLOCK_NUM);
	}
	else{
		init_flag = init_disk(FILENAME, BLOCK_SIZE, BLOCK_NUM);
	}

	if (init_flag == -1){
		printf("Initializing failed....\n");
		printf("Exiting.....\n");
		return;
	}
	if (fresh ==1){
		partition();
	}

	inti_directory_cache();
	init_oft();

	/*used to test free bit map
	void* buffer = malloc(BLOCK_SIZE);
    read_blocks(FREE_BIT_MAP_START_ADDRESS,1,buffer);
    unsigned char* a;
    a = (unsigned char*)buffer;
    free(buffer);
    printf("%u\n",a[0] );
    printf("%u\n",a[1] );
    printf("%u\n",a[2] );
    */
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
