#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void print_file_blocks(inode_t& inode){
    cout << "File blocks" << endl;
    int fileBlocks = inode.size / UFS_BLOCK_SIZE;
    if ((inode.size % UFS_BLOCK_SIZE) != 0) {
        fileBlocks += 1;
    }
    for (int i = 0; i < fileBlocks; ++i) {
        cout << inode.direct[i] << endl;
    }
    cout << endl;
}

void print_file_data(inode_t& inode, LocalFileSystem fs, int inodeNum){
    cout << "File data" << endl;
    int size = inode.size;
    unsigned char buffer[MAX_FILE_SIZE + 1];
    fs.read(inodeNum, buffer, size);

    cout << buffer;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    cout << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }
  string diskImageFile = argv[1];
  unsigned int inodeNum = atoi(argv[2]);

  // Initialize the Disk and LocalFileSystem
  Disk disk(diskImageFile, UFS_BLOCK_SIZE);
  LocalFileSystem fs(&disk);
  // Retrieve the inode
  inode_t inode;
  int ret = fs.stat(inodeNum, &inode);
  if (ret != 0) {
      cerr << "Invalid inode number: " << inodeNum << endl;
      return 1;
  }

  // Print file blocks num
  print_file_blocks(inode);
  // Print file data
  print_file_data(inode, fs, inodeNum);
  
  return 0;
}
