// 核心：跳表的类定义 + 逻辑实现
#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <vector>
#include <random>
template<typename K, typename V>
class SkipList { 
private:
    struct Node{
        K key;
        V value;
        // 存储每一层后继结点的指针数组
        std::vector<Node*> next;

        Node(K k, V v, int level): key(k), value(v), next(level, nullptr){}
    };

    int max_level;      // 跳表允许的最大层高
    int current_level;  // 当前跳表的实际最高层高
    Node* head;         // 头节点（哨兵）
    int node_count;     // 元素个数

public:
    SkipList(int max_level = 16): max_level(max_level), current_level(0), node_count(0){
        head = new Node(K(), V(), max_level);
    }
    ~SkipList();
    // 1. 核心增删改查
    bool insert_element(K key, V value);
    bool search_element(K key, V& value);
    bool delete_element(K key);

    // 2. 辅助功能
    int size() const { return node_count; }
    void display_list(); // 打印结构，方便调试

private:
    Node* create_node(K k, V v, int level);

    int get_random_level(int max_level = 16, float p = 0.5f) {
        // 静态变量确保生成器只初始化一次，提升性能并保证随机性
        static std::mt19937 gen(std::random_device{}());
        static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

        int level = 1;
        while (dis(gen) < p && level < max_level) {
            level++;
        }
        return level;
    }
};



#endif