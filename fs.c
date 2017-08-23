#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8]; 
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
		int i, index, inode_index = 0;

		// load superblock, inodeMap, blockMap and inodes into the memory
		if(disk_mount(name) == 1) {
				disk_read(0, (char*) &superBlock);
				disk_read(1, inodeMap);
				disk_read(2, blockMap);
				for(i = 0; i < numInodeBlock; i++)
				{  		index = i+3;
						disk_read(index, (char*) (inode+inode_index));
						inode_index += (BLOCK_SIZE / sizeof(Inode));
				}
				// root directory
				curDirBlock = inode[0].directBlock[0];
				disk_read(curDirBlock, (char*)&curDir);

		} else {
		// Init file system superblock, inodeMap and blockMap
				superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
				superBlock.freeInodeCount = MAX_INODE;

				//Init inodeMap
				for(i = 0; i < MAX_INODE / 8; i++)
				{
						set_bit(inodeMap, i, 0);
				}
				//Init blockMap
				for(i = 0; i < MAX_BLOCK / 8; i++)
				{
						if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
						else set_bit(blockMap, i, 0);
				}
				//Init root dir
				int rootInode = get_free_inode();
				curDirBlock = get_free_block();

				inode[rootInode].type =directory;
				inode[rootInode].owner = 0;
				inode[rootInode].group = 0;
				gettimeofday(&(inode[rootInode].created), NULL);
				gettimeofday(&(inode[rootInode].lastAccess), NULL);
				inode[rootInode].size = 1;
				inode[rootInode].blockCount = 1;
				inode[rootInode].directBlock[0] = curDirBlock;

				curDir.numEntry = 1;
				strncpy(curDir.dentry[0].name, ".", 1);
				curDir.dentry[0].name[1] = '\0';
				curDir.dentry[0].inode = rootInode;
				disk_write(curDirBlock, (char*)&curDir);
		}
		return 0;
}

int fs_umount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
		int i, index, inode_index = 0;
		disk_write(0, (char*) &superBlock);
		disk_write(1, inodeMap);
		disk_write(2, blockMap);
		for(i = 0; i < numInodeBlock; i++)
		{
				index = i+3;
				disk_write(index, (char*) (inode+inode_index));
				inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// current directory
		disk_write(curDirBlock, (char*)&curDir);

		disk_umount(name);	
}

int search_cur_dir(char *name)
{
		// return inode. If not exist, return -1
		int i;

		for(i = 0; i < curDir.numEntry; i++)
		{
				if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
		}
		return -1;
}

int file_create_large(char*name,int size){
		int i;
		int j=0;
		int block;
		if(size >= LARGE_FILE) {
				printf("File too large to create. File must be no larger than %d bytes \n",LARGE_FILE);
				return -1;
		}
		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) {
			printf("File create failed:  %s exist.\n", name);
			return -1;
		}
		if(curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) {
			printf("File create failed: directory is full!\n");
			return -1;
		}
		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;
		if(numBlock > superBlock.freeBlockCount) {
			printf("File create failed: not enough space\n");
			return -1;
		}
		if(superBlock.freeInodeCount < 1) {
			printf("File create failed: not enough inode\n");
			return -1;
		}
		
		char *tmp = (char*) malloc(sizeof(int) * size+1);
		rand_string(tmp, size);
		printf("rand_string = %s\n", tmp);
	
		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
			printf("File_create error: not enough inode.\n");
			return -1;
		}
		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;
		inode[inodeNum].group = 2;
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;
		
		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
		curDir.numEntry++;
		
		// get data blocks
		int extraPointers[128];
		int indirectBlock = get_free_block();
		inode[inodeNum].indirectBlock = indirectBlock;
		for(i = 0; i < numBlock; i++)
		{
			block = get_free_block();
			if(block == -1) {
				printf("File_create error: get_free_block failed\n");
				return -1;
			}
			if(i<10){
				inode[inodeNum].directBlock[i] = block;
				disk_write(block, tmp+(i*BLOCK_SIZE));
			}//if (direct) 
			else {
				extraPointers[j]= block;
				disk_write(block,tmp+(i*BLOCK_SIZE));
				//printf("extraPointer[%d]: %d\n",j,block);
				j++;
			}//else (indirect)
		}//for dataBlocks
		
	//write extraPointers to disk 
	disk_write(indirectBlock,(char*)extraPointers);
	
	//Test getting extrapointers
	/*
	int tempExtra[128];
	disk_read(indirectBlock,(char*)tempExtra);
	printf("\nextraPointer[8]: %d \n",extraPointers[8]);
	printf("tempExtra[8]: %d \n",tempExtra[8]);
	*/
	
	printf("Large file created: %s, inode %d, size %d\n", name, inodeNum, size);
	free(tmp);
	return 0;
}//file_create_large


