//
// Shared value metadata for WAL/SST/MemTable paths.
//

#ifndef NOVAKV_VALUERECORD_H
#define NOVAKV_VALUERECORD_H

#include <cstdint>
#include <string>

enum class ValueType : uint8_t {
    kValue = 1,
    kDeletion = 2
};

struct ValueRecord {
    ValueType type;
    std::string value;
};

#endif // NOVAKV_VALUERECORD_H
