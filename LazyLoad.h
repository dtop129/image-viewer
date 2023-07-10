#pragma once

#include "BS_thread_pool_light.hpp"

#include <future>

class LazyLoadBase {
  protected:
	static BS::thread_pool_light pool;
};

BS::thread_pool_light LazyLoadBase::pool(std::thread::hardware_concurrency() -
										 1);

template <class T> class LazyLoad : LazyLoadBase {
  private:
	T resource;
	std::future<T> loading_resource;

  public:
	template <class Callable> LazyLoad(Callable &&f) {
		loading_resource = pool.submit(f);
	}

	const T &get() {
		if (loading_resource.valid())
			resource = loading_resource.get();

		return resource;
	}

	bool available() const {
		if (!loading_resource.valid())
			return true;

		return loading_resource.wait_for(std::chrono::seconds(0)) ==
			   std::future_status::ready;
	}
};
