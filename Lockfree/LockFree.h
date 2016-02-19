#ifndef LOCK_FREE_H
#define LOCK_FREE_H

#include <atomic>
#include <stdint.h>
#include <stdexcept>
#include <functional>

#ifdef UNITTEST
#include <assert.h>
#define MY_ASSERT( EXPR ) assert( EXPR )
#else
#define MY_ASSERT( EXPR )
#endif

namespace LockFree
{
#ifdef UNITTEST
	struct DebugStruct
	{
		private:
			bool WasDeleted;
			const bool EditCounter;

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
			DebugStruct( int64_t v = 0 ): WasDeleted( false ),
			                              EditCounter( true ),
			                              Val( v )
			{
				if( EditCounter )
				{
					CounterWork( 1 );
				}
			}

			DebugStruct( bool, bool ): WasDeleted( false ),
			                     EditCounter( false ),
			                     Val( 123 )
			{
				if( EditCounter )
				{
					CounterWork( 1 );
				}
			}

			DebugStruct( const DebugStruct &deb ): WasDeleted( false ),
			                                       EditCounter( true ),
			                                       Val( deb.Val )
			{
				MY_ASSERT( !deb.WasDeleted );
				if( EditCounter )
				{
					CounterWork( 1 );
				}
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
				MY_ASSERT( !WasDeleted );
				MY_ASSERT( !deb.WasDeleted );
				return Val != deb.Val;
			}

			~DebugStruct()
			{
				MY_ASSERT( !WasDeleted );
				WasDeleted = true;
				if( EditCounter )
				{
					CounterWork( -1 );
				}
			}
	};
#endif

	namespace
	{
		/**
		 * @brief The StructElementType struct Класс элемента списка/стека/очереди
		 */
		template <typename T>
		struct StructElementType
		{
			/// Значение
			T Value;

			/// Указатель на следующий элемент
			std::atomic<StructElementType*> Next;

			StructElementType(): Value(), Next( nullptr ) {}

			template<typename Type>
			StructElementType( Type val ): Value( std::forward<Type>( val ) ),
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
			StructElementType( Type val ): Value( std::forward<Type>( val ) ),
										   Next( nullptr ) {}

			template <typename Type, typename ...Types>
			StructElementType( Type arg,
							   Types... args ): Value( arg ),
												Next( nullptr ) {}

#ifdef _DEBUG
			void operator delete( void* ) {}
#endif
		};

		template<>
		struct StructElementType<std::atomic<DebugStruct*>>
		{
			/// Значение
			std::atomic<DebugStruct*> Value;
			DebugStruct Fake;

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

			/// Указатель на следующий элемент
			std::atomic<StructElementType*> Next;

			StructElementType(): Fake( true, false ), Value(), Next( nullptr ) { CounterWork( 1 ); }
			~StructElementType()
			{
				MY_ASSERT( ( Value.load() == ( DebugStruct* ) ( 0 - 1 ) ) ||
				           ( Value.load() == nullptr ) );
				CounterWork( -1 );
			}

			template<typename Type>
			StructElementType( Type val ): Fake( true, true ), Value( std::forward<Type>( val ) ),
										   Next( nullptr ) { CounterWork( 1 ); }

			template <typename Type, typename ...Types>
			StructElementType( Type arg,
							   Types... args ): Fake( true ), Value( arg ),
												Next( nullptr ) { CounterWork( 1 ); }
#ifdef _DEBUG
			void operator delete( void* ) {}
#endif
		};
#endif
	}

	/**
	 * @brief The ForwardList class Класс потокобезопасного однонаправленого списка
	 */
	template <typename T>
	class ForwardList
	{
		public:
			typedef T Type;

		private:
			typedef StructElementType<T> ElementType;

			/// Начало списка
			std::atomic<ElementType*> Top;

		public:
			/**
			 * @brief The Unsafe class Класс потоконебезопасного списка
			 */
			class Unsafe
			{
				public:
					typedef T Type;

				private:
					/// Начало списка
					ElementType *Top;

				public:
					Unsafe( const Unsafe& ) = delete;
					Unsafe& operator=( const Unsafe& ) = delete;

