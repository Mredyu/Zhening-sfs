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

#define FILE_NAME_LIMIT (16 + 1)
#define FILE_EXT_LIMIT (3 + 1)
#define DIR_BLOCK_SIZE (sizeof(dir)+1023)/1024

/*================define block info==================*/
// the total num is 3000, 299 files alowed
#define SUPER_BLOCK_NUM 1
#define INODE_TABLE_SIZE 200
#define DATA_BLOCK_SIZE (2698 -DIR_BLOCK_SIZE)
#define FREE_BIT_MAP_SIZE 1

#define DIRECT_PTR_SIZE 12
#define INDIRECT_PTR_SIZE BLOCK_SIZE/sizeof(int)
#define FILE_BLOCK_LIMIT (DIRECT_PTR_SIZE + INDIRECT_PTR_SIZE)
//length of free_bit_map, unit is byte
#define FREE_BIT_MAP_LENGTH (DATA_BLOCK_SIZE+ 7)/8
#define SUPER_BLOCK_START_ADDRESS 0
#define INODE_TABLE_START_ADDRESS SUPER_BLOCK_NUM
#define ROOT_DIRCETORY_START_ADDRESS (INODE_TABLE_SIZE + INODE_TABLE_START_ADDRESS)
#define DATA_BLOCK_START_ADDRESS (ROOT_DIRCETORY_START_ADDRESS + DIR_BLOCK_SIZE)
#define FREE_BIT_MAP_START_ADDRESS (DATA_BLOCK_SIZE  + DATA_BLOCK_START_ADDRESS)

/*================data structure================*/


//this indirect pointer has 1024/4 pointers
typedef struct indirect_ptr_block{
	int ptr[INDIRECT_PTR_SIZE];
}indirect_ptr_block;


//one inode per block, it has a pointer to the data block
typedef struct inode{
	int initialized; 			//1 for initialized, uninitialized otherwise
	int size;
	int link_cnt;
	int block_cnt;
	/* an inode has 12 direct pointer, each points to a data block
	 * it also has a indirect pointer, this points to 1024/4 blocks
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
	inode inodes[INODE_TABLE_SIZE-1];
}inode_table;

//directory entry, it stores the file name and the i-Node(we use inode index here)
typedef struct dir_entry{
	int initialized; 			//1 for initialized, uninitialized otherwise
	char file_name[FILE_NAME_LIMIT];
	char file_ext[FILE_EXT_LIMIT];
	int inode_index;
	int visited;          //1 for visited, unvisited otherwise
} dir_entry;

/*
 *directory table, it keeps a copy of the directory block in memory
 */
typedef struct dir{
	int entry_cnt;    //number of entry
	dir_entry files[DIR_TABLE_SIZE];

	int locator;	//used to locate which entry we are on
}dir;

typedef struct open_fd_table_entry{
	int inode_number;
	int readptr;
	int writeptr;
	int initialized;	//1 for initialized, uninitialized otherwise
}open_fd_table_entry;

typedef struct open_fd_table{
	int entry_cnt;  //number of entry
	open_fd_table_entry files[OPEN_TABLE_SIZE];
}open_fd_table;




/*================In-memory variable================*/
inode_table it;   //inode table cache
dir* dir_cache;			//directory table cache
open_fd_table oft;						//open file table
unsigned char free_bit_map[FREE_BIT_MAP_SIZE];

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
	if (write_blocks(FREE_BIT_MAP_START_ADDRESS, 1, buffer)<0){
		free(buffer);
		return -1;
	}
	free(buffer);
	return 0;
}

int update_disk_directory(){
	void* rootD = (void*)dir_cache;
	void* buffer = malloc(BLOCK_SIZE);
	for (int i=0; i < DIR_BLOCK_SIZE; i++){
		memcpy(buffer,rootD+BLOCK_SIZE*i,BLOCK_SIZE);
		write_blocks(ROOT_DIRCETORY_START_ADDRESS+i,1,buffer);
	}
	free(buffer);
	return 0;
}


