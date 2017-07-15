#pragma once

#include "Coro.hpp"
#include "Utils.hpp"

#include <deque>
#include <mutex>
#include <vector>
#include <thread>
#include <memory>
#include "LockFree.hpp"

namespace Bicycle
{
	namespace ErrorCodes
	{
		/// Операция выполнена не внутри сопрограммы сервиса
		const err_code_t NotInsideSrvCoro = 0xFFFFFFF0;

		/// Дескриптор/сокет уже открыт
		const err_code_t AlreadyOpen = 0xFFFFFFF1;

		/// Дескриптор не был открыт
		const err_code_t NotOpen = 0xFFFFFFF2;

		/// Дескриптор был закрыт, операции ввода-вывода отменены
		const err_code_t WasClosed = 0xFFFFFFF3;

		/// Сервис закрыт, либо в процессе закрытия
		const err_code_t SrvStop = 0xFFFFFFF4;

		/// Операция отменена
#ifdef _WIN32
		const err_code_t OperationAborted = ERROR_OPERATION_ABORTED;
#else
		const err_code_t OperationAborted = 0xFFFFFFF5;
#endif

		/// Операция выполняется внутри сопрограммы сервиса
		const err_code_t InsideSrvCoro = 0xFFFFFFF6;
	} // namespace ErrorCodes

	namespace CoroService
	{
		using namespace Coro;

		class AbstractCloser;
		typedef std::pair<AbstractCloser*, SpinLock> PtrWithLocker;
		typedef std::shared_ptr<PtrWithLocker> BaseDescPtr;
		typedef std::weak_ptr<PtrWithLocker> BaseDescWeakPtr;

		/// Класс сервиса (очереди) сопрограмм
		class Service
		{
			friend Error Go( std::function<void()> task, size_t stack_sz );
			friend void YieldCoro();
			friend class AbstractCloser;
			friend class ServiceWorker;
			friend class BasicDescriptor;

			private:
				/// Флаг, предотвращающий повторный запуск сервиса
				std::atomic_flag RunFlag;

				/// Показывает, что сервис должен быть остановлен (должен обрабатываться в сопрограммах)
				std::atomic<bool> MustBeStopped;

				/// Количество сопрограмм, не считая основных сопрограмм потоков
				std::atomic<uint64_t> CoroCount;

				/// Количество потоков, выполняющих Execute
				std::atomic<uint64_t> WorkThreadsCount;

				/// Список указателей на дескрипторы, использующие данный сервис
				LockFree::ForwardList<BaseDescWeakPtr> Descriptors;

				/// Счётчик вызовов OnDescriptorRemove (чтобы запускать очистку Descriptors от пустых указателей не каждый раз)
				std::atomic<uint8_t> DescriptorsDeleteCount;

#ifdef _WIN32
				/// Дескриптор порта завершения ввода-вывода
				HANDLE Iocp;
#else
				/// Дескрипторы epoll (основной, на чтение, на запись, на чтение внеполосных данных)
				int EpollFds[ 4 ];

				/// Анонимный канал, используемый для добавления в очередь готовых к исполнению задач
				int PostPipe[ 2 ];

				/// Очередь на отложенное удаление
				LockFree::DeferredDeleter DeleteQueue;
#endif

				/// Задачи, готовые к исполнению
				std::deque<std::function<void()>> PostedTasks;

				/// Объект для синхронизации доступа к PostedTasks
				SpinLock TasksMutex;

				/// Закрывает все зарегистрированные дескрипторы и удаляет их из очереди
				void CloseAllDescriptors();

				/// Обработка удаления дескриптора (удаление пустых указателей из Descriptors)
				void OnDescriptorRemove();

				/// Платформозависимая инициализация сервиса
				void Initialize();

				/// Закрытие сервиса
				void Close();

				/// Добавление задачи в очередь Execute
				void Post( const std::function<void()> &task );

				/// Цикл ожидания готовности сопрограмм и их выполнения
				void Execute();

				/// Выполнение (в основной сопрограмме) задач, "оатсвленных" дочерней, из которой перешли
				void ExecLeftTasks();

				/**
				 * @brief Go Создание сопрограммы внутри сервиса сопрограмм
				 * @param task исполняемая задача
				 * @param stack_sz размер стека новой сопрограммы
				 * @return Ошибка выполнения
				 * @throw std::invalid_argument, если task - "пустышка"
				 */
				Error Go( std::function<void()> task, size_t stack_sz );

			public:
				Service( const Service& ) = delete;
				Service& operator=( const Service& ) = delete;

				Service();
				~Service();

				/**
				 * @brief Restart перезапуск сервиса
				 * @return успешность перезапуска (false вернёт, если сервис не был остановлен)
				 */
				bool Restart();

				/**
				 * @brief Stop остановка сервиса и ожидание завершения работы сопрограмм.
				 * После вызова Stop все операции ввода-вывода в сопрограммах и команды
				 * создания новых сопрограмм будут возвращать ошибку с кодом SrvStop,
				 * все операции ввода-вывода, ожидающие готовности, вернут ошибку OperationAborted
				 * (!!! если есть незавершённые сопрограммы и ни один поток не выполняет
				 * Run, то будет зависание !!!)
				 * @return true, если сервис был остановлен текущим вызовом,
				 * иначе false (если другой поток уже вызвал Stop, либо сервис уже был остановлен)
				 */
				bool Stop();

				/**
				 * @brief Run выполнение цикла ожидания готовности и выполнения сопрограмм
				 * @throw Exception в случае какой-либо ошибки
				 */
				void Run();

