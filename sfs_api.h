//Functions you should implement. 
//Return -1 for error besides mksfs

void mksfs(int fresh);
int sfs_get_next_file_name(char *fname);
int sfs_get_file_size(char* path);
int sfs_fopen(char *name);
int sfs_fclose(int fileID);
int sfs_frseek(int fileID, int loc);
int sfs_fwseek(int fileID, int loc);
int sfs_fwrite(int fileID, char *buf, int length);
int sfs_fread(int fileID, char *buf, int length);
int sfs_remove(char *file);

/*====================Disk====================*/
int update_disk_inode(int inode_index); 	//update inode on disk
int update_disk_fbm();						//update free bit map on disk
int update_disk_directory();				//update disk directory on disk
int alloate_fbm(int block_index);			//mark one block as allocated
int deallocate_fbm(int block_index);		//mark one block as free 
void init_free_bit_map();					//initialize the free bit map, write it to disk
void init_data_blocks();					//initialize the data blocks, write them to disk
int init_root_dir();						//initialize the root directory, write it to disk
void init_inode_table();					//initialize the inode table, write it to disk
void init_super_block();					//initialize the super block, write it to disk


/*====================Memory====================*/
int inti_directory_cache();					//initialize the root directory cache, it's in memory
void init_oft();							//initialize the open file table, it's in memory
int mark_entry(int entry_index);			//mark a entry as intialized, return 1 on success
void setup_inode_table_cache();				//put everything in the inode table on disk into cache
/*
 *partition the disk to superblock, inode table, data blocks and free bit map
 */
void partition();							

	
/*
 *find an empty dir entry and return its index
 *return -1 if disk is full, return -2 if all inode has been allocated
 */
int find_unallocated_dir_entry();

//Find the free block index.On no free block left, return -1
int find_free_block();	

//Find an empty inode in inode table
//return -1 if no empty inode
int find_empty_inode();


/*
 *find a file in directory cache,retun the entry index
 *if not found return -1;
 */
int find_file(char *input);

int divide_name(char* input_name, char* fname, char* fext); // divide the name into name and extension

int create_file(char *fname, char *fext);   //create a file, return the entry index of directory table