					Unsafe( Unsafe &&l ): Top( l.Top ) { l.Top = 0; }
					Unsafe& operator=( Unsafe &&l )
					{
						if( &l != this )
						{
							Top = l.Top;
							l.Top = 0;
						}

						return *this;
					}

					Unsafe(): Top( nullptr ){}
					Unsafe( std::atomic<ElementType*> &ptr ): Top( ptr.exchange( nullptr ) ){}
					~Unsafe()
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

					/// Извлечение элемента из начала списка
					T Pop( const T *def_value = nullptr )
					{
						if( Top != nullptr )
						{
							ElementType *old_top = Top;
							Top = Top->Next;
							T result( std::move( old_top->Value ) );
							delete old_top;
							return result;
						}
						else if( def_value != nullptr )
						{
							return *def_value;
						}
						else
						{
							throw std::out_of_range( "List is empty!" );
						}
					}

					/// Удаляет все элементы, удовлетворяющие условию
					void RemoveIf( std::function<bool( const T& )> checker )
					{
						if( checker && ( Top != nullptr ) )
						{
							ElementType *new_top = Top;
							ElementType *first_to_delete = nullptr;
							ElementType *last_to_delete  = nullptr;

							if( checker( Top->Value ) )
							{
								// Первый элемент нужно удалить
								first_to_delete = Top;
								last_to_delete  = Top;
								new_top = Top->Next.load();

								for( ; ( new_top != nullptr ) && ( checker( new_top->Value ) );
								     new_top = new_top->Next.load() )
								{
									last_to_delete = new_top;
								}

								if( last_to_delete != nullptr )
								{
									last_to_delete->Next.store( nullptr );
								}
							}

							if( new_top != nullptr )
							{
								// new_top указывает на первый элемент списка, который НЕ должен быть удалён
								for( auto nondel_ptr = new_top; nondel_ptr != nullptr; )
								{
									auto next_ptr = nondel_ptr->Next.load();
									if( ( next_ptr != nullptr ) && checker( next_ptr->Value ) )
									{
										// Следующий nondel_ptr-ом элемент должен быть удалён
										if( last_to_delete != nullptr )
										{
											last_to_delete->Next.store( next_ptr );
										}
										else
										{
											first_to_delete = next_ptr;
										}
										last_to_delete = next_ptr;

										auto next_nondel_ptr = next_ptr->Next.load();
										for( ; next_nondel_ptr != nullptr;
										     next_nondel_ptr = next_nondel_ptr->Next.load() )
										{
											if( checker( next_nondel_ptr->Value ) )
											{
												last_to_delete = next_nondel_ptr;
											}
											else
											{
												break;
											}
										}

										nondel_ptr->Next.store( next_nondel_ptr );
										nondel_ptr = next_nondel_ptr;
										last_to_delete->Next.store( nullptr );
									}
									else
									{
										nondel_ptr = next_ptr;
									}
								}
							} // if( new_top != nullptr )

							// Удаляем вссе элементы, какие надо
							ElementType *tmp = nullptr;
							Top = new_top;
							while( first_to_delete != nullptr )
							{
								tmp = first_to_delete;
								first_to_delete = first_to_delete->Next.load();
								delete tmp;
							}
						}
					} // void RemoveIf( std::function<bool( const T& )> checker )

					bool ReleaseTo( ElementType* &storage )
					{
						if( storage != nullptr )
						{
							return false;
						}

						storage = Top;
						Top = 0;
						return true;;
					}
			};

		public:
			ForwardList( const ForwardList& )= delete;
			ForwardList( ForwardList&& ) = delete;
			ForwardList& operator=( const ForwardList& ) = delete;
			ForwardList& operator=( ForwardList&& ) = delete;

			ForwardList(): Top( 0 ) {}
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

			template <typename ...Types>
			void Push( Types ...args )
			{
				ElementType *new_element = new ElementType( args... );
				ElementType *expected_top = Top.load();
				while( 1 )
				{
					new_element->Next.store( expected_top );
					if( Top.compare_exchange_strong( expected_top, new_element ) )
					{
						break;
					}
				}
			}

