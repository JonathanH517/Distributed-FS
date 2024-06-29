#include <iostream>
#include <string>
#include <vector>
#include <assert.h>

#include "LocalFileSystem.h"
#include "ufs.h"
#include <cstring>
using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

/**
 * Lookup an inode.
 *
 * Takes the parent inode number (which should be the inode number
 * of a directory) and looks up the entry name in it. The inode
 * number of name is returned.
 *
 * Success: return inode number of name
 * Failure: return -ENOTFOUND, -EINVALIDINODE.
 * Failure modes: invalid parentInodeNumber, name does not exist.
 */
int LocalFileSystem::lookup(int parentInodeNumber, std::string name) {
    super_t super;
    readSuperBlock(&super);

    // Error check: invalid parent inode number
    if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
        return -EINVALIDINODE;
    }

    // Load inodes into memory
    inode_t parentInode;
    // fill in the inode with the specified inodeNum
    int ret = stat(parentInodeNumber, &parentInode);
    if (ret != 0) {
        return -EINVALIDINODE; 
    }

    // Error check: parent inode is not a directory
    if (parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDINODE;
    }

    // Buffer to read directory entries
    unsigned char blockBuffer[UFS_BLOCK_SIZE];
    
    // Iterate over the direct pointers in the parent directory inode
    int fileBlocks = parentInode.size / UFS_BLOCK_SIZE;
    if ((parentInode.size % UFS_BLOCK_SIZE) != 0) {
      fileBlocks += 1;
    }
    for (int i = 0; i < fileBlocks; ++i) {
      int blockNum = parentInode.direct[i];

      // Read the block from the disk
      disk->readBlock(blockNum, blockBuffer);
      dir_ent_t *dirEntries = (dir_ent_t *)blockBuffer;

      // Iterate over directory entries in this block, account for the last not full block
      int numEntries = min(UFS_BLOCK_SIZE/sizeof(dir_ent_t), 
                          (parentInode.size % UFS_BLOCK_SIZE)/sizeof(dir_ent_t));
      for (int j = 0; j < numEntries; ++j) {
        if (name == string(dirEntries[j].name)) {
          return dirEntries[j].inum; // Found the entry
        }
      }
    }

    return -ENOTFOUND; // Name not found in the directory
}


int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
    /**
   * Read an inode.
   *
   * Given an inodeNumber this function will fill in the `inode` struct with
   * the type of the entry and the size of the data, in bytes, and direct blocks.
   *
   * Success: return 0
   * Failure: return -EINVALIDINODE
   * Failure modes: invalid inodeNumber
   */
  super_t super;
  readSuperBlock(&super);
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE; // Invalid inode number
  }
  // find the inode address in the block
  // find # of inodes in a block
  int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
  // starting from the start address, go to the block that has that inode
  int blockNumber = super.inode_region_addr + (inodeNumber / inodesPerBlock);
  // in that block, find the offset of it in size of inode
  int inodeOffset = (inodeNumber % inodesPerBlock) * sizeof(inode_t);

  // same idea, make the buffer sufficiently large in case 
  unsigned char buffer[UFS_BLOCK_SIZE];
  disk->readBlock(blockNumber, buffer);
  memcpy(inode, buffer + inodeOffset, sizeof(inode_t));

  return 0;
}

int min_of_three(int a, int b, int c){
  int min = a;
  if(b < min){
    min = b;
  } 
  if(c < min){
    min = c;
  }
  return min;
}
  /**
 * Read the contents of a file or directory.
 *
 * Reads up to `size` bytes of data into the buffer from file specified by
 * inodeNumber. The routine should work for either a file or directory;
 * directories should return data in the format specified by dir_ent_t.
 *
 * Success: number of bytes read
 * Failure: -EINVALIDINODE, -EINVALIDSIZE.
 * Failure modes: invalid inodeNumber, invalid size.
 */
