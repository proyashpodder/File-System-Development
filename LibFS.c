#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "LibDisk.h"
#include "LibFS.h"
#include <ctype.h>
// set to 1 to have detailed debug print-outs and 0 to have none
#define FSDEBUG 1

#if FSDEBUG
#define dprintf printf
#else
#define dprintf noprintf
void noprintf(char* str, ...) {}
#endif

// the file system partitions the disk into five parts:

// 1. the superblock (one sector), which contains a magic number at
// its first four bytes (integer)
#define SUPERBLOCK_START_SECTOR 0

// the magic number chosen for our file system
#define OS_MAGIC 0xdeadbeef

// 2. the inode bitmap (one or more sectors), which indicates whether
// the particular entry in the inode table (#4) is currently in use
#define INODE_BITMAP_START_SECTOR 1

// the total number of bytes and sectors needed for the inode bitmap;
// we use one bit for each inode (whether it's a file or directory) to
// indicate whether the particular inode in the inode table is in use
#define INODE_BITMAP_SIZE ((MAX_FILES+7)/8)
#define INODE_BITMAP_SECTORS ((INODE_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)

// 3. the sector bitmap (one or more sectors), which indicates whether
// the particular sector in the disk is currently in use
#define SECTOR_BITMAP_START_SECTOR (INODE_BITMAP_START_SECTOR+INODE_BITMAP_SECTORS)

// the total number of bytes and sectors needed for the data block
// bitmap (we call it the sector bitmap); we use one bit for each
// sector of the disk to indicate whether the sector is in use or not
#define SECTOR_BITMAP_SIZE ((TOTAL_SECTORS+7)/8)
#define SECTOR_BITMAP_SECTORS ((SECTOR_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)

// 4. the inode table (one or more sectors), which contains the inodes
// stored consecutively
#define INODE_TABLE_START_SECTOR (SECTOR_BITMAP_START_SECTOR+SECTOR_BITMAP_SECTORS)

// an inode is used to represent each file or directory; the data
// structure supposedly contains all necessary information about the
// corresponding file or directory
typedef struct _inode {
  int size; // the size of the file or number of directory entries
  int type; // 0 means regular file; 1 means directory
  int data[MAX_SECTORS_PER_FILE]; // indices to sectors containing data blocks
} inode_t;

// the inode structures are stored consecutively and yet they don't
// straddle accross the sector boundaries; that is, there may be
// fragmentation towards the end of each sector used by the inode
// table; each entry of the inode table is an inode structure; there
// are as many entries in the table as the number of files allowed in
// the system; the inode bitmap (#2) indicates whether the entries are
// current in use or not
#define INODES_PER_SECTOR (SECTOR_SIZE/sizeof(inode_t))
#define INODE_TABLE_SECTORS ((MAX_FILES+INODES_PER_SECTOR-1)/INODES_PER_SECTOR)

// 5. the data blocks; all the rest sectors are reserved for data
// blocks for the content of files and directories
#define DATABLOCK_START_SECTOR (INODE_TABLE_START_SECTOR+INODE_TABLE_SECTORS)

// other file related definitions

// max length of a path is 256 bytes (including the ending null)
#define MAX_PATH 256

// max length of a filename is 16 bytes (including the ending null)
#define MAX_NAME 16

// max number of open files is 256
#define MAX_OPEN_FILES 256

inode_t* getInodeHelper(int child_inode);
//int remove_file_or_directory(int type, char* pathname)
// each directory entry represents a file/directory in the parent
// directory, and consists of a file/directory name (less than 16
// bytes) and an integer inode number
typedef struct _dirent {
  char fname[MAX_NAME]; // name of the file
  int inode; // inode of the file
} dirent_t;

// the number of directory entries that can be contained in a sector
#define DIRENTS_PER_SECTOR (SECTOR_SIZE/sizeof(dirent_t))

// global errno value here
int osErrno;

// the name of the disk backstore file (with which the file system is booted)
static char bs_filename[1024];

/* the following functions are internal helper functions */

// check magic number in the superblock; return 1 if OK, and 0 if not
static int check_magic()
{
  char buf[SECTOR_SIZE];
  if(Disk_Read(SUPERBLOCK_START_SECTOR, buf) < 0)
    return 0;
  if(*(int*)buf == OS_MAGIC) return 1;
  else return 0;
}

// initialize a bitmap with 'num' sectors starting from 'start'
// sector; all bits should be set to zero except that the first
// 'nbits' number of bits are set to one
static void bitmap_init(int start, int num, int nbits)
{
  /* YOUR CODE */
  dprintf("Initializing Bitmap\n");
  
  int beginSector,beginByte,beginBit,bitToOne;
  
  for(beginSector = 0; beginSector < num ; beginSector++){ // here we will iterate through each sector once becasue we can use the Disk_Write operation sector wise.
    char *bitmap = calloc(512,sizeof(char)); // using calloc, we assign a particular sector and make all the values 0 at the same time
    int temp,mod; // These 2 variables are used to optimize the assigning of 1 to first nbits value
    
    if (nbits > 512*8){ // this if blocks indicate that the value of nbits is larger than the total size of a sector,so we can make the all sector set without using any sequential process.
      memset(bitmap,255,512); // set all the bytes of this sector to 255 which means all the bits are 1 of this particular sector.
      nbits = nbits - 512; // reducing the value of nbits to 512 as we have already set the 512 bytes
    }

    else if(nbits < 512*8 && nbits > 0){ // in this situation, the value of nbits is less than a sector contains
      //memset(buf,1,nbits);
      //nbits = 0;
      temp = nbits/8; // returns how many bytes we can be completely set using the 255.
      mod = nbits%8; // returns how many bits are left over
      memset(bitmap,255,temp); // set all the bytes we can set to 255
      for(beginBit = 0; beginBit < 8;beginBit++){ // seqentially modified the last byte with the leftover bits. Took some help from https://www.cprogramming.com/tutorial/bitwise_operators.html website whileimplementing this part.
        if(mod > 0){
	  bitmap[temp] = (1 << (7-beginBit));
	  mod--;
	}
      }
      nbits=0; // all nbits are set, so making the value to 0
    }
    else{
      //do nothing. just write a all 0 array to the disk
    }

    //print the bitmap
    /*for(int i =0; i< 512; i++)
      dprintf("%x ",bitmap[i]);
    dprintf("\n");*/
    Disk_Write(start+beginSector, bitmap);
   
    free(bitmap); // free the bitmap after use
  }
}