void update_disk_ip(inode current_inode,indirect_ptr_block ip){
	void* buffer = malloc(BLOCK_SIZE);
	memcpy(buffer,&(ip),sizeof(indirect_ptr_block));
	write_blocks(current_inode.indirect_ptr,1,buffer);
	free(buffer);
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
	memcpy(free_bit_map, buffer, sizeof(unsigned char)*FREE_BIT_MAP_SIZE);
	write_blocks(FREE_BIT_MAP_START_ADDRESS,FREE_BIT_MAP_SIZE,buffer);
	free(buffer);
}

//Initialize data blocks
void init_data_blocks(){
	void* db_buffer = (void*) malloc(BLOCK_SIZE);

	for (int i = 0; i < DATA_BLOCK_SIZE; i++){
		write_blocks(DATA_BLOCK_START_ADDRESS,1,db_buffer);
	}
	free(db_buffer);
}


//Initialize root directory
int init_root_dir(){
	dir rootDir;
	void* rootD = &rootDir;
	void* buffer = malloc(BLOCK_SIZE);
	for (int i=0; i < DIR_BLOCK_SIZE; i++){
		memcpy(buffer,rootD+BLOCK_SIZE*i,BLOCK_SIZE);
		it.inodes[0].direct_ptr[i] = i;
		write_blocks(ROOT_DIRCETORY_START_ADDRESS+i,1,buffer);
	}
	free(buffer);
	/* now we have initialized th root directory
	 *next step is to point the dir_node to it*/
	update_disk_inode(0);
	update_disk_fbm();
	return 0;
}

//Initialize root directory cache
int init_directory_cache(){
	void* buffer = malloc(BLOCK_SIZE);
	for (int i = 0; i < DIR_BLOCK_SIZE -1; i++){
		read_blocks(ROOT_DIRCETORY_START_ADDRESS+i,1,buffer);
		memcpy((void*)dir_cache + i*BLOCK_SIZE, buffer, BLOCK_SIZE);
	}
	read_blocks(ROOT_DIRCETORY_START_ADDRESS+(DIR_BLOCK_SIZE-1),
		1,buffer);
	memcpy((void*)dir_cache + (DIR_BLOCK_SIZE-1)*BLOCK_SIZE,
		buffer, sizeof(dir_cache)%1024);
	free(buffer);
	return 0;
}

//initialize open file table
void init_oft(){
	oft.entry_cnt = 0;
	for(int i=0; i < OPEN_TABLE_SIZE; i++){
		oft.files[i].inode_number = 0;
	}
	//printf("Open file descriptor table initilization complete\n");
}

//Initialize inode table and write it to block
void init_inode_table(){
	it.inodes[0].initialized = 1;
	it.inodes[0].link_cnt = 1;
	inode_table new_table;
	void* buffer = malloc(BLOCK_SIZE);
	for (int i = 1; i < INODE_TABLE_SIZE; i++){
		indirect_ptr_block ip_blk;
		memcpy(buffer,&(ip_blk),sizeof(indirect_ptr_block));
		write_blocks(DATA_BLOCK_START_ADDRESS+i,1,buffer);
		new_table.inodes[i].indirect_ptr = DATA_BLOCK_START_ADDRESS+i;
		memcpy(buffer, &(new_table.inodes[i]), sizeof(inode));
		write_blocks(INODE_TABLE_START_ADDRESS+i,1,buffer);
		alloate_fbm(i);
		update_disk_fbm();
	}

	free(buffer);
	//printf("INODE_TABLE_START_ADDRESS is %d\n",INODE_TABLE_START_ADDRESS );
	//printf("I-Node table intilization complete\n");
}

//initialize super block and write it to block
void init_super_block(){
	super_block sb;
	void* buffer = malloc(BLOCK_SIZE);
	sb.magic = (int)strtol("0*ACBD0005", NULL, 0);
	sb.block_size = BLOCK_SIZE;
	sb.file_system_size = BLOCK_NUM;
	sb.inode_table_length = BLOCK_NUM;
	sb.root_inode_index = 0;
	memcpy(buffer, &sb,BLOCK_SIZE);
	write_blocks(SUPER_BLOCK_START_ADDRESS,1,buffer);
	//printf("super_block inilization complete\n");

}

