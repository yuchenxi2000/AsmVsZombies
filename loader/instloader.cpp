// 在PvZ可执行文件中安装钩子，使其加载同目录下的avzloader.dll
#include <iostream>
#include <stdlib.h>

int main(int argc, char * argv[]) {
    // Modify entry point 0x61EBF2 -> 0x6510B0
    size_t pos1 = 0x00000120;
    const char data1[] = "\xB0\x10\x25";

    // ascii str "avzloader.dll", 0
    size_t pos2 = 0x00251090;
    const char data2[] = "\x61\x76\x7A\x6C\x6F\x61\x64\x65\x72\x2E\x64\x6C\x6C";

    // New entry point: push "avzloader.dll"; call LoadLibraryA; jmp 0x61EBF2(old entry point)
    size_t pos3 = 0x002510B0;
    const char data3[] = "\x68\x90\x10\x65\x00\xFF\x15\xA4\x20\x65\x00\xE9\x32\xDB\xFC\xFF";

    // read PvZ executable
    FILE * fp1 = fopen("PlantsVsZombies.exe", "rb");
    if (!fp1) {
        // try to get path from args
        if (argc >= 1) {
            fp1 = fopen(argv[1], "rb");
        }
        if (!fp1) {
            std::cerr << "Failed to find PlantsVsZombies.exe" << std::endl;
            return 1;
        }
    }
    size_t buffer_size = 4000000;
    char * buffer = new char[buffer_size];
    size_t num_read_bytes = fread(buffer, 1, buffer_size, fp1);
    fclose(fp1);
    if (num_read_bytes >= buffer_size) {
        std::cerr << "Your executable is oversized! The size of game executable should be 2,938KB" << std::endl;
        delete [] buffer;
        return 1;
    }

    // modify
    memcpy(buffer + pos1, data1, sizeof(data1));
    memcpy(buffer + pos2, data2, sizeof(data2));
    memcpy(buffer + pos3, data3, sizeof(data3));

    // write modded executable
    FILE * fp2 = fopen("PlantsVsZombies_modded.exe", "wb");
    if (!fp2) {
        std::cerr << "Failed to create new file PlantsVsZombies_modded.exe" << std::endl;
        delete [] buffer;
        return 1;
    }
    fwrite(buffer, 1, num_read_bytes, fp2);
    fclose(fp2);
    delete [] buffer;

    std::cout << "Success." << std::endl;
    system("pause");  // 别一下子退出了

    return 0;
}