// set the first unused bit from a bitmap of 'nbits' bits (flip the
// first zero appeared in the bitmap to one) and return its location;
// return -1 if the bitmap is already full (no more zeros)
static int bitmap_first_unused(int start, int num, int nbits)
{
  /* YOUR CODE */
  dprintf("bitmap first unused using nbits value of %d\n",nbits);
  int beginSector,beginByte,beginBit;
  int result;
  int position = 0;// this variable will be used to return the first unused position
  char *bitmap = calloc(512,sizeof(char));
  beginSector = 0; // we will iterate each sector using this variable
  
  while(nbits > 0){// we will iterate as long as we have a positivie nbits value
    if(Disk_Read(start+beginSector, bitmap) == 0){
      for(beginByte = 0; beginByte < 512 ; beginByte++){// we will iterate each byte
          result = bitmap[beginByte];
	  if(result == -1){ // this means all the bits are 1 in this particular byte, so no need to iterate through the bits
	    position +=8; // increase the position as 8
	    nbits -= 8;// decrease the nbits value to 8
	  }
	  else{	// here all the bits of this byte is not 1, so we will find our desired unused bit now
            for(beginBit = 0; beginBit < 8; beginBit++){ // iterate through the 8 bits of that byte
            //temp = bitmap[beginByte] >> beginBit;
	    //temp &= 1;
	    //temp = (bitmap[beginByte] & (1 << beginBit));
	    if((bitmap[beginByte] & (1 << (7-beginBit))) == 0){ // this particular bit is 0 (our desired one). Took help to get a particular bit from a byte from this website: https://stackoverflow.com/questions/4854207/get-a-specific-bit-from-byte
	      bitmap[beginByte] = bitmap[beginByte] | (1 << (7-beginBit)); // update the specific byte
	      Disk_Write(start+beginSector, bitmap); // update the disk with modified byte
	      return position;
	    }
            position++;
	    nbits --;  
	    }
         }
      }
    }
    beginSector++;
  }
  return -1;
}

// reset the i-th bit of a bitmap with 'num' sectors starting from
// 'start' sector; return 0 if successful, -1 otherwise
static int bitmap_reset(int start, int num, int ibit)
{
  /* YOUR CODE */
  dprintf("Bitmap reset function has been called");
  int beginSector, beginByte, beginBit;
  int Sector,byte,bit;
  char *bitmap = calloc(512,sizeof(char));
  Sector = ibit / (512*8);// we find out in which sector the ibit is located
  bit = ibit % 8; // find out the ibit's position in that particular sector
  beginByte = ibit / 512;// find out in which byte the ibit is located
  //ibit = ibit - (Sector*512*8) - (byte*8);
  dprintf("the sector and byte is %d and %d\n",Sector,byte);
  Disk_Read(start+Sector, bitmap);
  //for(int beginByte = 0; beginByte < 512; beginSector++){
    for(int beginBit = 0; beginBit < 8; beginBit++){
      if(bit == 0){
        bitmap[beginByte] = bitmap[beginByte] & (~(1 << (7 - beginBit)));
	Disk_Write(start+Sector, bitmap);
	return 0;
      }
      bit --;
    }
  //}
  return -1;
}

// return 1 if the file name is illegal; otherwise, return 0; legal
// characters for a file name include letters (case sensitive),
// numbers, dots, dashes, and underscores; and a legal file name
// should not be more than MAX_NAME-1 in length
static int illegal_filename(char* name)
{
  /* YOUR CODE */
  int len = strlen(name);
  //dprintf("Size is %d\n",len); 
  //dprintf("value is %s\n",name);
  if(len > MAX_NAME - 1){
    dprintf("Name is too big\n");
    return 1;
  }
  for(int i = 0; i<len;i++){
    if(!(isdigit(name[i]) || isalpha(name[i]) || name[i] == '.' || name[i] == '-' || name[i] == '_')){
      dprintf("name contains illegal charecter\n");
      return 1;
    }
  }
  
  return 0; 
}