void partition(){
	/*====================Partition====================*/
	//printf("Partition starts.....\n");


	//Initialize data blocks
	init_data_blocks();

	//Initialize free bit map
	init_free_bit_map();

	//Initialize root directory
	init_root_dir();

	//Initialize super block
	init_super_block();

	//Initialize inode table and write it to block
	init_inode_table();
}

//mark a entry on cache as intialized, return 1 on success
int mark_entry(int entry_index){
	if ((dir_cache->files[entry_index]).initialized == 0){
		(dir_cache->files[entry_index]).initialized = 1;
		return 1;
	}
	//printf("This block has been initialized\n");
	return -1;
}


//Find the free block index.On no free block left, return -1
int find_free_block(){
	for (int i = 0; i<FREE_BIT_MAP_LENGTH; i++){
		unsigned char map = free_bit_map[i];
		/*
		 *If map = 255, it means all blocks has been allocated
		 *so we move to next iteration
		 */
		if (map == 255){
			continue;
		}
		unsigned char mask = 128;
		for (int j = 0; j<8; j++){
			if ((map & mask) == 0){
				//found empty block
				return 8*i+j;
			}
			mask = mask>>1;
		}
	}
	printf("Disk full.\n");
	return -1;
}

//find an empty dir entry and return its index
//return -1 if disk is full, return -2 if all inode has been allocated
int find_unallocated_dir_entry(){
	if (dir_cache->entry_cnt == DIR_TABLE_SIZE){
		printf("Diks is full\n");
		return -1;
	}
	for (int i =0; i<DIR_TABLE_SIZE; i++){
		if ((dir_cache->files[i]).initialized != 1){
			return i;
		}
	}
	return -2;
}

//Find an empty inode in inode table
//return -1 if no empty inode
int find_empty_inode(){
	for (int i = 1; i< INODE_TABLE_SIZE; i++){
		if(it.inodes[i].initialized != 1){
			return i;
		}
	}
	return -1;
}

//put everything in the inode table on disk into cache
void setup_inode_table_cache(){
	void* buffer = malloc(BLOCK_SIZE);
	for (int i = 0; i < INODE_TABLE_SIZE; i++){
		read_blocks(INODE_TABLE_START_ADDRESS + i,1,buffer);
		memcpy(&(it.inodes[i]), buffer, sizeof(inode));
	}
	free(buffer);
}



/*
 *find a file in directory cache,retun the entry index
 *if not found return -1;
 */
int find_file(char *input){
	//printf("dir is %d\n",dir_cache->entry_cnt);
	int cnt = 0;
	char* file_name = (char*) malloc(sizeof(char)*
					(FILE_EXT_LIMIT + FILE_NAME_LIMIT));
	for (int i = 0; i < DIR_TABLE_SIZE; i++){
		if(cnt > dir_cache->entry_cnt){
			return -1;
		}
		if (dir_cache->files[i].initialized == 0) continue;
		strcpy(file_name, dir_cache->files[i].file_name);
		strcat(file_name, ".");
		strcat(file_name, dir_cache->files[i].file_ext);
		if (strcmp(file_name, input) == 0){
			free(file_name);
			return i;
		}
		cnt++;
	}
	free(file_name);
	return -1;
}

int divide_name(char* input_name, char* fname, char* fext){
	char* name = (char*) malloc(sizeof(char)*(FILE_NAME_LIMIT + FILE_EXT_LIMIT));
	strcpy(name, input_name);
	char* token;
	token = strtok (name, ".");
	strcpy(fname, token);
	token = strtok(NULL, ".");
	strcpy(fext, token);
	free(name);
	return 1;
}

