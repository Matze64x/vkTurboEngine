#include "vkte/thread_manager.hpp"

namespace vkte
{
ThreadManager::ThreadManager(size_t thread_count)
{
	if (thread_count == 0) thread_count = 1;
	workers.reserve(thread_count);
	for (size_t i = 0; i < thread_count; i++) workers.emplace_back([this, i] { worker_loop(i); });
}

ThreadManager::~ThreadManager()
{
	stop_and_join();
}

void ThreadManager::stop_and_join()
{
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		stopping = true;
	}
	tasks_cv.notify_all();
	for (std::thread& worker : workers) if (worker.joinable()) worker.join();
}

void ThreadManager::force_stop()
{
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		while (queue_head)
		{
			queue_head = std::move(queue_head->next);
			pending_tasks--;
		}
		queue_tail = nullptr;
		if (pending_tasks == 0) idle_cv.notify_all();
	}
	stop_and_join();
}

void ThreadManager::worker_loop(size_t worker_index)
{
	while (true)
	{
		std::unique_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(tasks_mutex);
			tasks_cv.wait(lock, [this] { return queue_head != nullptr || stopping; });
			if (!queue_head) return; // an empty queue only wakes us once 'stopping' is set
			task = std::move(queue_head);
			queue_head = std::move(task->next);
			if (!queue_head) queue_tail = nullptr;
		}
		task->func(worker_index);
		{
			std::lock_guard<std::mutex> lock(tasks_mutex);
			if (--pending_tasks == 0) idle_cv.notify_all();
		}
	}
}

void ThreadManager::wait_all()
{
	std::unique_lock<std::mutex> lock(tasks_mutex);
	idle_cv.wait(lock, [this] { return pending_tasks == 0; });
}
} // namespace vkte