// return the child inode of the given file name 'fname' from the
// parent inode; the parent inode is currently stored in the segment
// of inode table in the cache (we cache only one disk sector for
// this); once found, both cached_inode_sector and cached_inode_buffer
// may be updated to point to the segment of inode table containing
// the child inode; the function returns -1 if no such file is found;
// it returns -2 is something else is wrong (such as parent is not
// directory, or there's read error, etc.)
static int find_child_inode(int parent_inode, char* fname,
			    int *cached_inode_sector, char* cached_inode_buffer)
{
  int cached_start_entry = ((*cached_inode_sector)-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  int offset = parent_inode-cached_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* parent = (inode_t*)(cached_inode_buffer+offset*sizeof(inode_t));
  dprintf("... load parent inode: %d (size=%d, type=%d)\n",
	 parent_inode, parent->size, parent->type);
  if(parent->type != 1) {
    dprintf("... parent not a directory\n");
    return -2;
  }

  int nentries = parent->size; // remaining number of directory entries 
  int idx = 0;
  while(nentries > 0) {
    char buf[SECTOR_SIZE]; // cached content of directory entries
    if(Disk_Read(parent->data[idx], buf) < 0) return -2;
    for(int i=0; i<DIRENTS_PER_SECTOR; i++) {
      if(i>nentries) break;
      if(!strcmp(((dirent_t*)buf)[i].fname, fname)) {
	// found the file/directory; update inode cache
	int child_inode = ((dirent_t*)buf)[i].inode;
	dprintf("... found child_inode=%d\n", child_inode);
	int sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
	if(sector != (*cached_inode_sector)) {
	  *cached_inode_sector = sector;
	  if(Disk_Read(sector, cached_inode_buffer) < 0) return -2;
	  dprintf("... load inode table for child\n");
	}
	return child_inode;
      }
    }
    idx++; nentries -= DIRENTS_PER_SECTOR;
  }
  dprintf("... could not find child inode\n");
  return -1; // not found
}

// follow the absolute path; if successful, return the inode of the
// parent directory immediately before the last file/directory in the
// path; for example, for '/a/b/c/d.txt', the parent is '/a/b/c' and
// the child is 'd.txt'; the child's inode is returned through the
// parameter 'last_inode' and its file name is returned through the
// parameter 'last_fname' (both are references); it's possible that
// the last file/directory is not in its parent directory, in which
// case, 'last_inode' points to -1; if the function returns -1, it
// means that we cannot follow the path
static int follow_path(char* path, int* last_inode, char* last_fname)
{
  if(!path) {
    dprintf("... invalid path\n");
    return -1;
  }
  if(path[0] != '/') {
    dprintf("... '%s' not absolute path\n", path);
    return -1;
  }
  
  // make a copy of the path (skip leading '/'); this is necessary
  // since the path is going to be modified by strsep()
  char pathstore[MAX_PATH]; 
  strncpy(pathstore, path+1, MAX_PATH-1);
  pathstore[MAX_PATH-1] = '\0'; // for safety
  char* lpath = pathstore;
  
  int parent_inode = -1, child_inode = 0; // start from root
  // cache the disk sector containing the root inode
  int cached_sector = INODE_TABLE_START_SECTOR;
  char cached_buffer[SECTOR_SIZE];
  if(Disk_Read(cached_sector, cached_buffer) < 0) return -1;
  dprintf("... load inode table for root from disk sector %d\n", cached_sector);
  
  // for each file/directory name separated by '/'
  char* token;
  while((token = strsep(&lpath, "/")) != NULL) {
    dprintf("... process token: '%s'\n", token);
    if(*token == '\0') continue; // multiple '/' ignored
    if(illegal_filename(token)) {
      dprintf("... illegal file name: '%s'\n", token);
      return -1; 
    }
    if(child_inode < 0) {
      // regardless whether child_inode was not found previously, or
      // there was issues related to the parent (say, not a
      // directory), or there was a read error, we abort
      dprintf("... parent inode can't be established\n");
      return -1;
    }
    parent_inode = child_inode;
    child_inode = find_child_inode(parent_inode, token,
				   &cached_sector, cached_buffer);
    if(last_fname) strcpy(last_fname, token);
  }
  if(child_inode < -1) return -1; // if there was error, abort
  else {
    // there was no error, several possibilities:
    // 1) '/': parent = -1, child = 0
    // 2) '/valid-dirs.../last-valid-dir/not-found': parent=last-valid-dir, child=-1
    // 3) '/valid-dirs.../last-valid-dir/found: parent=last-valid-dir, child=found
    // in the first case, we set parent=child=0 as special case
    if(parent_inode==-1 && child_inode==0) parent_inode = 0;
    dprintf("... found parent_inode=%d, child_inode=%d\n", parent_inode, child_inode);
    *last_inode = child_inode;
    return parent_inode;
  }
}

// add a new file or directory (determined by 'type') of given name
// 'file' under parent directory represented by 'parent_inode'
int add_inode(int type, int parent_inode, char* file)
{
  // get a new inode for child
  int child_inode = bitmap_first_unused(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, INODE_BITMAP_SIZE);
  if(child_inode < 0) {
    dprintf("... error: inode table is full\n");
    return -1; 
  }
  dprintf("... new child inode %d\n", child_inode);

  // load the disk sector containing the child inode
  int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
  char inode_buffer[SECTOR_SIZE];
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);

  // get the child inode
  int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  int offset = child_inode-inode_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));

  // update the new child inode and write to disk
  memset(child, 0, sizeof(inode_t));
  child->type = type;
  if(Disk_Write(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... update child inode %d (size=%d, type=%d), update disk sector %d\n",
	 child_inode, child->size, child->type, inode_sector);

  // get the disk sector containing the parent inode
  inode_sector = INODE_TABLE_START_SECTOR+parent_inode/INODES_PER_SECTOR;
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for parent inode %d from disk sector %d\n",
	 parent_inode, inode_sector);

  // get the parent inode
  inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  offset = parent_inode-inode_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* parent = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
  dprintf("... get parent inode %d (size=%d, type=%d)\n",
	 parent_inode, parent->size, parent->type);

  // get the dirent sector
  if(parent->type != 1) {
    dprintf("... error: parent inode is not directory\n");
    return -2; // parent not directory
  }
  int group = parent->size/DIRENTS_PER_SECTOR;
  char dirent_buffer[SECTOR_SIZE];
  if(group*DIRENTS_PER_SECTOR == parent->size) {
    // new disk sector is needed
    int newsec = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);
    if(newsec < 0) {
      dprintf("... error: disk is full\n");
      return -1;
    }
    parent->data[group] = newsec;
    memset(dirent_buffer, 0, SECTOR_SIZE);
    dprintf("... new disk sector %d for dirent group %d\n", newsec, group);
  } else {
    if(Disk_Read(parent->data[group], dirent_buffer) < 0)
      return -1;
    dprintf("... load disk sector %d for dirent group %d\n", parent->data[group], group);
  }

  // add the dirent and write to disk
  int start_entry = group*DIRENTS_PER_SECTOR;
  offset = parent->size-start_entry;
  dirent_t* dirent = (dirent_t*)(dirent_buffer+offset*sizeof(dirent_t));
  strncpy(dirent->fname, file, MAX_NAME);
  dirent->inode = child_inode;
  if(Disk_Write(parent->data[group], dirent_buffer) < 0) return -1;
  dprintf("... append dirent %d (name='%s', inode=%d) to group %d, update disk sector %d\n",
	  parent->size, dirent->fname, dirent->inode, group, parent->data[group]);

  // update parent inode and write to disk
  parent->size++;
  if(Disk_Write(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... update parent inode on disk sector %d\n", inode_sector);
  
  return 0;
}

// used by both File_Create() and Dir_Create(); type=0 is file, type=1
// is directory
int create_file_or_directory(int type, char* pathname)
{
  int child_inode;
  char last_fname[MAX_NAME];
  int parent_inode = follow_path(pathname, &child_inode, last_fname);
  dprintf("the paren_inode is %d\n",parent_inode);
  if(parent_inode >= 0) {
    if(child_inode >= 0) {
      dprintf("... file/directory '%s' already exists, failed to create\n", pathname);
      osErrno = E_CREATE;
      return -1;
    } else {
      if(add_inode(type, parent_inode, last_fname) >= 0) {
	dprintf("... successfully created file/directory: '%s'\n", pathname);
	return 0;
      } else {
	dprintf("... error: something wrong with adding child inode\n");
	osErrno = E_CREATE;
	return -1;
      }
    }
  } else {
    dprintf("... error: something wrong with the file/path: '%s'\n", pathname);
    osErrno = E_CREATE;
    return -1;
  }
}

// remove the child from parent; the function is called by both
// File_Unlink() and Dir_Unlink(); the function returns 0 if success,
// -1 if general error, -2 if directory not empty, -3 if wrong type
int remove_inode(int type, int parent_inode, int child_inode)
{
 dprintf("entering remove inode function\n");
  /* YOUR CODE */
  //get child i_node
    inode_t* childnode = getInodeHelper(child_inode);
  
  //
  int sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
  char inode_buffer[SECTOR_SIZE];
    //check type validity
    if (childnode->type != type) 
    {
      dprintf("...filetype not valid\n");
      return -3;
    } 
  
    // check for empty directory
    else if (childnode->size!=0)
     { 
       dprintf("...directory not empty\n");
       return -2; 
     }
    dprintf("... validating type and if directory empty\n");
    /* considering the child inode is distribute */
    //remove data from child inode by 
    for(int i=0;i < MAX_SECTORS_PER_FILE;i++){ //MAX_SECTORS_PER_FILE=30
    if (childnode->data[i]) 
    { 
      //buffer size is 512 bytes
      char buffer[SECTOR_SIZE];
      dprintf("... deleting data of child node\n");
      bitmap_reset(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, childnode->data[i]); //it resets the bit at the specific file table position
      //setting the value of buffer of size 512 bytes to zero
      memset(buffer,0,SECTOR_SIZE);
      /*the memory buffer of inode data with the sector size of 512 
      is set to zero using memset() function because it has a nuilt in loop
      that will set the buffer size to 0 with faster run time intead of using 
      a for loop to clear the buffer */

    }
    }
    //remove child inode
    bitmap_reset(INODE_BITMAP_START_SECTOR,INODE_BITMAP_SECTORS, child_inode);
    /* bitmap_reset is called to perforrm the main function of removing i node 
    by resetting the bit and putting a pointer to know location. Here 
    star pointer is at inode bitmap star sector to the size of the bitmap sector 
    and here the child inode bit is flipped */
    memset(childnode,0,sizeof(inode_t));
    /* the memory buffer of childnode in size of the inode 
      is set to zero to clear the inode table */
    dprintf("... child inode is removed from i node table\n");

    /* The following four process of fetching parent inode
    ,parent inode sector,checking for directory parent type
    and getting the dirent sector
    has been borrowed from the provided code structure */
    
    // disk sector of parent inode is loaded
    sector = INODE_TABLE_START_SECTOR+parent_inode/INODES_PER_SECTOR;
    if(Disk_Read(sector, inode_buffer) < 0) return -1;
    dprintf("... load inode table for parent inode %d from disk sector %d\n"
    ,parent_inode, sector);

    // the parent inode is loaded 
    int inode_start_entry = (sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
    int offset = parent_inode-inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t* parent = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
    dprintf("... get parent inode %d (size=%d, type=%d)\n",
    parent_inode, parent->size, parent->type);
  
    //parent inode needs to be a directory to remove the child inode from
    if (parent->type != 1) { //this means parent inode is not type 1 & not a directory
        dprintf("... error: parent inode is not directory\n"); 
        return -2; // parent not directory
    }

    // get the dirent sectors 
    char dirent_buffer[SECTOR_SIZE];
    for (int j = 0; j < MAX_SECTORS_PER_FILE; j++) {
    if (parent->data[j]) {
      if (Disk_Read(parent->data[j], dirent_buffer) < 0) { 
        return -1;
       }
      dprintf("... load disk sector %d for dirent group %d\n", parent->data[j], j + 1);
    
    //consider directory over several sectors assigned for directory
    for (int k = 0; k < DIRENTS_PER_SECTOR; k++) { 
    dirent_t *dirent = (dirent_t *) (dirent_buffer + (k * sizeof(dirent_t))); //
    
    /* The following process is for directory unlink*/

    // remove child dirent 
    if (dirent->inode == child_inode) { // if child directory is found
      dprintf("... found match: dirent inode %d, child inode %d\n", dirent->inode, child_inode);
      memset(dirent, 0, sizeof(dirent_t));//clearing the buffer of directory by setting to zero
      if (Disk_Write(parent->data[j], dirent_buffer) < 0) { 
        return -1; //updating disk about parent inode data
        }
      if(parent->size > 0){
      parent->size--; // resetting the parent inode to previous i node 
      }
      /* directory inode restored to previous node */
      if (Disk_Write(sector, inode_buffer) < 0) {
         return -1; //update disk with new inode position
      }
      dprintf("... update parent inode on disk sector %d\n", sector);
      return 0; 
      /* this confirms inode has been remove for the 
      function delete_helper for 
      File_Unlink & Dir_Unlink*/

      dprintf("... exiting remove_inode function\n");
                }
            }
        }
    }
  return -1;
}
// representing an open file
typedef struct _open_file {
  int inode; // pointing to the inode of the file (0 means entry not used)
  int size;  // file size cached here for convenience
  int pos;   // read/write position
} open_file_t;
static open_file_t open_files[MAX_OPEN_FILES];

// return true if the file pointed to by inode has already been open
int is_file_open(int inode)
{
  for(int i=0; i<MAX_OPEN_FILES; i++) {
    if(open_files[i].inode == inode)
      return 1;
  }
  return 0;
}

// return a new file descriptor not used; -1 if full
int new_file_fd()
{
  for(int i=0; i<MAX_OPEN_FILES; i++) {
    if(open_files[i].inode <= 0)
      return i;
  }
  return -1;
}

/* end of internal helper functions, start of API functions */

int FS_Boot(char* backstore_fname)
{
  dprintf("FS_Boot('%s'):\n", backstore_fname);
  // initialize a new disk (this is a simulated disk)
  if(Disk_Init() < 0) {
    dprintf("... disk init failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  dprintf("... disk initialized\n");
  
  // we should copy the filename down; if not, the user may change the
  // content pointed to by 'backstore_fname' after calling this function
  strncpy(bs_filename, backstore_fname, 1024);
  bs_filename[1023] = '\0'; // for safety
  
  // we first try to load disk from this file
  if(Disk_Load(bs_filename) < 0) {
    dprintf("... load disk from file '%s' failed\n", bs_filename);

    // if we can't open the file; it means the file does not exist, we
    // need to create a new file system on disk
    if(diskErrno == E_OPENING_FILE) {
      dprintf("... couldn't open file, create new file system\n");

      // format superblock
      char buf[SECTOR_SIZE];
      memset(buf, 0, SECTOR_SIZE);
      *(int*)buf = OS_MAGIC;
      if(Disk_Write(SUPERBLOCK_START_SECTOR, buf) < 0) {
	dprintf("... failed to format superblock\n");
	osErrno = E_GENERAL;
	return -1;
      }
      dprintf("... formatted superblock (sector %d)\n", SUPERBLOCK_START_SECTOR);

      // format inode bitmap (reserve the first inode to root)
      bitmap_init(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, 1);
      dprintf("... formatted inode bitmap (start=%d, num=%d)\n",
	     (int)INODE_BITMAP_START_SECTOR, (int)INODE_BITMAP_SECTORS);
      
      // format sector bitmap (reserve the first few sectors to
      // superblock, inode bitmap, sector bitmap, and inode table)
      bitmap_init(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS,
		  DATABLOCK_START_SECTOR);
      dprintf("... formatted sector bitmap (start=%d, num=%d)\n",
	     (int)SECTOR_BITMAP_START_SECTOR, (int)SECTOR_BITMAP_SECTORS);
      
      // format inode tables
      for(int i=0; i<INODE_TABLE_SECTORS; i++) {
	memset(buf, 0, SECTOR_SIZE);
	if(i==0) {
	  // the first inode table entry is the root directory
	  ((inode_t*)buf)->size = 0;
	  ((inode_t*)buf)->type = 1;
	}
	if(Disk_Write(INODE_TABLE_START_SECTOR+i, buf) < 0) {
	  dprintf("... failed to format inode table\n");
	  osErrno = E_GENERAL;
	  return -1;
	}
      }
      dprintf("... formatted inode table (start=%d, num=%d)\n",
	     (int)INODE_TABLE_START_SECTOR, (int)INODE_TABLE_SECTORS);
      
      // we need to synchronize the disk to the backstore file (so
      // that we don't lose the formatted disk)
      if(Disk_Save(bs_filename) < 0) {
	// if can't write to file, something's wrong with the backstore
	dprintf("... failed to save disk to file '%s'\n", bs_filename);
	osErrno = E_GENERAL;
	return -1;
      } else {
	// everything's good now, boot is successful
	dprintf("... successfully formatted disk, boot successful\n");
	memset(open_files, 0, MAX_OPEN_FILES*sizeof(open_file_t));
	return 0;
      }
    } else {
      // something wrong loading the file: invalid param or error reading
      dprintf("... couldn't read file '%s', boot failed\n", bs_filename);
      osErrno = E_GENERAL; 
      return -1;
    }
  } else {
    dprintf("... load disk from file '%s' successful\n", bs_filename);
    
    // we successfully loaded the disk, we need to do two more checks,
    // first the file size must be exactly the size as expected (thiis
    // supposedly should be folded in Disk_Load(); and it's not)
    int sz = 0;
    FILE* f = fopen(bs_filename, "r");
    if(f) {
      fseek(f, 0, SEEK_END);
      sz = ftell(f);
      fclose(f);
    }
    if(sz != SECTOR_SIZE*TOTAL_SECTORS) {
      dprintf("... check size of file '%s' failed\n", bs_filename);
      osErrno = E_GENERAL;
      return -1;
    }
    dprintf("... check size of file '%s' successful\n", bs_filename);
    
    // check magic
    if(check_magic()) {
      // everything's good by now, boot is successful
      dprintf("... check magic successful\n");
      memset(open_files, 0, MAX_OPEN_FILES*sizeof(open_file_t));
      return 0;
    } else {      
      // mismatched magic number
      dprintf("... check magic failed, boot failed\n");
      osErrno = E_GENERAL;
      return -1;
    }
  }
}

int FS_Sync()
{
  if(Disk_Save(bs_filename) < 0) {
    // if can't write to file, something's wrong with the backstore
    dprintf("FS_Sync():\n... failed to save disk to file '%s'\n", bs_filename);
    osErrno = E_GENERAL;
    return -1;
  } else {
    // everything's good now, sync is successful
    dprintf("FS_Sync():\n... successfully saved disk to file '%s'\n", bs_filename);
    return 0;
  }
}

int File_Create(char* file)
{
  dprintf("File_Create('%s'):\n", file);
  return create_file_or_directory(0, file);
}

int File_Unlink(char* file)
{
    dprintf(" ... entering file unlink function\n");
  dprintf("... File_Unlink ('%s'):\n", file);
  //call delete_helper() to remove directory inod
  return delete_helper(0, file);//type 0 for file
  //return -1;
}

/*int File_Unlink(char* file)
{
   int type;
  char* pathname;
  int child_inode;
  char last_fname[MAX_NAME]; // file namee size is 30 byte
  int parent_inode=follow_path(pathname,&child_inode,last_fname);
  if (parent_inode>=0){
    if(child_inode>=0){
      if(remove_inode(type,parent_inode,child_inode)==0)
      {
        dprintf("Successful Removal of file \n ",pathname);
        return 0;
      }
      else if (remove_inode(type, parent_inode,child_inode)==-2)
      {
        dprintf("...directory not empty");
        return -2;
      }
      else if (remove_inode(type,parent_inode,child_inode)==-2)
      {
        dprintf("...type is wrong");
        return -3;
      }
    }
    if(child_inode < 1){ 
        dprintf("...File does not exist");
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }
    if (is_file_open(child_inode)==0)
    {
        dprintf("...File is already open");
        osErrno = E_FILE_IN_USE;
        return -1;       

    }
  
  }

  return -1;
}*/

int File_Open(char* file)
{
  dprintf("File_Open('%s'):\n", file);
  int fd = new_file_fd();
  if(fd < 0) {
    dprintf("... max open files reached\n");
    osErrno = E_TOO_MANY_OPEN_FILES;
    return -1;
  }

  int child_inode;
  follow_path(file, &child_inode, NULL);
  if(child_inode >= 0) { // child is the one
    // load the disk sector containing the inode
    int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if(Disk_Read(inode_sector, inode_buffer) < 0) { osErrno = E_GENERAL; return -1; }
    dprintf("... load inode table for inode from disk sector %d\n", inode_sector);

    // get the inode
    int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
    int offset = child_inode-inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
    dprintf("... inode %d (size=%d, type=%d)\n",
	    child_inode, child->size, child->type);

    if(child->type != 0) {
      dprintf("... error: '%s' is not a file\n", file);
      osErrno = E_GENERAL;
      return -1;
    }

    // initialize open file entry and return its index
    open_files[fd].inode = child_inode;
    open_files[fd].size = child->size;
    open_files[fd].pos = 0;
    return fd;
  } else {
    dprintf("... file '%s' is not found\n", file);
    osErrno = E_NO_SUCH_FILE;
    return -1;
  }  
}

int File_Read(int fd, void* buffer, int size){
  
  if(open_files[fd].inode == 0){// checking whether the file is open or not, return -1 if the file is not open
    osErrno = E_BAD_FD;
    return -1;
  }

  dprintf(".....the initial size is %d\n",size);
 // inode_t* node = getInodeHelper(child_inode);

  // this portion of the code is copied from add_inode function as we need the same functionality
  int f_inode = open_files[fd].inode;

  // load the disk sector containing the child inode
  int inode_sector = INODE_TABLE_START_SECTOR+f_inode/INODES_PER_SECTOR;
  char inode_buffer[SECTOR_SIZE];
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);
	
	
  // get the child inode
  int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  int offset = f_inode-inode_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* node = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
  
  //memset(buffer,0,size);
  int count = 0;// this variable will indicate how many bytes we have read, so initially it is 0
  int beginSector = open_files[fd].pos / 512; // figure out which sector we want to read
  dprintf("the current pos is %d\n",beginSector);
  int beginByte;// for iterating bytes
  char *temp = calloc(512,sizeof(char));
  char *data =(char*) buffer;
  int tempsize = size;

  /*if(node->data[0])
    dprintf("...got something\n");*/

  while(count < tempsize && node->data[beginSector]){// loop until the count value gets larger than the size or that particular file has some data
    //dprintf("...... size is %d\n",tempsize);

    Disk_Read(node->data[beginSector],temp);
    if(count == 0)// this indicates the first time, so we need to figure out the exact byte position
      beginByte = open_files[fd].pos % 512;
    else
      beginByte = 0;// from the second round, we will alwasy begin from the start of a sector
    /*for(int i = beginByte; i < 512; i++){
      if(count < size)
	    data[count++] = temp[i];
    }*/
    //int i =0;
    while( beginByte < 512 && count < size){// loop until we reached the sector size or bytes we have read becomes greater than the size
      data[count++] = temp[beginByte];
      beginByte++;
    }
	beginSector++;
 
  }
  
  open_files[fd].pos += count;// update the current position of the file
  //dprintf("...... get outside the loop and count is %d and the position is %d\n",count,open_files[fd].pos);
  return count;
}


int File_Write(int fd, void* buffer, int size)
{
  /* YOUR CODE */
 if(open_files[fd].inode == 0){
    osErrno = E_BAD_FD;
    return -1;
  }
 // int child_inode = open_files[fd].inode;
  //dprintf(".....the initial size is %d\n",size);
 // inode_t* node = getInodeHelper(child_inode);


  int f_inode = open_files[fd].inode;

  // load the disk sector containing the child inode
  int inode_sector = INODE_TABLE_START_SECTOR+f_inode/INODES_PER_SECTOR;
  char inode_buffer[SECTOR_SIZE];
  if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
  dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);

  
  // get the child inode
  int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
  int offset = f_inode-inode_start_entry;
  assert(0 <= offset && offset < INODES_PER_SECTOR);
  inode_t* node = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
  
  //memset(buffer,0,size);
  int count = 0;
  int beginSector = open_files[fd].pos / SECTOR_SIZE;
  //dprintf("the current position in write function is %d\n",beginSector);
  int beginByte;
  //char *temp = calloc(512,sizeof(char));
  char *temp =(char*) buffer;
  //dprintf("....the data buffer is %s",data);
  int tempsize = size;

  while(count < size){// loop until the bytes written is less than the size
    //dprintf("....the count is every loop is %d\n",count);
    int firstUnused = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);
    //dprintf(".... the new sector is %d\n",newsec);
    node->data[beginSector] = firstUnused;
    char *writeBuffer = calloc(512,sizeof(char));
    if(count == 0)
      beginByte = open_files[fd].pos % 512;
    else
      beginByte = 0;
    /*for(int j=beginByte; j< 512; j++){
      if(tempsize > 0){
        newbuf[j] = data[count++];
        tempsize --;
      }
    }*/
    dprintf("the new sector where will be written %d\n",node->data[beginSector]); 
    while( beginByte < 512 && tempsize > 0){
      writeBuffer[beginByte] = temp[count++];
      beginByte++;
      tempsize--;
    }
    dprintf("Written the following: %s\n", writeBuffer);
    Disk_Write(node->data[beginSector++], writeBuffer);
    //printf("...the value of count is %d\n",count);    
    

  }
  open_files[fd].pos += count;
  node->size += count;
  open_files[fd].size += count;
  Disk_Write(inode_sector, inode_buffer);

  return count;

  //return -1;
}

int File_Seek(int fd, int offset)
{
  /* YOUR CODE */
  dprintf("File_Seek (%d):\n",fd);
  if(offset < 0 || open_files[fd].size < offset){
    osErrno = E_SEEK_OUT_OF_BOUNDS;
    return -1;
  }
  
  if(open_files[fd].inode = 0){
    osErrno = E_BAD_FD;
    return -1;
  }

  open_files[fd].pos = offset;
  return 0;
}

int File_Close(int fd)
{
  dprintf("File_Close(%d):\n", fd);
  if(0 > fd || fd > MAX_OPEN_FILES) {
    dprintf("... fd=%d out of bound\n", fd);
    osErrno = E_BAD_FD;
    return -1;
  }
  if(open_files[fd].inode <= 0) {
    dprintf("... fd=%d not an open file\n", fd);
    osErrno = E_BAD_FD;
    return -1;
  }

  dprintf("... file closed successfully\n");
  open_files[fd].inode = 0;
  return 0;
}

inode_t *getInodeHelper(int child_inode){
//int child_inode;
  //follow_path(file, &child_inode, NULL);
  //if(child_inode >= 0) { // child is the one
    // load the disk sector containing the inode
    int inode_sector = INODE_TABLE_START_SECTOR+child_inode/INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if(Disk_Read(inode_sector, inode_buffer) < 0) { osErrno = E_GENERAL; return -1; }
    dprintf("... load inode table for inode from disk sector %d\n", inode_sector);

    // get the inode
    int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
    int offset = child_inode-inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t* child = (inode_t*)(inode_buffer+offset*sizeof(inode_t));
    dprintf("... inode %d (size=%d, type=%d)\n",
	    child_inode, child->size, child->type);
    return child;
 // }
}

int delete_helper(int type, char *pathname) {
    int child_inode;
    char last_fname[MAX_NAME];// MAX_NAME = 16 bytes
    int parent_inode = follow_path(pathname, &child_inode, last_fname);

    //first check if file is open 
    if (is_file_open(child_inode)==1) {
        /* when file is open the above function 
        returns 1 and remove inode will not be called */
        dprintf("... file '%s' is currently open\n", last_fname);
        osErrno = E_FILE_IN_USE;
        return -1;
    }
    //Check if file or directory exists or not
    if(child_inode<0) //if childnode is less than zero it means it doesnt exist on the list
    {
      dprintf(" ...  or directory does not exist\n");
      if(type){
      osErrno = E_NO_SUCH_FILE;
      }
      else {
      osErrno = E_NO_SUCH_DIR;
      }
      return  -1;
    }
    //Removing file or directory from inode
    if (parent_inode >= 0) {
      if (child_inode >= 0) {
      if (remove_inode(type, parent_inode, child_inode)==0) {
      // inode remove is successful when the above function returns 0
      if(type){
      dprintf("... directory '%s' successfully Unlinked\n", pathname);
      }
      else {
        dprintf("... file '%s' successfully Unlinked\n", pathname);
      }
      return 0;
      dprintf("DONE UNLINKING");
      } 
      else {
      if (remove_inode(type, parent_inode, child_inode) == -2) {
      dprintf("... directory '%s' is not empty.\n", pathname);
      osErrno = E_DIR_NOT_EMPTY;
      } 
      else if (remove_inode(type, parent_inode, child_inode) == -3) {
      dprintf("... wrong type '%s'.\n", pathname);
      osErrno = E_GENERAL;
      }
      else { 
      dprintf("... file/directory '%s' unable to Unlink\n", pathname);
      osErrno = E_GENERAL;
      }
      return -1;
       }
      } 
      } 
    
}
int Dir_Create(char* path){
  dprintf("Dir_Create('%s'):\n", path);
  return create_file_or_directory(1, path);
}

int Dir_Unlink(char* path){
  /* YOUR CODE */
dprintf("... entering directory unlink function\n");
  if (path == "/")
	{
    dprintf("directory %s is a root directory",path);
		osErrno = E_ROOT_DIR;
		return -1;
	}
  dprintf("... Dir_Unlink ('%s'):\n", path);
  //call delete_helper() to remove directory inode
  return delete_helper(1,path);// type 1 for directory 
  return -1;
}

int Dir_Size(char* path){
  /* YOUR CODE */
  int child_inode;
  follow_path(path, &child_inode, NULL); // usign this function we find out 
                                        //the child inode associated with the path
  if(child_inode >= 0) { 
    inode_t* directory = getInodeHelper(child_inode); // load the inode from the given inode no
    return directory->size * sizeof(dirent_t);
  }
  return 0;
}

int Dir_Read(char* path, void* buffer, int size)
{
  dprintf("entered in Dir_Read fucntion\n"); 
  /*int child_inode;
  follow_path(path,&child_inode,NULL);

  inode_t* directory = getInodeHelper(child_inode);*/


 int d_inode;
	char fname[MAX_NAME];
	follow_path(path, &d_inode, fname);

	//load child inode sector
	int inode_sector = INODE_TABLE_START_SECTOR+d_inode/INODES_PER_SECTOR;
	char inode_buffer[SECTOR_SIZE];
	if(Disk_Read(inode_sector, inode_buffer) < 0) return -1;
	dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);
	
	
	// get the child inode
	int inode_start_entry = (inode_sector-INODE_TABLE_START_SECTOR)*INODES_PER_SECTOR;
	int offset = d_inode-inode_start_entry;
	assert(0 <= offset && offset < INODES_PER_SECTOR);
	inode_t* directory = (inode_t*)(inode_buffer+offset*sizeof(inode_t));


  if(Dir_Size(path) > size){
    osErrno = E_BUFFER_TOO_SMALL;
    dprintf("here");
	    return -1;
  }
  //dprintf("size ok\n");
  char *buf =calloc(512,sizeof(char));//allocating buffer memory initially to zero
  int i = 0;
  int count = 0;
  char *temp = calloc(512,sizeof(char));//allocating the buffer to zero later to 
                                        // fill this memory with directory data[] content 
  if(directory->data[0]){
   dprintf("got something\n");
  }
  while(directory->data[i]){
    dprintf("entered in the while loop\n");
    Disk_Read(directory->data[i], temp);
    dprintf("%s\n", temp);// cannot find anything in the temp, so cannot proceed further
    i++;
  }

  return -1;
}


		
