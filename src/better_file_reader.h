#pragma once
#include <stdint.h>
#include <string>
#include <fstream>

class BetterFileReader {
public:
    BetterFileReader(std::string path) {
        file = std::ifstream(path.c_str(), std::ios::binary);
        valid = file.good();
    }

    uint32_t getSampleRate() {
        return sampleRate;
    }
    
    void setSampleRate(uint32_t sr) {
        sampleRate = sr;
    }

    bool isValid() {
        return valid && file.good();
    }

    void readSamples(void* data, size_t size) {
        char* _data = (char*)data;
        file.read(_data, size);
        int read = file.gcount();
        if (read < size) {
            file.clear();
            file.seekg(0);
            file.read(&_data[read], size - read);
        }
        bytesRead += size;
    }

    void rewind() {
        file.clear();
        file.seekg(0);
    }

    void close() {
        file.close();
    }

private:
    bool valid = false;
    std::ifstream file;
    size_t bytesRead = 0;
    uint32_t sampleRate = 1000000;
};
