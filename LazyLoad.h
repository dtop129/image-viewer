#pragma once

#include "BS_thread_pool_light.hpp"

#include <functional>
#include <future>

class LazyLoadBase {
  protected:
	static BS::thread_pool_light pool;
};

template <class T> class LazyLoad : LazyLoadBase {
  private:
	T resource;
	std::future<T> loading_resource;
	std::function<T()> getter;

  public:
	LazyLoad(std::function<T()> f);
	LazyLoad(const T &value);

	const T &get();
	bool available() const;
};

#include "LazyLoad.cpp"
