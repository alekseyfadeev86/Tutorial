#include "CoroSrv/BasicDescriptor.hpp"

namespace Bicycle
{
	namespace CoroService
	{
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
