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
#include "misc.h"
#include <sys/types.h>
#include <algorithm>
#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 512

using namespace std;

map<string,SOCKET> usersList;
map<string,vector<string>> roomsList;
fd_set readfds;
map<int,vector<string>> conferences;

string generate_users_list();
string generate_list();
DWORD WINAPI connections_accepter(LPVOID lpParam);
map<string,vector<string>>::iterator createRoomIfNotExists(string room);
void lstCommand(SOCKET socket);

void notificateConferenceUsers(int index, map<int,vector<string>>::iterator users);

_TCHAR  *optarg;
_TCHAR *__progname;
bool dflag = false;

int _tmain(int argc, _TCHAR* argv[])
{
	conferences.clear();
	string port = DEFAULT_PORT;
	
	char c;
	__progname = argv[0];

	NOTIFYICONDATA notifyicon;
	notifyicon.hWnd = GetConoleHwnd();
	lstrcpyn(notifyicon.szTip, "Stilsoft.Server", sizeof(notifyicon.szTip));
	notifyicon.hIcon = ExtractIcon(NULL, "Icon.ico", 0);
	notifyicon.uFlags = NIF_ICON | NIF_TIP;
	notifyicon.cbSize = sizeof(notifyicon);
	Shell_NotifyIcon(NIM_ADD, &notifyicon);

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

	cout << "Server started at port " << port << endl;

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
	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
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
		if (dflag) {
			//cout << "iteration" << endl;
			//cout << read_file_d.fd_count << endl;
		}
		if (usersList.size() == 0)
		{
			Sleep(1000);
			continue;
		}

		if ((sResult = select(0, &read_file_d, NULL, NULL, &timeout)) < 0) {
			cout << "Select failed with error: " << sResult << endl;
			cout << errno << endl;
			return 1;
		}
		//if (dflag) {
		//	cout << "select returned " << sResult << endl;
		//	cout << " and fd_set.size = " << read_file_d.fd_count << endl;
		//}
		if (sResult == 0)
			continue;
		for (map<string,SOCKET>::iterator it = usersList.begin(); it != usersList.end(); ++it)
		{
			if (FD_ISSET(it->second, &read_file_d)) {
				rResult = recv(it->second, recvbuf, BUFFER_LENGTH, 0);
				
				// ready for reading
				// read from SOCKET
				if (rResult <= 0)
				{
					if (dflag)
						cout << "rResult " << rResult << endl;

					if (rResult == 0)
						cout << it->first << " disconnected" << endl;
					else
						cout << it->first << " aborted connection" << endl;
					FD_CLR(it->second, &readfds);
					// уведомить остальные сокеты
					string user_nick = it->first;
					SOCKET user_socket = it->second;
					usersList.erase(it);
					closesocket(user_socket);

					for (map<string,SOCKET>::iterator it2 = usersList.begin(); it2 != usersList.end(); ++it2) {
						if (it2->first.compare(user_nick) != 0)
							lstCommand(it2->second);
					}
					// удаляем его из конференций
					for (map<int,vector<string>>::iterator it2 = conferences.begin(); it2 != conferences.end(); ++it2)
					{
						vector<string>::iterator pos;
						if ((pos = find(it2->second.begin(), it2->second.end(), user_nick)) != it2->second.end())
						{
							it2->second.erase(pos);
							if (it2->second.size() == 0)
							{
								cout << "#" << it2->first << " conference deleted" << endl;
								it2 = conferences.erase(it2);
								if (it2 == conferences.end())
									break;
							}
							else
							{
								cout << "#" << it2->first << " conference updated" << endl;
								notificateConferenceUsers(it2->first, it2);
							}
						}
					}
					break;
				}
				if (dflag)
					cout << it->first << " commands " << recvbuf << endl;

				if (strncmp(recvbuf, "send", 4) == 0)
				{
					cout << it->first << " sends message";
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
								map<string,SOCKET>::iterator it2 = usersList.find(userto);
								if (it2 != usersList.end())
								{
									sendbuf.clear();
									sendbuf += "msg ";
									sendbuf += it->first;
									sendbuf += ":";
									sendbuf += text;
									//sendbuf += "\r\n";
									send(it2->second, sendbuf.c_str(), sendbuf.length(), 0);
									break;
								}
								// иначе, поиск комнаты
								if (userto[0] == '#')
								{
									userto = userto.substr(1, string::npos);
									int index = atoi(userto.c_str());
									map<int,vector<string>>::iterator conf_it;
									// цикл поиска комнаты
									conf_it = conferences.find(index);
									if (conf_it != conferences.end())
									{
										// защита от письма в комнату, в которой не состоишь
										if (find(conf_it->second.begin(), conf_it->second.end(), it->first) != conf_it->second.end())
										{
											sendbuf.clear();
											sendbuf += "msg #";
											sendbuf += to_string(conf_it->first);
											sendbuf += "|";
											sendbuf += it->first;
											sendbuf += ":";
											sendbuf += text;
											// цикл перебора пользователей
											for (vector<string>::iterator it4 = conf_it->second.begin(); it4 != conf_it->second.end(); ++it4)
											{
												if (it->first.compare(*it4) == 0)
													continue;
												// получение сокета пользователя
												map<string,SOCKET>::iterator user = usersList.find(*it4);
												send(user->second, sendbuf.c_str(), sendbuf.length(), 0);
											}
											break;
										}
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
				} else if (strncmp(recvbuf, "conf", 4) == 0) {
					string usersString(recvbuf+5, rResult-5);
					vector<string> users;
					size_t pos = usersString.size(), start = 0;
					while ((pos = usersString.find(',', start)) != string::npos)
					{
						if (pos > start)
						{
							users.push_back(usersString.substr(start, pos - start));
						}
						start = pos + 1;
					}
					if (start != usersString.size() && pos > start)
					{
						users.push_back(usersString.substr(start, pos - start));
					}
					sort(users.begin(), users.end());
					if (find(users.begin(), users.end(), it->first) == users.end())
					{
						cout << "err:not present" << endl;
						send(it->second, "err", 3, 0);
					} 
					else if (users.size() < 3)
					{
						cout << "err:invalid command" << endl;
						send(it->second, "err", 3, 0);
					}
					else
					{
						map<int,vector<string>>::iterator pos = conferences.end();
						int index;

						for (map<int,vector<string>>::iterator it = conferences.begin(); it != conferences.end(); it++)
						{
							if (it->second == users)
							{
								pos = it;
							}
						} 

						if (conferences.size() > 0 && pos != conferences.end())
						{
							index = pos->first;
							cout << "conf id #" << index << endl;
							notificateConferenceUsers(index, pos);
						}
						else
						{
							index = conferences.size();
							cout << "new conference: " << usersString << endl;
							conferences.insert(pair<int,vector<string>>(index, users));
							cout << "id #" << index << endl;
							pos = conferences.find(index);
							notificateConferenceUsers(index, pos);
						}
					}
				} else if (strncmp(recvbuf, "list", 4) == 0) {
					lstCommand(it->second);
				} else if (strncmp(recvbuf, "quit", 4) == 0) {
					
				}
				memset(recvbuf, 0, rResult);
			}// else if (FD_ISSET(it->first, &except_file_d)) {
			//	cout << it->second << " in exceptfds" << endl;
			//}
		}
	}
	Shell_NotifyIcon(NIM_DELETE, &notifyicon);
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
		if (dflag)
			cout << "new incoming socket" << endl;
		
		if ((iResult = recv(clientSocket, recvbuf, BUFFER_LENGTH, 0)) == -1) {
			cout << "error " << WSAGetLastError();
			return WSAGetLastError();
		}
		userNick = string(recvbuf, iResult);
		cout << "user nick: " << userNick << endl;

		// проверка занятости ника

		if (usersList.find(userNick) != usersList.end()) {
			cout << "err:already logged" << endl;
			send(clientSocket, "err", 3, 0);
			Sleep(1500);
			closesocket(clientSocket);
			continue;
		}

		ioctlsocket(clientSocket, FIONBIO, (unsigned long*)&ulMode);
		usersList.insert(pair<string,SOCKET>(userNick, clientSocket));
		FD_SET(clientSocket, &readfds);

		// уведомить остальные сокеты
		for (map<string,SOCKET>::iterator it = usersList.begin(); it != usersList.end(); ++it) {
			lstCommand(it->second);
		}
		/*// уведомить сокет о комнатах
		if (roomsList.size() > 0)
		{
			sendbuf.clear();
			sendbuf += "lst ";
			sendbuf += generate_rooms_list();
			sendbuf += "\r\n";
			send(clientSocket, sendbuf.c_str(), sendbuf.length(), 0);
		}*/
	}
	closesocket(listenSocket);
	WSACleanup();
	return 0;
}