//create a file, return the entry index of directory table
int create_file(char *fname, char *fext){
	int entry_index = find_unallocated_dir_entry();
	int inode_index = find_empty_inode();
	if ((inode_index < 0) || (entry_index < 0)){
		return -1;
	}

	//Allocate and initialize an inode, file size is set to 0
	it.inodes[inode_index].initialized = 1;
	it.inodes[inode_index].size = 0;
	it.inodes[inode_index].link_cnt = 1;
	it.inodes[inode_index].block_cnt = 0;

	//write the mapping between INODE and file name in root directory
	dir_cache->entry_cnt++;
	dir_cache->files[entry_index].initialized = 1;
	dir_cache->files[entry_index].inode_index = inode_index;
	dir_cache->files[entry_index].visited = 0;
	strcpy(dir_cache->files[entry_index].file_name,fname);
	strcpy(dir_cache->files[entry_index].file_ext,fext);

	//update the inode and directory on disk;
	update_disk_inode(inode_index);
	update_disk_directory();
	return entry_index;
}


void mksfs(int fresh){
	/*====================Initialize disk====================*/
	//printf("Initialing disk......\n");
	dir_cache = (dir*) malloc(sizeof(dir));
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

	init_directory_cache();
 	init_oft();
	setup_inode_table_cache();


}

//works fine
int sfs_get_next_file_name(char *fname){
  if (dir_cache->entry_cnt == 0){
  	printf("The dir is empty\n");
  	return 0;
  }
  if ((dir_cache->locator) >= dir_cache->entry_cnt){
  	printf("No more entries in dir,end of directory\n");
  	printf("Reset locator\n");
  	dir_cache->locator = 0;

  	int file_cnt = 0;
  	for (int i=0; i <= DIR_TABLE_SIZE; i ++){
  		if (dir_cache->files[i].visited == 1){
  			dir_cache->files[i].visited = 0;
  			file_cnt ++;
  			update_disk_directory();
  			if(file_cnt >= dir_cache->entry_cnt){
  				break;
  			}
  		}
  		
  	}
  	update_disk_directory();
  	return 0;		
  }
  else{
  	int file_index;
  	for(file_index=0; file_index<dir_cache->entry_cnt;file_index++){
  		if(dir_cache->files[file_index].visited == 1){
  			continue;
  		}
  		break;
  	}
  	strcpy(fname, dir_cache->files[file_index].file_name);
	strcat(fname, ".");
	strcat(fname, dir_cache->files[file_index].file_ext);
	dir_cache->files[file_index].visited = 1;
	dir_cache->locator += 1;
	update_disk_directory();
	//no need to write iterator & visited info into disk.
	return file_index;
  }


  return 0;
}

int sfs_get_file_size(char* path){
	int file_index = find_file(path);
	if (file_index == -1){
		printf("No such file in get_file_size step\n");
		return -1;
	}

	int inode_index = dir_cache->files[file_index].inode_index;
	return it.inodes[inode_index].size;
}


int sfs_fopen(char *name){
	if (oft.entry_cnt == OPEN_TABLE_SIZE){
		return -1;
	}

	char fname[FILE_NAME_LIMIT];
	char fext[FILE_EXT_LIMIT];
	divide_name(name,fname,fext);
	int entry_index = find_file(name);
	if (entry_index == -1){
		entry_index = create_file(fname, fext);
		if (entry_index<0) return -1;
	}
	// add it to open file descriptor table
	int inode_index = dir_cache->files[entry_index].inode_index;
	oft.files[entry_index].initialized = 1;
	oft.files[entry_index].inode_number = inode_index;
	oft.files[entry_index].readptr = 0;
	oft.files[entry_index].writeptr = it.inodes[inode_index].size;
	oft.entry_cnt++;
	//printf("direct_cnt is %d\n",dir_cache->entry_cnt);
	printf("oft cnt is %d\n",oft.entry_cnt);
  	return entry_index;
}


