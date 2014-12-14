// Stilsoft.Serv.cpp: определяет точку входа для консольного приложения.
//
#include "stdafx.h"
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <vector>
#include <map>
#include "getopt.h"
	
#include <sys/types.h>
#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 512

using namespace std;

//vector<string> usersList;
map<SOCKET,string> socketsList;
map<string,string> fileTransmitters;
fd_set readfds;

string generate_users_list();
DWORD WINAPI connections_accepter(LPVOID lpParam);
map<SOCKET,string>::iterator searchInUsers(string username);

_TCHAR  *optarg;
_TCHAR *__progname;
bool dflag = false;

int _tmain(int argc, _TCHAR* argv[])
{
	string port = DEFAULT_PORT;
	
	char c;
	__progname = argv[0];

	while ((c = getopt(argc, argv, "p:d")) != -1)
	{
		switch (c)
		{
		case 'p':
			cout << optarg << endl;
			port = optarg;
			break;
		case 'd':
			cout << "Debugging enabled" << endl;
			dflag = true;
			break;
		default:
			//abort();
			break;
		}
	}
	
	HANDLE thread;
	DWORD idthread;

	int iResult;
	// инициализация библиотеки
	WSAData d;
	iResult = WSAStartup( MAKEWORD(2, 2), &d);
	if(iResult != 0) {
		std::cout << "Error at WSAStartup: " << iResult;
		return 1;
	}

	// подготовка данных
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof (hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	iResult = getaddrinfo(NULL, port.c_str(), &hints, &result);
	if (iResult != 0) 
	{
		std::cout << "Ошибка getaddrinfo: " << iResult;
		WSACleanup();
		return 1;
	}
	cout << "started at port " << port << endl;

	// создание сокета
	SOCKET listenSocket = INVALID_SOCKET;
	listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "Error at socket(): " << WSAGetLastError();
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	// создание сокета
	iResult = bind(listenSocket, result->ai_addr, result->ai_addrlen);
	if(iResult == SOCKET_ERROR)
	{
		std::cout << "Bind failed with error: " << WSAGetLastError(); 
		freeaddrinfo(result);
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}
 
	freeaddrinfo(result);

	// прослушивание подключений
	if ( listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cout << "Listen failed with error: " << WSAGetLastError();
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	FD_ZERO(&readfds);

	if (NULL == (thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)connections_accepter, (LPVOID)listenSocket, 0, &idthread))) {
		std::cout << "New thread creation failed: " << GetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return GetLastError();
	}

	
	int sResult;
	int rResult = 0;

	char recvbuf[BUFFER_LENGTH];
	memset(recvbuf, 0, BUFFER_LENGTH);

	timeval timeout = {5};
	
	fd_set read_file_d;
	fd_set except_file_d;
	
	string sendbuf, userto, text;

	while (1) {
		memcpy(&read_file_d, &readfds, sizeof(readfds));
		memcpy(&except_file_d, &readfds, sizeof(readfds));
		cout << "iteration" << endl;
		cout << read_file_d.fd_count << endl;
		if (socketsList.size() == 0)
		{
			Sleep(1000);
			continue;
		}

		if ((sResult = select(0, &read_file_d, NULL, NULL, &timeout)) < 0) {
			cout << "Select failed with error: " << sResult << endl;
			cout << errno << endl;
			return 1;
		}
		cout << "select returned " << sResult << endl;
		cout << " and fd_set.size = " << read_file_d.fd_count << endl;
		if (sResult == 0)
			continue;
		for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it)
		{
			if (FD_ISSET(it->first, &read_file_d)) {
				rResult = recv(it->first, recvbuf, BUFFER_LENGTH, 0);
				
				// ready for reading
				// read from SOCKET
				if (rResult <= 0)
				{
					cout << "rResult " << rResult << endl;
					if (rResult == 0)
						cout << it->second << " disconnected" << endl;
					else
						cout << it->second << " aborted connection" << endl;
		
					FD_CLR(it->first, &readfds);
					socketsList.erase(it);
					closesocket(it->first);

					// уведомить остальные сокеты
					sendbuf.clear();
					sendbuf += "lst ";
					sendbuf += generate_users_list();
					sendbuf += "\r\n";
					for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it) {
						send(it->first, sendbuf.c_str(), sendbuf.length(), 0);
					}
					break;
				}
				cout << it->second << " commands " << recvbuf << endl;
				if (strncmp(recvbuf, "send", 4) == 0)
				{
					cout << it->second << " sends message";
					char ns[5];
					int n = 0;
					int n2 = 0;
					memset(ns, 0, 5);
					if (strlen(recvbuf) >= 7)
					{
						strncpy_s(ns, 5, recvbuf+5, 2);
						n = atoi(ns);
						if (strlen(recvbuf) >= 7+n)
						{
							userto = string(recvbuf, 7, n);
							cout << " to " << userto;
							memset(ns, 0, 5);
							if (strlen(recvbuf) >= 10+n)
							{
								strncpy_s(ns, 5, recvbuf+7+n, 3);
								n2 = atoi(ns);
								cout << " of " << n2 << " byte(s)" << endl;
								text = string(recvbuf, 7+n+3, n2);
								cout << "text: " << text << endl;

								// поиск пользователя
								for (map<SOCKET,string>::iterator it2 = socketsList.begin(); it2 != socketsList.end(); ++it2) {
									if (it2->second.compare(userto) == 0)
									{
										cout << "it2->second " << it2->second << endl;
											sendbuf.clear();
											sendbuf += "msg ";
											sendbuf += it->second;
											sendbuf += ":";
											sendbuf += text;
											//sendbuf += "\r\n";
											send(it2->first, sendbuf.c_str(), sendbuf.length(), 0);
										break;
									}
								}
							}
							else
							{
								cout << "invalid message" << endl;
							}
						}
						else
						{
							cout << "invalid message" << endl;
						}
					}
					else
					{
						cout << "invalid message" << endl;
					}
				}// else if (strncmp(recvbuf, "file", 4) == 0) {
					// создаем отдельный серверный сокет
				//}
				memset(recvbuf, 0, rResult);
			}// else if (FD_ISSET(it->first, &except_file_d)) {
			//	cout << it->second << " in exceptfds" << endl;
			//}
		}
	}
}

