#pragma once

#include <atomic>
#include <vector>
#include <stdint.h>
#include <stdexcept>
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

		/**
		 * @brief Push добавление нового головного элемента однонаправленного списка
		 * @param head ссылка на головной элемент списка
		 * @param new_head новый головной элемент (должен быть не nullptr)
		 * @return был ли head нулевым до его замены
		 * @throw std::invalid_argument, если new_head == nullptr
		 */
		template <typename T>
		bool PushHead( std::atomic<StructElementType<T>*> &head,
		               StructElementType<T> *new_head )
		{
			if( new_head == nullptr )
			{
				MY_ASSERT( false );
				throw std::invalid_argument( "New head cannot be nullptr" );
			}

			StructElementType<T> *old_head = head.load();
			while( true )
			{
				new_head->Next.store( old_head );
				if( head.compare_exchange_weak( old_head, new_head ) )
				{
					return old_head == nullptr;
				}
			}

			MY_ASSERT( false );
			return false;
		}
	} // namespace internal

	/**
	 * @brief The ForwardList class Класс потокобезопасного однонаправленого списка
	 */
	template <typename T>
	class ForwardList
	{
		public:
			typedef T Type;

		private:
			typedef internal::StructElementType<T> ElementType;

			/// Начало списка
			std::atomic<ElementType*> Top;

		public:
			/**
			 * @brief The Unsafe class Класс потоконебезопасного однонаправленного списка
			 */
			class Unsafe
			{
				public:
					typedef T Type;

				private:
					/// Начало списка
					ElementType *Top;

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

				public:
					Unsafe( const Unsafe& ) = delete;
					Unsafe& operator=( const Unsafe& ) = delete;

					Unsafe( Unsafe &&l ): Top( l.Top ) { l.Top = nullptr; }
					Unsafe& operator=( Unsafe &&l )
					{
						if( &l != this )
						{
							Clean();
							Top = l.Top;
							l.Top = nullptr;
						}

						return *this;
					}

					Unsafe(): Top( nullptr ){}
					Unsafe( std::atomic<ElementType*> &&ptr ): Top( ptr.exchange( nullptr ) ){}

					~Unsafe()
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

						if( def_value == nullptr )
						{
							throw std::out_of_range( "List is empty!" );
						}

						return *def_value;
					}

					/// Удаляет все элементы, удовлетворяющие условию
					template<typename F>
					void RemoveIf( F checker )
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
						for( ElementType *ptr = Top,
						     *next_ptr = nullptr;
						     ptr != nullptr; )
						{
							MY_ASSERT( ptr != nullptr );
							MY_ASSERT( !checker( ptr->Value ) );
							next_ptr = ptr->Next.load();
							if( ( next_ptr != nullptr ) && checker( next_ptr->Value ) )
							{
								// Нужно удалить следующий элемент
								ptr->Next.store( next_ptr->Next.load() );
								delete next_ptr;
							}
							else
							{
								// Следующий элемент удалять не надо
								ptr = next_ptr;
							}
						}
					} // template<typename F> void RemoveIf( F checker )

					bool ReleaseTo( ElementType* &storage )
					{
						if( storage != nullptr )
						{
							return false;
						}

						storage = Top;
						Top = 0;
						return true;
					}
			};

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
				if( new_top == nullptr )
				{
					// Нечего добавлять
					return Top.load() == nullptr;
				}

				ElementType *bottom = new_top;
				for( ElementType *ptr = bottom->Next.load();
				     ptr != nullptr;
				     ptr = bottom->Next.load() )
				{
					bottom = ptr;
				}

				bool res = false;
				MY_ASSERT( bottom != nullptr );
				MY_ASSERT( bottom->Next.load() == nullptr );
				ElementType *expected_top = Top.load();
				while( 1 )
				{
					MY_ASSERT( bottom != nullptr );
					bottom->Next.store( expected_top );
					if( Top.compare_exchange_strong( expected_top, new_top ) )
					{
						res = expected_top == nullptr;
						break;
					}
				}

				return res;
			} // bool Push( Unsafe &l )

			/// Извлечение всех элементов в потоконебезопасный список
			Unsafe Release()
			{
				return Unsafe( std::move( Top ) );
			}
	}; // class ForwardList

	/**
	 * @brief The DeferredDeleter class очередь на отложенное удаление
	 */
	class DeferredDeleter
	{
		private:
			/// Абстрактный класс-"хранитель" удаляемого элемента
			class AbstractPtr
			{
				public:
					AbstractPtr( const AbstractPtr& ) = delete;
					AbstractPtr& operator=( const AbstractPtr& ) = delete;
					AbstractPtr(){}
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

					ConcretePtr( T *ptr ): Ptr( ptr ) {}
					virtual ~ConcretePtr()
					{
						if( Ptr != nullptr )
						{
							delete Ptr;
						}
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

		public:
			/**
			 * @brief The EpochKeeper class занимает ячейку эпохи и освобождает при удалении
			 */
			class EpochKeeper
			{
				private:
					/// Указатель на занимаемую ячейку эпохи
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
						if( &ep_keep != this )
						{
							Release();
							EpochPtr = ep_keep.EpochPtr;
							ep_keep.EpochPtr = nullptr;
						}

						return *this;
					}

					EpochKeeper( EpochType *ep_ptr ): EpochPtr( ep_ptr )
					{
						MY_ASSERT( ( ep_ptr == nullptr ) || ( ep_ptr->load() != 0 ) );
					}

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

			DeferredDeleter( uint8_t threads_num ): CurrentEpoch( 1 ),
			                                        Epochs( threads_num > 0 ? threads_num : 1 )
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

				// Смотрим, есть ли "занятые" эпохи и, если нет,
				// то удаляем объект сразу (если проверили эпоху,
				// а после этого она стала занята - не страшно, т.к.
				// поток, занявший эпоху, не будет обращаться к удаляемому объекту)
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
					// Увеличиваем текущую эпоху и добавляем ptr в очередь на удаление
					PtrEpochType new_elem;
					new_elem.first.reset( ( AbstractPtr* ) new ConcretePtr<T>( ptr ) );
					new_elem.second = CurrentEpoch++;
					QueueToDelete.Push( std::move( new_elem ) );
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
				auto checker = [ min_epoch ]( const PtrEpochType &elem ) -> bool
				{
					MY_ASSERT( elem.first );
					return ( elem.second < min_epoch );
				};
				auto queue = QueueToDelete.Release();
				queue.RemoveIf( checker );

				// Добавляем все неудалённые элементы обратно в очередь
				QueueToDelete.Push( std::move( queue ) );
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

			Stack( uint8_t threads_num ): Head( nullptr ),
			                              DefaultQueue( new DeferredDeleter( threads_num ) ),
			                              DefQueue( *DefaultQueue ) {}
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

				// "Захватываем эпоху" (пока не отпустим - можем спокойно разыменовывать указатели списка)
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
					DefQueue.Clear();
				}
				else
				{
					// Стек пуст
					DefQueue.Clear();
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
			}
	};

	/// Класс двусторонней очереди
	template <typename T>
	class Queue
	{
		public:
			typedef T Type;

		private:
			typedef internal::StructElementType<std::atomic<T*>> ElementType;

			/// Голова (откуда читаем)
			std::atomic<ElementType*> Head;

			/// Хвост (куда пишем)
			std::atomic<ElementType*> Tail;

			/// Очередь для отсроченного удаления, используемая по умолчанию
			std::unique_ptr<DeferredDeleter> DefaultQueue;

			/// Ссылка на очередь для отсроченного удаления
			DeferredDeleter &DefQueue;

			/// Добавляет новый элемент в хвост
			void PushElement( std::unique_ptr<T> &val_smart_ptr )
			{
				MY_ASSERT( val_smart_ptr );
				std::unique_ptr<ElementType> new_elem;

				// "Захватываем" эпоху, чтобы можно было обращаться к "хвосту"
				// без риска, что он будет удалён
				auto epoch_keeper = DefQueue.EpochAcquire();

				ElementType *old_tail = Tail.load();
				while( val_smart_ptr )
				{
					MY_ASSERT( old_tail != nullptr );

					// Создаём новый фиктивный элемент, если нужно
					if( !new_elem )
					{
						new_elem = std::unique_ptr<ElementType>( new ElementType( nullptr ) );
						MY_ASSERT( new_elem && ( new_elem.get()->Value.load() == nullptr ) );
					}

					// Ожидаем, что в хвост ещё не записаны данные
					T *expected_val = nullptr;

					// Пытаемся записать данные в фиктивный (предположительно) элемент
					if( old_tail->Value.compare_exchange_strong( expected_val, val_smart_ptr.get() ) )
					{
						// Значение добавлено, очищаем val_smart_reset
						// (теперь удалением объекта будет заниматься тот, кто извлечёт элемент)
						val_smart_ptr.release();
						MY_ASSERT( !val_smart_ptr && ( val_smart_ptr.get() == nullptr ) );
					}
					MY_ASSERT( old_tail->Value.load() != nullptr );

					// Пытаемся добавить фиктивный элемент в хвост
					ElementType *expected_elem_ptr = nullptr;
					if( old_tail->Next.compare_exchange_strong( expected_elem_ptr,
					                                            new_elem.get() ) )
					{
						// Фиктивный элемент добавлен
						expected_elem_ptr = new_elem.release();
						MY_ASSERT( !new_elem && ( new_elem.get() == nullptr ) );
					}

					// К этому моменту expected_elem_ptr хранить значение old_tail->Next
					MY_ASSERT( expected_elem_ptr != nullptr );

					// Записываем в Tail указатель на новый хвост
					Tail.compare_exchange_strong( old_tail, expected_elem_ptr );
				} // while( val_smart_ptr )
			} // bool PushElement( std::unique_ptr<T> &val_smart_ptr )

		public:
			Queue( const Queue& ) = delete;
			Queue& operator=( const Queue& ) = delete;

			Queue( DeferredDeleter &def_deleter ): Head( nullptr ), Tail( nullptr ),
			                                       DefaultQueue(),
			                                       DefQueue( def_deleter )
			{
				ElementType *fake_element = new ElementType( nullptr );
				Head.store( fake_element );
				Tail.store( fake_element );
			}

			Queue( uint8_t threads_num ): Head( nullptr ), Tail( nullptr ),
			                              DefaultQueue( new DeferredDeleter( threads_num ) ),
			                              DefQueue( *DefaultQueue )
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
			 * @param val новое значение
			 */
			void Push( const T &val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( val ) );
				PushElement( val_smart_ptr );
			} // void Push( Types ...args )

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param val новое значение
			 */
			void Push( T &&val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( std::move( val ) ) );
				PushElement( val_smart_ptr );
			} // void Push( Types ...args )

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param args аргументы для создания нового элемента
			 */
			template <typename ...Types>
			void Push( Types ...args )
			{
				std::unique_ptr<T> val_smart_ptr( new T( args... ) );
				PushElement( val_smart_ptr );
			} // void Push( Types ...args )

			/**
			 * @brief Pop извлечение элемента из головы очереди
			 * если очередь пуста - будет возвращён нулевой указатель
			 * @return элемент (в виде указателя) из головы очереди
			 */
			std::unique_ptr<T> Pop()
			{
				std::unique_ptr<T> res;
				auto epoch_keeper = DefQueue.EpochAcquire();
				ElementType *old_head = Head.load();

				while( !res )
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
						res.reset( old_head->Value.load() );
						MY_ASSERT( res );

						// Отпускаем эпоху и удаляем (через очередь) старую "голову
						epoch_keeper.Release();
						DefQueue.Delete( old_head );
					}
				}

				// Отпускаем эпоху, удаляем элементы очереди, которые можно
				epoch_keeper.Release();
				DefQueue.Clear();
				return res;
			} // std::unique_ptr<T> Pop()
	}; // class Queue
} // namespace LockFree
