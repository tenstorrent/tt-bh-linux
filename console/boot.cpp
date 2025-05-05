#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include "l2cpu.h"
#include "arc.h"

std::vector<uint8_t> read_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    file.seekg(0, std::ios::end);

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size, 0x0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    return buffer;
}

int main(int argc, char *argv[]){
    L2CPU l2cpu(0);

    std::string filename="../Image";
    std::vector<uint8_t> kernel_disk = read_file(filename);
    std::cout<<kernel_disk.size()<<"\n";

    uint64_t kernel_address = 0x400030200000ULL;
    // std::vector<uint8_t> kernel = l2cpu.read(kernel_address, kernel_disk.size());

    // for(int i=0; i<kernel_disk.size(); i++){
    //     if(kernel_disk[i] != kernel[i])
    //         std::cout<<std::hex<<i<<" "<<kernel_disk[i]<<" "<<kernel[i]<<"\n";
    // }
    std::vector<uint8_t> zeros(kernel_disk.size());
    l2cpu.write(kernel_address, zeros);
    std::vector<uint8_t> kernel = l2cpu.read(kernel_address, kernel_disk.size());
    for(int i=0; i<kernel_disk.size(); i++){
        if(zeros[i] != kernel[i])
            std::cout<<std::hex<<i<<" "<<zeros[i]<<" "<<kernel[i]<<"\n";
    }


}