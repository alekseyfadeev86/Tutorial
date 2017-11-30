#pragma once
#include "Service.hpp"

namespace Bicycle
{
	namespace CoroService
	{
#ifdef _WIN32
		typedef std::function<err_code_t( HANDLE, IocpStruct& )> IoTaskType;
#else
		typedef std::function<err_code_t( int )> IoTaskType;
#endif
#ifndef _DEBUG
#error "убрать"
		/*
		/// Структура с данными для перехода в сопрограмму
		struct EpWaitStruct
		{
			/// Ссылка на сопрограмму, на которую надо перейти
			Coroutine &CoroRef;

			/// Параметр events от последнего epoll_event-а
			uint64_t LastEpollEvents;

			/// Задача была отменена
			bool WasCancelled;

			EpWaitStruct( Coroutine &coro_ref );
		};

		/// Тип списка указателей на структуры сопрограмм
		typedef LockFree::ForwardList<EpWaitStruct*> EpWaitList;

		/// Список указателей на структуры сопрограмм + флаг срабатываний epoll-а
		typedef std::pair<EpWaitList, std::atomic_flag> EpWaitListWithFlag;
		
		/// Структура с данными одного дескриптора
		struct DescriptorStruct
		{
			/// Системный дескриптор файла
			int Fd;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению
			EpWaitListWithFlag ReadQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к записи
			EpWaitListWithFlag WriteQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению внеполосных данных
			EpWaitListWithFlag ReadOobQueue;
			
			/// Объект синхронизации доступа к полям структуры
			SharedSpinLock Lock;
		};*/
#endif
#error "вариант: вынести Fd и Lock в сам BasicDescriptor, сделать 3 указателя на каждую очередь EpWaitStruct-ов"
		/// Значение бесконечного времени ожидания таймаута
		const uint64_t TimeoutInfinite = 0xFFFFFFFFFFFFFFFF;
		
#ifndef _WIN32
		/// Структура с данными для перехода в сопрограмму
		class EpWaitStruct
		{
			protected:
				/// Ссылка на сопрограмму, на которую надо перейти
				Coroutine &CoroRef;
	
				/// Параметр events от последнего epoll_event-а
				uint64_t LastEpollEvents;
				
				/// Задача была отменена
				bool WasCancelled;

			public:
				EpWaitStruct( Coroutine &coro_ref );
				
				/**
				 * @brief Execute выполнение обработки: запоминает события epoll
				 * @param epoll_events битовая маска событий
				 * ожидания epoll
				 * @return указатель на сопрограмму, к которой
				 * нужно перейти
				 */
				Coroutine* Execute( uint64_t epoll_events = 0 );
				
				/**
				 * @brief Cancel отмена операции
				 * @return указатель на сопрограмму, к которой
				 * нужно перейти
				 */
				Coroutine* Cancel();
				
				/// Показывает, был ли вызван Cancel
				bool IsCancelled() const;
				
				/// Возвращает битовую маску событий epoll
				uint64_t EpollEvents() const;
		};
		
		/// Расширенная структура с данными для перехода в сопрограмму
		class EpWaitStructEx: public EpWaitStruct
		{
			private:
				/// Показывает, что структура была обработана
				std::atomic<bool> WasExecuted;

			public:
				EpWaitStructEx( Coroutine &coro_ref );
				
				/**
				 * @brief Execute потокобезопасное выполнение однократной
				 * обработки: запоминает события epoll
				 * @param epoll_events битовая маска событий
				 * ожидания epoll
				 * @return если обработка выполняется впервые,
				 * возвращает указатель на сопрограмму, к которой
				 * нужно перейти, иначе - nullptr
				 */
				Coroutine* Execute( uint64_t epoll_events = 0 );
				
				/**
				 * @brief Cancel потокобезопасная отмена операции
				 * @return если обработка выполняется впервые,
				 * возвращает указатель на сопрограмму, к которой
				 * нужно перейти, иначе - nullptr
				 */
				Coroutine* Cancel();
		};
		
		enum IoTaskTypeEnum { Read = 0, Write = 1, ReadOob = 2 };

		class DescriptorEpollWorker: public AbstractEpollWorker
		{
			public:
				typedef std::pair<LockFree::ForwardList<EpWaitStruct*>, std::atomic_flag> list_t;
				typedef std::pair<LockFree::ForwardList<std::weak_ptr<EpWaitStructEx>, std::atomic_flag> list_ex_t;
				
				/**
				 * @brief Cancel отменяет все невыполненные операции
				 * @return указатели на сопрограммы, чьи операции были
				 * отменены и которые готовы к работе
				 */
				virtual coro_list_t Cancel() = 0;