int file_create(char *name, int size)
{
	//printf("in file create \n\n");
		int i;

		if(size >= SMALL_FILE) {
				file_create_large(name,size);
				return 0;
		}

		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) {
				printf("File create failed:  %s exist.\n", name);
				return -1;
		}

		if(curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) {
				printf("File create failed: directory is full!\n");
				return -1;
		}

		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;

		if(numBlock > superBlock.freeBlockCount) {
				printf("File create failed: not enough space\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) {
				printf("File create failed: not enough inode\n");
				return -1;
		}

		char *tmp = (char*) malloc(sizeof(int) * size+1);

		rand_string(tmp, size);
		printf("rand_string = %s\n", tmp);
		
		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
				printf("File_create error: not enough inode.\n");
				return -1;
		}
		
		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;
		inode[inodeNum].group = 2;
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;
		
		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
		curDir.numEntry++;

		// get data blocks
		for(i = 0; i < numBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				inode[inodeNum].directBlock[i] = block;
				disk_write(block, tmp+(i*BLOCK_SIZE));
		}

		printf("Small file created: %s, inode %d, size %d\n", name, inodeNum, size);

		free(tmp);
		return 0;
}//file_create

int file_cat(char *name)
{		int inodeNum = search_cur_dir(name);
		int size = inode[inodeNum].size;
	
		if(inodeNum < 0){
			printf("%s :file not found.\n",name);
			return -1;
		}
		gettimeofday(&(inode[inodeNum].lastAccess), NULL); //update last access time
		int numBlocks = inode[inodeNum].blockCount; //number of Blocks file takes up
		int directBlockNum=0; int block;
		char *fileData = (char*) malloc(numBlocks*BLOCK_SIZE +1);
		int i=0, j=0;
		int tempExtra[128];//for extra indirect pointers;
		
		//large files
		if(size >= SMALL_FILE){
				disk_read(inode[inodeNum].indirectBlock,(char*)tempExtra); //readin array stored in indirectBlock
		}
		//read from disk
		for(i=0; i<numBlocks; i++){
			if(i<10){ //small files
				directBlockNum =inode[inodeNum].directBlock[i]; //block num holding the data to copy
				//disk_read(directBlockNum,fileData+(i*BLOCK_SIZE));
				disk_read(directBlockNum,fileData);
				printf("%s\n", fileData);
			}
			else{ //large files 
				if(i<inode[inodeNum].blockCount){
				disk_read(tempExtra[j],fileData);
				printf("%s\n",fileData);
				j++;
				}
			}
		}
	
		fileData = '\0';
		free(fileData);
		return 0;
}//cat 

int file_read(char *name, int offset, int size)
{		 
		int inodeNum = search_cur_dir(name);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL); //update last access time
	
		char * buf = (char*) malloc(512);	
		int block_begin = offset / 512; 
		int block_end = (offset + size)/512; 
		int temp_offset = offset;
		int temp_size = 0;
		//large files 
		int tempExtra[128];//for extra indirect pointers;
		int k;
		int filesize = inode[inodeNum].size;
		if(filesize >= SMALL_FILE){
			disk_read(inode[inodeNum].indirectBlock,(char*)tempExtra); //readin array stored in indirectBlock
			printf("in file_write, tempExtra[9]:%d\n",tempExtra[9]);
		}
		
		//read in 
		for(int i = block_begin; i <= block_end; i++)
		{
			if(i<10) {//small file 
			  disk_read(inode[inodeNum].directBlock[i], buf);
			} 
			else { //large file
				if(i<inode[inodeNum].blockCount){
					k = i - 10;
					disk_read(tempExtra[k],buf);
				}
			}
			
			if(i == block_begin) { //first block 
				for(int j = (offset%512); j < 512; j++) {
					printf("%c", buf[j]); 
					if(++temp_size >= size) break;
				}
			} else if(i == block_end) { //last block 
				for(int j = 0; j < (offset+size)%512; j++) {
					printf("%c", buf[j]);
					if(++temp_size >= size) break;
				}
			} else { //blocks between first and last 
				printf("%s", buf);
				temp_size += 512;
			}
		}printf("\n");
		return 0;
}//file_read 

