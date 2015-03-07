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
#define BUFFER_LENGTH 1024

using namespace std;

map<string,SOCKET> usersList;
fd_set readfds;
map<int,vector<string>> conferences;

string generate_users_list();
string generate_list();
DWORD WINAPI connections_accepter(LPVOID lpParam);
DWORD WINAPI file_transmitter(LPVOID lpParam);
map<string,vector<string>>::iterator createRoomIfNotExists(string room);
void lstCommand(SOCKET socket);
void sendMsg(SOCKET socket, string sender, string message);
void sendMsgInConf(string sender, string message, int conference, vector<string> users, map<string,SOCKET>sockets);
void cnfCommand(SOCKET socket, int conference, vector<string> users);
void cnfCommandToAll(int conference, vector<string> users, map<string,SOCKET> sockets);
void errCommand(SOCKET socket, int error);
void filCommand(SOCKET socket, string filename, int filesize, string sender);

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
								cnfCommandToAll(it2->first, it2->second, usersList);
							}
						}
					}
					break;
				}
				if (dflag)
					cout << it->first << " commands " << recvbuf << endl;

				if (strncmp(recvbuf, "send", 4) == 0)
				{
					cout << it->first << " sends message ";
					char* user_pos = strchr(recvbuf, ':');
					if (user_pos != NULL)
					{
						char* message_pos = strchr(user_pos+1, ':');
						if (message_pos != NULL)
						{
							string user = string(user_pos+1, message_pos-user_pos-1);
							cout << "to user [" << user << "] ";
							string text = string(message_pos+1, rResult - (message_pos - recvbuf));
							cout << "with text " << text << endl;

							// поиск пользователя
							map<string,SOCKET>::iterator user_it = usersList.find(user);
							if (user_it != usersList.end())
							{
								sendMsg(user_it->second, it->first, text);
								break;
							}
							// иначе, поиск комнаты
							else if (user[0] == '#')
							{
								user = user.substr(1, string::npos);
								int conference = atoi(user.c_str());
								map<int,vector<string>>::iterator conf_it;
								// цикл поиска комнаты
								conf_it = conferences.find(conference);
								if (conf_it != conferences.end())
								{
									// защита от письма в комнату, в которой не состоишь
									if (find(conf_it->second.begin(), conf_it->second.end(), it->first) != conf_it->second.end())
									{
										sendMsgInConf(it->first, text, conf_it->first, conf_it->second, usersList);
										break;
									}
									else
									{
										cout << "access restriction" << endl;
										errCommand(it->second, 3);
									}
								}
								else
								{
									errCommand(it->second, 8);
								}
							} else 
							{
								errCommand(it->second, 2);
							}
						}
						else
						{
							errCommand(it->second, 4);
						}
					}
					else
					{
						errCommand(it->second, 5);
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
						errCommand(it->second, 6);
					} 
					else if (users.size() < 3)
					{
						errCommand(it->second, 7);
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
							cnfCommand(it->second, pos->first, pos->second);
						}
						else
						{
							index = conferences.size();
							cout << "new conference: " << usersString << endl;
							conferences.insert(pair<int,vector<string>>(index, users));
							cout << "id #" << index << endl;
							pos = conferences.find(index);
							cnfCommandToAll(pos->first, pos->second, usersList);
						}
					}
				} else if (strncmp(recvbuf, "list", 4) == 0) {
					lstCommand(it->second);
				} else if (strncmp(recvbuf, "quit", 4) == 0) {
					
				} else if (strncmp(recvbuf, "file", 4) == 0) {
					cout << it->first << " sends file";
					char* size_pos = strchr(recvbuf+5, ':');
					if (size_pos != NULL) {
						string filename(recvbuf+5, size_pos - (recvbuf+5));
						cout << " filename: " << filename;
						cout << size_pos << endl;
						unsigned int filesize = atoi(size_pos+1);
						cout << " filesize: " << filesize;
						char* user_pos = strchr(size_pos+1, ':');
						if (user_pos != NULL) {
							string userto(user_pos+1, rResult - (user_pos+1 - recvbuf));
							cout << " to user " << userto << endl;
							map<string,SOCKET>::iterator u_it = usersList.find(userto);
							if (u_it != usersList.end()) {
								send(it->second, "ok", 2, 0);
								unsigned long ulMode = 0;
								ioctlsocket(it->second, FIONBIO, (unsigned long*)&ulMode);
								filCommand(u_it->second, filename, filesize, it->first);
								while (filesize > 0) {
									rResult = recv(it->second, recvbuf, 1024, 0);
									filesize -= rResult;
									send(u_it->second, recvbuf, rResult, 0);
								}
								ulMode = 1;
								ioctlsocket(it->second, FIONBIO, (unsigned long*)&ulMode);
								cout << "... complete" << endl;
							} else {
								cout << "invalid format" << endl;
							}
						} else {
						cout << "invalid format" << endl;
						}
					} else {
						cout << "invalid format" << endl;
					}
				}
				memset(recvbuf, 0, rResult);
			}// else if (FD_ISSET(it->first, &except_file_d)) {

			//}

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
			errCommand(clientSocket, 1);
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