			void Push( Unsafe &l )
			{
				ElementType *new_top = nullptr;
				l.ReleaseTo( new_top );
				if( new_top != nullptr )
				{
					ElementType *bottom = new_top;
					while( bottom->Next.load() != nullptr )
					{
#ifdef _DEBUG
						MY_ASSERT( bottom != nullptr );
#endif
						bottom = bottom->Next.load();
					}

					ElementType *expected_top = Top.load();
					while( 1 )
					{
#ifdef _DEBUG
						MY_ASSERT( bottom != nullptr );
#endif
						bottom->Next.store( expected_top );
						if( Top.compare_exchange_strong( expected_top, new_top ) )
						{
							break;
						}
					}
				} // if( new_top != nullptr )
			} // void Push( Unsafe &l )

			/// Извлечение всех элементов в потоконебезопасный список
			Unsafe Release()
			{
				return Unsafe( Top );
			}
	}; // class ForwardList

	/**
	 * @brief The DeferredDeleter class очередь на отложенное удаление
	 */
	template <typename T>
	class DeferredDeleter
	{
		private:
			typedef std::atomic<uint64_t> EpochType;

			/// Очередь на удаление
			ForwardList<std::pair<T*, uint64_t>> QueueToDelete;

			/// Текущая эпоха
			EpochType CurrentEpoch;

			/// Текущие эпохи потоков
			std::vector<EpochType> Epochs;

		public:
			/**
			 * @brief The EpochKeeper class занимает ячейку эпохи и освобождает при удалении
			 */
			class EpochKeeper
			{
				private:
					EpochType *EpochPtr;

				public:
					EpochKeeper() = delete;
					EpochKeeper( const EpochKeeper& ) = delete;
					EpochKeeper& operator=( const EpochKeeper& ) = delete;

					EpochKeeper( EpochKeeper &&ep_keep ): EpochPtr( ep_keep.EpochPtr )
					{
						ep_keep.EpochPtr = nullptr;
					}

					EpochKeeper& operator=( EpochKeeper &&ep_keep )
					{
						Release();
						EpochPtr = ep_keep.EpochPtr;
						ep_keep.EpochPtr = nullptr;
					}

					EpochKeeper( EpochType *ep_ptr ): EpochPtr( ep_ptr ) {}
					~EpochKeeper()
					{
						Release();
					}

					void Release()
					{
						if( EpochPtr != nullptr )
						{
							EpochPtr->store( 0 );
							EpochPtr = nullptr;
						}
					}
			};

		public:
			DeferredDeleter() = delete;
			DeferredDeleter( const DeferredDeleter& ) = delete;
			const DeferredDeleter& operator=( const DeferredDeleter& ) = delete;
			DeferredDeleter( DeferredDeleter&& ) = delete;
			const DeferredDeleter& operator=( DeferredDeleter&& ) = delete;

			DeferredDeleter( uint8_t threads_num ): CurrentEpoch( 1 ),
			                                        Epochs( threads_num != 0 ? threads_num : 1 )
			{
				for( auto &iter : Epochs )
				{
					iter.store( 0 );
				}
			}

			~DeferredDeleter()
			{
				try
				{
					auto queue = QueueToDelete.Release();
					while( true )
					{
						delete queue.Pop().first;
					}
				}
				catch( const std::out_of_range& )
				{
					// Все элементы удалены
				}
			}

			/**
			 * @brief Delete добавление указателя в очередь на удаление,
			 * либо удаление, если есть возможность
			 * @param ptr
			 */
			void Delete( T *ptr )
			{
				if( ptr == nullptr )
				{
					return;
				}

				bool deff_del = false;
				for( const auto &iter : Epochs )
				{
					if( iter.load() != 0 )
					{
						deff_del = true;
						break;
					}
				}

				if( deff_del )
				{
					// Увеличиваем эпоху и добавляем ptr в очередь на удаление
					QueueToDelete.Push( ptr, CurrentEpoch++ );
				}
				else
				{
					// Можем удалить сразу
					delete ptr;
				}
			}

