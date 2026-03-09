//
// Created by 26708 on 2026/3/8.
//

#ifndef NOVAKV_RESPENCODER_H
#define NOVAKV_RESPENCODER_H
#include <string>

#include "NetworkBuffer.h"

class RESPEncoder {
 public:
  /*
      格式：+<内容>\r\n
      职责：用于返回简短的、非二进制安全的成功状态。
      作用：最典型的用法是返回 +OK\r\n，表示 SET 或 DEL 操作成功。
      实现思路：直接拼接 +、内容和 \r\n 写入 Buffer。
   */
  static void EncodeSimpleString(NetworkBuffer* buffer, const std::string& str);

  /*
      格式：-ERR <描述>\r\n
      职责：向客户端报告异常情况。
      作用：当客户端发送了不支持的命令、参数错误或数据库内部发生故障时使用（如
     -ERR key not found\r\n）。 实现思路：类似 Simple String，前缀换成
     -。通常建议带上 ERR 前缀以符合 Redis 惯例。
   */
  static void EncodeError(NetworkBuffer* buffer, const std::string& msg);

  /*
      Integer (整数)
      格式：:<数字>\r\n
      职责：返回 64 位带符号整数。
      作用：在 Redis 中，DEL 命令通常返回被删除的 key
     数量，或者用于某些计数逻辑。 实现思路：将 int64_t
     转为字符串（std::to_string），前后拼接 : 和 \r\n。
   */
  static void EncodeInteger(NetworkBuffer* buffer, int64_t val);

  /*
      格式：$<长度>\r\n<内容>\r\n
      职责：二进制安全地传输任意长度的数据（包括 \0 或换行符）。
      作用：这是最核心的接口！GET 命令查到的 value 必须通过它返回，因为 value
     可能是图片、序列化对象等二进制流。 实现思路：
        1. 先写入 $ 和长度字符串。
        2. 写入第一个 \r\n。
        3. 写入原始数据（使用 buffer->Append(str.data(), str.size())
     确保二进制安全）。
        4. 写入结尾的 \r\n。
   */
  static void EncodeBulkString(NetworkBuffer* buffer, const std::string& str);

  /*
      Null (空响应)
      格式：$-1\r\n
      职责：表示“不存在”。
      作用：当 GET 一个不存在的 Key 或 Key 已被删除时，返回给客户端的特殊标记。
      实现思路：直接向 Buffer Append 固定字符串 $-1\r\n。
   */
  static void EncodeNull(NetworkBuffer* buffer);

  /*
      Array (数组)
      格式：*<元素数量>\r\n<元素1><元素2>...
      职责：封装一组 RESP 元素。
      作用：用于 RSCAN 命令返回 Key 列表，或者某些多值操作。
      实现思路：写入 * 和数量，随后循环调用上述其他 Encode 方法。
   */
  static void EncodeArray(NetworkBuffer* buffer,
                          const std::vector<std::string>& elements);

 private:
  constexpr static auto kCRLF = "\r\n";
};

#endif  // NOVAKV_RESPENCODER_H