void setup_indirectBlock(int inodeNum,bool done){
	if(!done){
		int extraPointers[128];
		int block = get_free_block();
		inode[inodeNum].indirectBlock = block;
		disk_write(block,(char*)extraPointers);
	}
}

int file_write(char *name, int offset, int size, char *buffer)
{	
		//error checking: size <= buf
		if (size > strlen(buffer)){
			printf("Error: size can not be greater than the word length\n");
			return -1;
		}
		//ensure file exists
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0){
			printf("%s :file not found.\n",name);
			return -1;
		}
		gettimeofday(&(inode[inodeNum].lastAccess), NULL); //update last access time
		char * buf = (char*) malloc(512); //buffer to store new text
		int block_begin = offset / 512;
		int block_end = (offset + size)/512;
		int temp_offset = offset;
		int temp_size = 0;
		//large files 
		bool initIndirect = false;
		bool done = false;
		int tempExtra[128];//for extra indirect pointers;
		int k;
		int filesize = inode[inodeNum].size;
		if(filesize >= SMALL_FILE){
			disk_read(inode[inodeNum].indirectBlock,(char*)tempExtra); //read in array stored in indirectBlock
		}
		
		for(int i = block_begin; i <= block_end; i++)
		{
			bzero(buf, 512);
			if((i+1) <= inode[inodeNum].blockCount) { //if i is within the amount of blocks the current file has (small or large file)
				if(i<10){ disk_read(inode[inodeNum].directBlock[i], buf);}
				else{ //large file
					if(i<inode[inodeNum].blockCount){
						k = i - 10;
						disk_read(tempExtra[k],buf);
					}
				}//else 
			} else { //appending to file: i greater than current amount of blocks
				int new_block = get_free_block();
				if(i<10){//append to small file
					inode[inodeNum].directBlock[i] = new_block;
				}
				else if(inode[inodeNum].indirectBlock > 0){//large file with indirectBlock
					k = i-10;
					tempExtra[k]= new_block;
				}
				else{//small to large file, setup indirectBlock
					setup_indirectBlock(inodeNum,done); //done only once
					done = true;
					initIndirect = true;
					k = i-10;
					tempExtra[k] = new_block;
				}
				inode[inodeNum].blockCount++;
			}
			if(i == block_begin) {
				for(int j = (offset%512); j < 512; j++) {
					buf[j] = buffer[temp_size]; 
					if(++temp_size >= size) break; 
				}
			} else if(i == block_end) {
				for(int j = 0; j < (offset+size)%512; j++) {
					buf[j] = buffer[temp_size]; 
					if(++temp_size >= size) break;
				}
			} else {
				strncpy(buf, &(buffer[temp_size]), 512); 
				temp_size += 512;
			}
			//write changes to disk 
			if(i<10) disk_write(inode[inodeNum].directBlock[i],buf);//small file
			else{ //large file
				if(i<inode[inodeNum].blockCount){
					k = i-10;
					disk_write(tempExtra[k],buf);
				}
			}
			if(initIndirect) disk_write(inode[inodeNum].indirectBlock,(char*)tempExtra);//write init_indirect to disk}
		}
		if(inode[inodeNum].size < offset+size) inode[inodeNum].size = offset+size; //if appending to file
		printf("Changes have been made to %s\n",name);
		return 0;	
}//file_write 

int file_stat(char *name)
{
		char timebuf[28];
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		printf("Inode = %d\n", inodeNum);
		if(inode[inodeNum].type == file) printf("type = file\n");
		else printf("type = directory\n");
		printf("owner = %d\n", inode[inodeNum].owner);
		printf("group = %d\n", inode[inodeNum].group);
		printf("size = %d\n", inode[inodeNum].size);
		printf("num of block = %d\n", inode[inodeNum].blockCount);
		format_timeval(&(inode[inodeNum].created), timebuf, 28);
		printf("Created time = %s\n", timebuf);
		format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
		printf("Last accessed time = %s\n", timebuf);
}

