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
	std::unique_ptr<Task> dropped;
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		stopping = true;
		dropped = std::move(queue_head);
		queue_tail = nullptr;
	}
	// outside the lock, since destroying a task runs its captures' destructors
	size_t dropped_count = 0;
	while (dropped)
	{
		std::unique_ptr<Task> next = std::move(dropped->next);
		finish(std::move(dropped));
		dropped = std::move(next);
		dropped_count++;
	}
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		pending_tasks -= dropped_count;
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
		task->run(worker_index);
		finish(std::move(task));
		{
			std::lock_guard<std::mutex> lock(tasks_mutex);
			if (--pending_tasks == 0) idle_cv.notify_all();
		}
	}
}

void ThreadManager::finish(std::unique_ptr<Task> task)
{
	TaskGroup* group = task->group;
	task.reset();
	if (group) group->finish_task();
}

void ThreadManager::wait_all()
{
	std::unique_lock<std::mutex> lock(tasks_mutex);
	idle_cv.wait(lock, [this] { return pending_tasks == 0; });
}

ThreadManager& get_thread_manager()
{
	static ThreadManager thread_manager;
	return thread_manager;
}

TaskGroup::~TaskGroup()
{
	wait();
}

void TaskGroup::wait()
{
	std::unique_lock<std::mutex> lock(mutex);
	idle_cv.wait(lock, [this] { return pending_tasks == 0; });
}

void TaskGroup::add_task()
{
	std::lock_guard<std::mutex> lock(mutex);
	pending_tasks++;
}

void TaskGroup::finish_task()
{
	// notifies under the lock, since a waiting owner may destroy the group as soon as it holds the mutex
	std::lock_guard<std::mutex> lock(mutex);
	if (--pending_tasks == 0) idle_cv.notify_all();
}
} // namespace vkte
