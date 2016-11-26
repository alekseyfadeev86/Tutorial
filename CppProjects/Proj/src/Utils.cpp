#include "Utils.hpp"

namespace Bicycle
{
	SpinLock::SpinLock()
	{
		Flag.clear();
	}
	
	void SpinLock::Lock()
	{
		while( Flag.test_and_set() )
		{}
	}
	
	void SpinLock::Unlock()
	{
		Flag.clear();
	}

	//------------------------------------------------------------------------------
	
	SharedSpinLock::SharedSpinLock(): Flag( 0 ) {}
	
	const uint64_t LockingFlag = 0x4000000000000000;
	const uint64_t LockedFlag  = LockingFlag | 0x8000000000000000;
	
	void SharedSpinLock::Lock()
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = 0;
			if( Flag.compare_exchange_weak( expected, LockedFlag ) )
			{
				// Блокировка на запись была захвачена текущим потоком (до этого никто не претендовал)
				break;
			}
			else if( expected == LockingFlag )
			{
				// Какой-то (возможно, текущий) поток уже претендует на монопольную блокировку
				if( Flag.compare_exchange_weak( expected, LockedFlag ) )
				{
					// Блокировка на запись была захвачена текущим потоком
					break;
				}
			}
			
			// Помечаем во флаге, что "претендуем" на монопольную блокировку
			Flag.compare_exchange_weak( expected, LockingFlag | expected );
		}
	} // void SharedSpinLock::Lock()
	
	void SharedSpinLock::SharedLock()
	{
		uint64_t expected = 0;
		while( 1 )
		{
			expected = Flag.load();
			if( ( expected & LockedFlag ) == 0 )
			{
				// Монопольная блокировка не захвачена и никто на неё не претендует
				if( Flag.compare_exchange_weak( expected, expected + 1 ) )
				{
					// Разделяемая блокировка захвачена текущим потоком
					break;
				}
			}
		}
	} // void SharedSpinLock::SharedLock()
	
	void SharedSpinLock::Unlock()
	{
		uint64_t expected = 0;
		uint64_t new_val = 0;
		while( 1 )
		{
			expected = Flag.load();
			if( expected == 0 )
			{
				// Блокировка не была захвачена ни одним потоком
				break;
			}
			
			new_val = ( expected == LockedFlag ) ? 0 : expected - 1;
			
			if( Flag.compare_exchange_weak( expected, new_val ) )
			{
				break;
			}
		} // while( 1 )
	}
} // namespace Bicycle
