#include "vkte/thread_manager.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
// the pool whose worker this thread is, if any
static thread_local const ThreadManager* worker_of = nullptr;

ThreadManager::ThreadManager(size_t thread_count) : default_group(std::make_unique<TaskGroup>(*this))
{
	if (thread_count == 0) thread_count = 1;
	workers.reserve(thread_count);
	for (size_t i = 0; i < thread_count; i++) workers.emplace_back([this, i] { worker_loop(i); });
}

ThreadManager::~ThreadManager()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	work_cv.notify_all();
	for (std::thread& worker : workers) worker.join();
}

void ThreadManager::queue_task(std::unique_ptr<Task> task)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		TaskGroup& group = task->group;
		// first, since it may throw, before anything is counted
		if (!group.queue_head) queued_groups.push_back(&group);
		group.pending_tasks++;
		Task* raw_task = task.get();
		if (group.queue_tail) group.queue_tail->next = std::move(task);
		else group.queue_head = std::move(task);
		group.queue_tail = raw_task;
	}
	work_cv.notify_one();
}

std::unique_ptr<ThreadManager::Task> ThreadManager::take_next_task()
{
	if (queued_groups.empty()) return nullptr;
	size_t next = 0;
	for (size_t i = 1; i < queued_groups.size(); i++)
	{
		const TaskGroup& group = *queued_groups[i];
		const TaskGroup& best = *queued_groups[next];
		const bool is_waited_for = group.waiting_threads > 0;
		const bool is_best_waited_for = best.waiting_threads > 0;
		if (is_waited_for != is_best_waited_for ? is_waited_for : group.priority > best.priority) next = i;
	}
	TaskGroup& group = *queued_groups[next];
	std::unique_ptr<Task> task = std::move(group.queue_head);
	group.queue_head = std::move(task->next);
	queued_groups.erase(queued_groups.begin() + next);
	if (group.queue_head) queued_groups.push_back(&group);
	else group.queue_tail = nullptr;
	return task;
}

void ThreadManager::worker_loop(size_t worker_index)
{
	worker_of = this;
	while (true)
	{
		std::unique_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(mutex);
			work_cv.wait(lock, [this] { return !queued_groups.empty() || stopping; });
			task = take_next_task();
			// woken without a task only when stopping
			if (!task) return;
		}
		task->run(worker_index);
		finish(std::move(task));
	}
}

void ThreadManager::finish(std::unique_ptr<Task> task)
{
	TaskGroup& group = task->group;
	task.reset();
	// notifies under the lock, since a waiting owner may destroy the group as soon as it holds the mutex
	std::lock_guard<std::mutex> lock(mutex);
	if (--group.pending_tasks == 0) group.idle_cv.notify_all();
}

void ThreadManager::wait_for_group(TaskGroup& group)
{
	std::unique_lock<std::mutex> lock(mutex);
	group.waiting_threads++;
	group.idle_cv.wait(lock, [&group] { return group.pending_tasks == 0; });
	group.waiting_threads--;
}

ThreadManager& get_thread_manager()
{
	static ThreadManager thread_manager;
	return thread_manager;
}

TaskGroup::~TaskGroup()
{
	thread_manager.wait_for_group(*this);
}

void TaskGroup::wait()
{
	VKTE_ASSERT(worker_of != &thread_manager, "vkte: A task waits for tasks, which can leave no worker to run them");
	thread_manager.wait_for_group(*this);
}
} // namespace vkte
