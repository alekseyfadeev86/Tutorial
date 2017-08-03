#pragma once

#include <atomic>
#include <stdint.h>
#include <stdexcept>
#include <vector>
#include <memory>

#ifndef MY_ASSERT
#define MY_ASSERT( EXPR )
#endif

namespace LockFree
{
#ifdef UNITTEST
	struct DebugStruct
	{
		private:
			bool WasDeleted;

			static int64_t CounterWork( char mode )
			{
				static std::atomic<int64_t> Counter( 0 );

				if( mode > 0 )
				{
					return ++Counter;
				}

				if( mode < 0 )
				{
					return --Counter;
				}

				return Counter.load();
			}

		public:
			static int64_t GetCounter()
			{
				return CounterWork( 0 );
			}

			int64_t Val;
			DebugStruct( int64_t v = 0 ): WasDeleted( false ), Val( v )
			{
				CounterWork( 1 );
			}

			DebugStruct( const DebugStruct &deb ): WasDeleted( false ), Val( deb.Val )
			{
				MY_ASSERT( !deb.WasDeleted );
				CounterWork( 1 );
			}

			DebugStruct& operator=( const DebugStruct &deb )
			{
				MY_ASSERT( !WasDeleted );
				MY_ASSERT( !deb.WasDeleted );

				if( ( const DebugStruct* ) this != &deb )
				{
					Val = deb.Val;
				}

				return *this;
			}

			bool operator==( const DebugStruct &deb ) const
			{
				MY_ASSERT( !WasDeleted );
				MY_ASSERT( !deb.WasDeleted );
				return Val == deb.Val;
			}

			bool operator!=( const DebugStruct &deb ) const
			{
				return !( *this == deb );
			}

			~DebugStruct()
			{
				MY_ASSERT( !WasDeleted );
				WasDeleted = true;
				CounterWork( -1 );
			}
	};
#endif

	namespace internal
	{
		/// Класс элемента списка/стека/очереди
		template <typename T>
		struct StructElementType
		{
			/// Значение
			T Value;

			/// Указатель на следующий элемент
			std::atomic<StructElementType*> Next;

			StructElementType(): Value(), Next( nullptr ) {}

			StructElementType( const T &val ): Value( val ),
			                                   Next( nullptr ) {}

			StructElementType( T &&val ): Value( std::move( val ) ),
			                              Next( nullptr ) {}

			template <typename Type, typename ...Types>
			StructElementType( Type arg,
			                   Types... args ): Value( arg, args... ),
			                                    Next( nullptr ) {}
		};

#ifdef UNITTEST
		template<>
		struct StructElementType<DebugStruct>
		{
			/// Значение
			DebugStruct Value;

			/// Указатель на следующий элемент
			std::atomic<StructElementType*> Next;

			StructElementType(): Value(), Next( nullptr ) {}

			template<typename Type>
			StructElementType( const Type &val ): Value( val ),
			                                      Next( nullptr ) {}

			template<typename Type>
			StructElementType( Type &&val ): Value( std::move( val ) ),
			                                 Next( nullptr ) {}

			template <typename Type, typename ...Types>
			StructElementType( Type arg,
			                   Types... args ): Value( arg ),
			                                    Next( nullptr ) {}

			void operator delete( void* ) {}
		};
#endif // #ifdef UNITTEST

		template <typename T>
		StructElementType<T>* GetBottom( StructElementType<T> *top )
		{
			if( top == nullptr )
			{
				return nullptr;
			}

			StructElementType<T> *res = nullptr;
			for( ; top != nullptr; res = top, top = top->Next.load() )
			{}

			MY_ASSERT( res != nullptr );
			return res;
		}

		/**
		 * @brief Push добавление новых элементов в начало однонаправленного списка
		 * @param head ссылка на головной элемент списка
		 * @param new_head новый головной элемент (должен быть не nullptr)
		 * @param add_only_one если true - добавляется только new_head,
		 * иначе - все элементы, что стоят за new_head-ом
		 * @return был ли head нулевым до его замены
		 * @throw std::invalid_argument, если new_head == nullptr
		 */
		template <typename T>
		bool PushHead( std::atomic<StructElementType<T>*> &head,
		               StructElementType<T> *new_head,
		               bool add_only_one = true )
		{
			if( new_head == nullptr )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "New head cannot be nullptr" );
			}
			StructElementType<T> *new_bottom = add_only_one ? new_head : GetBottom( new_head );
			MY_ASSERT( new_bottom != nullptr );