			/**
			 * @brief Clear удаление элементов очереди, которые возможно удалить
			 */
			void Clear()
			{
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
				auto checker = [ min_epoch ]( const std::pair<T*, uint64_t> &elem ) -> bool
				{
					if( elem.second < min_epoch )
					{
						// Все эпохи были "захвачены" потоками позже,
						// чем элемент был добавлен в очередь на удаление
						delete elem.first;
						return true;
					}

					return false;
				};
				auto queue = QueueToDelete.Release();
				queue.RemoveIf( checker );

				// Добавляем все неудалённые элементы обратно в очередь
				QueueToDelete.Push( queue );
			}

			/**
			 * @brief EpochAcquire "Захват" эпохи (пока эпоха не будет
			 * освобождена, все элементы, добавленные в очередь после
			 * её захвата, удалены не будут)
			 * @return "хранитель" эпохи
			 */
			EpochKeeper EpochAcquire()
			{
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
				}

				return EpochKeeper( ep_ptr );
			}
	};

	/// Класс стека (последний пришёл - первый вышел)
	template <typename T>
	class Stack
	{
		public:
			typedef T Type;

		private:
			typedef StructElementType<T> ElementType;

			/// Начало списка
			std::atomic<ElementType*> Head;

			/// Очередь для отсроченного удаления
			DeferredDeleter<ElementType> DefQueue;

		public:
			Stack( const Stack& ) = delete;
			Stack& operator=( const Stack& ) = delete;

			Stack( uint8_t threads_num ): Head( nullptr ),
			                              DefQueue( threads_num ) {}
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
			 * @param args аргументы для создания нового элемента
			 * @return true, если до добавления элемента стек был пуст
			 */
			template <typename ...Types>
			bool Push( Types ...args )
			{
				ElementType *new_head = new ElementType( args... );
				ElementType *old_head = Head.load();

				while( true )
				{
					new_head->Next = old_head;
					if( Head.compare_exchange_strong( old_head, new_head ) )
					{
						return old_head == nullptr;
					}
				}

				DefQueue.Clear();
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
				auto epoch_keeper = DefQueue.EpochAcquire();

				auto old_head = Head.load();

				while( true )
				{
					if( old_head == nullptr )
					{
						// Стек пуст
						break;
					}
					else
					{
						auto new_head = old_head->Next.load();
						if( Head.compare_exchange_strong( old_head, new_head ) )
						{
							if( is_empty != nullptr )
							{
								*is_empty = new_head == nullptr;
							}
							break;
						}
					}
				} // while( true )

				epoch_keeper.Release();
				if( old_head != nullptr )
				{
					result = std::move( old_head->Value );
					DefQueue.Delete( old_head );
				}
				else
				{
					// Стек пуст
					if( default_value_ptr != nullptr )
					{
						result = *default_value_ptr;
					}
					else
					{
						DefQueue.Clear();
						throw std::out_of_range( "Stack is empty!" );
					}
				}

				DefQueue.Clear();
				return result;
			}
	};

	/// Класс простого умного указателя
	template <typename T>
	class SmartPtr
	{
		private:
			T *Pointer;

			void Clear()
			{
				if( Pointer != nullptr )
				{
					delete Pointer;
				}
			}

		public:
			SmartPtr( const SmartPtr& ) = delete;
			SmartPtr& operator=( const SmartPtr& ) = delete;

			SmartPtr( T *ptr = nullptr ): Pointer( ptr ) {}
			SmartPtr( SmartPtr &&ptr ): Pointer( ptr.Pointer ) { ptr.Pointer = nullptr; }
			~SmartPtr()
			{
				Clear();
			}

			SmartPtr& operator=( SmartPtr &&ptr )
			{
				if( &ptr != this )
				{
					Clear();
					Pointer = ptr.Pointer;
					ptr.Pointer = nullptr;
				}

				return *this;
			}

			T* Get() { return Pointer; }
			const T* Get() const { return Pointer; }

			T* Release()
			{
				T *res = Pointer;
				Pointer = nullptr;
				return res;
			}

			operator bool() const
			{
				return Pointer != nullptr;
			}
	};

	/// Класс двусторонней очереди
	template <typename T>
	class Queue
	{
		public:
			typedef T Type;

		private:
			typedef StructElementType<std::atomic<T*>> ElementType;

			/// Голова (откуда читаем)
			std::atomic<ElementType*> Head;

			/// Хвост (куда пишем)
			std::atomic<ElementType*> Tail;

			/// Очередь для отсроченного удаления
			DeferredDeleter<ElementType> DefQueue;

		public:
			Queue( const Queue& ) = delete;
			Queue& operator=( const Queue& ) = delete;

			Queue( uint8_t threads_num ): Head( nullptr ), Tail( nullptr ),
			                              DefQueue( threads_num )
			{
				ElementType *fake_element = new ElementType( nullptr );
				Head.store( fake_element );
				Tail.store( fake_element );
			}

			~Queue()
			{
				ElementType *old_head = Head.load();
				ElementType *tmp = nullptr;
				T *val_ptr = nullptr;
				while( old_head != nullptr )
				{
					val_ptr = old_head->Value.exchange( nullptr );
					tmp = old_head;
					MY_ASSERT( ( val_ptr != nullptr ) == ( old_head->Next.load() != nullptr ) );
					old_head = old_head->Next.load();
					if( val_ptr != nullptr )
					{
						delete val_ptr;
					}
					delete tmp;
				}
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param args аргументы для создания нового элемента
			 */
			template <typename ...Types>
			void Push( Types ...args )
			{
				SmartPtr<T> val_smart_ptr( new T( args... ) );
				MY_ASSERT( val_smart_ptr );
				SmartPtr<ElementType> new_elem;

				while( val_smart_ptr )
				{
					// Создаём новый фиктивный элемент, если нужно
					if( !new_elem )
					{
						new_elem = SmartPtr<ElementType>( new ElementType( nullptr ) );
						MY_ASSERT( new_elem && ( new_elem.Get()->Value.load() == nullptr ) );
					}
					
					// "Захватываем" эпоху, чтобы можно было обращаться к "хвосту"
					// без риска, что он будет удалён
					auto epoch_keeper = DefQueue.EpochAcquire();

					// Ожидаем, что в хвост ещё не записаны данные
					T *expected_val = nullptr;
					ElementType *old_tail = Tail.load();

					// Пытаемся записать данные в фиктивный (предположительно) элемент
					if( old_tail->Value.compare_exchange_strong( expected_val, val_smart_ptr.Get() ) )
					{
						// Значение добавлено
						val_smart_ptr.Release();
						MY_ASSERT( !val_smart_ptr && ( val_smart_ptr.Get() == nullptr ) );
					}
					MY_ASSERT( old_tail->Value.load() != nullptr );

					// Пытаемся добавить фиктивный элемент в хвост
					ElementType *expected_elem_ptr = nullptr;
					if( old_tail->Next.compare_exchange_strong( expected_elem_ptr,
					                                            new_elem.Get() ) )
					{
						// Фиктивный элемент добавлен
						expected_elem_ptr = new_elem.Release();
					}

					// Записываем в Tail указатель на новый хвост
					Tail.compare_exchange_strong( old_tail, expected_elem_ptr );
				} // while( val_smart_ptr )
			} // void Push( Types ...args )

			/**
			 * @brief Pop извлечение элемента из головы очереди
			 * если очередь пуста - будет возвращён нулевой указатель
			 * @return элемент (в виде указателя) из головы очереди
			 */
			SmartPtr<T> Pop()
			{
				auto epoch_keeper = DefQueue.EpochAcquire();
				ElementType *old_head = Head.load();
				
				while( true )
				{
					MY_ASSERT( old_head != nullptr );
					if( old_head == Tail.load() )
					{
						// Очередь пуста
						epoch_keeper.Release();
						DefQueue.Clear();
						return SmartPtr<T>();
					}

					auto new_head = old_head->Next.load();
					MY_ASSERT( new_head != nullptr );
					if( Head.compare_exchange_strong( old_head, new_head ) )
					{
						// Элемент из головы очереди извлечён
						epoch_keeper.Release();
						SmartPtr<T> result( old_head->Value.exchange( ( T* ) ( 0 - 1 ) ) );
						MY_ASSERT( result );
						DefQueue.Delete( old_head );
						DefQueue.Clear();
						return SmartPtr<T>( std::move( result ) );
					}
				}
			} // SmartPtr<T> Pop()
	}; // class Queue
} // namespace LockFree

#endif // #ifndef LOCK_FREE_H