int file_remove(char *name)
{
	int inodeNum = search_cur_dir(name);
	//Error Checking 
	if(inodeNum < 0) {
		printf("Error: file does not exist.\n");
		return -1;
	}
	if(inode[inodeNum].type != file){
		printf("Error: %s is not a file.\n",name);
		return -1;
	}
	
	//update Dentry and DirectoryEntry 
	for (int i=0; i<curDir.numEntry;i++){ 
		//remove file name from directory
		if (curDir.dentry[i].inode == inodeNum){//traverse directory to find the file denetry
			int j=i;
			while(!(j+1 >= curDir.numEntry)){//shift name/inodes in dentry array to replace the removed file
				strncpy(curDir.dentry[j].name, curDir.dentry[j+1].name, strlen(curDir.dentry[j+1].name));
				curDir.dentry[j+1].name[strlen(curDir.dentry[j+1].name)] = '\0'; 
				curDir.dentry[j].inode = curDir.dentry[j+1].inode;
				j++;
			}//while
		}
	}//for 
	curDir.numEntry--;
	
	//update inode map: change inodeNum of file to 0
	set_bit(inodeMap, inodeNum, 0);
	superBlock.freeInodeCount ++;

	//large files 
	int tempExtra[128];//for extra indirect pointers;
	int j=0;
	int size = inode[inodeNum].size;
	if(size >= SMALL_FILE){
		disk_read(inode[inodeNum].indirectBlock,(char*)tempExtra); //readin array stored in indirectBlock
	}
	//update block map
	int numBlocks = inode[inodeNum].blockCount;
	int blocknum;
	for(int i=0; i<numBlocks; i++){
		if(i<10){ //small files
			blocknum = inode[inodeNum].directBlock[i];
			set_bit(blockMap,blocknum,0);
			superBlock.freeBlockCount ++;
		}
		else{ //large files
			blocknum = tempExtra[j];
			set_bit(blockMap,blocknum,0);
			superBlock.freeBlockCount ++;
			j++;
		}
	}
	printf("%s has been removed \n",name);
	return 0;
}//file_remove

int dir_make(char* name)
{
	//error checking 
	int inodeNum = search_cur_dir(name); 
	if(inodeNum >= 0) {
		printf("mkdir failed:  %s exist.\n", name);
		return -1;
	}
	if(curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) {
		printf("mkdir failed: directory is full!\n");
		return -1;
	}
	int numBlock = 1 / BLOCK_SIZE;
	if(1 % BLOCK_SIZE > 0) numBlock++;

	if(numBlock > superBlock.freeBlockCount) {
		printf("mkdir failed: not enough space\n");
		return -1;
	}
	if(superBlock.freeInodeCount < 1) {
		printf("mkdir failed: not enough inode\n");
		return -1;
	}
	
	//get inode and block for new directory
	int dirInode = get_free_inode();
	int dirBlock = get_free_block();
	//fill inode for the new directory file
	inode[dirInode].type = directory;
	inode[dirInode].owner = 1;
	inode[dirInode].group = 2;
	gettimeofday(&(inode[dirInode].created), NULL);
	gettimeofday(&(inode[dirInode].lastAccess), NULL);
	inode[dirInode].size = 1;
	inode[dirInode].blockCount = 1;
	inode[dirInode].directBlock[0]=dirBlock;    //.=new dir
	//inode[dirInode].directBlock[1]=curDirBlock; //.. = parent dir(which is current dir new dir is created in)
	
	//add new directory file into current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = dirInode;
	curDir.numEntry++;

	//create new directory structure and write to disk
	Dentry newDir;
	newDir.numEntry =2;
	strncpy(newDir.dentry[0].name, ".", 1); //new
	newDir.dentry[0].name[1] = '\0';
	strncpy(newDir.dentry[1].name,"..",2); //parent
	newDir.dentry[1].name[2] = '\0';
	newDir.dentry[0].inode = dirInode;//new dir
	newDir.dentry[1].inode = curDir.dentry[0].inode; //parent director
	disk_write(dirBlock,(char*)&newDir);
	
	printf("new directory created: %s, inode: %d, size: %d\n", name, dirInode, 1);
}//dir_make