				/**
				 * @brief GetListPtr возвращает указатель на список
				 * элементов для задачи нужного типа
				 * @param task_type тип задачи
				 * @return два указателя, один нулевой, другой - нет:
				 * если используется таймаут, то 1-й нулевой, иначе - 2-й
				 */
				virtual std::pair<list_t*, list_ex_t*> GetListPtr( IoTaskTypeEnum task_type ) = 0;
		};
		
#error "сделать 2 типа DescriptorStruct-а: для дескриптора с таймером (вместо указателя на EpWaitStruct придётся хранить shared_ptr с возможностью отметки о считывании) и без"
		/*
		/// Тип списка указателей на структуры сопрограмм
		typedef LockFree::ForwardList<EpWaitStruct*> EpWaitList;

		/// Список указателей на структуры сопрограмм + флаг срабатываний epoll-а
		typedef std::pair<EpWaitList, std::atomic_flag> EpWaitListWithFlag;
		/// Структура с данными одного дескриптора
		struct DescriptorStruct
		{
			/// Системный дескриптор файла
			int Fd;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению
			EpWaitListWithFlag ReadQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к записи
			EpWaitListWithFlag WriteQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению внеполосных данных
			EpWaitListWithFlag ReadOobQueue;
			
			/// Объект синхронизации доступа к полям структуры
			SharedSpinLock Lock;
		};
	*/
#endif
		
		/// Базовый класс сокетов и других дескрипторов
		class BasicDescriptor : public AbstractCloser
		{
			private:
				static std::shared_ptr<TimeTasksQueue> InitTimer( uint64_t read_timeout,
				                                                  uint64_t write_timeout );
				
			protected:
				/// Таймаут на считывание (в микросекундах)
				const uint64_t ReadTimeoutMicrosec;
				
				/// Таймаут на запись (в микросекундах)
				const uint64_t WriteTimeoutMicrosec;
				
#ifdef _WIN32
				/// Дескриптор
				HANDLE Fd;

				/// Объект синхронизации доступа к дескриптору
				mutable SharedSpinLock FdLock;

				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual HANDLE OpenNewDescriptor( Error &err ) = 0;

				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( HANDLE fd, Error &err );

				/// Привязка нового файлового дескриптора к iocp
				Error RegisterNewDescriptor( HANDLE fd );

				/**
				 * @brief ExecuteIoTask выполнение асинхронной задачи ввода-вывода
				 * @param task выполняемая задача (например, WSAReadFrom)
				 * @param io_size ссылка на буфер, куда будет записано количество
				 * записанных или отправленных байт
				 * @param is_read показывает, является ли task задачей чтения
				 * @return ошибка выполнения
				 * @throw std::invalid_argument, если task пустой
				 */
				Error ExecuteIoTask( const IoTaskType &task,
				                     size_t &io_size,
				                     bool is_read );
#else
				typedef std::function<void( DescriptorEpollWorker* )> deleter_t;
				typedef std::unique_ptr<DescriptorEpollWorker, deleter_t> desc_ptr_t;

				/// Указатель на структуту с очередями
				const desc_ptr_t Data;
				
				/// Системный дескриптор файла
				int Fd;
				
				/// Объект синхронизации доступа к Fd и полям структуры Data
				SharedSpinLock Lock;

				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual int OpenNewDescriptor( Error &err ) = 0;

				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( int fd, Error &err );

				/// Привязка нового файлового дескриптора к epoll-у
				Error InitAndRegisterNewDescriptor( int fd, const AbstractEpollWorker &worker_ptr );

				/**
				 * @brief ExecuteIoTask выполнение асинхронной задачи ввода-вывода
				 * @param task задача ввода-вывода (например, read или проверка наличия ошибки на сокете)
				 * @param task_type тип задачи task
				 * @return ошибка выполнения
				 * @throw std::invalid_argument, если task пустой
				 */
				Error ExecuteIoTask( const IoTaskType &task,
				                     IoTaskTypeEnum task_type );
				
				static desc_ptr_t InitDataPtr( uint64_t rd_timeout, uint64_t wr_timeout );
				
#endif
				
				const std::shared_ptr<TimeTasksQueue> TimerQueuePtr;

				/**
				 * @brief BasicDescriptor конструктор базового дескриптора
				 * @param read_timeout_microsec таймаут на чтение (в микросекундах)
				 * @param write_timeout_microsec таймаут на запись (в микросекундах)
				 */
				BasicDescriptor( uint64_t read_timeout_microsec = TimeoutInfinite,
				                 uint64_t write_timeout_microsec = TimeoutInfinite );

			public:
				BasicDescriptor( const BasicDescriptor& ) = delete;
				BasicDescriptor& operator=( const BasicDescriptor& ) = delete;

				virtual ~BasicDescriptor();

				/**
				 * @brief Open открытие дескриптора
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				void Open( Error &err );

				/**
				 * @brief Open открытие дескриптора
				 * @throw Exception в случае ошибки
				 */
				void Open();

				/**
				 * @brief Close закрытие дескриптора
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				virtual void Close( Error &err ) override final;

				/**
				 * @brief CLose закрытие дескриптора
				 * @throw Exception в случае ошибки
				 */
				void Close();

				/**
				 * @brief Cancel отмена всех операций, ожидающих завершения
				 * (соответствующие сопрограммы получат код ошибки OperationAborted)
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				void Cancel( Error &err );

				/**
				 * @brief Cancel отмена всех операций, ожидающих завершения
				 * (соответствующие сопрограммы получат код ошибки OperationAborted)
				 * @throw Exception в случае ошибки
				 */
				void Cancel();

				bool IsOpen() const;
		};
	} // namespace CoroService
} // namespace Bicycle