int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  if (size < 0 || size > MAX_FILE_SIZE) {
    return -EINVALIDSIZE; // Invalid size
  }
  super_t super;
  readSuperBlock(&super);
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }
  inode_t inode;
  // fill in the inode with the specified inodeNum
  int ret = stat(inodeNumber, &inode);
  if (ret != 0) {
      return -EINVALIDINODE; 
  }

  int bytesRead = 0;
  while (bytesRead < size && bytesRead < inode.size) {
    // locate the current block index and offset
    int blockIndex = (bytesRead / UFS_BLOCK_SIZE);
    int blockOffset = (bytesRead % UFS_BLOCK_SIZE);
    // For the current reading, get the max bytes we can read
    int remaining_bytes_required = size - bytesRead; //user wanted byte size
    int remaining_bytes_cur_block = UFS_BLOCK_SIZE - blockOffset; // remaining in cur block
    int remaining_bytes_in_file = inode.size - bytesRead; // remaining in the file

    int bytesReadCurrent = min_of_three(remaining_bytes_required,
                                        remaining_bytes_cur_block, 
                                        remaining_bytes_in_file);

    // create a block to store and read them
    int blockNum = inode.direct[blockIndex];
    unsigned char blockBuffer[UFS_BLOCK_SIZE];
    disk->readBlock(blockNum, blockBuffer);
    memcpy((unsigned char*)buffer + bytesRead, blockBuffer + blockOffset, bytesReadCurrent);
    bytesRead += bytesReadCurrent;
  }

  return bytesRead; // Success: return the number of bytes read
}



int LocalFileSystem::create(int parentInodeNumber, int type, std::string name) {
    super_t super;
    readSuperBlock(&super);

    // Error checks
    if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
        return -EINVALIDINODE;
    }
    if (name.length() >= DIR_ENT_NAME_SIZE - 1) {
        return -EINVALIDNAME;
    }
    if (type != UFS_REGULAR_FILE && type != UFS_DIRECTORY) {
        return -EINVALIDTYPE;
    }

    // Load parent inode
    inode_t parentInode;
    int ret = stat(parentInodeNumber, &parentInode);
    if (ret != 0 || parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDINODE;
    }

    // Load inode region
    inode_t inodes[super.num_inodes];
    readInodeRegion(&super, inodes);

    // Calculate the blocks it used
    int parentFileBlocks = parentInode.size / UFS_BLOCK_SIZE;
    if ((parentInode.size % UFS_BLOCK_SIZE) != 0) {
        parentFileBlocks += 1;
    }

    // Check for existing name in the directory
    unsigned char blockBuffer[UFS_BLOCK_SIZE];
    for (int i = 0; i < parentFileBlocks; ++i) {
        disk->readBlock(parentInode.direct[i], blockBuffer);
        dir_ent_t* dirEntries = (dir_ent_t*)blockBuffer;
        int numEntries = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
        for (int j = 0; j < numEntries; ++j) {
            if (name == string(dirEntries[j].name)) {
                return -EINVALIDNAME;  // Name already exists
            }
        }
    }

    // Find a free inode
    unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
    readInodeBitmap(&super, inodeBitmap);
    int freeInodeNum = -1;
    for (int i = 0; i < super.num_inodes; ++i) {
        if ((inodeBitmap[i / 8] & (1 << (i % 8))) == 0) {
            freeInodeNum = i;
            break;
        }
    }
    if (freeInodeNum == -1) {
        return -ENOTENOUGHSPACE;
    }

    // Add the entry to the parent directory
    bool entryAdded = false;
    for (int i = 0; i < DIRECT_PTRS; ++i) {
        if (parentInode.direct[i] != 0) { // block is used, check if still has space
            disk->readBlock(parentInode.direct[i], blockBuffer);
            dir_ent_t* dirEntries = (dir_ent_t*)blockBuffer;
            unsigned numEntries = parentInode.size / sizeof(dir_ent_t);
            if(numEntries < UFS_BLOCK_SIZE / sizeof(dir_ent_t)){// there is a space for entry
              dirEntries[numEntries].inum = freeInodeNum;
              strncpy(dirEntries[numEntries].name, name.c_str(), DIR_ENT_NAME_SIZE - 1);
              dirEntries[numEntries].name[DIR_ENT_NAME_SIZE - 1] = '\0';
              disk->writeBlock(parentInode.direct[i], blockBuffer);
              entryAdded = true;
              parentInode.size += sizeof(dir_ent_t);  // Update parent inode size
              break;
            }

            if (entryAdded) break;
        }
    }

    unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
    readDataBitmap(&super, dataBitmap);

    int firstFreeBlockNum = -1, secondFreeBlockNum = -1;
    // Allocate new block for the parent directory if needed
    if (!entryAdded) {
        for (int i = 0; i < super.num_data; ++i) {
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            if ((dataBitmap[byteIndex] & (1 << bitIndex)) == 0) {
                firstFreeBlockNum = i + super.data_region_addr;
                dataBitmap[byteIndex] |= (1 << bitIndex);  // Mark as used
                break;
            }
        }
        if (firstFreeBlockNum == -1) {
            return -ENOTENOUGHSPACE;
        }

        for (int i = 0; i < DIRECT_PTRS; ++i) {
            if (parentInode.direct[i] == 0) {
                parentInode.direct[i] = firstFreeBlockNum;
                memset(blockBuffer, 0, UFS_BLOCK_SIZE);
                dir_ent_t* dirEntries = (dir_ent_t*)blockBuffer;
                dirEntries[0].inum = freeInodeNum;
                strncpy(dirEntries[0].name, name.c_str(), DIR_ENT_NAME_SIZE - 1);
                dirEntries[0].name[DIR_ENT_NAME_SIZE - 1] = '\0';
                disk->writeBlock(parentInode.direct[i], blockBuffer);
                entryAdded = true;
                parentInode.size += sizeof(dir_ent_t);  // Update parent inode size
                break;
            }
        }
    }

    if (!entryAdded) {
        return -ENOTENOUGHSPACE;
    }

    // Set up the new inode
    inode_t newInode = {type, 0, {0}};
    if (type == UFS_DIRECTORY) {
        // Initialize the new directory
        memset(blockBuffer, 0, UFS_BLOCK_SIZE);
        dir_ent_t* newDirEntries = (dir_ent_t*)blockBuffer;
        newDirEntries[0].inum = freeInodeNum;
        strncpy(newDirEntries[0].name, ".", DIR_ENT_NAME_SIZE - 1);
        newDirEntries[0].name[DIR_ENT_NAME_SIZE - 1] = '\0';

        newDirEntries[1].inum = parentInodeNumber;
        strncpy(newDirEntries[1].name, "..", DIR_ENT_NAME_SIZE - 1);
        newDirEntries[1].name[DIR_ENT_NAME_SIZE - 1] = '\0';

        for (int i = 0; i < super.num_data; ++i) {
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            if ((dataBitmap[byteIndex] & (1 << bitIndex)) == 0) {
                secondFreeBlockNum = i + super.data_region_addr;
                dataBitmap[byteIndex] |= (1 << bitIndex);  // Mark as used
                break;
            }
        }
        if (secondFreeBlockNum == -1) {
            return -ENOTENOUGHSPACE;
        }
        newInode.direct[0] = secondFreeBlockNum;
        disk->writeBlock(secondFreeBlockNum, blockBuffer);
        newInode.size = 2 * sizeof(dir_ent_t);
    }

    // Update the inodes
    inodes[freeInodeNum] = newInode;
    inodes[parentInodeNumber] = parentInode;
    inodeBitmap[freeInodeNum / 8] |= (1 << (freeInodeNum % 8));  // Mark inode as used

    // Write updated inodes and bitmaps back to the disk
    writeInodeRegion(&super, inodes);
    writeInodeBitmap(&super, inodeBitmap);
    writeDataBitmap(&super, dataBitmap);
    return freeInodeNum;
}