int sfs_fclose(int fileID){
	printf("Stat of fclose\n");
	if (fileID<0 || fileID>=OPEN_TABLE_SIZE){
		printf("invaild id\n");
		return -1;
	}

	if (oft.files[fileID].initialized == 0){
		printf("File is not initialized\n");
		return -1;
	}

	//reset everything in the files
	oft.files[fileID].initialized = 0;
	oft.files[fileID].inode_number = 0;
	oft.files[fileID].readptr = 0;
	oft.files[fileID].writeptr = 0;
	oft.entry_cnt--;
	return 0;
}

int sfs_frseek(int fileID, int loc){
	if (fileID<0 || fileID>=OPEN_TABLE_SIZE){
		printf("Invalid fileID\n");
		return -1;
	}

	if (oft.files[fileID].initialized == 0){
		printf("Not initialized\n");
		return -1;
	}
	int inode_index = oft.files[fileID].inode_number;

	if (loc < 0 || loc > it.inodes[inode_index].size){
		return -1;
	}
	else{
		oft.files[fileID].readptr = loc;
		return 0;
	}
}

int sfs_fwseek(int fileID, int loc){
  	if (fileID<0 || fileID>=OPEN_TABLE_SIZE){
		return -1;
	}

	if (oft.files[fileID].initialized == 0){
		return -1;
	}
	int inode_index = oft.files[fileID].inode_number;

	if (loc < 0 || loc > it.inodes[inode_index].size){
		return -1;
	}
	else{
		oft.files[fileID].writeptr = loc;
		return 0;
	}
}


int sfs_fwrite(int fileID, char *buf, int length){

	int write_ptr = oft.files[fileID].writeptr;
	int offset = write_ptr % BLOCK_SIZE;	//the offset the write pointer is on
	int current_blk = write_ptr/BLOCK_SIZE;  // the i-th block the write pointer is on

	if (fileID<0 || fileID>=OPEN_TABLE_SIZE){
		return -1;
	}
	if (oft.files[fileID].initialized == 0){
		return -1;
	}
	if (length < 0){
		printf("Not valid length\n");
		return -1;
	}
	if (length + write_ptr > FILE_BLOCK_LIMIT*BLOCK_SIZE){
		return -1;
		printf("Exceed file size limit\n");
	}

	int inode_index = oft.files[fileID].inode_number;
	//inode current_inode = it.inodes[inode_index];
	void* ptr_buffer = malloc(BLOCK_SIZE);
	read_blocks(it.inodes[inode_index].indirect_ptr,1,ptr_buffer);
	indirect_ptr_block ip;
	memcpy(&ip,ptr_buffer,sizeof(indirect_ptr_block));
	printf("the file to be written on has block number %d\n", it.inodes[inode_index].block_cnt);

	//first, find the number of blocks of this file
	//int old_block_num = it.inodes[inode_index].block_cnt;

	//if we want to write more than this file size, set the file size
	//change the write pointer of it
	if(write_ptr > it.inodes[inode_index].size){
		printf("writeptr too long\n");
		write_ptr = it.inodes[inode_index].size;
		oft.files[fileID].writeptr = length;
	}

	if(length + write_ptr > it.inodes[inode_index].size ){
		it.inodes[inode_index].size = length + write_ptr;
		oft.files[fileID].writeptr = it.inodes[inode_index].size;
	}
	//else just change the write pointer
	else{
		oft.files[fileID].writeptr += length;
	}
	update_disk_inode(inode_index);
	int block_num = (it.inodes[inode_index].size+1023)/1024; // the total number of blocks we need to rewrite

	block_num = block_num-current_blk;
	int write_block = (length+offset+1023)/1024;

	//we need to ensure the file has enough blocks
	int size = it.inodes[inode_index].size/1024 + 1;
	for (int i=0; i<size;i++){
		if ((i)<=11){
			if (it.inodes[inode_index].direct_ptr[i] == 0){
				int block_index = find_free_block();
				if(block_index == -1){
					return -1;
				}

				it.inodes[inode_index].direct_ptr[i] = block_index + DATA_BLOCK_START_ADDRESS;
				printf("one more direct block added\n");
				update_disk_inode(inode_index);
				alloate_fbm(block_index);
				update_disk_fbm();
			}
		}
		if ((i > 11)){
			if (ip.ptr[i-12] == 0){
				int block_index = find_free_block();
				if(block_index == -1){
					return -1;
				}
				ip.ptr[i-12] = block_index + DATA_BLOCK_START_ADDRESS;
				printf("one more indirect block added\n");
				update_disk_inode(inode_index);
				alloate_fbm(block_index);
				alloate_fbm(block_index);
				update_disk_fbm();
				update_disk_ip(it.inodes[inode_index],ip);
			}
		}
	}
	update_disk_inode(inode_index);

	int start_address;

	start_address = get_start_address(current_blk,it.inodes[inode_index]);
	
	printf("block num to write is %d\n",write_block );
	// if block num == 0 then we only need to rewrite this one
	if(write_block == 1){
		//first, write to the first block
		void* buffer = malloc(BLOCK_SIZE);
		read_blocks(start_address, 1,buffer);
		memcpy(buffer+offset,buf,length);
		write_blocks(start_address,1,buffer);
		free(buffer);
		update_disk_inode(inode_index);
		return length;
	}

	//first, write to the first block
	void* buffer = malloc(BLOCK_SIZE);
	read_blocks(start_address, 1,buffer);
	memcpy(buffer+offset,buf,(BLOCK_SIZE-offset));
	buf += (BLOCK_SIZE-offset);
	write_blocks(start_address,1,buffer);
	current_blk ++;
	//fill blocks except last and first
	if(write_block > 2){
		for (int i=0; i<(write_block-2);i++){
			start_address = get_start_address(current_blk,it.inodes[inode_index]);
			read_blocks(start_address,1,buffer);
			memcpy(buffer, buf, BLOCK_SIZE);
			buf += BLOCK_SIZE;
			write_blocks(start_address,1,buffer);				
			current_blk ++;
		}
	}

	//fill last block
	printf("The last block is %d, we have in total %d blocks\n",current_blk, it.inodes[inode_index].block_cnt );
	start_address = get_start_address(current_blk,it.inodes[inode_index]);
	read_blocks(start_address,1,buffer);
	memcpy(buffer, buf, (length + write_ptr)%1024);
	write_blocks(start_address,1,buffer);
	update_disk_inode(inode_index);
	update_disk_fbm();
	return length;
}

