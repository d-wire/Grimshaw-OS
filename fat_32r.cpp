/**
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To write a FAT32 drive and allow a user to make its contents.
 */
#include <stdio.h>
#include "myfat.h"
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "helper_functions.cpp"
#include <algorithm>
#include <vector>
#include <cstddef>
#include <ctime>
#include <sys/time.h>
#include <time.h>

using namespace std;


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: New struct used to keep track of open files
*/
typedef struct {
    uint32_t clusNum;
    uint32_t offset;
    const char* parent;
    const char* fileName;
}file;

file file_descriptors[128]; // Global list of files to keep track of open files

bpbFat32 boot_sec_block; // Image disc
uint32_t firstDataSctr; // First data sector of image disc
bool initialized = false; // Value to check if disc is initialized
string cwd; // User's current working directory
int image_fd; // File descriptor for image disc
uint32_t EOC = 0x0FFFFFF8; // EOC marker in FAT table


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To grab the image disc and read it into a global variable for later calculations
*/
bool init() {
    const char* image_path = getenv("FAT_FS_PATH");
    image_fd = open(image_path, O_RDWR, 0);
    lseek(image_fd, 0, SEEK_SET);
    read(image_fd, (char*) &boot_sec_block, sizeof(boot_sec_block));
    
    initialized = true;
    firstDataSctr = boot_sec_block.bpb_rsvdSecCnt + (boot_sec_block.bpb_numFATs * boot_sec_block.bpb_FATSz32);
    
    memset(file_descriptors, 0, sizeof(file) * 128);
    cwd = "/";
    return true;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To get a new cluster number from the FAT table based on a previous cluster number
 * Parameters:
 * 	uint32_t clusNum: previous cluster number used to find new cluster number
*/
uint32_t getEntVal(uint32_t clusNum) {
    if(!initialized) {
	init();
    }
    char buffer[boot_sec_block.bpb_bytesPerSec];
    uint32_t fOffset = clusNum * 4;
    uint32_t fatSecNum = boot_sec_block.bpb_rsvdSecCnt + (fOffset / boot_sec_block.bpb_bytesPerSec);
    uint32_t fatEntOffset = fOffset % boot_sec_block.bpb_bytesPerSec;
    lseek(image_fd, fatSecNum * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
    read(image_fd, &buffer, boot_sec_block.bpb_bytesPerSec);
    uint32_t val = (* ((uint32_t*) &buffer[fatEntOffset])) & 0x0FFFFFFF;
    return val;
}


/* Helper function to convert cluster number to sector number */
uint32_t clusNum_to_secNum(uint32_t clusNum) {
    return ((clusNum - 2) * boot_sec_block.bpb_secPerClus) + firstDataSctr;
}


/* Helper function to get fat table entry val without mask at end */
uint32_t getEntValWOMask(uint32_t clusNum) {
    if(!initialized) {
	init();
    }
    char buffer[boot_sec_block.bpb_bytesPerSec];
    uint32_t fOffset = clusNum * 4;
    uint32_t fatSecNum = boot_sec_block.bpb_rsvdSecCnt + (fOffset / boot_sec_block.bpb_bytesPerSec);
    uint32_t fatEntOffset = fOffset % boot_sec_block.bpb_bytesPerSec;
    lseek(image_fd, fatSecNum * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
    read(image_fd, &buffer, boot_sec_block.bpb_bytesPerSec);
    uint32_t val = (* ((uint32_t*) &buffer[fatEntOffset]));
    return val;
}


/* Helper function to clear contents of an entire cluster */
int clearCluster(uint32_t clusNum) {
    char* blankClus = new char[boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec];
    memset(blankClus, 0x0, boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec);
    lseek(image_fd, clusNum_to_secNum(clusNum) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
    write(image_fd, blankClus, boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec);
    return 1;
}


/* Helper function to write an EOC mark to a specific FAT entry */
int writeEOCToFAT(uint32_t clusNum) {
  
  // If cluster num is not in data clusters then don't proceed
  if(clusNum_to_secNum(clusNum) > (boot_sec_block.bpb_rsvdSecCnt + (boot_sec_block.bpb_numFATs * boot_sec_block.bpb_FATSz32))) {
      uint32_t fOffset2 = clusNum * 4;
      uint32_t fatSecNum2 = boot_sec_block.bpb_rsvdSecCnt + (fOffset2 / boot_sec_block.bpb_bytesPerSec);
      uint32_t fatEntOffset2 = fOffset2 % boot_sec_block.bpb_bytesPerSec;
      uint32_t tmp = getEntValWOMask(clusNum);
      uint32_t result = (0xF0000000 & tmp) | (0x0FFFFFF8);
      
      lseek(image_fd, fatSecNum2 * boot_sec_block.bpb_bytesPerSec + fatEntOffset2, SEEK_SET);  
      write(image_fd, (void*) &result, 4);

      return 1;    
    }
    else {
      return -1;
    } 
}


/* Helper function to get first empty FAT entry index */
uint32_t getFirstEmptyFAT() {
  
    // Get location of FSInfo struct and read it in
    uint32_t fsInfo = boot_sec_block.bpb_FSInfo;
    char nxtFreeChar[4];
    lseek(image_fd, (fsInfo * boot_sec_block.bpb_bytesPerSec) + 492, SEEK_SET);
    read(image_fd, nxtFreeChar, 4);
    uint32_t nxtFree = (* ((uint32_t*) &nxtFreeChar));
    
    // Set clusNum to be starting clusNum from FSInfo
    uint32_t clusNum = nxtFree;
    uint32_t fatSecNum;
    
    // Special case if FSInfo breaks
    if(nxtFree == 0) {
	fatSecNum = boot_sec_block.bpb_rsvdSecCnt;
    }
    
    // Otherwise convert to fatSecNum
    else {
      fatSecNum = boot_sec_block.bpb_rsvdSecCnt + ((nxtFree * 4) / boot_sec_block.bpb_bytesPerSec);
    }
    lseek(image_fd, fatSecNum * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
    
    // Loop until we've reached the end of the FAT table
    while((fatSecNum * boot_sec_block.bpb_bytesPerSec) < (boot_sec_block.bpb_FATSz32 * boot_sec_block.bpb_bytesPerSec)) {
	char buffer[boot_sec_block.bpb_bytesPerSec];
	read(image_fd, &buffer, boot_sec_block.bpb_bytesPerSec);
	
	// Loop until end of sector is reached
	for(int i = (nxtFree*4)%boot_sec_block.bpb_bytesPerSec; i < boot_sec_block.bpb_bytesPerSec; i += 4) {
	  
	    // If entry is 0 then return cluster number
	    if(((* ((uint32_t*) &buffer[i])) & 0x0FFFFFFF) == 0) {
		writeEOCToFAT(clusNum);
		clearCluster(clusNum);
		return clusNum;
	    }
	    clusNum ++;
	}
	fatSecNum += 1;
	nxtFree = 0;
    }
    return -1;
}


/* Helper function to write one clusNum to fat index and write EOC to other clusNum */
int writeClusNumToFAT(uint32_t fatIndex, uint32_t clusNum) {
    if(clusNum_to_secNum(fatIndex) > (boot_sec_block.bpb_rsvdSecCnt + (boot_sec_block.bpb_numFATs * boot_sec_block.bpb_FATSz32))) {
      uint32_t fOffset1 = fatIndex * 4;
      uint32_t fatSecNum1 = boot_sec_block.bpb_rsvdSecCnt + (fOffset1 / boot_sec_block.bpb_bytesPerSec);
      uint32_t fatEntOffset1 = fOffset1 % boot_sec_block.bpb_bytesPerSec;
      uint32_t tmp = getEntValWOMask(fatIndex);
      uint32_t result = (0xF0000000 & tmp) | (0x0FFFFFFF & clusNum);
      
      lseek(image_fd, fatSecNum1 * boot_sec_block.bpb_bytesPerSec + fatEntOffset1, SEEK_SET);
      write(image_fd, (void*) &result, 4);
      
    }
    else {
      return -1;
    }
    
    if(clusNum_to_secNum(clusNum) > (boot_sec_block.bpb_rsvdSecCnt + (boot_sec_block.bpb_numFATs * boot_sec_block.bpb_FATSz32))) {
      uint32_t fOffset2 = clusNum * 4;
      uint32_t fatSecNum2 = boot_sec_block.bpb_rsvdSecCnt + (fOffset2 / boot_sec_block.bpb_bytesPerSec);
      uint32_t fatEntOffset2 = fOffset2 % boot_sec_block.bpb_bytesPerSec;
      uint32_t tmp = getEntValWOMask(clusNum);
      uint32_t result = (0xF0000000 & tmp) | (0x0FFFFFF8);
      
      lseek(image_fd, fatSecNum2 * boot_sec_block.bpb_bytesPerSec + fatEntOffset2, SEEK_SET);  
      write(image_fd, (void*) &result, 4);

      return 1;    
    }
    else {
      return -1;
    } 
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To return a specific dirEnt from a given path
 * Parameters:
 * 	const char* dirname: path to desired dirEnt
*/
dirEnt* getDirEnt(const char *dirname) {
    string path1;
    string remaining_path;
    if(dirname[0] != '/') { // Account for relative paths
      string newDir = string(dirname);
      string newCwd = string(dirname).insert(0, cwd); // Prepend the cwd to the path
      path1 = get_first_dir(newCwd);
      remaining_path = get_remaining_path(newCwd);
    }
    else { // Case for absolute paths
      string newDirName = string(dirname);
      path1 = get_first_dir(newDirName);
      remaining_path = get_remaining_path(newDirName);
    }
    uint32_t currentClusterNumber = boot_sec_block.bpb_RootClus; // keeps track of what cluster we are on
    
    // dirEnt that will be returned
    dirEnt *finalDir = new dirEnt;
    
    while(remaining_path.compare(path1) != 0) { // Loop until remaining path is equal to current path  
      dirEnt *currDir = new dirEnt;
      
      while(currentClusterNumber < EOC) { // Loop until the current cluster number hits EOC mark
	
	lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_bytesPerSec, SEEK_SET); // Seek to current cluster number in bytes
	
	int dirCount = 0; // Var to keep track of how many dirEnts have been read
	
	while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) { // Loop until more dirEnts have been read than fit in a cluster
	  
	  read(image_fd, (char*) currDir, sizeof(dirEnt)); // Read in first dirEnt
	  dirCount++;
	  
	  // If current dirEnt is not a directory don't use it or directory has been deleted
	  if(currDir->dir_attr == 0xF || currDir->dir_name[0] == 0xE5) {
	      continue;
	  }
	  
	  // If desired directory is found then stop
	  if(nameCompare((char*) currDir->dir_name, path1)) {
	    break;
	  }
	  
	  // If no more data exists in cluster then stop
	  if(currDir->dir_name[0] == 0x00) {
	    break;
	  }
	  
	}
	
	// If desired directory was found then stop again
	if(nameCompare((char*) currDir->dir_name, path1)) {
	  break;
	}
	
	// If no more data bytes in cluster then stop again
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
	
	// Otherwise find the next cluster to continue searching
	else {
	  currentClusterNumber = getEntVal(currentClusterNumber);
	}
      }
      
      // If directory was found set next cluster to its first cluster and set dir to be returned
      if(nameCompare((char*) currDir->dir_name, path1)) {
	  currentClusterNumber = clusNumMaker(*currDir);
	  finalDir = currDir;
      }
      
      // If all else has failed then stop and return nullptr
      else {
	  return nullptr;
      }
      
      // Grab next element of the path
      if(!remaining_path.empty()) {
	path1 = get_first_dir(remaining_path);
	remaining_path = get_remaining_path(remaining_path);
      }
      else {
	  break;
      }
    }
    
    // Return dirEnt at end of passed in path
    return finalDir;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To change the current working directory of the user to a new specified path
 * Parameters:
 * 	const char* path: path to change or append to current working directory
*/
int OS_cd(const char *path) {
  if(!initialized) {
	init();
    }
  string newPath = string(path);
  dirEnt* dir = OS_readDir(path);
  if(dir != nullptr) {
      if(newPath[0] == '/') { // Special case for absolute paths
	  cwd = newPath;
	  if(newPath[newPath.length() - 1] != '/') {
	      cwd.append("/");
	    }
	  return 1;
      }
      else { // Case for relative paths
	  if(newPath[0] == '/') {
	    cwd.append(newPath);
	    if(newPath[newPath.length() - 1] != '/') {
	      cwd.append("/");
	    }
	    return 1;
	  }
	  else if(cwd[cwd.length() - 1] != '/' && newPath[0] != '/') {
	      cwd.append("/");
	      cwd.append(newPath);
	      if(newPath[newPath.length() - 1] != '/') {
		cwd.append("/");
	      }
	      return 1;
	  }
	  else {
	    cwd.append(newPath);
	    if(newPath[newPath.length() - 1] != '/') {
	      cwd.append("/");
	    }
	    return 1;
	  }
      }
  }
  return -1;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To list all directory entries contained within the passed in directory.
 * Will also determine if the passed in directory is valid.
 * Parameters:
 * 	const char* dirname: path to directory to have contents listed
*/
dirEnt * OS_readDir(const char *dirname) {
  if(!initialized) {
	init();
    }
    string path1;
    string remaining_path;
    if(dirname[0] != '/') { // Account for relative paths
      string newDir = string(dirname);
      string newCwd = string(dirname).insert(0, cwd); // Prepend the cwd to the path
      path1 = get_first_dir(newCwd);
      remaining_path = get_remaining_path(newCwd);
    }
    else { // Case for absolute paths
      string newDirName = string(dirname);
      path1 = get_first_dir(newDirName);
      remaining_path = get_remaining_path(newDirName);
    }
    uint32_t currentClusterNumber = boot_sec_block.bpb_RootClus; // keeps track of what cluster we are on
    while(remaining_path.compare(path1) != 0) { // Loop until remaining path is equal to current path  
      dirEnt *currDir = new dirEnt;
      
      while(currentClusterNumber < EOC) { // Loop until the current cluster number hits EOC mark
	
	lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_bytesPerSec, SEEK_SET); // Seek to current cluster number in bytes
	
	int dirCount = 0; // Var to keep track of how many dirEnts have been read
	
	while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) { // Loop until more dirEnts have been read than fit in a cluster
	  
	  read(image_fd, (char*) currDir, sizeof(dirEnt)); // Read in first dirEnt
	  dirCount++;
	  
	  // If current dirEnt is not a directory don't use it
	  if(currDir->dir_attr == 0xF || currDir->dir_attr != 0x10) {
	      continue;
	  }
	  
	  // If desired directory is found then stop
	  if(nameCompare((char*) currDir->dir_name, path1)) {
	    break;
	  }
	  
	  // If no more data exists in cluster then stop
	  if(currDir->dir_name[0] == 0x00) {
	    break;
	  }
	  
	}
	
	// If desired directory was found then stop again
	if(nameCompare((char*) currDir->dir_name, path1)) {
	  break;
	}
	
	// If no more data bytes in cluster then stop again
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
	
	// Otherwise find the next cluster to continue searching
	else {
	  currentClusterNumber = getEntVal(currentClusterNumber);
	}
      }
      
      // If directory was found then set next cluster to be its first cluster
      if(nameCompare((char*) currDir->dir_name, path1)) {
	  currentClusterNumber = clusNumMaker(*currDir);
      }
      
      // If all else has failed then stop and return nullptr
      else {
	  return nullptr;
      }
      
      // Grab next element of the path
      if(!remaining_path.empty()) {
	path1 = get_first_dir(remaining_path);
	remaining_path = get_remaining_path(remaining_path);
      }
      else {
	  break;
      }
    }
    
    vector<dirEnt> dirEntList; // Vector to push dirEnts onto
    dirEnt *currDir = new dirEnt;
    
    // Loop until EOC mark is found
    while(currentClusterNumber < EOC) {
      lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	dirCount++;
	
	// Ignore long name dirEnts
	if(currDir->dir_attr == 0xF) {
	      continue;
	  }
	  
	// If no more data bytes exist in cluster then stop
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
	  dirEntList.push_back(*currDir); // Push dirEnt onto vector
      }
      
      // If no more data bytes in cluster then stop again
      if(currDir->dir_name[0] == 0x00) {
	break;
      }
      
      // Otherwise update cluster number to continue searching
      else {
	currentClusterNumber = getEntVal(currentClusterNumber);
      }
    }
    
    
    dirEnt* arr = new dirEnt[dirEntList.size() + 1]; // Array that will be returned
    memset(arr, 0, (dirEntList.size() + 1) * sizeof(dirEnt));
    
    // Read all entries from vector into array
    for(int i = 0; i < dirEntList.size(); i++) {
	arr[i] = dirEntList[i];
    }
    
    return arr;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To ready a file to be read. Will also determine if the file is valid.
 * Parameters:
 * 	const char* path: path of directories to file to be opened
*/
int OS_open(const char* path) {
    if(!initialized) {
	  init();
      }
    string parent;
    
    // Special case for relative path
    if(path[0] != '/') {
	string newCwd = string(path).insert(0, cwd); 
	parent = string(newCwd);
      }
      
    else {
	parent = string(path);
      }
      
    int index = parent.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parent.length() - 1) {
	parent = parent.substr(0, parent.length() - 1);
	index = parent.find_last_of("/");
    }
    
    dirEnt* dirEntList;
    
    parent[index] = 0x0; // Split string on last slash
    const char* file = &parent.c_str()[index+1]; // Grab filename from path
    
    // Special case for root dir
    if(index == 0) {
	dirEntList = OS_readDir("/");
    }
    else {
	dirEntList = OS_readDir(parent.c_str()); // Check if parent of file exists
    }
    
    // If parent of file doesn't exist then stop
    if(dirEntList == nullptr) {
	return -1;
    }
    
    int empty_index = 0;
    
    // Find first index of global file_descriptors where clusNum is 0
    for(int i = 0; i < 128; i++) {
	if(file_descriptors[i].clusNum == 0x0) {
	    break;
	}
	empty_index++;
    }
    
    // Look through parent directory to see if file exists
    for(int i = 0; dirEntList[i].dir_name[0] != 0x0; i++) {
	
	// If file is found...
	if(nameCompare((char*) dirEntList[i].dir_name, string(file))) {
	  
	  // If desired file is a directory then return -1
	  if(dirEntList[i].dir_attr == 0x10) {
	      return -1;
	  }
	  
	  /* Otherwise update the empty_index to contain the cluster number of the file, then
	   * parent path, and the file name and return that index */
	  else {      
	      string p2 = parent.substr(0, index);
	      const char* p = p2.c_str();
	      file_descriptors[empty_index].clusNum = clusNumMaker(dirEntList[i]);
	      file_descriptors[empty_index].parent = p;
	      file_descriptors[empty_index].fileName = file;
	      break;
	  }
	}
      }
	  
     
      return empty_index;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To remove a certain file from the list of open files.
 * Parameters:
 * 	int fd: index into global list of open files
*/
int OS_close(int fd) {
  if(!initialized) {
	init();
    }
    
    // If desired file to close exists then change its clusNum in file_descriptors to 0
    if(file_descriptors[fd].clusNum != 0x0) {
	file_descriptors[fd].clusNum = 0x0;
	file_descriptors[fd].parent = 0x0;
	file_descriptors[fd].fileName = 0x0;
	return 1;
    }
    
    // If file is already closed then return -1
    if(file_descriptors[fd].clusNum == 0x0) {
	return -1;
    }
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To read the contents of a file into a buffer.
 * Parameters:
 * 	int fildes: index into list of open files
 * 	void *buf: buffer to read the contents of the file into
 * 	int nbyte: number of bytes of the file to be read
 * 	int offset: number of bytes from the beginning of the file to start reading at
*/
int OS_read(int fildes, void *buf, int nbyte, int offset) {
  if(!initialized) {
	init();
    }
    
    uint32_t currentClusterNumber = file_descriptors[fildes].clusNum; // Grab clusNum of file
    
    // If file hasn't been opened then return -1
    if(currentClusterNumber == 0x0) {
	return -1;
    }
    
    // If offset is larger then the size of a cluster, then subtract cluster size until it's not
    while(offset > (boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec)) {
      currentClusterNumber = getEntVal(currentClusterNumber);
      
      // If EOC mark is reached then return -1
      if(currentClusterNumber > EOC) {
	  return -1;
      }
      
      offset -= (boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec);
    }
    
    int bytes_read = 0;
    
    lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_bytesPerSec + offset, SEEK_SET); // Initial seek to first cluster
    
    // Check to see how many bytes are left in cluster after offset is accounted for
    int remaining_in_cluster = ((boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec) - offset);
    
    // Check how many bytes left to read (should always be nbytes)
    int remaining_to_read = nbyte - bytes_read;
    
    char *buf2 = (char*) buf; // Cast void* to make C++ happy
    memset(buf2, 0, sizeof(buf2));
    
    /* Update bytes read to equal the min of bytes left in cluster and bytes left to read
     * This avoid reading too much or too little from the file
     */
    bytes_read = read(image_fd, &buf2[bytes_read], min(remaining_to_read, remaining_in_cluster));

    remaining_to_read -= min(remaining_to_read, remaining_in_cluster); // Update remaining to read
    remaining_in_cluster -= min(remaining_to_read, remaining_in_cluster); // Update left in cluster
    
    // Loop until we have read at most as many bytes as are specified in the param
    while(bytes_read < nbyte) {
      
      // If EOC mark is reached then break
      if(currentClusterNumber > EOC) {
	  break;
      }
      
      lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec, SEEK_SET); // Seek to new cluster number
      
      int min_bytes = min(remaining_to_read, remaining_in_cluster); // Pull out min calculation for readability
      
      bytes_read += read(image_fd, &buf2[bytes_read], min_bytes); // Update bytes read by min amount
      
      remaining_to_read -= min_bytes; // Update remaining to read by min amount
      
      remaining_in_cluster -= min_bytes; // Update left in cluster by min amount
      
      currentClusterNumber = getEntVal(currentClusterNumber); // Grab next cluster number from FAT table
    }
    
    return bytes_read;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To contruct a dirEnt from given params
 * Parameters:
 * 	string name: desired dir_name of dirEnt
 * 	bool isDir: bool to differentiate between dirs and files
 * 	uint32_t firstClus: cluster number of new dirEnt
*/
dirEnt* makeDirEnt(string name, bool isDir, uint32_t firstClus) {
    dirEnt* newDir = new dirEnt;
    char* dirName = makeDirName(name);
    for(int i = 0; i < 11; i++) {
      newDir->dir_name[i] = (uint8_t) (dirName[i]);
    }
    if(isDir) {
	newDir->dir_attr = 0x10;
    }
    else {
	newDir->dir_attr = 0x20;
    }
    newDir->dir_NTRes = 0x0;
    
    uint16_t clusHI = (firstClus >> 16) & 0xFFFF;
    uint16_t clusLO = (firstClus & 0x0000FFFF);
    newDir->dir_fstClusHI = clusHI;
    newDir->dir_fstClusLO = clusLO;
    
    // Initialize vals to 0
    newDir->dir_crtTimeTenth = (uint8_t) 0x0;      
    newDir->dir_crtTime = (uint16_t) 0x0;   
    newDir->dir_crtDate = (uint16_t) 0x0;
    newDir->dir_lstAccDate = (uint16_t) 0x0; 
    newDir->dir_wrtTime = (uint16_t) 0x0; 
    newDir->dir_wrtDate = (uint16_t) 0x0; 
    newDir->dir_fileSize = (uint32_t) 0x0;  
    
    return newDir;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To remove a dir from another dir
 * Parameters:
 * 	const char* path: path to desired dir to be removed
*/
int OS_rmdir(const char *path) {
    if(!initialized) {
	  init();
      }
    string parent;
    
    // Special case for relative path
    if(path[0] != '/') {
	string newCwd = string(path).insert(0, cwd); 
	parent = string(newCwd);
      }
      
    else {
	parent = string(path);
      }
      
    // Don't let users delete . or ..
    if(parent == "." || parent == "..") {
	return -1;
    }
      
    int index = parent.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parent.length() - 1) {
	parent = parent.substr(0, parent.length() - 1);
	index = parent.find_last_of("/");
    }
    
    dirEnt* parentDirEnt;
    
    parent[index] = 0x0; // Split string on last slash
    const char* dir = &parent.c_str()[index+1]; // Grab filename from path
    
    // Special case for root dir
    if(index == 0) {
	parentDirEnt = getDirEnt("/");
    }
    else {
	parentDirEnt = getDirEnt(parent.c_str()); // Check if parent of file exists
    }
    
    if(parentDirEnt == nullptr) {
	return -1;
    }
    
    // Cast dir to be deleted to string for more operations
    string dirname = string(dir);
    
    // Assumes . and .. can't be removed
    if(dirname == "." || dirname == "..") {
	return -1;
    }
    
    // Check if "dir" is a file
    if(dirname.find(".") != string::npos) {
	return -2;
    }
    
    // Check if dir to be deleted is empty or not
    dirEnt* emptyCheck = OS_readDir(path);
    
    // Check if dir to be deleted exists
    if(emptyCheck == nullptr) {
	return -1;
    }
    else {
     // Count the directories contained in dir to be deleted
     int counter = 0;
     for(int i = 0; emptyCheck[i].dir_name[0] != 0x00 && emptyCheck[i].dir_name[0] != 0xE5; i++) {
	counter++;
      }
      // If more dirEnts than . and .. then return -3
     if(counter > 2) {
	return -3;
     }
    }
    
    uint32_t parentCluster = clusNumMaker(*parentDirEnt);
    dirEnt* currDir = new dirEnt;
    
     while(parentCluster < EOC) {
      lseek(image_fd, clusNum_to_secNum(parentCluster) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	
	dirCount++;
	
	if(currDir->dir_attr == 0xF || currDir->dir_name[0] == 0xE5) {
	      continue;
	  }
	
	// Ignore long name dirEnts and deleted dirEnts
	
	  
	if(nameCompare((char*) currDir->dir_name, string(dir))) {
	    int emptyDir = 0xE5; // Empty value to be written
	    lseek(image_fd, -1 * sizeof(dirEnt), SEEK_CUR);
	    write(image_fd, &emptyDir, 1);
	    
	    // Clear the FAT entry of deleted dir
	    writeClusNumToFAT(clusNumMaker(*currDir), 0x00000000);
	    return 1;
	}
	  
	// If no more data bytes exist in cluster then break
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
      }
      
      // If no more data bytes in cluster then return -1
      if(currDir->dir_name[0] == 0x00) {
	return -1;
      }
      
      // Otherwise update cluster number to continue searching
      else {
	parentCluster = getEntVal(parentCluster);
      }
    }
    return -1;
}



/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To create a new dir within another dir
 * Parameters:
 * 	const char* path: path to and name of new dir to be created
*/
int OS_mkdir(const char *path) {
   if(!initialized) {
	  init();
      }
    string parent;
    uint32_t emptyClus = getFirstEmptyFAT();
    // Special case for relative path
    if(path[0] != '/') {
	string newCwd = string(path).insert(0, cwd); 
	parent = string(newCwd);
      }
      
    else {
	parent = string(path);
      }
      
    int index = parent.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parent.length() - 1) {
	parent = parent.substr(0, parent.length() - 1);
	index = parent.find_last_of("/");
    }
    
    dirEnt* parentDirEnt;
    
    parent[index] = 0x0; // Split string on last slash
    const char* dir = &parent.c_str()[index+1]; // Grab filename from path
    
    // Special case for root dir
    if(index == 0) {
	parentDirEnt = getDirEnt("/");
    }
    else {
	parentDirEnt = getDirEnt(parent.c_str()); // Check if parent of file exists
    }
    
    if(parentDirEnt == nullptr) {
	return -1;
    }
    
    // Cast dir to string for more operations
    string dirname = string(dir);
    
     // Check if "dir" is a file
    if(dirname.find(".") != string::npos) {
	return -1;
    }
    
    uint32_t parentCluster = clusNumMaker(*parentDirEnt);
    dirEnt* newDir = makeDirEnt(string(dir), true, emptyClus); // Construct dirEnt to write
    dirEnt* currDir = new dirEnt;
    bool foundLocation = false; // Bool for checking if open spot for writing is found
    
     while(parentCluster < EOC) {
      lseek(image_fd, clusNum_to_secNum(parentCluster) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	dirCount++;
	
	// Ignore long name dirEnts
	if(currDir->dir_attr == 0xF) {
	      continue;
	  }
	  
	// If dirname to be written exists then return -2
	if(nameCompare((char*) currDir->dir_name, string(dir))) {
	    return -2;
	}
	  
	// If no more data bytes exist in cluster then write a new one
	if(currDir->dir_name[0] == 0x00 || currDir->dir_name[0] == 0xE5) {
	  foundLocation = true;
	  break;
	}
      }
      
      // If no more data bytes in cluster then write new one
      if(currDir->dir_name[0] == 0x00 || currDir->dir_name[0] == 0xE5) {
	lseek(image_fd, -1 * sizeof(dirEnt), SEEK_CUR);
	write(image_fd, newDir, sizeof(dirEnt));
	
	// Construct dot entry of new dir
	dirEnt* dot = makeDirEnt(".", true, clusNumMaker(*newDir));
	
	// Construct dotdot entry of new dir
	dirEnt* dotdot = makeDirEnt("..", true, clusNumMaker(*parentDirEnt));
	
	lseek(image_fd, clusNum_to_secNum(emptyClus) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
	write(image_fd, dot, sizeof(dirEnt));
	
	write(image_fd, dotdot, sizeof(dirEnt));
	return 1;
      }
      
      // Otherwise allocate a new cluster to the parent chain and continue searching
      else {
	uint32_t tmp = parentCluster;
	parentCluster = getEntVal(parentCluster);
	if(parentCluster > EOC && !foundLocation) {
	    uint32_t newEmptyClus = getFirstEmptyFAT();
	    writeClusNumToFAT(tmp, newEmptyClus);
	    parentCluster = newEmptyClus;
	}
      }
    }
    return -1;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To remove a specific file from within a dir
 * Parameters:
 * 	const char* path: path to and name of file to be removed
*/
int OS_rm(const char *path) {
        if(!initialized) {
	  init();
      }
    string parent;
    
    // Special case for relative path
    if(path[0] != '/') {
	string newCwd = string(path).insert(0, cwd); 
	parent = string(newCwd);
      }
      
    else {
	parent = string(path);
      }
      
    // Don't let users delete . or ..
    if(parent == "." || parent == "..") {
	return -1;
    }
      
    int index = parent.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parent.length() - 1) {
	parent = parent.substr(0, parent.length() - 1);
	index = parent.find_last_of("/");
    }
    
    dirEnt* parentDirEnt;
    
    parent[index] = 0x0; // Split string on last slash
    const char* file = &parent.c_str()[index+1]; // Grab filename from path
    
    // Special case for root dir
    if(index == 0) {
	parentDirEnt = getDirEnt("/");
    }
    else {
	parentDirEnt = getDirEnt(parent.c_str()); // Check if parent of file exists
    }
    
    if(parentDirEnt == nullptr) {
	return -1;
    }
    
    // Cast dir to be deleted to string for more operations
    string fileName = string(file);
    
    // Check if "fileName" is a file
    if(fileName.find(".") == string::npos || fileName == "." || fileName == "..") {
	return -2;
    }
    
    uint32_t parentCluster = clusNumMaker(*parentDirEnt);
    dirEnt* currDir = new dirEnt;
    
     while(parentCluster < EOC) {
      lseek(image_fd, clusNum_to_secNum(parentCluster) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	dirCount++;
	
	// Ignore long name dirEnts and deleted dirEnts
	if(currDir->dir_attr == 0xF || currDir->dir_name[0] == 0xE5) {
	      continue;
	  }
	  
	// If file to be removed is found then remove it
	if(nameCompare((char*) currDir->dir_name, fileName)) {
	    int emptyDir = 0xE5; // Empty value to be written
	    lseek(image_fd, -1 * sizeof(dirEnt), SEEK_CUR);
	    write(image_fd, &emptyDir, 1);
	    
	    // Clear cluster of removed file
	    writeClusNumToFAT(clusNumMaker(*currDir), 0x00000000);
	    return 1;
	}
	  
	// If no more data bytes exist in cluster then break
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
      }
      
      // If no more data bytes in cluster then break
      if(currDir->dir_name[0] == 0x00) {
	return -1;
      }
      
      // Otherwise update cluster number to continue searching
      else {
	parentCluster = getEntVal(parentCluster);
      }
    }
    return -1;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To create a new file within a specific dir
 * Parameters:
 * 	const char* path: path to and name of file to be created
*/
int OS_creat(const char *path) {
    if(!initialized) {
	  init();
      }
    string parent;
    uint32_t emptyClus = getFirstEmptyFAT();
    // Special case for relative path
    if(path[0] != '/') {
	string newCwd = string(path).insert(0, cwd); 
	parent = string(newCwd);
      }
      
    else {
	parent = string(path);
      }
      
    int index = parent.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parent.length() - 1) {
	parent = parent.substr(0, parent.length() - 1);
	index = parent.find_last_of("/");
    }
    
    dirEnt* parentDirEnt;
    
    parent[index] = 0x0; // Split string on last slash
    const char* file = &parent.c_str()[index+1]; // Grab filename from path
    
    // Special case for root dir
    if(index == 0) {
	parentDirEnt = getDirEnt("/");
    }
    else {
	parentDirEnt = getDirEnt(parent.c_str()); // Check if parent of file exists
    }
    
    //cout << (char*) parentDirEnt->dir_name << endl;
    
    if(parentDirEnt == nullptr) {
	return -1;
    }
    
    string fileName = string(file);
    
    // Check if "fileName" is a file
    if(fileName.find(".") == string::npos || fileName == "." || fileName == "..") {
	return -1;
    }
    
    uint32_t parentCluster = clusNumMaker(*parentDirEnt);
    dirEnt* newFile = makeDirEnt(fileName, false, emptyClus);
    dirEnt* currDir = new dirEnt;
    bool foundLocation = false;
    
     while(parentCluster < EOC) {
      lseek(image_fd, clusNum_to_secNum(parentCluster) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	dirCount++;
	
	// Ignore long name dirEnts
	if(currDir->dir_attr == 0xF) {
	      continue;
	  }
	  
	if(nameCompare((char*) currDir->dir_name, string(fileName))) {
	    return -2;
	}
	  
	// If no more data bytes exist in cluster then write a new one
	if(currDir->dir_name[0] == 0x00 || currDir->dir_name[0] == 0xE5) {
	  foundLocation = true;
	  break;
	}
      }
      
      // If no more data bytes in cluster then write new one
      if(currDir->dir_name[0] == 0x00 || currDir->dir_name[0] == 0xE5) {
	lseek(image_fd, -1 * sizeof(dirEnt), SEEK_CUR);
	write(image_fd, newFile, sizeof(dirEnt));
	return 1;
      }
      
      // Otherwise allocate a new cluster to the parent chain and continue searching
      else {
	uint32_t tmp = parentCluster;
	parentCluster = getEntVal(parentCluster);
	if(parentCluster > EOC && !foundLocation) {
	    uint32_t newEmptyClus = getFirstEmptyFAT();
	    writeClusNumToFAT(tmp, newEmptyClus);
	    parentCluster = newEmptyClus;
	}
      }
    }
    return -1;
}


/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: To write contents to a specific file
 * Parameters:
 * 	int fildes: file descriptor of file to be written to
 * 	const void* buf: buffer containing contents to be written
 * 	int nbyte: number of bytes to be written
 * 	int offset: byte offset to begin writing at
*/
int OS_write(int fildes, const void *buf, int nbyte, int offset) {
    if(!initialized) {
	init();
    }
    
    uint32_t currentClusterNumber = file_descriptors[fildes].clusNum; // Grab clusNum of file
    
    // If file hasn't been opened then return -1
    if(currentClusterNumber == 0x0) {
	return -1;
    }
    
    // If offset is larger then the size of a cluster, then subtract cluster size until it's not
    while(offset > (boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec)) {
      uint32_t tmp = currentClusterNumber;
      currentClusterNumber = getEntVal(currentClusterNumber);
      
      // If EOC mark is reached then return -1
      if(currentClusterNumber > EOC) {
	  uint32_t newEmptyClus = getFirstEmptyFAT();
	  writeClusNumToFAT(tmp, newEmptyClus);
	  currentClusterNumber = newEmptyClus;
      }
      
      offset -= (boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec);
    }
    
    int bytes_written = 0;
    
    lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_bytesPerSec + offset, SEEK_SET); // Initial seek to first cluster
    
    // Check to see how many bytes are left in cluster after offset is accounted for
    int remaining_in_cluster = ((boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec) - offset);
    
    // Check how many bytes left to write (should always be nbytes)
    int remaining_to_write = nbyte - bytes_written;
    
    char *buf2 = (char*) buf; // Cast void* to make C++ happy
    
    /* Update bytes written to equal the min of bytes left in cluster and bytes left to write
     * This avoid writing too much or too little to the file
     */
    bytes_written = write(image_fd, &buf2[bytes_written], min(remaining_to_write, remaining_in_cluster));

    remaining_to_write -= min(remaining_to_write, remaining_in_cluster); // Update remaining to write
    remaining_in_cluster -= min(remaining_to_write, remaining_in_cluster); // Update left in cluster
    
    // Loop until we have written at most as many bytes as are specified in the param
    while(bytes_written < nbyte) {
      
      uint32_t tmp = currentClusterNumber;
      // If EOC mark is reached then allocate a new cluster and continue searching
      if(currentClusterNumber > EOC) {
	  uint32_t newEmptyClus = getFirstEmptyFAT();
	  writeClusNumToFAT(tmp, newEmptyClus);
	  currentClusterNumber = newEmptyClus;
      }
      
      lseek(image_fd, clusNum_to_secNum(currentClusterNumber) * boot_sec_block.bpb_secPerClus * boot_sec_block.bpb_bytesPerSec, SEEK_SET); // Seek to new cluster number
      
      int min_bytes = min(remaining_to_write, remaining_in_cluster); // Pull out min calculation for readability
      
      bytes_written += write(image_fd, &buf2[bytes_written], min_bytes); // Update bytes written by min amount
      
      remaining_to_write -= min_bytes; // Update remaining to write by min amount
      
      remaining_in_cluster -= min_bytes; // Update left in cluster by min amount
      
      
      currentClusterNumber = getEntVal(currentClusterNumber); // Grab next cluster number from FAT table
    }
    
    // Get parent path of written file
    dirEnt* currDir = new dirEnt;
    char* tempP = (char*) file_descriptors[fildes].parent;
    string parentDir = string(tempP);
    
    if(parentDir[0] != '/') {
	string newCwd = parentDir.insert(0, cwd); 
	parentDir = newCwd;
      }
    
    int index = parentDir.find_last_of("/"); // Find last slash to grab filename
    
    if(index == parentDir.length() - 1) {
	parentDir = parentDir.substr(0, parentDir.length() - 1);
	index = parentDir.find_last_of("/");
    }
    
    // Get parent dirEnt of written file
    dirEnt* parentDirEnt;
    parentDirEnt = getDirEnt(parentDir.c_str()); // Check if parent of file exists
     
    
    uint32_t parentCluster = clusNumMaker(*parentDirEnt);
    char* fileName = (char*) file_descriptors[fildes].fileName;
    
     while(parentCluster < EOC) {
      lseek(image_fd, clusNum_to_secNum(parentCluster) * boot_sec_block.bpb_bytesPerSec, SEEK_SET);
      int dirCount = 0;
      
      // Loop until read dirEnts exceed size of cluster
      while((dirCount * sizeof(dirEnt)) < (boot_sec_block.bpb_bytesPerSec * boot_sec_block.bpb_secPerClus)) {
	read(image_fd, (char*) currDir, sizeof(dirEnt));
	dirCount++;
	
	// Ignore long name dirEnts
	if(currDir->dir_attr == 0xF) {
	      continue;
	  }
	  
	  
	// If file is found then update file size
	if(nameCompare((char*) currDir->dir_name, string(fileName))) {
	  
	    // Get original file size
	    uint32_t fileSize = currDir->dir_fileSize;
	    
	    // Make new file size max between og file size and bytes_written + offset
	    uint32_t newSize = max((uint32_t) (bytes_written + offset), fileSize);
	    
	    // Set new file size of dirEnt and write it to old location
	    currDir->dir_fileSize = newSize;
	    lseek(image_fd, -1 * sizeof(dirEnt), SEEK_CUR);
	    write(image_fd, currDir, sizeof(dirEnt));
	}
	  
	// If no more data bytes exist in cluster then break
	if(currDir->dir_name[0] == 0x00) {
	  break;
	}
      }
      
      // If no more data bytes in cluster then break
      if(currDir->dir_name[0] == 0x00) {
	break;
      }
      
      // Otherwise get next cluster and continue searching
      else {
	parentCluster = getEntVal(parentCluster);
	}
      }
      
    return bytes_written;
}