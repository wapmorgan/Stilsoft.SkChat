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
#include <algorithm>
#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 512

using namespace std;

//vector<string> usersList;
map<SOCKET,string> socketsList;
map<string,vector<string>> roomsList;
fd_set readfds;
map<int,vector<string>> conferences;

string generate_users_list();
string generate_rooms_list();
string generate_list();
DWORD WINAPI connections_accepter(LPVOID lpParam);
map<SOCKET,string>::iterator searchInUsers(string username);
map<string,vector<string>>::iterator createRoomIfNotExists(string room);
map<int,vector<string>>::iterator findInConferences(map<int,vector<string>> conferences, vector<string> users);
void lstCommand(SOCKET socket);

void notificateConferenceUsers(int index, map<int,vector<string>>::iterator users);
SOCKET getSocketByUser(string user);

_TCHAR  *optarg;
_TCHAR *__progname;
bool dflag = false;

int _tmain(int argc, _TCHAR* argv[])
{
	conferences.clear();
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
		if (dflag) {
			cout << "select returned " << sResult << endl;
			cout << " and fd_set.size = " << read_file_d.fd_count << endl;
		}
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
					if (dflag)
						cout << "rResult " << rResult << endl;

					if (rResult == 0)
						cout << it->second << " disconnected" << endl;
					else
						cout << it->second << " aborted connection" << endl;
					FD_CLR(it->first, &readfds);
					// уведомить остальные сокеты
					string user_nick = it->second;
					SOCKET user_socket = it->first;
					socketsList.erase(it);
					closesocket(user_socket);

					for (map<SOCKET,string>::iterator it2 = socketsList.begin(); it2 != socketsList.end(); ++it2) {
						if (it2->second.compare(user_nick) != 0)
							lstCommand(it2->first);
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
								map<SOCKET,string>::iterator it2;
								for (it2 = socketsList.begin(); it2 != socketsList.end(); ++it2) {
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
								// иначе, поиск комнаты
								if (it2 == socketsList.end() && userto[0] == '#')
								{
									userto = userto.substr(1, string::npos);
									int index = atoi(userto.c_str());
									map<int,vector<string>>::iterator it3;
									// цикл поиска комнаты

									for (it3 = conferences.begin(); it3 != conferences.end(); ++it3)
									{
										if (it3->first == index)
										{
											// защита от письма в комнату, в которой не состоишь
											if (find(it3->second.begin(), it3->second.end(), it->second) != it3->second.end())
											{
												// цикл перебора пользователей
												for (vector<string>::iterator it4 = it3->second.begin(); it4 != it3->second.end(); ++it4)
												{
													if (it->second.compare(*it4) == 0)
														continue;
													// получение сокета пользователя
													map<SOCKET,string>::iterator user = searchInUsers(*it4);
													sendbuf.clear();
													sendbuf += "msg #";
													sendbuf += to_string(it3->first);
													sendbuf += "|";
													sendbuf += it->second;
													sendbuf += ":";
													sendbuf += text;
													send(user->first, sendbuf.c_str(), sendbuf.length(), 0);
												}
												break;
											}
											
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
						users.push_back(usersString.substr(start, pos - start));
						start = pos + 1;
					}
					if (start != usersString.size())
					{
						users.push_back(usersString.substr(start, pos - start));
					}
					sort(users.begin(), users.end());
					if (find(users.begin(), users.end(), it->second) == users.end())
					{
						cout << "err:not present" << endl;
						send(it->first, "err", 3, 0);
					} 
					else if (users.size() < 3)
					{
						cout << "err:invalid command" << endl;
						send(it->first, "err", 3, 0);
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
					//cout << it->second << " joins room ";
					/*char ns[5];
					int n = 0;
					string room;
					memset(ns, 0, 5);
					if (strlen(recvbuf) >= 7)
					{
						strncpy_s(ns, 5, recvbuf+5, 2);
						n = atoi(ns);
						if (strlen(recvbuf) >= 7+n)
						{
							room = string(recvbuf, 7, n);
							cout << "#" << room << endl;
							// поиск комнаты
							map<string,vector<string>>::iterator pos;
							pos = roomsList.find(room);
							if (pos == roomsList.end())
							{
								cout << " new room " << endl;
								vector<string> vec;
								vec.push_back(it->second);
								roomsList.insert(pair<string,vector<string>>(room, vec));
								// уведомить остальные сокеты
								sendbuf.clear();
								sendbuf += "lst ";
								sendbuf += generate_rooms_list();
								sendbuf += "\r\n";
								for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it) {
									send(it->first, sendbuf.c_str(), sendbuf.length(), 0);
								}
							}
							else if (find(pos->second.begin(), pos->second.end(), it->second) == pos->second.end())
							{
								pos->second.push_back(it->second);
								notificateRoomUsers(room);
							}
						}
						else
						{
							cout << "invalid room command" << endl;
						}
					}
					else
					{
						cout << "invalid room command" << endl;
					}
					*/
				/* } else if (strncmp(recvbuf, "quit", 4) == 0) {
					cout << it->second << " leaves room ";
					char ns[5];
					int n = 0;
					string room;
					memset(ns, 0, 5);
					if (strlen(recvbuf) >= 7)
					{
						strncpy_s(ns, 5, recvbuf+5, 2);
						n = atoi(ns);
						if (strlen(recvbuf) >= 7+n)
						{
							room = string(recvbuf, 7, n);
							cout << "#" << room << endl;
							map<string,vector<string>>::iterator pos = roomsList.find(room);
							if (pos != roomsList.end())
							{
								vector<string>::iterator pos2 = find(pos->second.begin(), pos->second.end(), it->second);
								if (pos2 != pos->second.end())
								{
									pos->second.erase(pos2);
									if (pos->second.size() == 0)
									{
										roomsList.erase(pos);
										// уведомить сокет о комнатах
										sendbuf.clear();
										sendbuf += "lst ";
										sendbuf += generate_rooms_list();
										sendbuf += "\r\n";
										for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it) {
											send(it->first, sendbuf.c_str(), sendbuf.length(), 0);
										}
									}
									else
									{
										notificateRoomUsers(pos->first);
									}
								}
							}
						}
						else
						{
							cout << "invalid quit command" << endl;
						}
					}
					else
					{
						cout << "invalid quit command" << endl;
					}
				}*/// else if (strncmp(recvbuf, "file", 4) == 0) {
					// создаем отдельный серверный сокет
				//}
				} else if (strncmp(recvbuf, "list", 4) == 0) {
					lstCommand(it->first);
				} else if (strncmp(recvbuf, "quit", 4) == 0) {
					
				}
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
		if (dflag)
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
		// уведомить сокет о комнатах
		if (roomsList.size() > 0)
		{
			sendbuf.clear();
			sendbuf += "lst ";
			sendbuf += generate_rooms_list();
			sendbuf += "\r\n";
			send(clientSocket, sendbuf.c_str(), sendbuf.length(), 0);
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
		if (buf.size() > 0)
		{
			buf += ",";
		}
		buf += it->second;
	}
	return buf;
}

string generate_rooms_list()
{
	string buf;
	for (map<string,vector<string>>::iterator it = roomsList.begin(); it != roomsList.end(); ++it) {
		// не первый элемент
		if (it != roomsList.begin())
		{
			buf += ",";
		}
		buf += "#";
		buf += it->first;
	}
	return buf;
}

string generate_list()
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
	for (map<string,vector<string>>::iterator it = roomsList.begin(); it != roomsList.end(); ++it) {
		if (buf.size() > 0)
		{
			buf += ",";
		}
		buf += "#";
		buf += it->first;
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

void notificateConferenceUsers(int index, map<int,vector<string>>::iterator users)
{
	string sendbuf;
	// уведомить пользователей конференции о конференции
	sendbuf += "cnf #";
	sendbuf += to_string(index);
	sendbuf += " ";
	for (vector<string>::iterator it = users->second.begin(); it != users->second.end(); ++it)
	{
		if (it > users->second.begin())
		{
			sendbuf += ";";
		}
		sendbuf += *it;
	}
	sendbuf += "\r\n";
	for (vector<string>::iterator it = users->second.begin(); it != users->second.end(); ++it)
	{
		send(getSocketByUser(*it), sendbuf.c_str(), sendbuf.size(), 0);
	}
}

SOCKET getSocketByUser(string user)
{
	for (map<SOCKET,string>::iterator it = socketsList.begin(); it != socketsList.end(); ++it)
	{
		if (it->second.compare(user) == 0)
		{
			return it->first;
		}
	}
	return INVALID_SOCKET;
}

void dumpVector(vector<string> vec)
{
	for (vector<string>::iterator it = vec.begin(); it != vec.end(); ++it)
	{
		if (it > vec.begin())
			cout << ",";
		cout << *it << endl;
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