void sendMsg(SOCKET socket, string sender, string message)
{
	string sendbuf;
	sendbuf = "msg:";
	sendbuf += sender;
	sendbuf += ":";
	sendbuf += message;
	send(socket, sendbuf.c_str(), sendbuf.size(), 0);
}

void sendMsgInConf(string sender, string message, int conference, vector<string> users, map<string,SOCKET>sockets)
{
	string sendbuf;
	sendbuf += "msg:#";
	sendbuf += to_string(conference);
	sendbuf += ":";
	sendbuf += sender;
	sendbuf += ":";
	sendbuf += message;
	// цикл перебора пользователей
	for (vector<string>::iterator user_it = users.begin(); user_it != users.end(); ++user_it)
	{
		if (sender.compare(*user_it) == 0)
			continue;
		map<string,SOCKET>::iterator user = sockets.find(*user_it);
		send(user->second, sendbuf.c_str(), sendbuf.length(), 0);
	}
}

void lstCommand(SOCKET socket)
{
	string sendbuf;
	sendbuf += "lst:";
	sendbuf += generate_users_list();
	send(socket, sendbuf.c_str(), sendbuf.size(), 0);
}

void cnfCommand(SOCKET socket, int conference, vector<string> users)
{
	string sendbuf;
	sendbuf += "cnf:";
	sendbuf += to_string(conference);
	sendbuf += ":";
	for (vector<string>::iterator it = users.begin(); it != users.end(); ++it)
	{
		if (it > users.begin())
			sendbuf += ",";
		sendbuf += *it;
	}
	send(socket, sendbuf.c_str(), sendbuf.size(), 0);
}

void cnfCommandToAll(int conference, vector<string> users, map<string,SOCKET> sockets)
{
	string sendbuf;
	// уведомить пользователей конференции о конференции
	sendbuf += "cnf:";
	sendbuf += to_string(conference);
	sendbuf += ":";
	for (vector<string>::iterator user_it = users.begin(); user_it != users.end(); ++user_it)
	{
		if (user_it > users.begin())
		{
			sendbuf += ",";
		}
		sendbuf += *user_it;
	}
	for (vector<string>::iterator user_it = users.begin(); user_it != users.end(); ++user_it)
	{
		send((sockets.find(*user_it))->second, sendbuf.c_str(), sendbuf.size(), 0);
	}
}

void errCommand(SOCKET socket, int error)
{
	string sendbuf;
	sendbuf += "err:";
	sendbuf += to_string(error);
	send(socket, sendbuf.c_str(), sendbuf.size(), 0);
}

void filCommand(SOCKET socket, string filename, int filesize, string sender)
{
	string sendbuf;
	sendbuf = "fil:";
	sendbuf += filename;
	sendbuf += ":";
	sendbuf += to_string(filesize);
	sendbuf += ":";
	sendbuf += sender;
	send(socket, sendbuf.c_str(), sendbuf.size(), 0);
}