int LocalFileSystem::unlink(int parentInodeNumber, std::string name) {
    if (name == "." || name == "..") {
        return -EUNLINKNOTALLOWED;  // Prevent unlinking special directory entries
    }

    super_t super;
    readSuperBlock(&super);

    if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
        return -EINVALIDINODE; 
    }
    if (name.empty() || name.length() >= DIR_ENT_NAME_SIZE - 1) {
        return -EINVALIDNAME;
    }
    inode_t parentInode;
    if (stat(parentInodeNumber, &parentInode) < 0 
        || parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDINODE;  
    }
    if(lookup(parentInodeNumber, name) < 0){ // not found
      return 0;
    }
    inode_t inodes[super.num_inodes];
    readInodeRegion(&super, inodes);

    unsigned char blockBuffer[UFS_BLOCK_SIZE];
    bool found = false;
    int inodeToRemove = -1;
    int blockNumToRemove = -1;
    int entryIndexToRemove = -1;

    int parentFileBlocks = (parentInode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    for (int i = 0; i < DIRECT_PTRS && parentInode.direct[i] != 0; i++) {
        if (parentInode.direct[i] == 0) continue;
        disk->readBlock(parentInode.direct[i], blockBuffer);
        dir_ent_t* dirEntries = reinterpret_cast<dir_ent_t*>(blockBuffer);

        // Calculate the number of entries valid in the current block
        int numEntries = min(UFS_BLOCK_SIZE/sizeof(dir_ent_t), 
                          (parentInode.size % UFS_BLOCK_SIZE)/sizeof(dir_ent_t));
        if (i == parentFileBlocks - 1) {
            numEntries = (parentInode.size % UFS_BLOCK_SIZE) / sizeof(dir_ent_t);
        }

        for (int j = 0; j < numEntries; j++) {
            if (strcmp(dirEntries[j].name, name.c_str()) == 0) {
                inodeToRemove = dirEntries[j].inum;
                blockNumToRemove = parentInode.direct[i];
                entryIndexToRemove = j;
                found = true;
                dirEntries[entryIndexToRemove] = dirEntries[numEntries - 1];  // Move the last entry to the deleted spot
                memset(&(dirEntries[numEntries - 1]), 0, sizeof(dir_ent_t));  // Clear the last entry
                disk->writeBlock(blockNumToRemove, blockBuffer);
                break;
            }
        }
        if (found){
          break;
        } 
    }

    if (!found) {
        return 0;  // Entry not found is treated as a non-error in unlinking
    }

    // If it's a directory, ensure it's empty
    inode_t& inodeToDelete = inodes[inodeToRemove];
    if (inodeToDelete.type == UFS_DIRECTORY && (unsigned) inodeToDelete.size > 2 * sizeof(dir_ent_t)) {
        return -EDIRNOTEMPTY;
    }

    // Clear data blocks and update the data bitmap if it's a directory or file
    unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
    readDataBitmap(&super, dataBitmap);
    unsigned char blockClearBuffer[UFS_BLOCK_SIZE];
    for (int i = 0; i < DIRECT_PTRS && inodeToDelete.direct[i] != 0; i++) {
        int bitmapIndex = inodeToDelete.direct[i] - super.data_region_addr;
        dataBitmap[bitmapIndex / 8] &= ~(1 << (bitmapIndex % 8));  // Clear the bit in the data bitmap
        memset(blockClearBuffer, 0, UFS_BLOCK_SIZE);
        disk->writeBlock(inodeToDelete.direct[i], blockClearBuffer);  // Clear the block
        inodeToDelete.direct[i] = 0;  // Remove reference to the block
    }
    writeDataBitmap(&super, dataBitmap);

    // Free the inode
    unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
    readInodeBitmap(&super, inodeBitmap);
    inodeBitmap[inodeToRemove / 8] &= ~(1 << (inodeToRemove % 8));  // Clear the bit in the inode bitmap
    memset(&inodes[inodeToRemove], 0, sizeof(inode_t));  // Clear inode data
    writeInodeBitmap(&super, inodeBitmap);

    // Update the parent inode and all other inodes in the inode region
    parentInode.size -= sizeof(dir_ent_t);  // Update the size of the parent inode
    inodes[parentInodeNumber] = parentInode;
    writeInodeRegion(&super, inodes);

    return 0;
}