int sfs_fread(int fileID, char *buf, int length){
	int read_ptr = oft.files[fileID].readptr;
	int offset = read_ptr % BLOCK_SIZE;		//the offset the read pointer is on
	int current_blk = read_ptr/BLOCK_SIZE;  // the i-th block the read pointer is on
	

	if (fileID<0 || fileID>=OPEN_TABLE_SIZE){
		return -1;
	}
	if (oft.files[fileID].initialized == 0){
		return -1;
	}
	if (length < 0){
		printf("Not valid length\n");
		return -1;
	}
	if (length + read_ptr > FILE_BLOCK_LIMIT*BLOCK_SIZE){
		printf("Exceed file size limit\n");
		return -1;
	}
	
	int inode_index = oft.files[fileID].inode_number;
	if (read_ptr > it.inodes[inode_index].size ){
		read_ptr = it.inodes[inode_index].size;
		oft.files[fileID].readptr = read_ptr;
	}
	if (it.inodes[inode_index].size < (read_ptr + length)){
		length = it.inodes[inode_index].size  - read_ptr;
		oft.files[fileID].readptr = it.inodes[inode_index].size ;
	}
	else{
		oft.files[fileID].readptr += length;
	}


	int block_num = (length + offset )/1024 + 1	;
	int start_address;

	start_address = get_start_address(current_blk,it.inodes[inode_index]);


	if (block_num == 0){
		return -1;
	}
	// if block num == 1 then we only need to read this one
	if(block_num == 1){
		printf("Only one block to read\n");
		void* buffer = malloc(BLOCK_SIZE);
		read_blocks(start_address,1,buffer);
		memcpy(buf,buffer+offset,(length));
		free(buffer);
		return length;
	}
	printf("now read first block %d\n",current_blk);
	void* buffer = malloc(BLOCK_SIZE);
	read_blocks(start_address,1,buffer);
	memcpy(buf,buffer+offset,(BLOCK_SIZE-offset));
	buf += (BLOCK_SIZE-offset);
	current_blk ++;
	//fill blocks except last and first
	if(block_num > 2){
		
		for (int i=0; i<(block_num-2);i++){
			start_address = get_start_address(current_blk,it.inodes[inode_index]);
			read_blocks(start_address,1,buffer);
			memcpy(buf, buffer, BLOCK_SIZE);
			buf += BLOCK_SIZE;
			printf("reading current_blk number %d\n",current_blk );
			printf("start_address is %d\n", start_address);
			current_blk ++;
		}
	}
	printf("read last blk number %d\n",current_blk);
	//fill last block
	start_address = get_start_address(current_blk,it.inodes[inode_index]);
	read_blocks(start_address,1,buffer);
	memcpy(buf, buffer, (length + read_ptr)%1024);
	free(buffer);
	return length;
}

