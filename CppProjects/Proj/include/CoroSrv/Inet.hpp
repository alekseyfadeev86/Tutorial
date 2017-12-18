#pragma once

#include "BasicDescriptor.hpp"

#if defined( _WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

#include <string>

namespace Bicycle
{
	namespace CoroService
	{
		using std::string;
#error "переписать с учётом таймаута, дописать проверки"
		/// Структура адреса Ip4 для функций обмена данными по сети
		struct Ip4Addr
		{
			sockaddr_in Addr;

			Ip4Addr( const Ip4Addr& ) = default;
			Ip4Addr& operator=( const Ip4Addr& ) = default;

			Ip4Addr();

			/**
			 * @brief GetIp получение адреса ip
			 * @return адрес ip
			 * @throw Exception, если произошла ошибка
			 */
			string GetIp() const;

			/**
			 * @brief GetIp получение адреса ip
			 * @param err ссылка на объект ошибки, куда будет записан результат операции
			 * @return адрес ip
			 */
			string GetIp( Error &err ) const;

			/// Получение № порта адреса
			uint16_t GetPortNum() const;

			/**
			 * @brief SetIp установка адреса ip
			 * @param ip устанавливаемый адрес ip
			 * @throw Exception, если произошла ошибка
			 */
			void SetIp( const string &ip );

			/**
			 * @brief SetIp установка адреса ip
			 * @param ip устанавливаемый адрес ip
			 * @param err ссылка на объект ошибки, куда будет записан результат операции
			 * @throw Exception, если произошла ошибка
			 */
			void SetIp( const string &ip, Error &err );

			/// Установка № порта
			void SetPortNum( uint16_t port );
		};

		//-------------------------------------------------------------------------------

		// Классы и функции для работы внутри сопрограмм сервиса
		// (!!! все операции с ними должны проводиться только внутри сопрограммы сервиса !!!)

		/// Базовый класс для сокетов
		class BasicSocket: public BasicDescriptor
		{
			protected:
#if defined( _WIN32) || defined(_WIN64)
				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual HANDLE OpenNewDescriptor( Error &err ) override final;

				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( HANDLE fd, Error &err ) override final;

				/// Вызывает C-шную функцию socket с нужными параметрами
				virtual SOCKET CreateNewSocket() = 0;
#else
				/// Открывает и возвращает новый дескриптор, записывает ошибку в err
				virtual int OpenNewDescriptor( Error &err ) override final;

				/// Закрывает дескриптор, записывает ошибку в err
				virtual void CloseDescriptor( int fd, Error &err ) override final;

				/// Вызывает C-шную функцию socket с нужными параметрами
				virtual int CreateNewSocket() = 0;
#endif
				BasicSocket();

			public:
				virtual ~BasicSocket();

				/**
				 * @brief Bind привязка сокета к адресу
				 * @param addr адрес
				 * @throw Exception в случае ошибки
				 */
				void Bind( const Ip4Addr &addr );

				/**
				 * @brief Bind привязка сокета к адресу
				 * @param addr адрес
				 * @param err ссылка на объект ошибки, куда будет записан результат
				 */
				void Bind( const Ip4Addr &addr, Error &err );
		};

		typedef std::pair<uint8_t*, size_t> BufferType;
		typedef std::pair<const uint8_t*, size_t> ConstBufferType;

		/// Класс сокета UDP
		class UdpSocket: public BasicSocket
		{
			protected:
#if defined( _WIN32) || defined(_WIN64)
				/// Вызывает C-шную функцию socket для создания сокета UDP
				virtual SOCKET CreateNewSocket() override final;
#else
				/// Вызывает C-шную функцию socket для создания сокета UDP
				virtual int CreateNewSocket() override final;
#endif

			public:
				/**
				 * @brief SendTo отправка данных
				 * @param data данные для отправки
				 * @param addr адрес получателя
				 * @param err ошибка выполнения
				 * @return количество отправленных байт
				 */
				size_t SendTo( const ConstBufferType &data,
				               const Ip4Addr &addr,
				               Error &err );

