#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
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

// Runs tasks on its workers, each from a TaskGroup's queue: the next task comes from the groups that are waited for first,
// then from those of the highest priority, taking turns between groups of the same priority, so that every group
// progresses while it isn't outranked; a running task is never interrupted, so tasks should be short. Threads that wait
// don't run tasks themselves, and a task must not wait for tasks, which TaskGroup::wait() asserts, since all workers could
// end up waiting for tasks that no worker is left to run. A task gets the index of the worker that runs it, below get_worker_count(),
// which runs one task at a time, so it can key per-worker state. Tasks without a group run in one of priority 0.
class ThreadManager
{
public:
	explicit ThreadManager(size_t thread_count = std::thread::hardware_concurrency());
	// runs the queued tasks, also those they queue, before it returns
	~ThreadManager();

	// the future gets the task's result or exception, which is lost if the future is discarded
	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run(F&& task);
	size_t get_worker_count() const { return workers.size(); }
	// whether the calling thread is one of its workers, running a task
	bool is_worker_thread() const;

private:
	friend class TaskGroup;

	struct Task
	{
		explicit Task(TaskGroup& group) : group(group) {}
		virtual ~Task() = default;
		virtual void run(size_t worker_index) = 0;
		TaskGroup& group;
		std::unique_ptr<Task> next;
	};

	// a task with its result's type, which its future gets
	template <typename ReturnType>
	struct PackagedTask : Task
	{
		PackagedTask(std::packaged_task<ReturnType(size_t)> task, TaskGroup& group) : Task(group), task(std::move(task)) {}
		void run(size_t worker_index) override { task(worker_index); }
		std::packaged_task<ReturnType(size_t)> task;
	};

	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run_in_group(F&& task, TaskGroup& group);
	void queue_task(std::unique_ptr<Task> task);
	// under the lock
	std::unique_ptr<Task> take_next_task();
	void worker_loop(size_t worker_index);
	void wait_for_group(TaskGroup& group);
	// destroys the task with its captures before it finishes in its group, so that nothing of it outlives the group's wait
	void finish(std::unique_ptr<Task> task);

	// guards the groups' queues and counts too
	std::mutex mutex;
	std::condition_variable work_cv;
	// those with queued tasks; one that a task was taken from goes to the back, so that equal ones take turns
	std::vector<TaskGroup*> queued_groups;
	bool stopping = false;
	// of the tasks without a group
	std::unique_ptr<TaskGroup> default_group;
	// last, so that the workers are joined before the rest is destroyed
	std::vector<std::thread> workers;
};

// Tasks on a ThreadManager in their own queue, which can be waited for apart from the others; the ThreadManager must
// outlive the group, which waits for its tasks when destroyed. A task counts as finished once it ran and was destroyed
// with its captures.
class TaskGroup
{
public:
	// higher priorities run first
	explicit TaskGroup(ThreadManager& thread_manager, int32_t priority = 0) : thread_manager(thread_manager), priority(priority) {}
	~TaskGroup();
	TaskGroup(const TaskGroup&) = delete;
	TaskGroup& operator=(const TaskGroup&) = delete;

	// the future gets the task's result or exception, which is lost if the future is discarded
	template <typename F>
	std::future<std::invoke_result_t<F, size_t>> run(F&& task);
	// meanwhile its tasks run before those of groups that aren't waited for
	void wait();

private:
	friend class ThreadManager;

	ThreadManager& thread_manager;
	int32_t priority;
	// the rest is guarded by the ThreadManager's mutex
	std::unique_ptr<ThreadManager::Task> queue_head;
	ThreadManager::Task* queue_tail = nullptr;
	// queued and running
	size_t pending_tasks = 0;
	size_t waiting_threads = 0;
	std::condition_variable idle_cv;
};

// The process's one pool, created on first use, so that all of its work shares the cores without oversubscribing them.
// It is destroyed at exit after the static objects constructed before its first use, so their groups must be done by then,
// and exit() must not be called from one of its tasks, whose worker it would wait for.
ThreadManager& get_thread_manager();

template <typename F>
std::future<std::invoke_result_t<F, size_t>> ThreadManager::run(F&& task)
{
	return run_in_group(std::forward<F>(task), *default_group);
}

template <typename F>
std::future<std::invoke_result_t<F, size_t>> ThreadManager::run_in_group(F&& task, TaskGroup& group)
{
	using ReturnType = std::invoke_result_t<F, size_t>;
	std::packaged_task<ReturnType(size_t)> packaged_task(std::forward<F>(task));
	std::future<ReturnType> future = packaged_task.get_future();
	queue_task(std::make_unique<PackagedTask<ReturnType>>(std::move(packaged_task), group));
	return future;
}

template <typename F>
std::future<std::invoke_result_t<F, size_t>> TaskGroup::run(F&& task)
{
	return thread_manager.run_in_group(std::forward<F>(task), *this);
}
} // namespace vkte
