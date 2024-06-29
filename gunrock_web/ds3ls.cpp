#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void Recursive_LS(LocalFileSystem &fs, int inodeNumber, const string &full_path) {
  inode_t inode;
  
  if (fs.stat(inodeNumber, &inode) != 0 || inode.type != UFS_DIRECTORY) {
      cerr << "Error: Invalid or non-directory inode " << inodeNumber << endl;
      return;
  }

  // Print the directory path
  cout << "Directory " << full_path << "/" << endl;

  vector<dir_ent_t> directory_entries;
  unsigned char block[UFS_BLOCK_SIZE];
  int totalBytesRead = 0;

  while (totalBytesRead < inode.size) {
    int remaining_bytes_inFile = inode.size - totalBytesRead;
    // call read to compute bytesRead, and use it to find # of entries in this block
    int bytesRead = fs.read(inodeNumber, block, min(UFS_BLOCK_SIZE, remaining_bytes_inFile));
    //calculate how many entries are in this block, since it no need to be full block
    int entriesInBlock = bytesRead / sizeof(dir_ent_t);
    dir_ent_t *entry = (dir_ent_t *)block;
    
    for (int i = 0; i < entriesInBlock; ++i) {
      directory_entries.push_back(entry[i]);
    }

    totalBytesRead += bytesRead;
  }

  // Sort and print the directory entries
  sort(directory_entries.begin(), directory_entries.end(), 
      [](const dir_ent_t &a, const dir_ent_t &b) {
      // it needs to be in lexcio order, a < b --> a before b
      return strcmp(a.name, b.name) < 0;
  });

  for (const auto &entry : directory_entries) {
    cout << entry.inum << "\t" << entry.name << endl;
  }
  cout << endl;

  // Recursively traverse subdirectories
  for (const auto &entry : directory_entries) {
    if (strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0) {
      inode_t entryInode;
      fs.stat(entry.inum, &entryInode);
      // only iterate down when it's a directory
      if (entryInode.type == UFS_DIRECTORY) {
        string new_full_path;
        // for the first root
        if (full_path == "/") {
          new_full_path = full_path + entry.name;
        } else {
          new_full_path = full_path + "/" + entry.name;
        }
        Recursive_LS(fs, entry.inum, new_full_path);
      }
    }
  }
}

void ls_operation(LocalFileSystem &fs, int inodeNumber) {
  Recursive_LS(fs, inodeNumber, "");
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    cout << argv[0] << ": diskImageFile" << endl;
    return 1;
  }
  string diskImageFile = argv[1];

  // Initialize the Disk and LocalFileSystem
  Disk disk(diskImageFile, UFS_BLOCK_SIZE);
  LocalFileSystem fs(&disk);

  // fs.create(UFS_ROOT_DIRECTORY_INODE_NUMBER, UFS_DIRECTORY, "Directory1");
  // fs.create(4, UFS_REGULAR_FILE, "ABCD");
  
  // fs.unlink(4, "ABCD");
  // fs.unlink(UFS_ROOT_DIRECTORY_INODE_NUMBER, "Directory1");
  // fs.unlink(2, "c.txt");
  // starting from the root
  // fs.create(2, UFS_REGULAR_FILE, "ABCD");
  // fs.unlink(2, "ABCD");
  // fs.unlink(1, "b");
  ls_operation(fs, 0);


  return 0;
}