string generate_users_list() 
{
	string buf;
	for (map<string,SOCKET>::iterator it = usersList.begin(); it != usersList.end(); ++it) {
		// не первый элемент
		if (buf.size() > 0)
		{
			buf += ",";
		}
		buf += it->first;
	}
	return buf;
}

void notificateConferenceUsers(int index, map<int,vector<string>>::iterator conference)
{
	string sendbuf;
	// уведомить пользователей конференции о конференции
	sendbuf += "cnf #";
	sendbuf += to_string(index);
	sendbuf += " ";
	for (vector<string>::iterator it = conference->second.begin(); it != conference->second.end(); ++it)
	{
		if (it > conference->second.begin())
		{
			sendbuf += ";";
		}
		sendbuf += *it;
	}
	//sendbuf += "\r\n";
	for (vector<string>::iterator it = conference->second.begin(); it != conference->second.end(); ++it)
	{
		send((usersList.find(*it))->second, sendbuf.c_str(), sendbuf.size(), 0);
	}
}

void lstCommand(SOCKET socket)
{
	string sendbuf;
	sendbuf.clear();
	sendbuf += "lst ";
	sendbuf += generate_users_list();
	sendbuf += "\r\n";
	send(socket, sendbuf.c_str(), sendbuf.length(), 0);
}