int dir_remove(char *name)
{
	int inodeNum = search_cur_dir(name);
	disk_write(curDirBlock, (char*)&curDir); //cur dir
	disk_read(inode[inodeNum].directBlock[0],(char*)&curDir); //dir to rm
	//Error Checking 
	if(inodeNum < 0) {
		printf("Error: directory does not exist.\n");
		//read back dir written 
		disk_read(curDirBlock, (char*)&curDir);
		return -1;
	}
	if(inode[inodeNum].type != directory){
		printf("Error: %s is not a directory.\n",name);
		disk_read(curDirBlock, (char*)&curDir);
		return -1;
	}
	int numEntries = curDir.numEntry;
	if(numEntries > 2){
		printf ("Error: directory %s is not empty.\n",name);
		disk_read(curDirBlock, (char*)&curDir);
		return -1;
	}
	disk_read(curDirBlock, (char*)&curDir);
	
	//update Dentry and DirectoryEntry of curDir 
	for (int i=0; i<curDir.numEntry;i++){ 
		//remove file name from directory
		if (curDir.dentry[i].inode == inodeNum){//traverse directory to find the file denetry
			int j=i;
			while(!(j+1 >= curDir.numEntry)){//shift name/inodes in dentry array to replace the removed file
				strncpy(curDir.dentry[j].name, curDir.dentry[j+1].name, strlen(curDir.dentry[j+1].name));
				curDir.dentry[j+1].name[strlen(curDir.dentry[j+1].name)] = '\0'; 
				curDir.dentry[j].inode = curDir.dentry[j+1].inode;
				j++;
			}//while
		}
	}//for 
	curDir.numEntry--;
	
	//update inode map and block map of removed dir 
	set_bit(inodeMap, inodeNum, 0);
	superBlock.freeInodeCount ++;
	int blocknum = inode[inodeNum].directBlock[0];
	set_bit(blockMap,blocknum,0);
	superBlock.freeBlockCount ++;
	
	printf("Directory %s has been removed \n",name);
	return 0;
}//dir_remove

int dir_change(char* name)
{
	//get inode of dir
	int inodeNum = search_cur_dir(name); 
	
	if(inodeNum < 0){
		printf("%s :directory not found.\n",name);
		return -1;
	}
	if(inode[inodeNum].type != directory){  
		printf("Error: %s is not a directory.\n",name);
		return -1;
	}
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);
	
	//write current directory to disk
	disk_write(curDirBlock, (char*)&curDir);
	//read in change dir name to disk
	disk_read(inode[inodeNum].directBlock[0],(char*)&curDir);
	curDirBlock = inode[inodeNum].directBlock[0];
	
	return 0;
}

int ls()
{
		int i;
		for(i = 0; i < curDir.numEntry; i++)
		{
				int n = curDir.dentry[i].inode;
				if(inode[n].type == file) printf("type: file, ");
				else printf("type: dir, ");
				printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
		}

		return 0;
}

int fs_stat()
{
		printf("File System Status: \n");
		printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{
		if(command(comm, "create")) {
				if(numArg < 2) {
						printf("error: create <filename> <size>\n");
						return -1;
				}
				return file_create(arg1, atoi(arg2)); // (filename, size)
		} else if(command(comm, "cat")) {
				if(numArg < 1) {
						printf("error: cat <filename>\n");
						return -1;
				}
				return file_cat(arg1); // file_cat(filename)
		} else if(command(comm, "write")) {
				if(numArg < 4) {
						printf("error: write <filename> <offset> <size> <buf>\n");
						return -1;
				}
				return file_write(arg1, atoi(arg2), atoi(arg3), arg4); // file_write(filename, offset, size, buf);
		}	else if(command(comm, "read")) {
				if(numArg < 3) {
						printf("error: read <filename> <offset> <size>\n");
						return -1;
				}
				return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);
		} else if(command(comm, "rm")) {
				if(numArg < 1) {
						printf("error: rm <filename>\n");
						return -1;
				}
				return file_remove(arg1); //(filename)
		} else if(command(comm, "mkdir")) {
				if(numArg < 1) {
						printf("error: mkdir <dirname>\n");
						return -1;
				}
				return dir_make(arg1); // (dirname)
		} else if(command(comm, "rmdir")) {
				if(numArg < 1) {
						printf("error: rmdir <dirname>\n");
						return -1;
				}
				return dir_remove(arg1); // (dirname)
		} else if(command(comm, "cd")) {
				if(numArg < 1) {
						printf("error: cd <dirname>\n");
						return -1;
				}
				return dir_change(arg1); // (dirname)
		} else if(command(comm, "ls"))  {
				return ls();
		} else if(command(comm, "stat")) {
				if(numArg < 1) {
						printf("error: stat <filename>\n");
						return -1;
				}
				return file_stat(arg1); //(filename)
		} else if(command(comm, "df")) {
				return fs_stat();
		} else {
				fprintf(stderr, "%s: command not found.\n", comm);
				return -1;
		}
		return 0;
}

