#include "CoroService.hpp"

#ifndef _WIN32
#include <unistd.h> // для read
#endif

namespace Bicycle
{
	namespace CoroService
	{
		/// Размер стека сопрограммы
		const size_t CoroStackSize = 10*1024;

		/// Структура с информацией для сервисов
		struct SrvInfoStruct
		{
			/// Ссылка на сервис, используемый сопрограммой
			Service &ServiceRef;

			/// Ссылка на основную сопрограмму потока
			Coroutine &MainCoro;

			/// Ссылка на сопрограмму, которая удаляет сопрограмму, из которой в неё перешли, если та завершена
			Coroutine &DeleteCoro;

			/// Указатель на задачу, "оставленную" дескриптором при переходе в основную сопрограмму
			std::function<void()> *DescriptorTask;

			SrvInfoStruct( Service &srv_ref,
			               Coroutine &main_coro,
			               Coroutine &del_coro ): ServiceRef( srv_ref ),
			                                      MainCoro( main_coro ),
			                                      DeleteCoro( del_coro ),
			                                      DescriptorTask( nullptr )
			{}
		};

		/// "Потоколокальный" указатель на SrvInfoStruct
		ThreadLocal SrvInfoPtr;

		void Service::CloseAllDescriptors()
		{
			auto desc = Descriptors.Release();
			Error err;

			// Вызываем RemoveIf только для перебора всех элементов,
			// ни один из них удалён не будет
			desc.RemoveIf( [ &err ]( const BaseDescWeakPtr &elem ) -> bool
			{
				auto ptr = elem.lock();
				if( ptr )
				{
					LockGuard<SpinLock> lock( ptr->second );
					if( ptr->first != nullptr )
					{
						ptr->first->Close( err );
					}
				}
				return true;
			});
			MY_ASSERT( !desc );
		}

		void Service::OnDescriptorRemove()
		{
			if( ( ++DescriptorsDeleteCount % 0x40 ) == 0 )
			{
				std::function<void()> task = [ this ]()
				{
					auto desc = Descriptors.Release();

					desc.RemoveIf( []( const BaseDescWeakPtr &elem ) -> bool
					{
						return elem.expired();
					});

					Descriptors.Push( std::move( desc ) );

					if( MustBeStopped.load() )
					{
						// Закрываем все зарегистрированные дескрипторы, очищаем Descriptors
						// (тут оно надо, т.к. если одновременно будут вызваны Stop и
						// OnDescriptorRemove, то Stop может застать пустую (временно) очередь
						// Descriptors)
						CloseAllDescriptors();
					}
				};
				auto err = Post( task );

				if( err )
				{
					MY_ASSERT( false );
					task();
				}
			}
		} // void Service::OnDescriptorRemove()

		void Service::ExecLeftTasks()
		{
			// Выполняем задачу, "оставленную" дочерней сопрограммой
			SrvInfoStruct *srv_info_ptr = nullptr;

			while( true )
			{
				std::function<void()> task;
				srv_info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
				if( ( srv_info_ptr != nullptr ) &&
				    ( srv_info_ptr->DescriptorTask != nullptr ) )
				{
					task = std::move( *( srv_info_ptr->DescriptorTask ) );
					srv_info_ptr->DescriptorTask = nullptr;
				}

				if( !task )
				{
					// Задач не оставлено
					break;
				}

				// Выполняем задачу
				task();

				// Тут в DescriptorTask-е может быть уже другое значение,
				// заданное при выполнении task-а
			}
		} // void Service::ExecLeftTasks()

		Error Service::Go( std::function<void()> task, size_t stack_sz )
		{
			if( MustBeStopped.load() )
			{
				// Сервис закрывается
				return Error( ErrorCodes::SrvStop, "Service stopped or stopping" );
			}

			if( !task )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "Invalid task" );
			}

			++CoroCount;

			// Логика следующая: формируем задачу, которая 100% будет выполнена из основной сопрограммы потока
			// Когда сопрограмма завершается, управление переходит в сопрограмму,
			// которая удаляет её и передаёт управление в основную сопрограмму
			// Если отказаться от Post-а, можем попасть в ситуацию, когда управление больше не будет передано
			// в текущую сопрограмму
			std::function<void()> post_task( [ this, task, stack_sz ]
			{
				CoroTaskType coro_fnc = [ task ]() -> Coroutine*
				{
					task();
					SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
					Coroutine *res = info_ptr != nullptr ? &( info_ptr->DeleteCoro ) : nullptr;
					MY_ASSERT( res != nullptr );
					MY_ASSERT( !res->IsDone() );
					return res;
				};

				// Создаваемая сопрограмма будет позже удалена в цикле сопрограммы очистки
				Coroutine *new_coro = new Coroutine( coro_fnc, stack_sz == 0 ? CoroStackSize : stack_sz );
				bool res = new_coro->SwitchTo();
				MY_ASSERT( res );
			});

