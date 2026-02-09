#include "SkipList.h"

int main() {
    // 初始化一个最大层级为 6 的跳表
    SkipList<int, std::string> sl(6);

    sl.insert_element(1, "A");
    sl.insert_element(3, "B");
    sl.insert_element(7, "C");
    sl.insert_element(8, "D");
    sl.insert_element(19, "E");

    // 调用你的显示函数
    sl.display_list();

    sl.delete_element(7);
    sl.display_list();

    return 0;
}
