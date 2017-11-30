#include "CoroSrv/BasicDescriptor.hpp"

namespace Bicycle
{
	namespace CoroService
	{	
		std::shared_ptr<TimeTasksQueue> BasicDescriptor::InitTimer( uint64_t read_timeout,
		                                                            uint64_t write_timeout )
		{
			if( ( read_timeout != 0 ) && ( read_timeout != TimeoutInfinite ) &&
			    ( write_timeout != 0 ) && ( write_timeout != TimeoutInfinite ) )
			{
				return TimeTasksQueue::GetQueue();
			}
			
			return std::shared_ptr<TimeTasksQueue>();
		}
		
		BasicDescriptor::~BasicDescriptor()
		{
			Error err;
			Close( err );
			MY_ASSERT( !err );
		}

		void BasicDescriptor::Open()
		{
			Error err;
			Open( err );
			ThrowIfNeed( err );
		}

		void BasicDescriptor::Close()
		{
			Error err;
			Close( err );
			ThrowIfNeed( err );
		}

		void BasicDescriptor::Cancel()
		{
			Error err;
			Cancel( err );
			ThrowIfNeed( err );
		}
	} // namespace CoroService
} // namespace Bicycle
