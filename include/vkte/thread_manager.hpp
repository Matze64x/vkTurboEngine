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
class TaskGroup;

// Runs tasks on its workers in the order they were queued. Waiting for tasks from inside a task can deadlock, once all
// workers wait for tasks that no worker is left to run.
class ThreadManager
{
public:
	explicit ThreadManager(size_t thread_count = std::thread::hardware_concurrency());
	// runs the queued tasks before it returns
	~ThreadManager();

	// the future gets the task's result or exception, which is lost if the future is discarded
	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run(F&& task);
	// waits for all queued tasks, also those of the groups on it, so not from inside a task, which counts itself
	void wait_all();
	// drops the queued tasks, whose futures report a broken promise, waits for the running ones and stops the workers;
	// tasks run afterwards are dropped too
	void force_stop();
	size_t worker_count() const { return workers.size(); }

private:
	friend class TaskGroup;

	struct Task
	{
		explicit Task(TaskGroup* group) : group(group) {}
		virtual ~Task() = default;
		virtual void run(size_t worker_index) = 0;
		// the group it counts in, if any
		TaskGroup* group;
		std::unique_ptr<Task> next;
	};

	// a task with its result's type, which its future gets
	template <typename ReturnType>
	struct PackagedTask : Task
	{
		PackagedTask(std::packaged_task<ReturnType(size_t)> task, TaskGroup* group) : Task(group), task(std::move(task)) {}
		void run(size_t worker_index) override { task(worker_index); }
		std::packaged_task<ReturnType(size_t)> task;
	};

	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run_in_group(F&& task, TaskGroup* group);
	void worker_loop(size_t worker_index);
	void stop_and_join();
	// destroys the task with its captures before it finishes in its group, so that nothing of it outlives the group's wait
	static void finish(std::unique_ptr<Task> task);

	std::mutex tasks_mutex;
	std::condition_variable tasks_cv;
	std::condition_variable idle_cv;
	std::unique_ptr<Task> queue_head;
	Task* queue_tail = nullptr;
	size_t pending_tasks = 0;
	bool stopping = false;
	std::vector<std::thread> workers;
};

// Tasks on a ThreadManager, which must outlive it, that can be waited for apart from its other tasks; waits for them
// when destroyed. A task counts as finished once it ran and was destroyed with its captures, or once force_stop() dropped
// it.
class TaskGroup
{
public:
	explicit TaskGroup(ThreadManager& thread_manager) : thread_manager(thread_manager) {}
	~TaskGroup();
	TaskGroup(const TaskGroup&) = delete;
	TaskGroup& operator=(const TaskGroup&) = delete;

	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run(F&& task);
	void wait();

private:
	friend class ThreadManager;

	ThreadManager& thread_manager;
	std::mutex mutex;
	std::condition_variable idle_cv;
	size_t pending_tasks = 0;

	void add_task();
	void finish_task();
};

// the process's one pool, created on first use, so that all of its work shares the cores without oversubscribing them
ThreadManager& get_thread_manager();

template <typename F>
std::future<std::invoke_result_t<F, size_t>> ThreadManager::run(F&& task)
{
	return run_in_group(std::forward<F>(task), nullptr);
}

template <typename F>
std::future<std::invoke_result_t<F, size_t>> ThreadManager::run_in_group(F&& task, TaskGroup* group)
{
	using ReturnType = std::invoke_result_t<F, size_t>;
	std::packaged_task<ReturnType(size_t)> packaged_task(std::forward<F>(task));
	std::future<ReturnType> future = packaged_task.get_future();
	// allocated before anything is counted, so that a failed allocation leaves the counts as they were
	std::unique_ptr<Task> new_task = std::make_unique<PackagedTask<ReturnType>>(std::move(packaged_task), group);
	if (group) group->add_task();
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		if (!stopping)
		{
			pending_tasks++;
			Task* raw_task = new_task.get();
			if (queue_tail) queue_tail->next = std::move(new_task);
			else queue_head = std::move(new_task);
			queue_tail = raw_task;
		}
	}
	// no worker is left to run it
	if (new_task)
	{
		finish(std::move(new_task));
		return future;
	}
	tasks_cv.notify_one();
	return future;
}

template <typename F>
std::future<std::invoke_result_t<F, size_t>> TaskGroup::run(F&& task)
{
	return thread_manager.run_in_group(std::forward<F>(task), this);
}
} // namespace vkte
