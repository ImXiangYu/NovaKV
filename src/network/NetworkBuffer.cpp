//
// Created by 26708 on 2026/3/5.
//

#include "network/NetworkBuffer.h"

#include <bits/types/struct_iovec.h>
#include <sys/uio.h>

#include <algorithm>
#include <cerrno>
NetworkBuffer::NetworkBuffer(const size_t initial_size)
    : buffer_(initial_size), read_index_(0), write_index_(0) {}
void NetworkBuffer::EnsureWritableBytes(const size_t len) {
  if (WritableBytes() < len) {
    MakeSpace(len);
  }
  // 如果现有的可写空间已经足够，什么都不做
}
void NetworkBuffer::Append(const char* data, size_t len) {
  // 确保有足够空间
  EnsureWritableBytes(len);
  // 拷入数据
  std::copy_n(data, len, buffer_.begin() + write_index_);
  // 挪指针
  write_index_ += len;
}
void NetworkBuffer::Retrieve(const size_t len) {
  if (len < ReadableBytes()) {
    read_index_ += len;  // 消费掉 len 字节
  } else {
    RetrieveAll();
  }
}
void NetworkBuffer::RetrieveAll() {
  read_index_ = 0;
  write_index_ = 0;
}
const char* NetworkBuffer::FindCRLF() const {
  // 在 [Peek(), Peek() + ReadableBytes()) 范围内查找 "\r\n"
  const auto pos =
      std::search(Peek(), Peek() + ReadableBytes(), "\r\n", "\r\n" + 2);
  return (pos == Peek() + ReadableBytes()) ? nullptr : pos;
}

ssize_t NetworkBuffer::ReadFromFd(const int fd, int* savedErrno) {
  char extra_buffer[65536];
  struct iovec iov[2];

  // 1. 缓存当前剩余可写空间
  const size_t writable = WritableBytes();

  iov[0].iov_base = buffer_.data() + write_index_;
  iov[0].iov_len = writable;
  iov[1].iov_base = extra_buffer;
  iov[1].iov_len = sizeof(extra_buffer);

  // 2. 使用 ssize_t 接收，处理错误
  const ssize_t n = ::readv(fd, iov, 2);

  if (n < 0) {
    if (savedErrno != nullptr) {
      *savedErrno = errno;
    }
    return -1;
  }
  if (static_cast<size_t>(n) <= writable) {
    // 数据全部落入了初始缓冲区
    write_index_ += n;
  } else {
    // 初始缓冲区满了，数据溢出到了 extra_buffer
    write_index_ = buffer_.size();
    // 关键点：使用之前缓存的 writable 变量
    Append(extra_buffer, n - writable);
  }
  return n;
}

void NetworkBuffer::MakeSpace(const size_t len) {
  // 前面读过的废弃空间 + 后面剩下的空间 是否够 len？
  // read_index 之前的空间都是“废弃”的
  if (WritableBytes() + read_index_ < len) {
    // 大小彻底不够了，只能扩容
    buffer_.resize(write_index_ + len);
  } else {
    // 大小够用，把还没读的数据挪到最前边 [read_index, write_index)
    const size_t readable = ReadableBytes();
    std::copy(buffer_.begin() + read_index_, buffer_.begin() + write_index_,
              buffer_.begin());
    read_index_ = 0;
    write_index_ = readable;
  }
}
