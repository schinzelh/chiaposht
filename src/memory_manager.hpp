// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_CPP_CACHE_MANAGER_HPP_
#define SRC_CPP_CACHE_MANAGER_HPP_

#include <mutex>
#include <assert.h>
#include <vector>


enum cache_mode : uint8_t {
	SKIP_NEW = 1,
	FLUSH_OLD = 2
};

struct ICacheConsumer{
	virtual uint64_t getUsedCache() const = 0;
	virtual void FreeCache() = 0;
};

struct ConsumerEntry {
	ICacheConsumer * consumer;
	ConsumerEntry * next = nullptr;
	ConsumerEntry * prev;

	ConsumerEntry( ICacheConsumer * cons, ConsumerEntry * prev ) : consumer(cons), prev(prev){}

	bool isInList( const ConsumerEntry *entry ){
		for( auto c = this; c != nullptr; c = c->next )
			if( c == entry ) return true;
		return false;
	}

	bool isInList( const ICacheConsumer *entry ){
		for( auto c = this; c != nullptr; c = c->next )
			if( c->consumer == entry ) return true;
		return false;
	}
};

struct MemoryManager{
	bool isForced = false;
	bool isFIFO = false;

	MemoryManager( uint64_t size ) : total_size(size){}

	inline uint64_t getAccessibleRam() {
		uint64_t res = total_size - used_ram;

		{
			std::lock_guard<std::mutex> lk(sync_consumers);

			for( auto cur = consumers; cur != nullptr; cur = cur->next )
				res += cur->consumer->getUsedCache();
		}

		return res;
	}

	inline int64_t getFreeRam() const {
		return total_size - used_ram;
	}

	inline bool request( const uint64_t &size, ICacheConsumer *consumer ){
		return request( size, false, consumer );
	}

	inline bool request( const uint64_t &size, bool forced = false, ICacheConsumer *consumer = nullptr ){

		{
			std::scoped_lock lk (sync_size);

			if( getFreeRam() >= size ){
				used_ram += size;
				return true;
			}
		}

		if( ( forced || (isForced && ( getAccessibleRam() - ( consumer == nullptr ? 0 : consumer->getUsedCache() ) ) < size) )
					&& CleanCache( consumer, size ) ){
			std::scoped_lock lk (sync_size);
			used_ram += size;
			return true;
		}

		return false;
	}

	inline void requier( const uint64_t & size ){
		CleanCache( nullptr, size );
		std::scoped_lock lk(sync_size);
		used_ram += size;
	}

	inline void release( const uint32_t &size ){
		std::scoped_lock lk(sync_size);

		assert( (int64_t)size <= used_ram );
		used_ram -= size;

	}

	inline ConsumerEntry* registerConsumer( ICacheConsumer * consumer ){
		std::scoped_lock lk ( sync_consumers );
		assert( consumers == nullptr || !consumers->isInList(consumer) );

		if( consumers == nullptr )
			return consumers = last_consumer = new ConsumerEntry( consumer, consumers );

		return last_consumer = (last_consumer->next = new ConsumerEntry( consumer, last_consumer ) );
	}

	inline void unregisterConsumer( ConsumerEntry * consumer ){
		assert( consumer != nullptr );

		std::scoped_lock lk ( sync_consumers );
		removeConsumer( consumer );
		delete consumer;
	}


private:

	const int64_t total_size;
	int64_t used_ram = 0;
	std::mutex sync_size, sync_consumers;
	ConsumerEntry * consumers = nullptr;
	ConsumerEntry * last_consumer = nullptr;


	inline void removeConsumer( const ConsumerEntry * consumer ){
		assert( consumers != nullptr );
		assert( consumers->isInList(consumer) );

		if( consumer == last_consumer )	{
			last_consumer = consumer->prev;
			if( last_consumer == nullptr ){
				assert( consumer == consumers );
				consumers = nullptr; // list is clear
			}
			else{
				assert( last_consumer->next == consumer );
				last_consumer->next = nullptr;
			}
		} else if( consumer == consumers ) {
			consumers = consumers->next;
			assert( consumers != nullptr );
			consumers->prev = nullptr;
		} else {
			assert( consumer->prev != nullptr ); // it is because it is not the head
			assert( consumer-> next != nullptr ); // it is because it is not the last
			consumer->prev->next = consumer->next;
			consumer->next->prev = consumer->prev;
		}
	}

	inline bool CleanCache( ICacheConsumer * cur_consumer, uint32_t need_size ){

		std::lock_guard<std::mutex> lk(sync_consumers);

		auto cur = isFIFO ? consumers : last_consumer;
		while( cur != nullptr && getFreeRam() < need_size ) {

			auto next = isFIFO ? cur->next : cur->prev;

			if( cur->consumer != cur_consumer ){
				removeConsumer( cur );
				cur->consumer->FreeCache();
				delete cur;
			}

			cur = next;
		}

		return getFreeRam() >= need_size;
	}

};


#endif // SRC_CPP_CACHE_MANAGER_HPP_
