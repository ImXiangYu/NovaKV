//
// Created by 26708 on 2026/2/12.
//

#ifndef NOVAKV_DBITERATOR_H
#define NOVAKV_DBITERATOR_H
#include <memory>
#include <vector>

class DBIterator {
 public:
  explicit DBIterator(std::vector<std::pair<std::string, std::string>>&& rows);
  void Seek(const std::string& start_key);
  void Next();
  bool Valid() const;
  const std::string& key() const;
  const std::string& value() const;

 private:
  std::vector<std::pair<std::string, std::string>> rows_;
  size_t pos_;
};

#endif  // NOVAKV_DBITERATOR_H