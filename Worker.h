#pragma once

#include <thread>
#include <memory>
#include <deque>
#include <mutex>
#include <atomic>
#include <iostream>
#include <functional>
#include <system_error>
#include <condition_variable>

class Worker {
public:
	Worker()
	{
		this->thread = std::make_unique<std::thread>([this]() {

			auto get_work = [this]() {
				std::unique_lock lock(this->mutex);

				this->condvar.wait(lock, [this]() {
					return !this->buffer.empty(); });

				auto work = this->buffer.front();
				this->buffer.pop_front();
				return work;
			};

			while (!this->done) {
				auto work = get_work();
				this->working = true;
				work();
				this->working = false;
				this->condvar.notify_one();
			}});
	}

	~Worker()
	{
		this->add_work([this]() {
			this->done = true; });

		try {
			this->thread->join();
		}
		catch (const std::system_error& e) {
			std::cerr << "Caught system_error in ~Worker(): " << e.what() << std::endl;
		}
	}

	void add_work(std::function<void()> work)
	{
		std::unique_lock lock(this->mutex);
		this->buffer.push_back(work);
		this->condvar.notify_one();
	}


	std::mutex mutex;
	std::atomic_bool done{ false };
	std::atomic_bool working{ false };
	std::condition_variable condvar;
	std::unique_ptr<std::thread> thread;
	std::deque<std::function<void()>> buffer;
};
