#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


void print_inode_bitmap(super_t& super, LocalFileSystem &fs){
    vector<unsigned char> inodeBitmap(super.inode_bitmap_len * UFS_BLOCK_SIZE);
    fs.readInodeBitmap(&super, inodeBitmap.data());
    
    cout << "Inode bitmap" << endl;
    for (size_t i = 0; i < inodeBitmap.size(); ++i) {
        cout << (unsigned int) inodeBitmap[i] << " ";
    }
    cout << endl << endl;
}

void print_data_bitmap(super_t& super, LocalFileSystem &fs){
    vector<unsigned char> dataBitmap(super.data_bitmap_len * UFS_BLOCK_SIZE);
    fs.readDataBitmap(&super, dataBitmap.data());

    cout << "Data bitmap" << endl;
    for (size_t i = 0; i < dataBitmap.size(); ++i) {
        cout << (unsigned int) dataBitmap[i] << " ";
    }
    cout << endl;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <disk image file>" << endl;
        return 1;
    }

    string diskImageFile = argv[1];

    // Initialize the Disk and LocalFileSystem
    Disk disk(diskImageFile, UFS_BLOCK_SIZE);

    LocalFileSystem fs(&disk);

    // Read the superblock
    super_t super;
    fs.readSuperBlock(&super);

    // Print the superblock metadata
    cout << "Super" << endl;
    cout << "inode_region_addr " << super.inode_region_addr << endl;
    cout << "data_region_addr " << super.data_region_addr << endl;
    cout << endl;

    // Read and print the inode bitmap
    print_inode_bitmap(super, fs);

    // Read and print the data bitmap
    print_data_bitmap(super, fs);

    return 0;
}
