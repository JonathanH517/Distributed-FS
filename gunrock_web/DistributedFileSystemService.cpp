#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <cstring>
#include <algorithm>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"

using namespace std;



DistributedFileSystemService::DistributedFileSystemService(string diskFile) : HttpService("/ds3/") {
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}  

vector<string> handleGetPath(const string &path) {
    // keep the rest
    vector<string> components;
    stringstream stringFile(path);
    string item;
    
    while (getline(stringFile, item, '/')) {
        if (!item.empty()) {
            components.push_back(item);
        }
    }
    components.erase(components.begin());
    return components;
}

void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) {
  // Extract and validate path
  vector<string> components = handleGetPath(request->getPath());

  // Traverse the directory structure to find the target file or directory
  int currentInodeNum = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  LocalFileSystem *fs = this->fileSystem;

  // Check if it exists
  for (size_t i = 0; i < components.size(); ++i) {
    int nextInodeNum = fs->lookup(currentInodeNum, components[i]);
    if (nextInodeNum < 0) {
      throw ClientError::notFound();
    }
    currentInodeNum = nextInodeNum;
  }

  // Load the inode information
  inode_t targetInode;
  if (fs->stat(currentInodeNum, &targetInode) != 0) {
    throw ClientError::notFound();
  }

  if (targetInode.type == UFS_REGULAR_FILE) {
    // Allocate buffer dynamically to handle large files
    vector<unsigned char> buffer(targetInode.size);
    int bytesRead = fs->read(currentInodeNum, buffer.data(), targetInode.size);
    if (bytesRead < 0) {
      throw ClientError::notFound();
    }
    response->setBody(string(reinterpret_cast<char*>(buffer.data()), bytesRead));
  } else if (targetInode.type == UFS_DIRECTORY) {
    vector<string> entries;
    unsigned char blockBuffer[UFS_BLOCK_SIZE];
    // Calculate # of blocks it used
    int numBlocks = targetInode.size / UFS_BLOCK_SIZE;
    if ((targetInode.size % UFS_BLOCK_SIZE) != 0) {
      numBlocks += 1;
    }

    for (int i = 0; i < numBlocks; ++i) {
      if (targetInode.direct[i] != 0) {
        fs->disk->readBlock(targetInode.direct[i], blockBuffer);
        dir_ent_t* dirEntries = (dir_ent_t*)(blockBuffer);

        // Blocks can have different # of entries
        int numEntries = (targetInode.size % UFS_BLOCK_SIZE) / sizeof(dir_ent_t);
        if (i == numBlocks - 1) { // in case the last block is not full
          numEntries = (targetInode.size % UFS_BLOCK_SIZE) / sizeof(dir_ent_t);
        } else {
          numEntries = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
        }

        // for every entry in a block
        for (int j = 0; j < numEntries; ++j) {
          if (strcmp(dirEntries[j].name, ".") != 0 && strcmp(dirEntries[j].name, "..") != 0) {
            inode_t entryInode;
            string entryName = dirEntries[j].name;
            if (fs->stat(dirEntries[j].inum, &entryInode) == 0) {
              if (entryInode.type == UFS_DIRECTORY) { // if entry is a dir, add '/' then list
                entryName += "/";
              }
              entries.push_back(entryName);
            }
          }
        }
      }
    }

    sort(entries.begin(), entries.end());
    string body;
    for (const auto& entry : entries) {
      body += entry + "\n";
    }
    response->setBody(body);
  } else { // not supported type
    throw ClientError::notFound();
  }

  response->setStatus(200);
}



void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) {
  // Extract and validate path
  this->fileSystem->disk->beginTransaction();
  vector<string> components = handleGetPath(request->getPath());

  // Extract the data from the request
  string data = request->getBody();

  // Traverse the directory structure and create directories if needed
  int currentInodeNum = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  LocalFileSystem *fs = this->fileSystem;

  for (size_t i = 0; i < components.size() - 1; ++i) {
    // iterately look for the next directory
    int nextInodeNum = fs->lookup(currentInodeNum, components[i]);
    if (nextInodeNum < 0) { // entry does not exist, create it
      if (nextInodeNum == -ENOTFOUND) {
        int newDirInodeNum = fs->create(currentInodeNum, UFS_DIRECTORY, components[i]);
        if (newDirInodeNum < 0) {
          this->fileSystem->disk->rollback(); // Roll back on error
          throw ClientError::insufficientStorage();
        }
        currentInodeNum = newDirInodeNum;
      } else {
        throw ClientError::badRequest();
      }
    } else { // entry exists, check for if it's a directory
      inode_t inode;
      if (fs->stat(nextInodeNum, &inode) < 0 || inode.type != UFS_DIRECTORY) {
        throw ClientError::conflict();
      }
      currentInodeNum = nextInodeNum;
    }
  }

  // Get the file name
  string fileName = components[components.size() - 1];

  // Check if the final path component already exists as a file or directory
  int fileInode = fs->lookup(currentInodeNum, fileName);
  if (fileInode > 0) {
    // File exists, overwrite it
    if (fs->write(fileInode, data.c_str(), data.size()) < 0) {
      this->fileSystem->disk->rollback(); // Roll back on error
      throw ClientError::insufficientStorage();
    }
  } else {
    // Create a new file
    int newFileInodeNum = fs->create(currentInodeNum, UFS_REGULAR_FILE, fileName);
    if (newFileInodeNum < 0) {
      this->fileSystem->disk->rollback(); // Roll back on error
      throw ClientError::insufficientStorage();
    }
    
    if (fs->write(newFileInodeNum, data.c_str(), data.size()) < 0) {
      this->fileSystem->disk->rollback(); // Roll back on error
      throw ClientError::insufficientStorage();
    }
  }

  // Set the response status to 200 OK
  this->fileSystem->disk->commit();
  response->setStatus(200);
}




void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) {
    vector<string> components = handleGetPath(request->getPath());
    if (components.empty()) {
        throw ClientError::badRequest();
    }

    int currentInodeNum = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    int parentInodeNum = currentInodeNum;

    // a b c.txt
    for (size_t i = 0; i < components.size(); ++i) {
        parentInodeNum = currentInodeNum;
        currentInodeNum = this->fileSystem->lookup(currentInodeNum, components[i]);
        if (currentInodeNum < 0) {
            this->fileSystem->disk->rollback(); // Roll back on error
            throw ClientError::notFound();
        }
    }

    string entryName = components.back();
    // Start the transaction before making changes
    this->fileSystem->disk->beginTransaction();
    int ret = this->fileSystem->unlink(parentInodeNum, entryName);
    if (ret != 0) {
        this->fileSystem->disk->rollback(); // Roll back on error
        throw ClientError::badRequest();
    }

    this->fileSystem->disk->commit(); // Commit the transaction if all is well
    response->setStatus(200);
}