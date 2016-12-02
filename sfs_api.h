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

/*================define top-level info=================*/
#define FILENAME "SFS"
#define BLOCK_SIZE 1024
#define BLOCK_NUMBER 3000

#define DIR_TABLE_SIZE 299;
#define OPEN_TABLE_SIZE 299;

#define FILE_NAME_LIMIT 16
#define FILE_EXT_LIMIT 3

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
typedef struct super_block{
	int magic;
	int block_size;
	int file_system_size;	
	int inode_table_length;
	inode root_directory;
} super_block;

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
	indirect_ptr_block indirect_ptr;
}inode;


//this indirect pointer has 1024/4 pointers
typedef struct indirect_ptr_block{
	int ptr[INDIRECT_PTR_SIZE];
}indirect_ptr_block;

//inode table
typedef struct inode_table{
	int entry_cnt;
	inode inodes[INODE_TABLE_SIZE];
}inode_table;

//directory entry, it stores the file name and the i-Node(we use inode index here)
typedef struct dir_entry{
	int initialized; 			//1 for initialized, uninitialized otherwise
	char file_name[FILE_NAME_LIMIT];
	char file_ext[FILE_EXT_LIMIT];
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
	open_fd_table_entry[OPEN_TABLE_SIZE];
}open_fd_table;

/*================In-memory structure================*/
inode_table it;    //inode table
dir d;			//directory table
open_fd_table oft;						//open file table
unsigned char free_bit_map[FREE_BIT_MAP_LENGTH];