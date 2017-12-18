#include "CoroSrv/Service.hpp"

#if !(defined( _WIN32) || defined(_WIN64))
#include <unistd.h> // для read
#endif

namespace Bicycle
{
	namespace CoroService
	{
		/// Размер стека сопрограммы
		const size_t CoroStackSize = 10*1024;

		/// Периодичность удаления указателей на закрытые дескрипторы
		const uint64_t DescriptorsRemovePeriod = 0x40;

		/// Структура с информацией для сервисов
		struct SrvInfoStruct
		{
			/// Ссылка на сервис, используемый сопрограммой
			Service &ServiceRef;

			/// Ссылка на основную сопрограмму потока
			Coroutine &MainCoro;

			/// Ссылка на сопрограмму, которая удаляет сопрограмму, из которой в неё перешли, если та завершена
			Coroutine &DeleteCoro;

			/// Задача, "оставленная" дескриптором при переходе в основную сопрограмму
			std::function<void()> DescriptorTask;

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
			if( ( ++DescriptorsDeleteCount % 0x100 ) == 0 )
			{
				NeedToClearDescriptors.store( true );
			}
		} // void Service::OnDescriptorRemove()

		void Service::RemoveClosedDescriptors()
		{
			if( !NeedToClearDescriptors.exchange( false ) )
			{
				return;
			}

			// Удаляем указатели на закрытые дескрипторы из Descriptors
			auto desc = Descriptors.Release();
			MY_ASSERT( desc );

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
		} // void Service::RemoveClosedDescriptors()

