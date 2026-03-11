#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>
namespace Ayu {
class ThreadPool {
 public:
  // 构造函数：启动线程
  explicit ThreadPool(const size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
      workers.emplace_back([this] {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock lock(this->queue_mutex);
            // 等待：直到线程池停止 或 任务队列不为空
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });

            // 关键：只有当 stop 为 true 且 任务已经全部处理完时，才真正退出
            if (this->stop && this->tasks.empty()) {
              return;
            }

            task = std::move(this->tasks.front());
            this->tasks.pop();
          }
          task();  // 在锁外执行
        }
      });
    }
  }

  // 提交任务
  template <class F, class... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<std::result_of_t<F(Args...)>> {
    using return_type = std::result_of_t<F(Args...)>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock lock(queue_mutex);
      if (stop) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
      }
      tasks.emplace([task] { (*task)(); });
    }
    condition.notify_one();
    return res;
  }

  void Shutdown() {
    {
      std::unique_lock lock(queue_mutex);
      if (stop) {
        return;
      }
      stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  // 析构函数：优雅关闭
  ~ThreadPool() {
    Shutdown();
  }

  // 禁止拷贝构造和赋值（线程池不应该是可拷贝的）
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

 private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};
}  // namespace Ayu

#endif