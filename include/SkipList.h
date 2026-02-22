// 核心：跳表的类定义 + 逻辑实现
#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <atomic>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

template <typename K, typename V>
class SkipList {
 public:
  struct Node {
    K key;
    V value;
    // 存储每一层后继结点的指针数组
    // std::vector<Node*> next;
    // 用atomic改造next
    std::vector<std::atomic<Node*>> next;

    Node(K k, V v, int level) : key(k), value(v), next(level) {
      // atomic不可拷贝，因此不能全初始化为nullptr
      for (int i = 0; i < level; i++) {
        next[i].store(nullptr);  // 只能用store
      }
    }
  };

 private:
  int max_level;                   // 跳表允许的最大层高
  std::atomic<int> current_level;  // 当前跳表的实际最高层高
  Node* head;                      // 头节点（哨兵）
  std::atomic<int> node_count;     // 元素个数

 public:
  SkipList(int max_level = 16)
      : max_level(max_level), current_level(0), node_count(0) {
    head = new Node(K(), V(), max_level);
  }
  ~SkipList() {
    // 跳表有很多层，但我们不需要每一层都去删，因为所有层级的指针指向的其实是同一个
    // Node 对象。 我们只需要沿着最底层的“主干道”（即
    // next[0]）把所有楼拆了就行。
    Node* curr = head->next[0];  // 从第 0 层的第一个有效节点开始
    while (curr) {
      Node* next_node = curr->next[0].load();  // 1. 先记住下一个人的地址
      delete curr;                             // 2. 放心大胆地把当前节点拆了
      curr = next_node;                        // 3. 挪到下一个人那里
    }
    delete head;  // 最后把那个一直带路的“哨兵楼”也拆了
  }
  // 1. 核心增删改查
  bool insert_element(K key, V value) {
    Node* curr = head;
    std::vector<Node*> update(max_level, head);

    // 1. 寻找每一层的前驱
    for (int i = current_level - 1; i >= 0; i--) {
      while (curr->next[i] && curr->next[i].load()->key < key) {
        curr = curr->next[i];
      }
      update[i] = curr;
    }

    curr = curr->next[0];
    // 2. 重复性检查
    if (curr && curr->key == key) {
      curr->value = value;  // 示例：直接覆盖
      return true;
    }

    // 3. 处理随机层数
    int level = get_random_level();
    if (level > current_level) {
      for (int i = current_level; i < level; i++) {
        update[i] = head;
      }
      current_level = level;
    }

    // 4. 创建并插入
    Node* new_node = create_node(key, value, level);
    for (int i = 0; i < level; i++) {
      // new_node->next[i] = update[i]->next[i];
      // update[i]->next[i] = new_node;
      // 改成load和store
      new_node->next[i].store(update[i]->next[i].load());
      update[i]->next[i].store(new_node);
    }

    ++node_count;
    return true;
  }
  bool search_element(K key, V& value) const {
    // 1. 从 head 开始，从当前最高层 (current_level - 1) 往下找
    Node* curr = head;
    for (int i = current_level - 1; i >= 0; i--) {
      Node* next_node = curr->next[i].load();  // 原子加载
      // 2. 在每一层中，只要“下一个节点的 key”小于“目标 key”，就一直向右走
      while (next_node && next_node->key < key) {
        curr = next_node;
        next_node = curr->next[i].load();
      }
      // 3. 如果当前层走不动了（下一节点大于目标或为空），就下降一层
      // 4. 重复上述过程，直到降到第 0 层
      // 这里相当于已经默认了走不动了后自动向下一层
    }
    // 5. 检查第 0 层的下一个节点：
    //    - 如果 key 相等，把 value 存入参数并返回 true
    //    - 否则，说明 key 不存在，返回 false
    if (curr->next[0] && curr->next[0].load()->key == key) {
      value = curr->next[0].load()->value;
      return true;
    } else {
      return false;
    }
  }
  bool delete_element(K key) {
    // 1. 同样定义 update[max_level] 数组，记录每一层目标节点的前驱
    std::vector<Node*> update(max_level, head);
    // 2. 从最高层开始向下寻找，填充 update 数组
    Node* curr = head;
    for (int i = current_level - 1; i >= 0; i--) {
      while (curr->next[i] && curr->next[i].load()->key < key) {
        curr = curr->next[i];
      }
      update[i] = curr;
    }
    // 3. 检查第 0 层的下一个节点是否是要删的 key
    //    - 如果不是，直接返回 false（没找到）
    // 4. 如果找到了，从第 0 层向上遍历：
    //    - 如果 update[i] 的下一个节点是我们要删的节点，就把它“跳过去”
    //    - 如果某一层 update[i] 指向的不是该节点，说明更高层也没有了，停止循环
    // 5. delete 该节点内存，node_count--
    if (curr->next[0] && curr->next[0].load()->key == key) {
      // 确定了，要删的就是这个next[0]
      Node* del_node = curr->next[0];
      for (int i = 0; i < current_level; i++) {
        if (update[i]->next[i].load() != del_node) {
          break;
        }
        // 关键动作：跳过去
        update[i]->next[i].store(del_node->next[i].load());
      }
      // 删除结点
      delete del_node;
      --node_count;
    } else {
      return false;
    }
    // 6. 善后：检查
    // current_level，如果删除了某层唯一的节点，导致高层变空，记得降层
    // 只要最高层没有后继节点，且层数还没降到 0，就一直降
    while (current_level > 0 && head->next[current_level - 1] == nullptr) {
      --current_level;
    }
    return true;
  }

  // 2. 辅助功能
  int size() const { return node_count; }
  void display_list() {
    // 1. 获取基准 key（逻辑不变）
    std::vector<K> keys;
    Node* base = head->next[0].load();
    while (base) {
      keys.push_back(base->key);
      base = base->next[0];
    }

    for (int i = current_level - 1; i >= 0; i--) {
      std::cout << "Level " << i << ": ";
      Node* curr = head->next[i];

      for (const auto& k : keys) {
        // --- 核心改进：万能转字符串 ---
        std::stringstream ss;
        ss << k;
        std::string key_str = ss.str();
        int width = key_str.length() + 3;  // 3 是后面 "---" 的长度
        // ---------------------------

        if (curr && curr->key == k) {
          std::cout << key_str << "---";
          curr = curr->next[i];
        } else {
          // 根据不同 key 的实际长度继续向后延伸直到后继
          std::cout << std::string(width, '-');
        }
      }
      std::cout << "nullptr" << std::endl;
    }
  }

  // --- 迭代器类定义 ---
  class Iterator {
   public:
    // 初始化迭代器指向某个节点
    explicit Iterator(Node* node) : current_(node) {}

    // 1. 获取 Key
    const K& key() const { return current_->key; }

    // 2. 获取 Value
    const V& value() const { return current_->value; }

    // 3. 移动到下一个节点 (Level 0)
    void Next() {
      if (current_) {
        // 因为 next 是 atomic，所以要 load
        current_ = current_->next[0].load();
      }
    }

    // 4. 判断是否还有效
    bool Valid() const { return current_ != nullptr; }

   private:
    Node* current_;
  };

  // --- 获取迭代器的接口 ---
  Iterator begin() {
    // 返回第 0 层的第一个有效节点
    return Iterator(head->next[0].load());
  }

 private:
  static Node* create_node(K k, V v, int level) {
    return new Node(k, v, level);
  }

  int get_random_level() const {
    // 静态变量确保生成器只初始化一次，提升性能并保证随机性
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    int level = 1;
    while (dis(gen) < 0.5f && level < max_level) {
      level++;
    }
    return level;
  }
};

#endif