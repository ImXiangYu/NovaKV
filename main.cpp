#include <iostream>
#include "skiplist.h"

int main() {
    // 初始化一个最大层级为 6 的跳表
    SkipList<int, std::string> sl(6);

    std::cout << "--- 插入测试数据 ---" << std::endl;
    sl.insert_element(1, "A");
    sl.insert_element(3, "B");
    sl.insert_element(7, "C");
    sl.insert_element(8, "D");
    sl.insert_element(19, "E");

    // 调用你的显示函数
    sl.display_list();

    std::cout << "\n--- 删除元素 7 后 ---" << std::endl;
    sl.delete_element(7);
    sl.display_list();

    return 0;
}