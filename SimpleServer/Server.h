#ifndef SERVER_H
#define SERVER_H

#include <stdexcept>
#include <vector>
#include <set>
#include <string.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <mutex>

using std::string;

#ifndef _DEBUG
#error "запилить асинхронный эхо-сервер С ВОЗМОЖНОСТЬЮ ОСТАНОВКИ И ЗАКРЫТИЯ ВСЕХ ОТКРЫТЫХ ИМ СОКЕТОВ"
#endif

struct DescriptorInfo
{
	int Descriptor;
	string DataToSend;
	bool HangedUp;

	DescriptorInfo(): Descriptor( -1 ), HangedUp( false ) {}
};

class InfoKeeper
{
	private:
		std::set<DescriptorInfo*> Data;
		std::mutex Mutex;

	public:
		InfoKeeper();
		~InfoKeeper();

		void Add( DescriptorInfo *ptr );
		void Remove( DescriptorInfo *ptr );
		std::set<DescriptorInfo*> Release();
};

class Server
{
	private:
		/// Дескриптор epoll
		int EpollDescriptor;

		/// Дескриптор сокета-приёмника
		DescriptorInfo AcceptorInfo;

		/// Дескрипторы именованного канала (на чтение и запись)
		int Pipe[ 2 ];

		/// Данные всех соединений
		InfoKeeper ConnectionsInfo;

	public:
		Server( const string &ip, uint16_t port );
		~Server();

		void Execute();
		void Close();
};

#endif // SERVER_H