				/**
				 * @brief AddCoro добавление новой сопрограммы
				 * @param task задача, которая будет запущена в новой сопрограмме
				 * (если task - "пустышка", ничего не делает)
				 * @param stack_sz размер стека новой сопрограммы
				 * @return успешность выполнения
				 */
				Error AddCoro( const std::function<void()> &task,
				               size_t stack_sz = 0 );
		};

		// Классы и функции для работы внутри сопрограмм сервиса
		// (!!! все операции с ними должны проводиться только внутри сопрограммы сервиса !!!)
		/**
		 * @brief Go Создание сопрограммы внутри сервиса сопрограмм
		 * @param task исполняемая задача
		 * @param stack_sz размер стека новой сопрограммы
		 * @return Ошибка выполнения
		 * @throw Exception, если выполняется не внутри сервиса или
		 * std::invalid_argument, если task - "пустышка"
		 */
		Error Go( std::function<void()> task, size_t stack_sz = 0 );

		/**
		 * @brief YieldCoro переход в основную сопрограмму
		 * (позже управление будет передано сопрограмме,
		 * из которой ушли)
		 * @throw Exception, если вызов не из сопрограммы сервиса
		 */
		void YieldCoro();

		//-------------------------------------------------------------------------------

#ifdef _WIN32
		/// Структура с данными, возвращаемая Iocp
		struct IocpStruct
		{
			/// Структура, необходимая для всех перекрывающих задач
			OVERLAPPED Ov;

			/// Указатель на сопрограмму, на которую надо перейти
			Coroutine *Coro;

			/// Количество байт, котрое было прочитано
			size_t IoSize;

			/// Код ошибки
			err_code_t ErrorCode;
		};
#else
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

		typedef LockFree::ForwardList<EpWaitStruct*> EpWaitList;

		/// Структура с данными одного дескриптора
		struct DescriptorStruct
		{
			/// Системный дескриптор файла
			int Fd;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению
			EpWaitList ReadQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к записи
			EpWaitList WriteQueue;

			/// Список сопрограмм, на которые нужно перейти при готовности дескриптора к чтению внеполосных данных
			EpWaitList ReadOobQueue;
			
			/// Объект синхронизации доступа к полям структуры
			SharedSpinLock Lock;
		};
#endif

#ifdef _WIN32
		typedef std::function<err_code_t( HANDLE, IocpStruct& )> IoTaskType;
#else
		typedef std::function<err_code_t( int )> IoTaskType;
#endif

		/// Класс, имеющий доступ к "потрохам" сервиса сопрограмм
		class ServiceWorker
		{
			protected:
				/// Ссылка на сервис, к которому привязан дескриптор
				Service &SrvRef;

				ServiceWorker();
				ServiceWorker( const ServiceWorker& ) = delete;
				ServiceWorker& operator=( const ServiceWorker& ) = delete;

				/**
				 * @brief PostToSrv Добавление задачи в Post сервиса
				 * @param task задача
				 * @throw std::invalid_argument, если task - "пустая задача",
				 * т.е., ( bool ) task == false
				 */
				void PostToSrv( const std::function<void()> &task );

				/// Сохранение указателя на задачу и переход в основную сопрограмму сервиса
				void SetPostTaskAndSwitchToMainCoro( std::function<void()> *task );

				/// Показывает, находится ли сервис в процессе остановки
				bool IsStopped() const;
		};

		class AbstractCloser: public ServiceWorker
		{
			private:
				/// Хранит указатель на текущий дескриптор (надо для регистрации в сервисе)
				BaseDescPtr Ptr;

			protected:
				AbstractCloser();

			public:
				virtual ~AbstractCloser();

				/**
				 * @brief Close закрытие сокета
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				virtual void Close( Error &err ) = 0;

				/**
				 * @brief Close закрытие сокета
				 * @throw Exception в случае ошибки
				 */
				void Close();
		};

		/// Базовый класс сокетов и других дескрипторов
		class BasicDescriptor : public AbstractCloser
		{
			protected:
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
#else
				/// Указатель на структуту с дескриптором и очередями
				std::shared_ptr<DescriptorStruct> DescriptorData;

				/// Объект синхронизации доступа к структуре дескриптора
				mutable SharedSpinLock DescriptorLock;

				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual int OpenNewDescriptor( Error &err ) = 0;
				
				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( int fd, Error &err );

				/// Привязка нового файлового дескриптора к epoll-у
				Error RegisterNewDescriptor( int fd );
#endif

#ifdef _WIN32
				/**
				 * @brief ExecuteIoTask выполнение асинхронной задачи ввода-вывода
				 * @param task выполняемая задача (например, WSAReadFrom)
				 * @param io_size ссылка на буфер, куда будет записано количество
				 * записанных или отправленных байт
				 * @return ошибка выполнения
				 * @throw std::invalid_argument, если task пустой
				 */
				Error ExecuteIoTask( const IoTaskType &task, size_t &io_size );
#else
				enum IoTaskTypeEnum { Read = 0, Write = 1, ReadOob = 2 };

				/**
				 * @brief ExecuteIoTask выполнение асинхронной задачи ввода-вывода
				 * @param task задача ввода-вывода (например, read или проверка наличия ошибки на сокете)
				 * @param task_type тип задачи task
				 * @return ошибка выполнения
				 * @throw std::invalid_argument, если task пустой
				 */
				Error ExecuteIoTask( const IoTaskType &task,
				                     IoTaskTypeEnum task_type );

				std::shared_ptr<DescriptorStruct> NewDescriptorStruct( int fd );
#endif

				BasicDescriptor();

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
				 * @brief Close закрытие сокета
				 * @param err ссылка на ошибку, куда будет записан результат операции
				 */
				virtual void Close( Error &err ) override final;

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