int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  // Error check: invalid inode number
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
      return -EINVALIDINODE;
  }

  // Load inodes into memory
  inode_t inodes[super.num_inodes];
  readInodeRegion(&super, inodes);
  // actual inode in inodes, update to inode will update inodes
  inode_t *inode = &inodes[inodeNumber];

  if (inode->type != UFS_REGULAR_FILE) {
      return -EINVALIDTYPE;
  }
  if (size < 0 || size > MAX_FILE_SIZE) {
      return -EINVALIDSIZE;
  }

  // Calculate the number of blocks needed for the new size
  int newFileBlocks = size / UFS_BLOCK_SIZE;
  if (size % UFS_BLOCK_SIZE != 0) {
      newFileBlocks += 1;
  }

  // Initialize the databitmap
  unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
  readDataBitmap(&super, dataBitmap);

  /*out of storage errors, before modify anything*/ 
  int freeBlocks = 0;
  // go thorugh the entire data region until enough free blocks are found
  for (int j = 0; j < super.num_data && freeBlocks < newFileBlocks; ++j) {
      int byteIndex = j / 8;
      int bitIndex = j % 8;
      if ((dataBitmap[byteIndex] & (1 << bitIndex)) == 0) {
          freeBlocks++;
      }
  }
  if (freeBlocks < newFileBlocks) {
    return -ENOTENOUGHSPACE;
  }

  // Clear existing data blocks
  int currentFileBlocks = inode->size / UFS_BLOCK_SIZE;
  if (inode->size % UFS_BLOCK_SIZE != 0) {
      currentFileBlocks += 1;
  }
  // S I D |Data_region_addr
  // 0 1 2 |3 4 5 6 7 8 9 10
  for (int i = 0; i < currentFileBlocks; ++i) {
    // data region starts at 4th block, subtract it to relatively get the true index
    int blockNumber = inode->direct[i] - super.data_region_addr;
    int byteIndex = blockNumber / 8;
    int bitIndex = blockNumber % 8;
    dataBitmap[byteIndex] &= ~(1 << bitIndex);
    inode->direct[i] = 0;
  }

  // Allocate new blocks for this file and write data into them
  const char *data = (const char *)buffer;
  int bytesToWrite = size;
  int bytesWritten = 0;

  for (int i = 0; i < newFileBlocks && bytesToWrite > 0; ++i) {
      int blockNumber = -1;
      // iterate through all data blocks and find free blocks to write in
      for (int j = 0; j < super.num_data; ++j) {
          int byteIndex = j / 8;
          int bitIndex = j % 8;
          // if there is a free block
          if ((dataBitmap[byteIndex] & (1 << bitIndex)) == 0) {
              blockNumber = j + super.data_region_addr;
              // change it to indicate it's used
              dataBitmap[byteIndex] |= (1 << bitIndex); 
              break;
          }
      }

      // Update the inode to the new blocknum
      inode->direct[i] = blockNumber;
      // Write to that block
      int bytesToCopy = min(UFS_BLOCK_SIZE, bytesToWrite);
      disk->writeBlock(blockNumber, (void*)(data + bytesWritten));
      bytesWritten += bytesToCopy;
      bytesToWrite -= bytesToCopy;
  }

  // Update inode size
  inode->size = size;

  // Write updated inodes and bitmaps back to the disk
  writeInodeRegion(&super, inodes);
  writeDataBitmap(&super, dataBitmap);

  return bytesWritten; // Success: return the number of bytes written
}

