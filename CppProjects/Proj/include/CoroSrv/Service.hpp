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

		/// Тип списка указателей на структуры сопрограмм
		typedef LockFree::ForwardList<EpWaitStruct*> EpWaitList;

		/// Список указателей на структуры сопрограмм + флаг срабатываний epoll-а
		typedef std::pair<EpWaitList, std::atomic_flag> EpWaitListWithFlag;
#endif
		
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
				std::atomic<uint64_t> DescriptorsDeleteCount;

				/// Флаг, показывающий необходимость очистки пустых указателей из Descriptors
				std::atomic<bool> NeedToClearDescriptors;

#ifdef _WIN32
				/// Дескриптор порта завершения ввода-вывода
				HANDLE Iocp;
#else
				/// Дескриптор epoll
				int EpollFd;

				/// Анонимный канал, используемый для добавления в очередь готовых к исполнению задач
				int PostPipe[ 2 ];

				/// Очередь на отложенное удаление
				LockFree::DeferredDeleter DeleteQueue;

				/// Сопрограммы, готовые к исполнению
				LockFree::ForwardList<Coroutine*> CoroutinesToExecute[ 8 ];

				/// Счётчик срабатываний Post-а
				std::atomic<uint8_t> CoroListNum;
				
				/// Обработка сопрограмм, добавленных через Post
				void WorkPosted();
				
				/**
				 * @brief WorkEpoll обработка готовности дескриптора
				 * @param coros_list набор ожидающих сопрограмм
				 * @param evs_mask маска событий epoll
				 */
				void WorkEpoll( EpWaitListWithFlag &coros_list, uint32_t evs_mask );
#endif

				/// Закрывает все зарегистрированные дескрипторы и удаляет их из очереди
				void CloseAllDescriptors();

				/// Обработка удаления дескриптора
				void OnDescriptorRemove();

				/// Удаление указателей на закрытые дескрипторы
				void RemoveClosedDescriptors();

				/// Платформозависимая инициализация сервиса
				void Initialize();

				/// Закрытие сервиса
				void Close();

				/// Добавление готовой сопрограммы в очередь на исполнение
				void Post( Coroutine *coro_ptr );

				/// Цикл ожидания готовности сопрограмм и их выполнения
				void Execute();

				/// Выполнение (в основной сопрограмме) задач, "оставленных" дочерней, из которой перешли
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

#ifndef _WIN32
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
				 * @brief PostToSrv Добавление сопрограммы в Post сервиса
				 * @param coro_ref ссылка на сопрограмму
				 */
				void PostToSrv( Coroutine &coro_ref );

				typedef std::function<void( Coroutine& )> poster_t;

				/// Формирует функтор (не зависящий от времени жизни объекта ServiceWorker), выполняющий PostToSrv
				poster_t GetPoster() const;

				/// Сохранение указателя на задачу и переход в основную сопрограмму сервиса
				void SetPostTaskAndSwitchToMainCoro( std::function<void()> &&task );

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
	} // namespace CoroService
} // namespace Bicycle