			StructElementType<T> *old_head = head.load();
			while( true )
			{
				new_bottom->Next.store( old_head );
				if( head.compare_exchange_weak( old_head, new_head ) )
				{
					return old_head == nullptr;
				}
			}

			MY_ASSERT( false );
			return false;
		}
	} // namespace internal


	/// Класс потоконебезопасного однонаправленного списка
	template <typename T>
	class UnsafeForwardList
	{
		public:
			typedef T Type;

		private:
			typedef internal::StructElementType<T> ElementType;

			/// Начало списка
			ElementType *Top;

			/// Очистка списка
			void Clean()
			{
				ElementType *top = Top;
				Top = nullptr;
				ElementType *ptr = nullptr;

				while( top != nullptr )
				{
					ptr = top;
					top = top->Next.load();
					delete ptr;
				}
			}

			/**
			 * @brief InternalPop Извлечение элемента из начала списка
			 * @param def_value указатель на значение по умолчанию.
			 * Если def_value не нулевой и список пуст - будет возвращено
			 * значение, на которое указывает def_value.
			 * @return первый элемент списка, либо значение по умолчанию
			 * @throw std::out_of_range, если список пуст и значение
			 * по умолчанию не задано
			 */
			T InternalPop( const T *def_value )
			{
				if( Top != nullptr )
				{
					ElementType *old_top = Top;
					Top = Top->Next;
					T result( std::move( old_top->Value ) );
					delete old_top;
					return result;
				}

				if( def_value == nullptr )
				{
					throw std::out_of_range( "List is empty!" );
				}

				return *def_value;
			}

		public:
			UnsafeForwardList( const UnsafeForwardList& ) = delete;
			UnsafeForwardList& operator=( const UnsafeForwardList& ) = delete;

			UnsafeForwardList( UnsafeForwardList &&l ): Top( l.Top ) { l.Top = nullptr; }
			UnsafeForwardList& operator=( UnsafeForwardList &&l )
			{
				if( &l != this )
				{
					Clean();
					Top = l.Top;
					l.Top = nullptr;
				}

				return *this;
			}

			UnsafeForwardList(): Top( nullptr ){}
			UnsafeForwardList( std::atomic<ElementType*> &&ptr ): Top( ptr.exchange( nullptr ) ){}

			~UnsafeForwardList()
			{
				Clean();
			}

			operator bool() const
			{
				return Top != nullptr;
			}

			/**
			 * @brief Push Добавление элемента в начало списка
			 * @param new_val новое значение
			 */
			void Push( const T &new_val )
			{
				ElementType *new_element = new ElementType( new_val );
				new_element->Next = Top;
				Top = new_element;
			}

			/**
			 * @brief Push Добавление элемента в начало списка
			 * @param new_val новое значение
			 */
			void Push( T &&new_val )
			{
				ElementType *new_element = new ElementType( std::move( new_val ) );
				new_element->Next = Top;
				Top = new_element;
			}

			/**
			 * @brief Push Добавление элемента в начало списка
			 * @param args аргументы для создания нового значения
			 */
			template <typename ...Types>
			void Push( Types ...args )
			{
				ElementType *new_element = new ElementType( args... );
				new_element->Next = Top;
				Top = new_element;
			}

			/**
			 * @brief Push Добавление элементов в начало списка
			 * @param u добавляемые элементы (u будет очищен)
			 */
			void Push( UnsafeForwardList &&u )
			{
				if( !u )
				{
					// Нечего добавлять
					return;
				}

				ElementType *old_top = Top;
				Top = u.Top;
				u.Top = nullptr;

				if( old_top != nullptr )
				{
					// Проходим добавляемый список до конца
					ElementType *bottom = Top;
					ElementType *next = nullptr;
					while( true )
					{
						next = bottom->Next.load();
						if( next == nullptr )
						{
							// Нашли последний элемент добавляемого списка,
							// связываем его с первым элементом прежнего
							bottom->Next.store( old_top );
							break;
						}

						bottom = next;
					}
				} // if( old_top != nullptr )
			} // void Push( UnsafeForwardList &&u )

			/**
			 * @brief Pop Извлечение элемента из начала списка
			 * @param def_value ссылка на значение по умолчанию
			 * @return первый элемент списка, либо значение по умолчанию
			 */
			T Pop( const T &def_value )
			{
				return InternalPop( &def_value );
			}

			/**
			 * @brief Pop Извлечение элемента из начала списка
			 * @return первый элемент списка
			 * @throw std::out_of_range, если список пуст
			 */
			T Pop()
			{
				return InternalPop( nullptr );
			}

			/// Удаляет все элементы, удовлетворяющие условию
			template<typename F>
			void RemoveIf( const F &checker )
			{
				// Удаляем все элементы с начала списка, пока не встретим элемент,
				// для которого checker выдаст false, либо пока не очистим весь список
				for( ; ( Top != nullptr ) && checker( Top->Value ); )
				{
					ElementType *old_top = Top;
					Top = Top->Next.load();
					delete old_top;
				}

				if( Top == nullptr )
				{
					// Список пуст
					return;
				}

				MY_ASSERT( Top != nullptr );
				MY_ASSERT( !checker( Top->Value ) );

				// Удаляем все элементы, какие надо
				for( ElementType *ptr = Top, *next_ptr = nullptr; ptr != nullptr; )
				{
					MY_ASSERT( ptr != nullptr );
					MY_ASSERT( !checker( ptr->Value ) );

					next_ptr = ptr->Next.load();
					if( ( next_ptr != nullptr ) && checker( next_ptr->Value ) )
					{
						// Нужно удалить элемент, следующий за ptr-ом
						ptr->Next.store( next_ptr->Next.load() );
						delete next_ptr;
					}
					else
					{
						// Следующий элемент удалять не надо - переходим к нему
						ptr = next_ptr;
					}
				} // for( ElementType *ptr = Top, *next_ptr = nullptr; ptr != nullptr; )
			} // template<typename F> void RemoveIf( const F &checker )

			/**
			 * @brief Reverse меняет порядок элементов
			 * @return количество элементов
			 */
			size_t Reverse()
			{
				if( Top == nullptr )
				{
					return 0;
				}

				size_t res = 0;

				ElementType *prev = nullptr;
				ElementType *next = nullptr;
				while( true )
				{
					++res;
					next = Top->Next.exchange( prev );
					if( next == nullptr )
					{
						break;
					}

					prev = Top;
					Top = next;
				}

				return res;
			} // size_t Reverse()

			/**
			 * @brief ReleaseTo передача указателя на начальный элемент списка
			 * и обнуление его в самом списке.
			 * @param storage ссылка на буфер, куда будет записан указатель
			 * на начальный элемент списка
			 * @return успешность
			 */
			bool ReleaseTo( internal::StructElementType<T>* &storage )
			{
				if( storage != nullptr )
				{
					return false;
				}

				storage = Top;
				Top = nullptr;
				return true;
			}
	};

	/// Класс потокобезопасного однонаправленого списка
	template <typename T>
	class ForwardList
	{
		public:
			typedef T Type;
			typedef UnsafeForwardList<T> Unsafe;

		private:
			typedef internal::StructElementType<T> ElementType;

			/// Начало списка
			std::atomic<ElementType*> Top;

		public:

		public:
			ForwardList( const ForwardList& )= delete;
			ForwardList( ForwardList&& ) = delete;
			ForwardList& operator=( const ForwardList& ) = delete;
			ForwardList& operator=( ForwardList&& ) = delete;

			ForwardList(): Top( nullptr ) {}
			~ForwardList()
			{
				ElementType *top = Top.exchange( nullptr );
				ElementType *ptr = nullptr;
				while( top != nullptr )
				{
					ptr = top;
					top = top->Next.load();
					delete ptr;
				}
			}

			/**
			 * @brief Push добавляет новый элемент
			 * @param val добавляемое значение
			 * @return true, если до добавления список был пуст
			 */
			bool Push( const T &val )
			{
				return internal::PushHead( Top, new ElementType( val ) );
			}

			/**
			 * @brief Push добавляет новый элемент
			 * @param val добавляемое значение
			 * @return true, если до добавления список был пуст
			 */
			bool Push( T &&val )
			{
				return internal::PushHead( Top, new ElementType( std::move( val ) ) );
			}

			/**
			 * @brief Push добавляет новый элемент
			 * @param args аргументы для создания нового значения списка
			 * @return true, если до добавления список был пуст
			 */
			template <typename ...Types>
			bool Push( Types ...args )
			{
				return internal::PushHead( Top, new ElementType( args... ) );
			}

			/**
			 * @brief Push добавляет новые элементы
			 * @param l добавляемый (потоконебезопасный) список
			 * @return true, если до добавления список был пуст
			 */
			bool Push( Unsafe &&l )
			{
				ElementType *new_top = nullptr;
				l.ReleaseTo( new_top );
				return new_top == nullptr ? false : internal::PushHead( Top, new_top, false );
			} // bool Push( UnsafeForwardList &l )

			/// Извлечение всех элементов в потоконебезопасный список
			Unsafe Release()
			{
				return Unsafe( std::move( Top ) );
			}

			operator bool() const
			{
				return Top.load() != nullptr;
			}
	}; // class ForwardList

	/// Очередь на отложенное удаление
	class DeferredDeleter
	{
		private:
			/// Абстрактный класс-"хранитель" удаляемого элемента
			class AbstractPtr
			{
				protected:
					AbstractPtr( const AbstractPtr& ) = delete;
					AbstractPtr& operator=( const AbstractPtr& ) = delete;
					AbstractPtr(){}

				public:
					virtual ~AbstractPtr() {}
			};

			/// Конкретный класс-"хранитель" удаляемого элемента
			template <typename T>
			class ConcretePtr: public AbstractPtr
			{
				private:
					T *Ptr;

				public:
					ConcretePtr( const ConcretePtr& ) = delete;
					ConcretePtr& operator=( const ConcretePtr& ) = delete;

					ConcretePtr( T *ptr ): Ptr( ptr )
					{
						MY_ASSERT( Ptr != nullptr );
					}
					virtual ~ConcretePtr()
					{
						delete Ptr;
					}
			};

			typedef std::atomic<uint64_t> EpochType;
			typedef std::unique_ptr<AbstractPtr> PtrType;
			typedef std::pair<PtrType, uint64_t> PtrEpochType;

			/// Очередь на удаление
			ForwardList<PtrEpochType> QueueToDelete;

			/// Текущая эпоха
			EpochType CurrentEpoch;

			/// Текущие эпохи потоков
			std::vector<EpochType> Epochs;

			/// Счётчик удалений
			std::atomic<uint16_t> DelCount;

			/// Счётчик занятых эпох
			std::atomic<uint16_t> EpochsCounter;

			/// Периодичность автоматических удалений из очереди
			const uint16_t DelPeriod;

			/// Показывает, надо ли удалять объекты из очереди
			std::atomic<bool> NeedToClean;

		public:
			/**
			 * @brief The EpochKeeper class занимает ячейку эпохи и освобождает при удалении
			 */
			class EpochKeeper
			{
				private:
					friend class DeferredDeleter;

					/// Указатель на занимаемую ячейку эпохи
					EpochType *EpochPtr;

					/// Указатель на счётчик занятых эпох
					std::atomic<uint16_t> *CounterPtr;

				public:
					EpochKeeper( const EpochKeeper& ) = delete;
					EpochKeeper& operator=( const EpochKeeper& ) = delete;

					EpochKeeper( EpochKeeper &&ep_keep ): EpochPtr( ep_keep.EpochPtr ),
					                                      CounterPtr( ep_keep.CounterPtr )
					{
						ep_keep.EpochPtr = nullptr;
						ep_keep.CounterPtr = nullptr;
					}

					EpochKeeper& operator=( EpochKeeper &&ep_keep )
					{
						if( &ep_keep != this )
						{
							Release();

							EpochPtr = ep_keep.EpochPtr;
							CounterPtr = ep_keep.CounterPtr;

							ep_keep.EpochPtr = nullptr;
							ep_keep.CounterPtr = nullptr;
						}

						return *this;
					}

					EpochKeeper(): EpochPtr( nullptr ), CounterPtr( nullptr ) {}

					EpochKeeper( EpochType &ep_ref,
					             std::atomic<uint16_t> &count_ref ): EpochPtr( &ep_ref ),
					                                                 CounterPtr( &count_ref )
					{}

					~EpochKeeper()
					{
						Release();
					}

					void Release()
					{
						MY_ASSERT( ( EpochPtr == nullptr ) == ( CounterPtr == nullptr ) );
						if( EpochPtr != nullptr )
						{
							EpochPtr->store( 0 );
							EpochPtr = nullptr;
						}

						if( CounterPtr != nullptr )
						{
							--( *CounterPtr );
							CounterPtr = nullptr;
						}
					}
			};

		public:
			DeferredDeleter() = delete;
			DeferredDeleter( const DeferredDeleter& ) = delete;
			const DeferredDeleter& operator=( const DeferredDeleter& ) = delete;

			/**
			 * @brief DeferredDeleter
			 * @param threads_num количество потоков
			 * @param period периодичность вызова Crear из Delete (0 - вызываться не будет)
			 */
			DeferredDeleter( uint8_t threads_num,
			                 uint16_t del_period = 0 ): CurrentEpoch( 1 ),
			                                            Epochs( threads_num > 0 ? threads_num : 1 ),
			                                            DelCount( 0 ), EpochsCounter( 0 ),
			                                            DelPeriod( del_period == 0 ? 1 : del_period )
			{
				MY_ASSERT( Epochs.size() > 0 );
				for( auto &iter : Epochs )
				{
					// 0 - значит, эпоха свободна
					iter.store( 0 );
				}
			}

			~DeferredDeleter()
			{
#ifdef UNITTEST
				for( const auto &ep : Epochs )
				{
					MY_ASSERT( ep.load() == 0 );
				}
#endif
			}

			/**
			 * @brief Delete добавление указателя в очередь на удаление,
			 * либо удаление, если есть возможность
			 * @param ptr указатель на удаляемый объект
			 */
			template <typename T>
			void Delete( T *ptr )
			{
				if( ptr == nullptr )
				{
					return;
				}
				else if( EpochsCounter.load() == 0 )
				{
					// Ни одной эпохи не захвачено
					delete ptr;
					return;
				}

				// Увеличиваем текущую эпоху и добавляем ptr в очередь на удаление
				PtrEpochType new_elem;
				new_elem.first.reset( ( AbstractPtr* ) new ConcretePtr<T>( ptr ) );
				new_elem.second = CurrentEpoch++;
				QueueToDelete.Push( std::move( new_elem ) );

				MY_ASSERT( DelPeriod > 0 );
				if( ( ++DelCount % DelPeriod ) == 0 )
				{
					NeedToClean.store( true );
				}
			} // void Delete( T *ptr )

			/// Удаление элементов очереди, которые возможно удалить
			void Clear()
			{
				// Извлекаем все элементы очереди на удаление
				auto queue = QueueToDelete.Release();
				if( !queue || ( EpochsCounter.load() == 0 ) )
				{
					// Очередь пуста, либо занятые эпохи отсутствуют
					// (во 2-м случае элементы удалятся автоматически, при уничтожении queue)
					return;
				}

				// Определяем минимальную занятую эпоху
				uint64_t min_epoch = 0xFFFFFFFFFFFFFFFF;
				uint64_t val;
				for( const auto &iter : Epochs )
				{
					val = iter.load();
					if( ( val > 0 ) && ( val < min_epoch ) )
					{
						min_epoch = val;
					}
				}

				// Удаляем все элементы, добавленные в очередь раньше, чем были заняты эпохи
				auto checker = [ min_epoch ]( const PtrEpochType &elem ) -> bool
				{
					MY_ASSERT( elem.first );
					return ( elem.second < min_epoch );
				};
				queue.RemoveIf( checker );

				// Добавляем все неудалённые элементы обратно в очередь
				QueueToDelete.Push( std::move( queue ) );
			}

			/**
			 * @brief ClearIfNeed Удаление элементов очереди, которые возможно удалить.
			 * Выполняется, если было выполнено достаточное количество вызовов Delete
			 */
			void ClearIfNeed()
			{
				if( NeedToClean.exchange( false ) )
				{
					Clear();
				}
			} // void ClearIfNeed()

			/**
			 * @brief EpochAcquire "Захват" эпохи (пока эпоха не будет
			 * освобождена, все элементы, добавленные в очередь после
			 * её захвата, удалены не будут)
			 * @return "хранитель" эпохи
			 */
			EpochKeeper EpochAcquire()
			{
				++EpochsCounter;
				EpochType *ep_ptr = nullptr;
				while( ep_ptr == nullptr )
				{
					for( auto &ep : Epochs )
					{
						uint64_t expected = 0;
						if( ep.compare_exchange_strong( expected,
						                                CurrentEpoch.load() ) )
						{
							// Ячейка эпохи "захвачена"
							ep_ptr = &ep;
							break;
						}
					}
				} // while( ep_ptr == nullptr )

				return EpochKeeper( *ep_ptr, EpochsCounter );
			} // EpochKeeper EpochAcquire()

			/// Обновление "занятой" эпохи у "хранителя"
			void UpdateEpoch( EpochKeeper &keeper )
			{
				if( keeper.EpochPtr != nullptr )
				{
					keeper.EpochPtr->store( CurrentEpoch.load() );
				}
			}
	};

	/// Максимальный рекомендованный размер данных очереди на удаление
	const uint16_t MaxSizeToDelete = 1024;
	
	template <typename T>
	uint16_t GetCleanPeriod()
	{
		size_t sz = sizeof( T );
		return MaxSizeToDelete / ( sz == 0 ? 1 : sz );
	}

	/// Класс стека (последний пришёл - первый вышел)
	template <typename T>
	class Stack
	{
		public:
			typedef T Type;

		private:
			typedef internal::StructElementType<T> ElementType;

			/// Начало списка
			std::atomic<ElementType*> Head;

			/// Очередь для отсроченного удаления, используемая по умолчанию
			std::unique_ptr<DeferredDeleter> DefaultQueue;

			/// Ссылка на очередь для отсроченного удаления
			DeferredDeleter &DefQueue;

		public:
			Stack( const Stack& ) = delete;
			Stack& operator=( const Stack& ) = delete;

			Stack( DeferredDeleter &def_queue ): Head( nullptr ),
			                                     DefaultQueue(),
			                                     DefQueue( def_queue ) {}

			Stack( uint8_t threads_num,
			       uint8_t clean_period = GetCleanPeriod<T>() ):
			    Head( nullptr ),
			    DefaultQueue( new DeferredDeleter( threads_num, clean_period ) ),
			    DefQueue( *DefaultQueue )
			{
				MY_ASSERT( DefaultQueue );
			}

			~Stack()
			{
				ElementType *head = Head.load();
				ElementType *ptr  = nullptr;
				while( head != nullptr )
				{
					ptr = head;
					head = head->Next.load();
					delete ptr;
				}
			}

			/**
			 * @brief Push добавление нового элемента в стек
			 * @param val значение нового элемента
			 * @return true, если до добавления элемента стек был пуст
			 */
			bool Push( const T &val )
			{
				return internal::PushHead( Head, new ElementType( val ) );
			}

			/**
			 * @brief Push добавление нового элемента в стек
			 * @param val значение нового элемента
			 * @return true, если до добавления элемента стек был пуст
			 */
			bool Push( T &&val )
			{
				return internal::PushHead( Head, new ElementType( std::move( val ) ) );
			}

			/**
			 * @brief Push добавление нового элемента в стек
			 * @param args аргументы для создания нового элемента
			 * @return true, если до добавления элемента стек был пуст
			 */
			template <typename ...Types>
			bool Push( Types ...args )
			{
				return internal::PushHead( Head, new ElementType( args... ) );
			}

			/**
			 * @brief Pop извлечение элемента из стека
			 * @param default_value_ptr указатель на значение по умолчанию,
			 * которое будет возвращено, если стек пуст;
			 * если параметр нулевой и стек пуст - будет выброшено исключение std::out_of_range
			 * @param is_empty если параемтр ненулевой - в него будет записана информация о том,
			 * был ли удалён последний элемент из стека (если он был непустым)
			 * @return элемент стека
			 */
			T Pop( const T *default_value_ptr = nullptr,
			       bool *is_empty = nullptr )
			{
				T result;
				if( is_empty != nullptr )
				{
					*is_empty = false;
				}

				// "Захватываем эпоху" (пока не отпустим - можем
				// спокойно разыменовывать указатели списка)
				auto epoch_keeper = DefQueue.EpochAcquire();

				auto old_head = Head.load();

				// Извлекаем первый элемент из головы списка,
				// помещаем в голову следующий элемент
				while( old_head != nullptr )
				{
					MY_ASSERT( old_head != nullptr );

					// Получаем указатель на элемент, следующий за головным
					auto new_head = old_head->Next.load();

					// Пытаемся изменить значение Head
					if( Head.compare_exchange_weak( old_head, new_head ) )
					{
						// Получилось
						if( is_empty != nullptr )
						{
							*is_empty = new_head == nullptr;
						}
						break;
					}
				} // while( old_head != nullptr )

				// Отпускаем "эпоху"
				epoch_keeper.Release();

				if( old_head != nullptr )
				{
					// Стек не был пуст
					result = std::move( old_head->Value );
					DefQueue.Delete( old_head );
					DefQueue.ClearIfNeed();
				}
				else
				{
					// Стек пуст
					DefQueue.ClearIfNeed();
					if( default_value_ptr != nullptr )
					{
						result = *default_value_ptr;
					}
					else
					{
						throw std::out_of_range( "Stack is empty!" );
					}
				}

				return result;
			} // T Pop
			
			/// Очистить очередь на отложенное удаление
			void CleanDeferredQueue()
			{
				DefQueue.Clear();
			}
	};
	
	/// Класс двусторонней очереди, хранящей 64-битные беззнаковые числа
	class DigitsQueue
	{
		public:
			typedef uint64_t Type;
			
			/// Фиктивное значение
			const Type FakeValue;

		private:
			typedef internal::StructElementType<std::atomic<Type>> ElementType;

			/// Голова (откуда читаем)
			std::atomic<ElementType*> Head;

			/// Хвост (куда пишем)
			std::atomic<ElementType*> Tail;

			/// Очередь для отсроченного удаления, используемая по умолчанию
			std::unique_ptr<DeferredDeleter> DefaultQueue;

			/// Ссылка на очередь для отсроченного удаления
			DeferredDeleter &DefQueue;
			
			void Init()
			{
				ElementType *fake_element = new ElementType( FakeValue );
				Head.store( fake_element );
				Tail.store( fake_element );
			}

		public:
			DigitsQueue( const DigitsQueue& ) = delete;
			DigitsQueue& operator=( const DigitsQueue& ) = delete;

			DigitsQueue( Type fake_value,
			             DeferredDeleter &def_deleter ): FakeValue( fake_value ),
			                                             Head( nullptr ), Tail( nullptr ),
			                                             DefaultQueue(),
			                                             DefQueue( def_deleter )
			{
				Init();
			}

			DigitsQueue( Type fake_value,
			             uint8_t threads_num,
			             uint16_t clean_period = GetCleanPeriod<Type>() ):
			    FakeValue( fake_value ),
			    Head( nullptr ), Tail( nullptr ),
			    DefaultQueue( new DeferredDeleter( threads_num, clean_period ) ),
			    DefQueue( *DefaultQueue )
			{
				Init();
			}

			~DigitsQueue()
			{
				ElementType *old_head = Head.load();
				ElementType *tmp = nullptr;
				while( old_head != nullptr )
				{
					tmp = old_head;
					old_head = old_head->Next.load();
					delete tmp;
				}
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param val новое значение
			 * @throw std::invalid_argument, если val имеет фиктивное значение
			 */
			void Push( Type val )
			{
				if( val == FakeValue )
				{
					// Недопустимое значение
					MY_ASSERT( false );
					throw std::invalid_argument( "Cannot add fake value to queue" );
				}
				
				std::unique_ptr<ElementType> new_elem;

				// "Захватываем" эпоху, чтобы можно было обращаться к "хвосту"
				// без риска, что он будет удалён
				auto epoch_keeper = DefQueue.EpochAcquire();

				ElementType *old_tail = Tail.load();
				while( val != FakeValue )
				{
					MY_ASSERT( old_tail != nullptr );

					// Создаём новый фиктивный элемент, если нужно
					if( !new_elem )
					{
						new_elem = std::unique_ptr<ElementType>( new ElementType( FakeValue ) );
						MY_ASSERT( new_elem && ( new_elem.get()->Value.load() == FakeValue ) );
					}

					// Ожидаем, что в хвост ещё не записаны данные
					Type expected_val = FakeValue;

					// Пытаемся записать данные в фиктивный (предположительно) элемент
					if( old_tail->Value.compare_exchange_strong( expected_val, val ) )
					{
						// Значение добавлено, присваиваем val-у фиктивное значение
						val = FakeValue;
					}
					MY_ASSERT( old_tail->Value.load() != FakeValue );

					// Пытаемся добавить фиктивный элемент в хвост
					ElementType *expected_elem_ptr = nullptr;
					if( old_tail->Next.compare_exchange_strong( expected_elem_ptr,
					                                            new_elem.get() ) )
					{
						// Фиктивный элемент добавлен
						expected_elem_ptr = new_elem.release();
						MY_ASSERT( !new_elem && ( new_elem.get() == nullptr ) );
					}

					// К этому моменту expected_elem_ptr хранит значение old_tail->Next
					MY_ASSERT( expected_elem_ptr != nullptr );

					// Записываем в Tail указатель на новый хвост
					Tail.compare_exchange_strong( old_tail, expected_elem_ptr );
				} // while( val != FakeValue )
			} // void Push( Type val )
			
			template <typename T>
			void Push( T *ptr )
			{
				Push( ( Type ) ptr );
			}

			/**
			 * @brief Pop извлечение элемента из головы очереди
			 * если очередь пуста - будет возвращено фиктивное значение
			 * @return элемент из головы очереди
			 */
			Type Pop()
			{
				Type res = FakeValue;
				auto epoch_keeper = DefQueue.EpochAcquire();
				ElementType *old_head = Head.load();

				while( res == FakeValue )
				{
					MY_ASSERT( old_head != nullptr );
					if( old_head == Tail.load() )
					{
						// Очередь состоит из одного элемента (считаем, что пуста)
						break;
					}

					auto new_head = old_head->Next.load();
					MY_ASSERT( new_head != nullptr );
					if( Head.compare_exchange_weak( old_head, new_head ) )
					{
						// Элемент из головы очереди извлечён
						res = old_head->Value.load();
						MY_ASSERT( res != FakeValue );
					}
				} // while( res == FakeValue )

				// Отпускаем эпоху, удаляем элементы очереди, которые можно
				epoch_keeper.Release();
				DefQueue.ClearIfNeed();
				
				return res;
			} // Type Pop()
			
			/// Очистить очередь на отложенное удаление
			void CleanDeferredQueue()
			{
				DefQueue.Clear();
			}
	}; // class DigitsQueue

	/// Класс двусторонней очереди
	template <typename T>
	class Queue
	{
		public:
			typedef T Type;

		private:
			/// Очередь, хранящая указатели в виде чисел
			DigitsQueue PtrsQueue;

			/// Добавляет новый элемент в хвост
			void PushElement( std::unique_ptr<T> &val_smart_ptr )
			{
				MY_ASSERT( val_smart_ptr );
				MY_ASSERT( sizeof( DigitsQueue::Type ) >= sizeof( val_smart_ptr.get() ) );
				PtrsQueue.Push( ( DigitsQueue::Type ) val_smart_ptr.get() );
				val_smart_ptr.release();
			}

		public:
			Queue( const Queue& ) = delete;
			Queue& operator=( const Queue& ) = delete;

			Queue( DeferredDeleter &def_deleter ): PtrsQueue( 0, def_deleter )
			{}

			Queue( uint8_t threads_num,
			       uint16_t clean_period = GetCleanPeriod<T>() ): PtrsQueue( 0, threads_num, clean_period )
			{}

			~Queue()
			{
				for( auto val = PtrsQueue.Pop(); val != 0; val = PtrsQueue.Pop() )
				{
					delete ( T* ) val;
				}
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param val новое значение
			 */
			void Push( const T &val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( val ) );
				PushElement( val_smart_ptr );
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param val новое значение
			 */
			void Push( T &&val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( std::move( val ) ) );
				PushElement( val_smart_ptr );
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param args аргументы для создания нового элемента
			 */
			template <typename ...Types>
			void Push( Types ...args )
			{
				std::unique_ptr<T> val_smart_ptr( new T( args... ) );
				PushElement( val_smart_ptr );
			}

			/**
			 * @brief Pop извлечение элемента из головы очереди
			 * если очередь пуста - будет возвращён нулевой указатель
			 * @return элемент (в виде указателя) из головы очереди
			 */
			std::unique_ptr<T> Pop()
			{
				return std::unique_ptr<T>( ( T* ) PtrsQueue.Pop() );
			}
			
			/// Очистить очередь на отложенное удаление
			void CleanDeferredQueue()
			{
				PtrsQueue.CleanDeferredQueue();
			}
	}; // class Queue
} // namespace LockFree