int sfs_remove(char *file){
	int file_ID = find_file(file);
	if (file_ID == -1){
		printf("File %s not found........\n", file);
		return -1;
	}
	int inode_index = dir_cache->files[file_ID].inode_index;
	dir_cache->files[file_ID].initialized = 0;
	dir_cache->files[file_ID].inode_index = 0;
	dir_cache->files[file_ID].visited = 0;
	dir_cache->entry_cnt--;

	void* buffer = malloc(sizeof(BLOCK_SIZE));
	int blk_size = it.inodes[inode_index].block_cnt;

	int blk_index;
	for(int i = 0; i<blk_size; i++){
		if ( i<= 11){
			blk_index = it.inodes[inode_index].direct_ptr[i];
		}
		else if(i>11){
			blk_index = get_start_address(i,it.inodes[inode_index]);
		}
		write_blocks(blk_index,1,buffer);

		deallocate_fbm(blk_index - DATA_BLOCK_START_ADDRESS);
	}
	//reset inode
	it.inodes[inode_index].initialized = 0;
	it.inodes[inode_index].size = 0;
	it.inodes[inode_index].link_cnt =0;
	it.inodes[inode_index].block_cnt = 0;

	void* ptr_buffer = malloc(BLOCK_SIZE);
	read_blocks(it.inodes[inode_index].indirect_ptr,1,ptr_buffer);
	indirect_ptr_block ip;
	memcpy(&ip,ptr_buffer,sizeof(indirect_ptr_block));

	for(int i=0; i< INDIRECT_PTR_SIZE; i++){
		ip.ptr[i] = 0;
	}
	update_disk_ip(it.inodes[inode_index],ip);

	
	//remove from file open table
	inode_index = oft.files[file_ID].inode_number;

	if (inode_index != 0){
		oft.files[file_ID].inode_number = 0;
		oft.files[file_ID].readptr = 0;
		oft.files[file_ID].writeptr = 0;
		oft.files[file_ID].initialized = 0;
		oft.entry_cnt --;
	}
	free(buffer);

	update_disk_inode(inode_index);
	update_disk_directory();
	update_disk_fbm();
	return 0;
}

//return the start address in the inode of position current_blk
int get_start_address(int current_blk, inode current_inode){
	//direct ptr
	if (current_blk <=11 ){
		return current_inode.direct_ptr[current_blk];
	}
	//indirect ptr
	if(current_blk >11){
		int address = current_inode.indirect_ptr;
		void* buffer = malloc(BLOCK_SIZE);
		read_blocks(address,1,buffer);
		indirect_ptr_block ip;
		memcpy(&ip,buffer,sizeof(indirect_ptr_block));
		return ip.ptr[current_blk-12];
	}
	return -1;
}