			return Post( post_task );
		} // Error Go( std::function<void()> task )

		Service::Service(): MustBeStopped( true ),
		                    CoroCount( 0 ),
		                    WorkThreadsCount( 0 ),
		                    DescriptorsDeleteCount( 0 )
#ifndef _WIN32
		                    , DeleteQueue( 0xFF )
#endif
		{
			RunFlag.clear();

			try
			{
				// Платформизависимая инициализация
				Initialize();
			}
			catch( ... )
			{
				// Ошибка
				Close();
				throw;
			}
		}

		Service::~Service()
		{
			if( RunFlag.test_and_set() )
			{
				// Сервис не был остановлен
				// Создаём поток на всякий случай, чтобы 100% не было зависания
				std::thread th( [ & ] { Execute(); } );
				Stop();
				th.join();
			}

			Close();
		}

		bool Service::Restart()
		{
			if( RunFlag.test_and_set() )
			{
				// Сервис уже запущен
				return false;
			}

			MustBeStopped.store( false );
			MY_ASSERT( PostedTasks.empty() );

			return true;
		} // bool Service::Restart()

		bool Service::Stop()
		{
			// Отмечаем флаг "всем стоп"
			if( MustBeStopped.exchange( true ) )
			{
				// Другой поток уже останавливает сервис,
				// либо сервис уже остановлен
				return false;
			}

			// Закрываем все дескрипторы
			CloseAllDescriptors();
			MY_ASSERT( !Descriptors.Release() );

			// TODO: ??? запилить нормальное ожидание завершения сопрограмм (как вариант, std::condition_variable в помощь) ???
			while( ( CoroCount.load() != 0 ) || ( WorkThreadsCount.load() != 0 ) )
			{
				std::this_thread::yield();
			}

			MY_ASSERT( CoroCount.load() == 0 );
			MY_ASSERT( PostedTasks.empty() || ( !PostedTasks.front() && ( PostedTasks.size() == 1 ) ) );

			PostedTasks.clear();
			RunFlag.clear();

#ifndef _WIN32
			char buf[ 100 ] = { 0 };
			for( Error err; err.Code != EAGAIN; err = GetLastSystemError() )
			{
				read( PostPipe[ 0 ], buf, sizeof( buf ) );
			}
#endif
			return true;
		} // bool Service::Stop()

		void Service::Run()
		{
			if( SrvInfoPtr.Get() != nullptr )
			{
				// Ошибка: нельзя вызывать из сопрограммы сервиса
				throw Exception( ErrorCodes::InsideSrvCoro,
				                 "Cannot execute inside service coroutine" );
			}
			else if( MustBeStopped.load() && ( WorkThreadsCount.load() != 0 ) )
			{
				// Сервис остановлен, либо останавливается
				throw Exception( ErrorCodes::SrvStop, "Service stopping or stopped" );
			}

			Coroutine *del_coro_ptr = nullptr;

			try
			{
				// Увеличиваем счётчик поотков
				++WorkThreadsCount;

				// Перед выходом уменьшим счётчик
				Defer defer( [ this ]{ --WorkThreadsCount; } );

				// Создаём основную сопрограмму потока
				Coroutine main_coro;
				Coroutine *main_coro_ptr = &main_coro;// Создаём сопрограмму удаления отработавших дочерних сопрограмм

				Coroutine del_coro( [ this, main_coro_ptr ]() -> Coroutine*
				{
					// Нужно для подготовки сопрограммы к работе
					Coroutine *prev_coro_ptr = nullptr; // Указатель на предыдущую сопрограмму
					main_coro_ptr->SwitchTo( &prev_coro_ptr );

					// В этом месте prev_coro_ptr - указатель на сопрограмму для удаления
					// (если убрать 2 верхние строки, то указатель на первую удаляемую сопрограмму будет утерян)
					while( CoroCount.load() > 0 )
					{
						MY_ASSERT( prev_coro_ptr != nullptr );
						if( prev_coro_ptr->IsDone() )
						{
							// Сопрограмма prev_coro_ptr закончила работу,
							// её можно удалять
							delete prev_coro_ptr;
							if( --CoroCount == 0 )
							{
								// Сопрограммы закончились
								Post( std::function<void()>() );
							}
						}

						main_coro_ptr->SwitchTo( &prev_coro_ptr );
					}

					//return ( Coroutine* ) MainCoro.Get();
					return main_coro_ptr;
				}, CoroStackSize );

				del_coro_ptr = &del_coro;

				// Создаём служебную структуру потока и запоминаем указатель на неё в
				// "потоколокальном" указателе
				MY_ASSERT( SrvInfoPtr.Get() == nullptr );
				SrvInfoStruct srv_info( *this, main_coro, del_coro );
				SrvInfoPtr.Set( ( void* ) &srv_info );

				// Переходим в сопрограмму очистки и обратно
				// (Нужно для подготовки сопрограммы к работе)
				del_coro.SwitchTo();

				// Запускаем цикл ожидания готовности и выполнения сопрограмм
				Execute();

				// Если сопрограмма удаления ещё не завершена - переходим в неё
				if( !del_coro.IsDone() )
				{
					del_coro.SwitchTo();
				}

				SrvInfoPtr.Set( nullptr );
				del_coro_ptr = nullptr;
			}
			catch( const Exception& )
			{
				// Если сопрограмма удаления ещё не завершена - переходим в неё
				MY_ASSERT( del_coro_ptr != nullptr );
				if( !del_coro_ptr->IsDone() )
				{
					del_coro_ptr->SwitchTo();
				}

				SrvInfoPtr.Set( nullptr );

				throw;
			}
#ifdef _DEBUG
			catch( ... )
			{
				MY_ASSERT( false );

				// Если сопрограмма удаления ещё не завершена - переходим в неё
				MY_ASSERT( del_coro_ptr != nullptr );
				if( !del_coro_ptr->IsDone() )
				{
					del_coro_ptr->SwitchTo();
				}

				SrvInfoPtr.Set( nullptr );

				throw;
			}
#endif
		} // void Service::Run()

		Error Service::AddCoro( const std::function<void()> &task, size_t stack_sz )
		{
			if( SrvInfoPtr.Get() != nullptr )
			{
				// Ошибка: нельзя вызывать её из сопрограммы сервиса
				throw Exception( ErrorCodes::InsideSrvCoro,
				                 "Cannot execute inside service coroutine" );
			}

			MY_ASSERT( task );
			return task ? Go( task, stack_sz ) : Error();
		}

		Error Go( std::function<void()> task, size_t stack_sz )
		{
			SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();

			if( info_ptr == nullptr )
			{
				MY_ASSERT( false );
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Not inside service coroutine" );
			}

			return info_ptr->ServiceRef.Go( task, stack_sz );
		} // Error Go( std::function<void()> task )

		//-------------------------------------------------------------------------------

		/**
		 * @brief GetCurrentService получение ссылки на сервис,
		 * к которому относится дескриптор
		 * @return ссылка на сервис
		 * @throw Exception, если функция вызвана не из сопрограммы сервиса
		 */
		inline Service& GetCurrentService()
		{
			SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
			Service *ptr = info_ptr != nullptr ? &( info_ptr->ServiceRef ) : nullptr;

			if( ptr == nullptr )
			{
				// Не находимся в одной из сопрограмм сервиса
				throw Exception( ErrorCodes::NotInsideSrvCoro, "Not inside service coroutine" );
			}

			return *ptr;
		}

		Error BasicDescriptor::PostToSrv( const std::function<void()> &task )
		{
			return SrvRef.Post( task );
		}

		void BasicDescriptor::SetPostTaskAndSwitchToMainCoro( std::function<void()> *task )
		{
			if( ( task == nullptr ) || !*task )
			{
				MY_ASSERT( false );
				return;
			}

			SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
			MY_ASSERT( info_ptr != nullptr );
			MY_ASSERT( info_ptr->DescriptorTask == nullptr );
			info_ptr->DescriptorTask = task;
			
			info_ptr->MainCoro.SwitchTo();
		} // void BasicDescriptor::SetPostTaskAndSwitchToMainCoro( std::function<void()> *task )

		BasicDescriptor::BasicDescriptor(): SrvRef( GetCurrentService() ),
