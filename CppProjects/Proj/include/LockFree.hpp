#pragma once

#include <atomic>
#include <stdint.h>
#include <stdexcept>
#include <vector>
#include <memory>
#include <utility>

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

			template <typename ...Types>
			StructElementType( Types... args ): Value( args... ),
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
#error "TODO: проверить утечку памяти в контейнерах (добавить счётчик в StructElementType и проверять его)"
		};
#endif // #ifdef UNITTEST

		template <typename T>
		StructElementType<T>* GetBottom( StructElementType<T> *top ) noexcept
		{
#ifdef _DEBUG
			try
			{
#endif
			if( top == nullptr )
			{
				return nullptr;
			}

			StructElementType<T> *res = nullptr;
			for( ; top != nullptr;
			     res = top, top = top->Next.load() )
			{}

			MY_ASSERT( res != nullptr );
			return res;
#ifdef _DEBUG
			}
			catch( ... )
			{
				MY_ASSERT( false );
			}
#endif
		}

		/**
		 * @brief PushHead добавление новых элементов в начало однонаправленного списка
		 * @param head ссылка на головной элемент списка
		 * @param new_head новый головной элемент (должен быть не nullptr)
		 * @param add_only_one если true - добавляется только new_head,
		 * иначе - все элементы, что стоят за new_head-ом
		 * @return был ли head нулевым до его замены
		 */
		template <typename T>
		bool PushHead( std::atomic<StructElementType<T>*> &head,
		               StructElementType<T> *new_head,
		               bool add_only_one = true ) noexcept
		{
			MY_ASSERT( new_head != nullptr );
#ifdef _DEBUG
			try
			{
#endif
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
#ifdef _DEBUG
			}
			catch( ... )
			{
				MY_ASSERT( false );
			}
#endif

			MY_ASSERT( false );
			return false;
		}
		
		/**
		 * @brief TryPushHead добавление новых элементов в начало однонаправленного списка
		 * при условии, что он пуст
		 * @param head ссылка на головной элемент списка
		 * @param new_head новый головной элемент (должен быть не nullptr)
		 * @param add_only_one если true - добавляется только new_head,
		 * иначе - все элементы, что стоят за new_head-ом
		 * @return был ли новый элемент добавлен (если нет - список уже был не пуст)
		 */
		template <typename T>
		bool TryPushHead( std::atomic<StructElementType<T>*> &head,
		                  std::unique_ptr<StructElementType<T>> &new_head,
		                  bool add_only_one = true ) noexcept
		{
			MY_ASSERT( new_head );
#ifdef _DEBUG
			try
			{
#endif
			StructElementType<T> *new_bottom = add_only_one ? new_head.get() : GetBottom( new_head.get() );
			MY_ASSERT( new_bottom != nullptr );

			StructElementType<T> *old_head = nullptr;
			new_bottom->Next.store( old_head );
			if( head.compare_exchange_strong( old_head, new_head.get() ) )
			{
				// Удалось
				new_head.release();
				return true;
			}
			
			// Список был не пуст
			new_bottom->Next.store( nullptr );
#ifdef _DEBUG
			}
			catch( ... )
			{
				MY_ASSERT( false );
			}
#endif
			
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
			void Clean() noexcept
			{
#ifdef _DEBUG
				try
				{
#endif
				ElementType *top = Top;
				Top = nullptr;
				ElementType *ptr = nullptr;

				while( top != nullptr )
				{
					ptr = top;
					top = top->Next.load();
					delete ptr;
				}
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
					throw;
				}
#endif
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
					std::unique_ptr<ElementType> old_top( Top );
					try
					{
						Top = Top->Next;
						return std::move_if_noexcept( old_top->Value );
					}
					catch( ... )
					{
						MY_ASSERT( old_top->Next.load() == Top );
						Top = old_top.release();
						throw;
					}
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
			UnsafeForwardList& operator=( UnsafeForwardList &&l ) noexcept
			{
#ifdef _DEBUG
				try
				{
#endif
				if( &l != this )
				{
					Clean();
					Top = l.Top;
					l.Top = nullptr;
				}
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
					throw;
				}
#endif

				return *this;
			}

			UnsafeForwardList() noexcept: Top( nullptr ){}
			UnsafeForwardList( std::atomic<ElementType*> &&ptr ) noexcept: Top( ptr.exchange( nullptr ) ){}

			~UnsafeForwardList()
			{
				Clean();
			}

			operator bool() const noexcept
			{
				return Top != nullptr;
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
			 * @param new_val новое значение
			 */
			void Push( const T &new_val )
			{
				Push( T( new_val ) );
			}

			/**
			 * @brief Emplace Добавление элемента в начало списка
			 * @param args аргументы для создания нового значения
			 */
			template <typename ...Types>
			void Emplace( Types ...args )
			{
				ElementType *new_element = new ElementType( args... );
				new_element->Next = Top;
				Top = new_element;
			}

			/**
			 * @brief Push Добавление элементов в начало списка
			 * @param u добавляемые элементы (u будет очищен)
			 */
			void Push( UnsafeForwardList &&u ) noexcept
			{
#ifdef _DEBUG
				try
				{
#endif
				if( !u )
				{
					// Нечего добавлять
					return;
				}
				
				MY_ASSERT( u.Top != nullptr );
				if( Top != nullptr )
				{
					// Связываем последний элемент добавляемого списка
					// с первым элементом прежнего
					internal::GetBottom( u.Top )->Next.store( Top );
				}
				Top = u.Top;
				u.Top = nullptr;
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
					throw;
				}
#endif
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
			size_t Reverse() noexcept
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
			bool ReleaseTo( internal::StructElementType<T>* &storage ) noexcept
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

			ForwardList() noexcept: Top( nullptr ) {}
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
			 * @brief Emplace добавляет новый элемент
			 * @param args аргументы для создания нового значения списка
			 * @return true, если до добавления список был пуст
			 */
			template <typename ...Types>
			bool Emplace( Types ...args )
			{
				return internal::PushHead( Top, new ElementType( args... ) );
			}

			/**
			 * @brief Push добавляет новые элементы
			 * @param l добавляемый (потоконебезопасный) список
			 * @return true, если до добавления список был пуст
			 */
			bool Push( Unsafe &&l ) noexcept
			{
#ifdef _DEBUG
				try
				{
#endif
				ElementType *new_top = nullptr;
				l.ReleaseTo( new_top );
				return new_top == nullptr ? false : internal::PushHead( Top, new_top, false );
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
					throw;
				}
#endif
			} // bool Push( UnsafeForwardList &l )
			
			/**
			 * @brief TryPush добавляет новый элемент, если список пуст
			 * @param val добавляемое значение
			 * @return успешность операции
			 */
			bool TryPush( const T &val )
			{
				if( Top.load() != nullptr )
				{
					// Список не пуст
					return false;
				}
				
				std::unique_ptr<ElementType> ptr( new ElementType( val ) );
				return internal::TryPushHead( Top, ptr );
			}
			
			/**
			 * @brief TryPush добавляет новый элемент, если список пуст
			 * @param val добавляемое значение
			 * @return успешность операции
			 */
			bool TryPush( T &&val )
			{
				if( Top.load() != nullptr )
				{
					// Список не пуст
					return false;
				}
				
				std::unique_ptr<ElementType> ptr( new ElementType( std::move( val ) ) );
				return internal::TryPushHead( Top, ptr );
			}

			/**
			 * @brief TryEmplace добавляет новый элемент, если список пуст
			 * @param args аргументы для создания нового значения списка
			 * @return успешность операции
			 */
			template <typename ...Types>
			bool TryEmplace( Types ...args )
			{
				if( Top.load() != nullptr )
				{
					// Список не пуст
					return false;
				}
				
				std::unique_ptr<ElementType> ptr( new ElementType( args... ) );
				return internal::TryPushHead( Top, ptr );
			}
			
//			/**
//			 * @brief TryPush добавляет новые элементы, если список пуст
//			 * @param l добавляемый (потоконебезопасный) список
//			 * @return успешность операции
//			 */
//			bool TryPush( Unsafe &&l )
//			{
//#error "непонятно, что делать с элементами l-а в случае неудачи: удалять или возвращать обратно"
//				if( ( Top.load() != nullptr ) || !l )
//				{
//					// Список не пуст, либо добавляемый список пустой
//					return false;
//				}
				
//				ElementType *new_top = nullptr;
//				l.ReleaseTo( new_top );
//				std::unique_ptr<ElementType> ptr( new ElementType( new_top ) );
//				MY_ASSERT( ptr );
//				return internal::TryPushHead( Top, ptr, false );
//			}
			
			/// Извлечение всех элементов в потоконебезопасный список
			Unsafe Release() noexcept
			{
				return Unsafe( std::move( Top ) );
			}

			operator bool() const noexcept
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
					AbstractPtr() noexcept {}

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

					ConcretePtr( T *ptr ) noexcept: Ptr( ptr )
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

					EpochKeeper( EpochKeeper &&ep_keep ) noexcept:
					    EpochPtr( ep_keep.EpochPtr ),
					    CounterPtr( ep_keep.CounterPtr )
					{
						ep_keep.EpochPtr = nullptr;
						ep_keep.CounterPtr = nullptr;
					}

					EpochKeeper& operator=( EpochKeeper &&ep_keep ) noexcept
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

					EpochKeeper() noexcept: EpochPtr( nullptr ), CounterPtr( nullptr ) {}

					EpochKeeper( EpochType &ep_ref,
					             std::atomic<uint16_t> &count_ref ) noexcept:
					    EpochPtr( &ep_ref ), CounterPtr( &count_ref )
					{}

					~EpochKeeper()
					{
						Release();
					}

					void Release() noexcept
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
			                                            DelPeriod( del_period == 0 ? 1 : del_period ),
			                                            NeedToClean( false )
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
				PtrEpochType new_elem( PtrType( ( AbstractPtr* ) new ConcretePtr<T>( ptr ) ), CurrentEpoch++ );
				QueueToDelete.Push( std::move( new_elem ) );

				MY_ASSERT( DelPeriod > 0 );
				if( ( ++DelCount % DelPeriod ) == 0 )
				{
					NeedToClean.store( true );
				}
			} // void Delete( T *ptr )

			/// Удаление элементов очереди, которые возможно удалить
			void Clear() noexcept
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
				
				try
				{
					queue.RemoveIf( checker );
				}
				catch( ... )
				{
					MY_ASSERT( false );
				}

				// Добавляем все неудалённые элементы обратно в очередь
				QueueToDelete.Push( std::move( queue ) );
			}

			/**
			 * @brief ClearIfNeed Удаление элементов очереди, которые возможно удалить.
			 * Выполняется, если было выполнено достаточное количество вызовов Delete
			 */
			void ClearIfNeed() noexcept
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
			EpochKeeper EpochAcquire() noexcept
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
			void UpdateEpoch( EpochKeeper &keeper ) noexcept
			{
				if( keeper.EpochPtr != nullptr )
				{
					keeper.EpochPtr->store( CurrentEpoch.load() );
				}
			}
	};

	/// Максимальный рекомендованный размер данных очереди на удаление (в байтах)
	const uint16_t MaxSizeToDelete = 1024;
	
	template <typename T>
	uint16_t GetCleanPeriod()
	{
		size_t sz = sizeof( T );
		return ( uint16_t ) ( MaxSizeToDelete / ( sz == 0 ? 1 : sz ) );
	}
	
	namespace internal
	{
		// Формирование "хранителя" для извлекаемого из контейнера элемента
		template <typename T>
		std::unique_ptr<T, std::function<void( T* )>>
		MakePtrWithDeleter( DeferredDeleter &del )
		{
			auto h = [ &del ]( T *ptr )
			{
				MY_ASSERT( ptr != nullptr );
				while( true )
				{
					try
					{
						del.Delete( ptr );
						break;
					}
#ifdef _DEBUG
					catch( const std::bad_alloc& )
					{
						// По идее, других исключений быть не должно
					}
#endif
					catch( ... )
					{
						// Повторяем в надежде, что другие потоки
						// освободят эпохи, что позволит удалить
						// old_head сразу, минуя очередь
						// (в этом случае исключений 100% не будет)
						MY_ASSERT( false );
					}
				} // while( true )
			};
			return std::unique_ptr<T, std::function<void( T* )>>( nullptr, h );
		} // MakePtrWithDeleter( T *ptr, DeferredDeleter &del )
	} // namespace internal

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

			Stack( DeferredDeleter &def_queue ) noexcept:
			    Head( nullptr ),
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
			 * @brief Emplace добавление нового элемента в стек
			 * @param args аргументы для создания нового элемента
			 * @return true, если до добавления элемента стек был пуст
			 */
			template <typename ...Types>
			bool Emplace( Types ...args )
			{
				return internal::PushHead( Head, new ElementType( args... ) );
			}
			
			/**
			 * @brief TryPush добавляет новый элемент, если список пуст
			 * @param val добавляемое значение
			 * @return успешность операции
			 */
			bool TryPush( T &&val )
			{
				if( Head.load() != nullptr )
				{
					// Список не пуст
					return false;
				}
				
				std::unique_ptr<ElementType> ptr( new ElementType( std::move( val ) ) );
				return internal::TryPushHead( Head, ptr );
			}
			
			/**
			 * @brief TryPush добавляет новый элемент, если список пуст
			 * @param val добавляемое значение
			 * @return успешность операции
			 */
			bool TryPush( const T &val )
			{
				return TryPush( T( val ) );
			}

			/**
			 * @brief TryEmplace добавляет новый элемент, если список пуст
			 * @param args аргументы для создания нового значения списка
			 * @return успешность операции
			 */
			template <typename ...Types>
			bool TryEmplace( Types ...args )
			{
				if( Head.load() != nullptr )
				{
					// Список не пуст
					return false;
				}
				
				std::unique_ptr<ElementType> ptr( new ElementType( args... ) );
				return internal::TryPushHead( Head, ptr );
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
				
				auto old_head_keeper = internal::MakePtrWithDeleter<ElementType>( DefQueue );
				MY_ASSERT( !old_head_keeper );

				// Извлекаем первый элемент из головы списка,
				// помещаем в голову следующий элемент
				while( old_head != nullptr )
				{
					MY_ASSERT( !old_head_keeper );

					// Получаем указатель на элемент, следующий за головным
					auto new_head = old_head->Next.load();

					// Пытаемся изменить значение Head
					if( Head.compare_exchange_weak( old_head, new_head ) )
					{
						// Получилось
						old_head_keeper.reset( old_head );
						if( is_empty != nullptr )
						{
							*is_empty = new_head == nullptr;
						}
						break;
					}
				} // while( old_head != nullptr )

				// Отпускаем "эпоху"
				epoch_keeper.Release();

				const bool has_value = ( bool ) old_head_keeper;
				if( old_head_keeper )
				{
					// Стек не был пуст
					try
					{
						result = std::move_if_noexcept( old_head_keeper->Value );
						
						// Помещаем бывший головной элемент в очередь на удаление
						old_head_keeper.reset();
					}
					catch( ... )
					{
						// Исключение при копировании - возвращаем элемент обратно в контейнер
						internal::PushHead( Head, old_head_keeper.release() );
						MY_ASSERT( !old_head_keeper );
						throw;
					}
				}
				MY_ASSERT( !old_head_keeper );
				
				// Удаляем элементы из очереди (если надо)
				DefQueue.ClearIfNeed();
				
				if( !has_value )
				{
					// Стек пуст
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
			void CleanDeferredQueue() noexcept
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

			/**
			 * @brief DigitsQueue Конструктор очереди чисел
			 * @param fake_value фиктивное значение (не может присутствовать в очереди)
			 * @param def_deleter ссылка на очередь на отложенное удаление
			 */
			DigitsQueue( Type fake_value,
			             DeferredDeleter &def_deleter ) noexcept:
			    FakeValue( fake_value ),
			    Head( nullptr ), Tail( nullptr ),
			    DefaultQueue(),
			    DefQueue( def_deleter )
			{
				Init();
			}

			/**
			 * @brief DigitsQueue Конструктор очереди чисел
			 * @param fake_value фиктивное значение (не может присутствовать в очереди)
			 * @param threads_num максимальное количество потоков, одновременно
			 * обращающихся к очереди
			 * @param clean_period периодичность очистки очереди на отложенное
			 * удаление (через каждые clean_period удалений элементов DigitsQueue)
			 */
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
						new_elem.reset( new ElementType( FakeValue ) );
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

					// Пытаемся записать в Tail указатель на новый хвост
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
			 * @return элемент из головы очереди, либо фиктивное
			 * значение, если очередь пуста
			 */
			Type Pop() noexcept
			{
#ifdef _DEBUG
				try
				{
#endif
				Type res = FakeValue;
				auto epoch_keeper = DefQueue.EpochAcquire();
				ElementType *old_head = Head.load();
				
				auto old_head_keeper = internal::MakePtrWithDeleter<ElementType>( DefQueue );
				MY_ASSERT( !old_head_keeper );

				while( res == FakeValue )
				{
					MY_ASSERT( old_head != nullptr );
					MY_ASSERT( !old_head_keeper );
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
						old_head_keeper.reset( old_head );
						res = old_head->Value.load();
						MY_ASSERT( res != FakeValue );
					}
				} // while( res == FakeValue )

				// Отпускаем эпоху
				epoch_keeper.Release();
				
				// Помещаем бывший головной элемент в очередь на удаление
				// (если он был извлечён)
				old_head_keeper.reset();

				// Удаляем элементы очереди, которые можно
				DefQueue.ClearIfNeed();
				
				return res;
#ifdef _DEBUG
				}
				catch( ... )
				{
					MY_ASSERT( false );
					throw;
				}
#endif
			} // Type Pop()
			
			/// Очистить очередь на отложенное удаление
			void CleanDeferredQueue() noexcept
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

			Queue( DeferredDeleter &def_deleter ) noexcept: PtrsQueue( 0, def_deleter )
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
			void Push( T &&val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( std::move( val ) ) );
				PushElement( val_smart_ptr );
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
			void Emplace( T &&val )
			{
				std::unique_ptr<T> val_smart_ptr( new T( std::move( val ) ) );
				PushElement( val_smart_ptr );
			}

			/**
			 * @brief Push добавление нового элемента в хвост очереди
			 * @param args аргументы для создания нового элемента
			 */
			template <typename ...Types>
			void Emplace( Types ...args )
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
			void CleanDeferredQueue() noexcept
			{
				PtrsQueue.CleanDeferredQueue();
			}
	}; // class Queue
} // namespace LockFree
