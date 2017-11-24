#pragma once
#include "Service.hpp"

namespace Bicycle
{
	namespace CoroService
	{
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
				typedef std::unique_ptr<DescriptorStruct, std::function<void( DescriptorStruct* )>> desc_ptr_t;

				/// Указатель на структуту с дескриптором и очередями
				const desc_ptr_t DescriptorData;

				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual int OpenNewDescriptor( Error &err ) = 0;

				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( int fd, Error &err );

				/// Привязка нового файлового дескриптора к epoll-у
				Error InitAndRegisterNewDescriptor( int fd, const desc_ptr_t &desc_data_ptr );

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