#ifdef _WIN32
		                                    Fd( INVALID_HANDLE_VALUE )
#else
		                                    DescriptorData( nullptr )
#endif
		{
			Ptr.reset( new PtrWithLocker );
			MY_ASSERT( Ptr );
			Ptr->first = this;
			SrvRef.Descriptors.Push( BaseDescWeakPtr( Ptr ) );
			MY_ASSERT( Ptr );
		}

		BasicDescriptor::~BasicDescriptor()
		{
			MY_ASSERT( Ptr );
			{
				LockGuard<SpinLock> lock( Ptr->second );
				Ptr->first = nullptr;
			}
			Ptr.reset();
			SrvRef.OnDescriptorRemove();

			Error err;
			Close( err );
			MY_ASSERT( !err );
		}

		void BasicDescriptor::Open()
		{
			Error err;
			Open( err );
			if( err )
			{
				throw Exception( err.Code, err.What.c_str() );
			}
		}

		void BasicDescriptor::Close()
		{
			Error err;
			Close( err );
			if( err )
			{
				throw Exception( err.Code, err.What.c_str() );
			}
		}

		void BasicDescriptor::Cancel()
		{
			Error err;
			Cancel( err );
			if( err )
			{
				throw Exception( err.Code, err.What.c_str() );
			}
		}
	} // namespace CoroService
} // namespace Bicycle