				/**
				 * @brief SendTo отправка данных
				 * @param data данные для отправки
				 * @param addr адрес получателя
				 * @return количество отправленных байт
				 * @throw Exception в случае ошибки
				 */
				size_t SendTo( const ConstBufferType &data,
				               const Ip4Addr &addr );

				/**
				 * @brief RecvFrom получение данных
				 * @param data буфер для считываемых данных
				 * @param addr буфер для записи адреса отправителя
				 * @param err ошибка выполнения
				 * @return количество принятых байт
				 */
				size_t RecvFrom( const BufferType &data,
				                 Ip4Addr &addr,
				                 Error &err );

				/**
				 * @brief RecvFrom получение данных
				 * @param data буфер для считываемых данных
				 * @param addr буфер для записи адреса отправителя
				 * @return количество принятых байт
				 * @throw Exception в случае ошибки
				 */
				size_t RecvFrom( const BufferType &data,
				                 Ip4Addr &addr );
		};

		/// Класс сокета TCP, базового класса для соединения TCP и приёмника соединений
		class TcpSocket: public BasicSocket
		{
			protected:
#if defined( _WIN32) || defined(_WIN64)
				/// Вызывает C-шную функцию socket для создания сокета UDP
				virtual SOCKET CreateNewSocket() override final;
#else
				/// Вызывает C-шную функцию socket для создания сокета UDP
				virtual int CreateNewSocket() override final;
#endif

				TcpSocket();
		};

		/// Класс соединения TCP
		class TcpConnection: public TcpSocket
		{
			private:
				friend class TcpAcceptor;

			public:
				/**
				 * @brief Connect подключение к указанному адресу
				 * @param addr адрес, к которому происходит подключение
				 * @param err буфер для записи ошибки выполнения
				 */
				void Connect( const Ip4Addr &addr, Error &err );

				/**
				 * @brief Connect подключение к указанному адресу
				 * @param addr адрес, к которому происходит подключение
				 * @throw Exception в случае ошибки
				 */
				void Connect( const Ip4Addr &addr );

				/**
				 * @brief Send отправка данных
				 * @param data данные для отправки
				 * @param err ошибка выполнения
				 * @return количество отправленных байт
				 */
				size_t Send( const ConstBufferType &data,
				             Error &err );

				/**
				 * @brief Send отправка данных
				 * @param data данные для отправки
				 * @return количество отправленных байт
				 * @throw Exception в случае ошибки
				 */
				size_t Send( const ConstBufferType &data );

				/**
				 * @brief Recv получение данных
				 * @param data буфер для считываемых данных
				 * @param err ошибка выполнения
				 * @return количество принятых байт
				 */
				size_t Recv( const BufferType &data,
				             Error &err );

				/**
				 * @brief Recv получение данных
				 * @param data буфер для считываемых данных
				 * @return количество принятых байт
				 * @throw Exception в случае ошибки
				 */
				size_t Recv( const BufferType &data );
		};


		class TcpAcceptor: public TcpSocket
		{
			public:
				/**
				 * @brief Listen начало прослушивания входящих соединений
				 * @param максимальный размер очереди входящих соединений
				 * @param err буфер для записи ошибки выполнения
				 */
				void Listen( uint16_t backlog, Error &err );

				/**
				 * @brief Listen начало прослушивания входящих соединений
				 * @param максимальный размер очереди входящих соединений
				 * @throw Exception в случае ошибки
				 */
				void Listen( uint16_t backlog );

				/**
				 * @brief Accept приём входящего соединения
				 * @param conn буфер для нового соединения
				 * @param addr буфер для записи адреса нового подключения
				 * @param err буфер для записи ошибки выполнения
				 */
				void Accept( TcpConnection &conn, Ip4Addr &addr, Error &err );

				/**
				 * @brief Accept приём входящего соединения
				 * @param conn буфер для нового соединения
				 * @param addr буфер для записи адреса нового подключения
				 * @throw Exception в случае ошибки
				 */
				void Accept( TcpConnection &conn, Ip4Addr &addr );
		};
	} // namespace CoroService
} // namespace Bicycle
