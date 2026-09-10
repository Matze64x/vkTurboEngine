#pragma once

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace vkte
{
class ThreadManager
{
public:
	explicit ThreadManager(size_t thread_count = std::thread::hardware_concurrency());
	~ThreadManager();

	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run(F&& task);
	void wait_all();
	void force_stop();
	size_t worker_count() const { return workers.size(); }

private:
	struct Task
	{
		explicit Task(std::packaged_task<void(size_t)> func) : func(std::move(func)) {}
		std::packaged_task<void(size_t)> func;
		std::unique_ptr<Task> next;
	};

	void worker_loop(size_t worker_index);
	void stop_and_join();

	std::mutex tasks_mutex;
	std::condition_variable tasks_cv;
	std::condition_variable idle_cv;
	std::unique_ptr<Task> queue_head;
	Task* queue_tail = nullptr;
	size_t pending_tasks = 0;
	bool stopping = false;
	std::vector<std::thread> workers;
};

template <typename F>
std::future<std::invoke_result_t<F, size_t>> ThreadManager::run(F&& task)
{
	using ReturnType = std::invoke_result_t<F, size_t>;
	std::packaged_task<ReturnType(size_t)> inner_task(std::forward<F>(task));
	std::future<ReturnType> future = inner_task.get_future();
	std::packaged_task<void(size_t)> erased_task([inner_task = std::move(inner_task)](size_t worker_index) mutable { inner_task(worker_index); });
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		pending_tasks++;
		std::unique_ptr<Task> new_task = std::make_unique<Task>(std::move(erased_task));
		Task* raw_task = new_task.get();
		if (queue_tail) queue_tail->next = std::move(new_task);
		else queue_head = std::move(new_task);
		queue_tail = raw_task;
	}
	tasks_cv.notify_one();
	return future;
}
} // namespace vkte