DWORD WINAPI connections_accepter(LPVOID lpParam) {
	SOCKET listenSocket = (SOCKET)lpParam;
	SOCKET clientSocket = SOCKET_ERROR;

	// принятие запросов
	
	string sendbuf;
	int iResult;
	char recvbuf[BUFFER_LENGTH];
	string userNick;

	unsigned long ulMode = 1;
	
	while (1) {
		clientSocket = accept(listenSocket, NULL, NULL);
		// создание отдельного потока и обработка запроса в нем
		cout << "new incoming socket" << endl;
		
		if ((iResult = recv(clientSocket, recvbuf, BUFFER_LENGTH, 0)) == -1) {
			cout << "error " << WSAGetLastError();
			return WSAGetLastError();
		}
		userNick = string(recvbuf, iResult);
		cout << "user nick: " << userNick << endl;

		// проверка занятости ника

		//if (usersList.find(userNick) != usersList.end()) {
		//if (find(usersList.begin(), usersList.end(), userNick) != usersList.end()) {
		if (searchInUsers(userNick) != socketsList.end()) {
			cout << "err:already logged" << endl;
			send(clientSocket, "err", 3, 0);
			Sleep(1500);
			closesocket(clientSocket);
			continue;
		}

		ioctlsocket(clientSocket, FIONBIO, (unsigned long*)&ulMode);
		socketsList.insert(pair<SOCKET,string>(clientSocket, userNick));
		FD_SET(clientSocket, &readfds);

		// уведомить остальные сокеты
		sendbuf.clear();
		sendbuf += "lst ";
		sendbuf += generate_users_list();
		sendbuf += "\r\n";
		for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it) {
			send(it->first, sendbuf.c_str(), sendbuf.length(), 0);
		}
		
	}
	closesocket(listenSocket);
	WSACleanup();
	return 0;
}
/*
DWORD WINAPI second(LPVOID lpParam) {
	SOCKET ClientSocket = (SOCKET)lpParam;
	// обмен данными
	
	int iSendResult;
	int iResult;
	string userNick;

	if ((iResult = recv(ClientSocket, recvbuf, BUFFER_LENGTH, 0)) == -1)
	{
		cout << "error " << WSAGetLastError();
		return WSAGetLastError();
	}
	userNick = string(recvbuf, iResult);
	cout << "user nick: " << userNick << endl;
	

	// проверка занятости ника
	if (usersList.find(userNick) != usersList.end()) {
		cout << "err:already logged" << endl;
		send(ClientSocket, "err:logged\r\n", 12, 0);
		Sleep(1500);
		closesocket(ClientSocket);
		return -1;
	}

	// добавляем ник в список
	//vector<message> messages;
	//usersList.insert(pair<string,vector<message>>(userNick, messages));
	iSendResult = send(ClientSocket, "inf:ok\r\n", 8, 0);
	string buff;
	string userto, text;
	
	// устанавливаем бит обновления для остальных клиентов
	map<string,vector<message>>::iterator uit;
	
	// устанавливаем бит обновления для остальных клиентов
	for (uit = usersList.begin(); uit != usersList.end(); ++uit) {
		if (updateFlags.find(uit->first) == updateFlags.end())
			updateFlags.insert(pair<string,bool>(uit->first, true));
	}

	while (1) {
		// обработка команд
		memset(recvbuf, 0, iResult);
		iResult = recv(ClientSocket, recvbuf, 4, 0);
		if (dflag)
		{
			cout << "command[" << iResult << "]" << string(recvbuf, iResult) << endl;
		}

		if (iResult == 0)
		{
			cout << GetCurrentProcessId() << ": " << userNick << " wrongly disconnected" << endl;
			break;
		}
		else if (iResult == SOCKET_ERROR)
		{
			cout << GetCurrentProcessId() << ": "<< userNick << " has bad network connection" << endl;
			break;
		}

		// команда получения списка пользователей
		if (strncmp(recvbuf, "list", 4) == 0) {
			cout << GetCurrentProcessId() << ": "<< userNick << " asks for list" << endl;
			sendbuf.clear();
			sendbuf += "lst ";
			for (map<string,vector<message>>::iterator it = usersList.begin(); it != usersList.end(); ++it) {
				// не первый элемент
				if (it != usersList.begin())
				{
					sendbuf += ",";
				}
				sendbuf += it->first;
			}
			sendbuf += "\r\n";
			if (dflag)
			{
				cout << "list: " << sendbuf << endl;
			}
			send(ClientSocket, sendbuf.c_str(), sendbuf.length(), 0);
		} 
		// команда отправки сообщения пользователю
		else if (strncmp(recvbuf, "send", 4) == 0) {
			cout << GetCurrentProcessId() << ": "<< userNick << " sends message";
			char ns[5];
			memset(ns, 0, 5);
			int n = 0;
			// number of bytes of nick name (2 digits: up to 99)
			recv(ClientSocket, ns, 2, 0);
			n = atoi(ns);
			// read n bytes of nick name
			memset(recvbuf, 0, 4);
			recv(ClientSocket, recvbuf, n, 0);
			userto = string(recvbuf, n);
			cout << " to " << userto;
			// number of bytes of text (3 digits: up to 999)
			recv(ClientSocket, ns, 3, 0);
			memset(recvbuf, 0, n);
			n = atoi(ns);
			cout << " of " << n << " byte(s)" << endl;
			// read n bytes of text
			recv(ClientSocket, recvbuf, n, 0);
			text = string(recvbuf, n);
			cout << "text: " << text << endl;

			int sent = 0;
			
			// поиск пользователя
			for (map<string,vector<message>>::iterator it = usersList.begin(); it != usersList.end(); ++it) {
				if (it->first.compare(userto) == 0)
				{
					sent = 1;
					message msg;
					msg.userfrom = userNick;
					msg.text = text;
					it->second.push_back(msg);
					send(ClientSocket, "inf:ok\r\n", 8, 0);
					break;
				}
			}
			if (sent == 0)
			{
				send(ClientSocket, "err:404\r\n", 9, 0);
			}
			
		} 
		else if (strncmp(recvbuf, "refr", 4) == 0) {
			cout << GetCurrentProcessId() << ": "<< userNick << " refreshes" << endl;
			// новые сообщения
			map<string,bool>::iterator flag_it = updateFlags.find(userNick);
			if (flag_it != updateFlags.end()) {
				// шлем команду обновления списка пользователей
				send(ClientSocket, "inf:newlist\r\n", 13, 0);
				updateFlags.erase(flag_it);
			} else {
				map<string,vector<message>>::iterator it;
				it = usersList.find(userNick);
				if (it->second.size() > 0)
				{
					string sendbuf = generate_users_list();
					for (vector<message>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
						
					}
					it->second.clear();
				}
				else
				{
					send(ClientSocket, "inf:empty\r\n", 11, 0);
				}
			}
		}

		

		Sleep(1000);
	}

	// удаляем сохраненые данные
	uit = usersList.find(userNick);
	usersList.erase(uit);
	closesocket(ClientSocket);
	// устанавливаем бит обновления для остальных клиентов
	for (uit = usersList.begin(); uit != usersList.end(); ++uit) {
		updateFlags.insert(pair<string,bool>(uit->first, true));
	}
	return 0;
}
*/
string generate_users_list() 
{
	string buf;
	for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it) {
		// не первый элемент
		if (it != socketsList.begin())
		{
			buf += ",";
		}
		buf += it->second;
	}
	return buf;
}

map<SOCKET,string>::iterator searchInUsers(string username)
{
	map<SOCKET,string>::iterator iRet = socketsList.end();
    for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it)
    {
        if (it->second == username)
        {
            iRet = it;
            break;
        }
    }
    return iRet;
}

