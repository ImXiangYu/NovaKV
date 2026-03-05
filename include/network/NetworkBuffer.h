//
// Created by 26708 on 2026/3/5.
//

#ifndef NOVAKV_NETWORKBUFFER_H
#define NOVAKV_NETWORKBUFFER_H
#include <vector>

class NetworkBuffer {
 public:
  explicit NetworkBuffer(size_t initial_size = 1024);
  ~NetworkBuffer() = default;

  // 返回 write_index_ - read_index_，即当前缓冲区里有多少没解析的数据
  [[nodiscard]] size_t ReadableBytes() const {
    return write_index_ - read_index_;
  };
  // 返回 buffer_.size() - write_index_，即还能往里塞多少数据
  [[nodiscard]] size_t WritableBytes() const {
    return buffer_.size() - write_index_;
  };
  // 如果空间不够，自动扩容（std::vector::resize）
  void EnsureWritableBytes(size_t len);

  // 将数据拷入缓冲区，并移动 write_index_
  void Append(const char* data, size_t len);
  // 返回指向 buffer_[read_index_] 的指针，用于观察数据但不取出
  [[nodiscard]] const char* Peek() const {
    return buffer_.data() + read_index_;
  };
  // 当解析完 len 长度的数据后，移动 read_index_
  void Retrieve(size_t len);
  // 清空缓冲区（重置两个 index 为 0）
  void RetrieveAll();

  // 在当前可读区域查找 \r\n。RESP 协议的所有行都以 CRLF
  // 结尾，这是解析器的“锚点” 返回找到的 \r\n 的位置指针
  [[nodiscard]] const char* FindCRLF() const;

  // 一次性尽量读完 socket 里的所有数据，减少 epoll_wait 的系统调用次数
  size_t ReadFromFd(int fd);

 private:
  std::vector<char> buffer_;  // 底层存储容器
  size_t read_index_;         // 记录下一个要读取/解析的字节位置
  size_t write_index_;  // 记录下一个要写入（从 socket 读入数据）的字节位置

  // 空间挪动优化
  void MakeSpace(size_t len);
};

#endif  // NOVAKV_NETWORKBUFFER_H