// Helper functions, you should read/write the entire inode and bitmap regions
void LocalFileSystem::readSuperBlock(super_t *super){
  // since block 0 is the super block and its size ought to be 4096 bytes, we make a temp buffer to ensure it
  char buffer[UFS_BLOCK_SIZE];
  disk->readBlock(0, buffer);
  memcpy(super, buffer, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap){
    // Calculate the start address and the number of blocks to read for the inode bitmap
    int startBlock = super->inode_bitmap_addr;
    int numBlocks = super->inode_bitmap_len;

    // Loop through the blocks and read them into the inodeBitmap buffer
    for (int i = 0; i < numBlocks; ++i) {
        disk->readBlock(startBlock + i, inodeBitmap + (i * UFS_BLOCK_SIZE));
    }
}
void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap){
    // Calculate the start address and the number of blocks to read for the data bitmap
    int startBlock = super->data_bitmap_addr;
    int numBlocks = super->data_bitmap_len;

    // Loop through the blocks and read them into the dataBitmap buffer
    for (int i = 0; i < numBlocks; ++i) {
        disk->readBlock(startBlock + i, dataBitmap + (i * UFS_BLOCK_SIZE));
    }
}
void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes){
  int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
  int numBlocks = super->inode_region_len;

  for (int i = 0; i < numBlocks; ++i) {
    unsigned char buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->inode_region_addr + i, buffer);
    for (int j = 0; j < inodesPerBlock; ++j) {
        int inodeIndex = i * inodesPerBlock + j;
        if (inodeIndex >= super->num_inodes){
          break;
        } 
        memcpy(&inodes[inodeIndex], buffer + j * sizeof(inode_t), sizeof(inode_t));
    }
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap){
  int startBlock = super->inode_bitmap_addr;
  int numBlocks = super->inode_bitmap_len;

  for (int i = 0; i < numBlocks; ++i) {
      disk->writeBlock(startBlock + i, inodeBitmap + i * UFS_BLOCK_SIZE);
  }
}
void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap){
  int startBlock = super->data_bitmap_addr;
  int numBlocks = super->data_bitmap_len;

  for (int i = 0; i < numBlocks; ++i) {
    disk->writeBlock(startBlock + i, dataBitmap + i * UFS_BLOCK_SIZE);
  }
}
void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes){
  int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
  int numBlocks = super->inode_region_len;
  // for every block allocated for inoderegion
  for (int i = 0; i < numBlocks; ++i) {
      unsigned char buffer[UFS_BLOCK_SIZE];
      // go into each block and update every inode
      for (int j = 0; j < inodesPerBlock; ++j) {
        /*
        a b c
        d e f
        g h i
        represented: a b c d e f g h i
        */
        int inodeIndex = i * inodesPerBlock + j;
        if (inodeIndex >= super->num_inodes){
          break;
        } 
        // Copy the inode info to the buffer
        memcpy(buffer + j * sizeof(inode_t), &inodes[inodeIndex], sizeof(inode_t));
      }
      // write the updated block to the disk
      disk->writeBlock(super->inode_region_addr + i, buffer);
  }
}