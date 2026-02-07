//
// Created by 26708 on 2026/2/5.
//

#ifndef NOVAKV_FILEFORMATS_H
#define NOVAKV_FILEFORMATS_H

#include <fstream>
#include <string>
#include <cstdint>

class WritableFile {
    public:
        explicit WritableFile(const std::string& filename)
            : os_(filename, std::ios::binary | std::ios::app) {}

        void Append(const std::string& data) {
            os_.write(data.data(), data.size());
            size_ += data.size();
        }

        uint64_t Size() const { return size_; }

        void Flush() { os_.flush(); }

        void Close() { os_.close(); }

    private:
        std::ofstream os_;
        uint64_t size_ = 0;
};

#endif //NOVAKV_FILEFORMATS_H