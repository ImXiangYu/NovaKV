//
// Created by 26708 on 2026/2/12.
//

#include "DBIterator.h"
#include <algorithm>
#include <cassert>
#include <string>
void DBIterator::Seek(const std::string &start_key) {
    // 把游标定位到第一个 >= start_key 的可见记录
    auto it = std::lower_bound(rows_.begin(), rows_.end(), start_key,
        [](const std::pair<std::string, std::string>& element, const std::string& val) {
            return element.first < val; // 只比较 Key
        });
    pos_ = it - rows_.begin();
}
void DBIterator::Next() {
    if (Valid()) {
        ++pos_;
    }
}
bool DBIterator::Valid() const {
    return pos_ < rows_.size();
}
const std::string & DBIterator::key() const {
    assert(Valid());
    return rows_[pos_].first;
}
const std::string & DBIterator::value() const {
    assert(Valid());
    return rows_[pos_].second;
}