		void Service::ExecLeftTasks()
		{
			// Выполняем задачу, "оставленную" дочерней сопрограммой
			SrvInfoStruct *srv_info_ptr = nullptr;

			while( true )
			{
				srv_info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
				MY_ASSERT( srv_info_ptr != nullptr );
				if( !srv_info_ptr->DescriptorTask )
				{
					// Задач не оставлено
					break;
				}
				
				std::function<void()> task( std::move( srv_info_ptr->DescriptorTask ) );

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

			// Логика следующая: созддаём новую сопрограмму, добавляем в очередь. Затем указатель
			// на сопрограмму будет извлечёно в основной сопрограмме, и оттуда будет совершёно переход.
			// Когда сопрограмма завершается, управление переходит в сопрограмму,
			// которая удаляет её и передаёт управление в основную сопрограмму
			// Если отказаться от Post-а, можем попасть в ситуацию, когда управление больше не будет передано
			// в текущую сопрограмму. Также будет глюк, если вызов не из сопрограммы.
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
			Coroutine *new_coro_ptr = new Coroutine( coro_fnc, stack_sz == 0 ? CoroStackSize : stack_sz );

			Post( new_coro_ptr );
			return Error();
		} // Error Go( std::function<void()> task )

		Service::Service(): MustBeStopped( true ),
		                    CoroCount( 0 ),
		                    WorkThreadsCount( 0 ),
							DescriptorsDeleteCount( 0 ),
							NeedToClearDescriptors( false )
#if !(defined( _WIN32) || defined(_WIN64))
							, DeleteQueue( 0xFF, 0x100 ),
							CoroListNum( 0 )
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

#if !(defined( _WIN32) || defined(_WIN64))
			char buf[ 100 ] = { 0 };
			int read_res = -1;
			for( Error err; err.Code != EAGAIN; err = GetLastSystemError() )
			{
				read_res = read( PostPipe[ 0 ], buf, sizeof( buf ) );
			}

			DeleteQueue.Clear();
#endif

			RunFlag.clear();
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
								Post( nullptr );
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

		void YieldCoro()
		{
			SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
			Coroutine *cur_coro_ptr = GetCurrentCoro();

			MY_ASSERT( ( info_ptr == nullptr ) == ( cur_coro_ptr == nullptr ) );
			if( ( info_ptr == nullptr ) || ( cur_coro_ptr == nullptr ) )
			{
				// Выполняем не из сопрограммы сервиса
				MY_ASSERT( false );
				throw Exception( ErrorCodes::NotInsideSrvCoro,
				                 "Not inside service coroutine" );
			}

			MY_ASSERT( info_ptr->DescriptorTask == nullptr );
			MY_ASSERT( cur_coro_ptr != &( info_ptr->MainCoro ) );
			std::function<void()> task( [ info_ptr, cur_coro_ptr ]()
			{
				info_ptr->ServiceRef.Post( cur_coro_ptr );
			});

			MY_ASSERT( !info_ptr->DescriptorTask );
			info_ptr->DescriptorTask = std::move( task );
			bool res = info_ptr->MainCoro.SwitchTo();
			MY_ASSERT( res );
		} // void YieldCoro()

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

		ServiceWorker::ServiceWorker(): SrvRef( GetCurrentService() ){}

		void ServiceWorker::PostToSrv( Coroutine &coro_ref )
		{
			SrvRef.Post( &coro_ref );
		}

		ServiceWorker::poster_t ServiceWorker::GetPoster() const
		{
			Service &srv_ref = SrvRef;
			return poster_t( [ &srv_ref ]( Coroutine &coro_ref ){ srv_ref.Post( &coro_ref ); } );
		}

		void ServiceWorker::SetPostTaskAndSwitchToMainCoro( std::function<void()> &&task )
		{
			if( !task )
			{
				MY_ASSERT( false );
				return;
			}

			SrvInfoStruct *info_ptr = ( SrvInfoStruct* ) SrvInfoPtr.Get();
			MY_ASSERT( info_ptr != nullptr );
			MY_ASSERT( info_ptr->DescriptorTask == nullptr );
			info_ptr->DescriptorTask = std::move( task );

			info_ptr->MainCoro.SwitchTo();
		} // void ServiceWorker::SetPostTaskAndSwitchToMainCoro( std::function<void()> &&task )

		bool ServiceWorker::IsStopped() const
		{
			return SrvRef.MustBeStopped.load();
		}

		AbstractCloser::AbstractCloser(): ServiceWorker(), Ptr()
		{
			Ptr.reset( new PtrWithLocker );
			MY_ASSERT( Ptr );
			Ptr->first = this;
			SrvRef.Descriptors.Push( BaseDescWeakPtr( Ptr ) );
			MY_ASSERT( Ptr );
		}

		AbstractCloser::~AbstractCloser()
		{
			MY_ASSERT( Ptr );
			{
				LockGuard<SpinLock> lock( Ptr->second );
				Ptr->first = nullptr;
			}
			Ptr.reset();
			SrvRef.OnDescriptorRemove();
		}

		void AbstractCloser::Close()
		{
			Error err;
			Close( err );
			ThrowIfNeed( err );
		}

		//------------------------------------------------------------------------------
		
		static const uint8_t InitState = ( uint8_t ) TaskState::InProc;
		
		Coroutine* CoroKeeper::ChangeState( TaskState new_state )
		{
			const uint8_t new_val = ( uint8_t ) new_state;			
			uint8_t expected = InitState;
			
			// Меняем текущее состояние только, если
			// оно имеет значение "В процессе"
			if( State.compare_exchange_strong( expected, new_val ) )
			{
				return CoroPtr.exchange( nullptr );
			}
			
			return nullptr;
		} // Coroutine* CoroKeeper::ChangeState( TaskState new_state )
		
		CoroKeeper::CoroKeeper( Coroutine *coro_ptr ): CoroPtr( coro_ptr ),
		                                               State( InitState )
		{
			MY_ASSERT( CoroPtr.load() == coro_ptr );
			MY_ASSERT( State.load() == ( uint8_t ) TaskState::InProc );
		}
		
		CoroKeeper::~CoroKeeper()
		{
			MY_ASSERT( CoroPtr.load() == nullptr );
		}
		
		TaskState CoroKeeper::GetState() const
		{
			MY_ASSERT( State.load() < 4 );
			return TaskState( State.load() );
		}
		
		Coroutine* CoroKeeper::MarkWorked()
		{
			return ChangeState( TaskState::Worked );
		}
		
		Coroutine* CoroKeeper::MarkCancel()
		{
			return ChangeState( TaskState::Cancelled );
		}
		
		Coroutine* CoroKeeper::MarkTimeout()
		{
			return ChangeState( TaskState::TimeoutExpired );
		}
		
		Coroutine* CoroKeeper::Release()
		{
			return CoroPtr.exchange( nullptr );
		}
		
		bool CoroKeeper::Reset( Coroutine &new_coro )
		{
			if( State.load() != InitState )
			{
				// Кто-то уже пометил задачу как неактивную
				// (выполненная, "просроченная" или отменённая)
				return false;
			}
			
			Coroutine *expected = nullptr;
			if( !CoroPtr.compare_exchange_strong( expected, &new_coro ) )
			{
				// Указатель на сопрограмму был ненулевым (что странно)
				MY_ASSERT( false );
				return false;
			}
			
			if( State.load() != InitState )
			{
				// Кто-то уже пометил задачу как неактивную
				// (выполненная, "просроченная" или отменённая)
				MY_ASSERT( CoroPtr.exchange( nullptr ) == &new_coro );
				CoroPtr.exchange( nullptr );
				return false;
			}
			
			return true;
		} // bool CoroKeeper::Reset( Coroutine &new_coro )
	} // namespace CoroService
} // namespace Bicycle
