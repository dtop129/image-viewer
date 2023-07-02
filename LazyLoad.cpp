#include "util.h"
#include <thread>

BS::thread_pool_light LazyLoadBase::pool(std::thread::hardware_concurrency() - 1);

template<class T>
LazyLoad<T>::LazyLoad(std::function<T()> f) : getter(f)
{
	loading_resource = pool.submit(f);
}

template<class T>
LazyLoad<T>::LazyLoad(const T& value) : resource(value) {}

template<class T>
const T& LazyLoad<T>::get()
{
	if (loading_resource.valid())
		resource = loading_resource.get();

	return resource;
}

template<class T>
bool LazyLoad<T>::available() const
{
	if (!loading_resource.valid())
		return true;

	return loading_resource.wait_for(std::chrono::seconds(0)) ==
		std::future_status::ready;
}
