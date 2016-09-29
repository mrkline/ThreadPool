#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool final {
public:
    // the constructor just launches some amount of workers
    ThreadPool(size_t threads_n = std::thread::hardware_concurrency()) : stop(false)
    {
        if(threads_n <= 0)
            throw std::invalid_argument("more than zero threads expected");

        workers.reserve(threads_n);
        for(; threads_n > 0; --threads_n)
            workers.emplace_back(
                [this]
                {
                    while(true)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex);
                            condition.wait(lock,
                                [this]{ return stop || !tasks.empty(); });
                            if(stop && tasks.empty()) return;
                            task = std::move(tasks.front());
                            tasks.pop();
                        }

                        task();
                    }
                }
            );
    }

    // Disable copy and move
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // add new work item to the pool
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type> enqueue(F&& f, Args&&... args)
    {
        using packaged_task_t = std::packaged_task<typename std::result_of<F(Args...)>::type ()>;

        packaged_task_t* task = new packaged_task_t(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        auto res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.emplace([task](){ (*task)(); delete task; });
        }
        condition.notify_one();
        return res;
    }
    // the destructor joins all threads
    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
            condition.notify_all();
        }
        for(std::thread& worker : workers)
            worker.join();
    }
private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::function<void()>> tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
