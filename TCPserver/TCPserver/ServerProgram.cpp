#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <string>
#include <sstream>
#include <ctime>
#include <fstream>
#include <list>
using namespace std;

const int BUFFER_SIZE = 1024;
const int TIME_PORT = 27015;
const int MAX_SOCKETS = 60;
const int EMPTY = 0; //free socket
const int LISTEN = 1; //the socket how handel new clients
const int RECEIVE = 2; //a client sand message to this socket
const int IDLE = 3; //in the send at sockectState
const int SEND = 4; //the socket how need to send message

struct responseMessage
{
	string httpVersion = "HTTP/1.1";
	string statusCode = "200 OK";
	string date = "";
	string serverName = "Server: JonathanTCPNonBlockingServer";
	string responseData = "";
	string contentLength = "";
	string contentType = "Content-Type: text/html";
	string connection = "Connection: keep-alive";
};

struct SocketState
{
	SOCKET id;			// Socket handle
	int	recv;			// Receiving?
	int	send;			// Sending?
	char buffer[BUFFER_SIZE];
	int len;
	clock_t lastByteRecvTime;
	list<responseMessage*> Responses;
	int LastByteSentResponse = 0;
};

//methods:
bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
void RemoveCharsFromSocketBuff(int index, int numOfChars);
string GetCurrentTimeString();
void SetBadBodyRequestResponse(responseMessage* response);
char* ResponseToCharArray(responseMessage* response);
void HandleRequest(int index, const string& request, const string& timeStr);
string ParseMethodFromRequest(string request);
bool ParseRequest(const string& request, string& method, string& path, string& queryString, string& version, string& contentLength, string& lang);
bool ParseFirstLine(const string& request, string& method, string& path, string& queryString, string& version);
bool ExtractContentLength(const string& request, string& contentLength);
void ExtractLangFromQueryString(const string& queryString, string& lang);

void OptionsRequest(int index, string timeStr, responseMessage* response);
void GetOrHeadRequest(int index, const string& request, string timeStr, responseMessage* response);
void PostOrPutRequest(int index, const string& request, string timeStr, responseMessage* response);
void DeleteRequest(int index, const string& request, string timeStr, responseMessage* response);
void TraceRequest(int index, const string& request, string timeStr, responseMessage* response);

string GetFullPath(const string& path);
bool GetContentLength(string request, string& contentLen);
bool HandelGetOrHaedRequest(const string& method, string& path, string& lang, responseMessage* response);
bool ReadFromFile(const string& method, const string& path, responseMessage* response);
void WirteToFile(const string& path, const string& request, const string& method, responseMessage* response, string contentLen);
void RelaseList(list<responseMessage*>& Responses);

//Data base:
struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;


void main()
{
	// Initialize Winsock
	WSAData wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "\nTime Server: Error at WSAStartup()\n";
		return;
	}

	// Server side:
	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == listenSocket)
	{
		cout << "Time Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}
	sockaddr_in serverService;
	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(TIME_PORT);

	// Bind the socket for client's requests.
	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService)))
	{
		cout << "Time Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	// Listen on the Socket for incoming connections.
	if (SOCKET_ERROR == listen(listenSocket, 5))
	{
		cout << "Time Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
	addSocket(listenSocket, LISTEN);

	cout << "Time Server: Opening Connection.\n";

	bool serverIsRunning = true;
	while (serverIsRunning)
	{
		// Check for timeouts
		for (int i = 1; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].recv != EMPTY)
			{
				time_t currentTime = time(nullptr);
				if (currentTime - sockets[i].lastByteRecvTime > 120)
				{
					cout << "\nClosing socket " << i << " due to timeout.\n";
					closesocket(sockets[i].id);
					removeSocket(i);
				}
			}
		}

		fd_set waitRecv;
		FD_ZERO(&waitRecv);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
				FD_SET(sockets[i].id, &waitRecv);
		}

		fd_set waitSend;
		FD_ZERO(&waitSend);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].send == SEND)
				FD_SET(sockets[i].id, &waitSend);
		}

		// Wait for interesting event.
		// Note: First argument is ignored. The fourth is for exceptions.
		// And as written above the last is a timeout, hence we are blocked if nothing happens.
		int nfd;
		nfd = select(0, &waitRecv, &waitSend, NULL, NULL);
		if (nfd == SOCKET_ERROR)
		{
			cout << "\nTime Server: Error at select(): " << WSAGetLastError() << endl;
			WSACleanup();
			return;
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitRecv))
			{
				nfd--;
				switch (sockets[i].recv)
				{
					case LISTEN:
						acceptConnection(i);
						break;

					case RECEIVE:
						receiveMessage(i);
						break;
				}
			}
		}

		for (int i = 1; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				if (sockets[i].send == SEND)
				{
					sendMessage(i);
				}
			}
		}
	}

	// Closing connections and Winsock.

	cout << "\n\nTime Server: Closing Connection.\n";
	cout << "Time Server: the server was up for " << (time(nullptr) - sockets[0].lastByteRecvTime) / 60 << "sec\n";
	closesocket(listenSocket);
	WSACleanup();
}

bool addSocket(SOCKET id, int what)
{
	unsigned long flag = 1;
	if (ioctlsocket(id, FIONBIO, &flag) != 0)
	{
		cout << "\nTime Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
			sockets[i].lastByteRecvTime = time(nullptr);

			socketsCount++;
			return (true);
		}
	}
	return (false);
}

void removeSocket(int index)
{
	sockets[index].recv = EMPTY;
	sockets[index].send = EMPTY;
	RelaseList(sockets[index].Responses);
	socketsCount--;
}

void acceptConnection(int index)
{
	SOCKET id = sockets[index].id;
	struct sockaddr_in from;		// Address of sending partner
	int fromLen = sizeof(from);

	SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
	if (INVALID_SOCKET == msgSocket)
	{
		cout << "\nTime Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "\nTime Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

	if (addSocket(msgSocket, RECEIVE) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(id);
	}
	return;
}

void receiveMessage(int index)
{
	sockets[index].lastByteRecvTime = time(nullptr);
	string timeStr = GetCurrentTimeString();

	SOCKET msgSocket = sockets[index].id;
	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv)
	{
		cout << "\nTime Server: Error at recv(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	if (bytesRecv == 0)
	{
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	else
	{
		sockets[index].buffer[len + bytesRecv + 1] = '\0';
		sockets[index].len += bytesRecv;

		//cout << "Time Server: Received: \n" << bytesRecv << " bytes of the message :\n "
		//	<< &sockets[index].buffer[len] << endl;

		string request(sockets[index].buffer);

		cout <<"\n#VVVVVVV# RECV #VVVVVVV#\n\n" << request << "\n#^^^^^^^# RECV #^^^^^^^#\n";
		HandleRequest(index, request, timeStr);

		RemoveCharsFromSocketBuff(index, request.size());
	}
}

void sendMessage(int index)
{
	SOCKET msgSocket = sockets[index].id;
	list<responseMessage*>& responses = sockets[index].Responses;

	int bytesSent = sockets[index].LastByteSentResponse;
	int countResponsesSent = 1;

	while (!responses.empty() && countResponsesSent < 10)
	{
		responseMessage* responseToSend = responses.front();
		char* responseStr = ResponseToCharArray(responseToSend);
		int totalLen = strlen(responseStr);
		cout << "\n#VVVVVVV# SEND #VVVVVVV#\n\n" << responseStr << "\n#^^^^^^^# SEND #^^^^^^^#\n";

		while (bytesSent < totalLen)
		{
			int result = send(msgSocket, responseStr + bytesSent, totalLen - bytesSent, 0);
			
			if (result == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					sockets[index].LastByteSentResponse = bytesSent;
					delete[] responseStr;
					return;
				}
				else
				{
					cerr << "Server: Error at send(): " << WSAGetLastError() << endl;
					closesocket(msgSocket);
					removeSocket(index);
					delete[] responseStr;
					return;
				}
			}
			bytesSent += result;
			cout << "\nTime Server: Sent " << result << " of " << totalLen << " bytes of responses no " << countResponsesSent << "\n";
		}

		if (bytesSent == totalLen) //sent one response
		{
			bytesSent = 0;
			sockets[index].LastByteSentResponse = 0;
			delete[] responseStr;
			delete responseToSend;
			sockets[index].Responses.pop_front();
			cout << "\nTime Server: Sent "<< countResponsesSent << " responses for socket index "<< index << endl;
			countResponsesSent++;

			//shutdown(msgSocket, SD_SEND); // Signal no more data will be sent
			//closesocket(msgSocket); // Close the socket completely
			//removeSocket(index);
		}
	}

	if(responses.empty())
		sockets[index].send = IDLE;
}

string GetCurrentTimeString()
{
	time_t timeOfNow;
	time(&timeOfNow);
	string timeStr = ctime(&timeOfNow);
	timeStr.pop_back();

	return timeStr;
}

void SetBadBodyRequestResponse(responseMessage* response)
{
	response->statusCode = "400 Bad Request";
	response->responseData = "Bad Body Request";
	response->contentLength = to_string(response->responseData.length());
	response->contentType = "Content-Type: text/html";
	response->connection = "Connection: close";
}

void RemoveCharsFromSocketBuff(int index, int numOfChars)
{
	memcpy(sockets[index].buffer, &sockets[index].buffer[numOfChars], sockets[index].len);
	sockets[index].len -= numOfChars;
}

char* ResponseToCharArray(responseMessage* response)
{
	ostringstream responseStream;
	responseStream << response->httpVersion << " "
		<< response->statusCode << "\r\n"
		<< response->date << "\r\n"
		<< response->serverName << "\r\n"
		<< response->contentType << "\r\n"
		<< response->contentLength << "\r\n"
		<< response->connection << "\r\n\r\n";

	if (!response->responseData.empty())
		responseStream << response->responseData;

	string responseStr = responseStream.str();
	char* responseArray = new char[responseStr.size() + 1];
	strcpy(responseArray, responseStr.c_str());

	return responseArray;
}

void HandleRequest(int index, const string& request, const string& timeStr)
{	
	string method = ParseMethodFromRequest(request);
	responseMessage* response = new responseMessage();

	if (method == "OPTIONS")
	{
		sockets[index].send = SEND;
		OptionsRequest(index, timeStr, response);
	}
	else if (method == "GET" || method == "HEAD")
	{
		sockets[index].send = SEND;
		GetOrHeadRequest(index, request, timeStr, response);
	}
	else if (method == "POST" || method == "PUT")
	{
		sockets[index].send = SEND;
		PostOrPutRequest(index, request, timeStr, response);
	}
	else if (method == "DELETE")
	{
		sockets[index].send = SEND;
		DeleteRequest(index, request, timeStr, response);
	}
	else if (method == "TRACE")
	{
		sockets[index].send = SEND;
		TraceRequest(index, request, timeStr, response);
	}
	else if (method == "Exit")
	{
		SOCKET msgSocket = sockets[index].id;
		closesocket(msgSocket);
		removeSocket(index);
	}
	else
	{
		cout << "\nTime Server: Unknown request type: " << method << endl;
	}

	sockets[index].Responses.push_back(response);
}

string ParseMethodFromRequest(string request)
{
	size_t methodEnd = request.find(' ');
	if (methodEnd != string::npos)
	{
		return request.substr(0, methodEnd);
	}
	return "";
}

bool ParseRequest(const string& request, string& method, string& path, string& queryString, string& version, string& contentLength, string& lang)
{
	if (!ParseFirstLine(request, method, path, queryString, version))
		return false; // Bad request in the first line

	if (!ExtractContentLength(request, contentLength))
		return false; // Bad request in content length

	ExtractLangFromQueryString(queryString, lang);

	return true;
}

bool ParseFirstLine(const string& request, string& method, string& path, string& queryString, string& version)
{
	size_t firstLineEnd = request.find("\r\n");
	if (firstLineEnd == string::npos) return false; // Bad request

	string firstLine = request.substr(0, firstLineEnd);

	size_t methodEnd = firstLine.find(" ");
	if (methodEnd == string::npos) return false; // Bad request
	method = firstLine.substr(0, methodEnd);

	size_t pathEnd = firstLine.find(" ", methodEnd + 1);
	if (pathEnd == string::npos) return false; // Bad request
	string fullPath = firstLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);

	size_t queryStart = fullPath.find("?");
	if (queryStart != string::npos)
	{
		path = fullPath.substr(0, queryStart);
		queryString = fullPath.substr(queryStart + 1);
	}
	else
	{
		path = fullPath;
		queryString.clear();
	}

	version = firstLine.substr(pathEnd + 1);

	return true; 
}

bool ExtractContentLength(const string& request, string& contentLength)
{
	size_t contentLengthStart = request.find("Content-Length: ");
	if (contentLengthStart != string::npos)
	{
		contentLengthStart += 16;
		size_t contentLengthEnd = request.find("\r\n", contentLengthStart);

		contentLength = request.substr(contentLengthStart, contentLengthEnd - contentLengthStart);
		try
		{
			int length = stoi(contentLength);
			if (length < 0) 
				return false; // Content-Length is negative
		}
		catch (const std::exception&)
		{
			return false; // Content-Length is not a number
		}
	}
	else
	{
		contentLength.empty();
	}

	return true;
}

void ExtractLangFromQueryString(const string& queryString, string& lang)
{
	size_t langStart = queryString.find("lang=");
	if (langStart != string::npos)
	{
		langStart += 5;
		size_t langEnd = queryString.find("&", langStart);
		if (langEnd == string::npos) langEnd = queryString.size();
		lang = queryString.substr(langStart, langEnd - langStart);

		// Validate lang
		if (lang != "en" && lang != "he" && lang != "fr")
		{
			lang.clear(); //Invalid language
		}
	}
	else
	{
		lang.clear(); //No language
	}
}


void OptionsRequest(int index, string timeStr, responseMessage* response)
{
	response->date = "Date: " + timeStr;
	response->responseData = "Allow: OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE";
	response->contentLength = "Content-Length: " + to_string(response->responseData.size());
}

void GetOrHeadRequest(int index, const string& request, string timeStr, responseMessage* response)
{
	string method, path, queryString, version, contentLen, lang;

	if (!ParseRequest(request, method, path, queryString, version, contentLen, lang))
	{
		SetBadBodyRequestResponse(response);
		return;
	}

	if (!HandelGetOrHaedRequest(method, path, lang, response))
	{
		response->statusCode = "404 Not Found";
		response->responseData = "File not found";
		response->contentLength = "Content-Length: " + to_string(response->responseData.length());
		response->connection = "Connection: close";
	}
}

void PostOrPutRequest(int index, const string& request, string timeStr, responseMessage* response)
{
	string method, path, queryString, version, contentLen, lang;

	if (!ParseRequest(request, method, path, queryString, version, contentLen, lang))
	{
		SetBadBodyRequestResponse(response);
		return;
	}

	WirteToFile(path, request, method, response, contentLen);
}

void DeleteRequest(int index, const string& request, string timeStr, responseMessage* response)
{
	string method, path, queryString, version, contentLen, lang;

	if (!ParseRequest(request, method, path, queryString, version, contentLen, lang))
	{
		SetBadBodyRequestResponse(response);
		return;
	}

	string fileName = GetFullPath(path);

	if (remove(fileName.c_str()) == 0)
	{
		response->statusCode = "204 No Content";
		response->contentLength = "Content-Length: 0";
	}
	else
	{
		response->statusCode = "404 Not Found";
		response->responseData = "File not found: " + path;
		response->contentType = "Content-Type: text/plain";
		response->contentLength = "Content-Length: " + to_string(response->responseData.size());
	}
}

void TraceRequest(int index, const string& request, string timeStr, responseMessage* response)
{
	response->responseData = request;
	response->contentType = "Content-Type: message/http";
	response->contentLength = "Content-Length: " + to_string(response->responseData.size());
}


string GetFullPath(const string& path)
{
	string fileName;

	if (!path.empty() && path[0] == '/')
	{
		fileName = "C:/Temp" + path;
	}
	else
	{
		fileName = "C:/Temp/" + path;
	}

	return fileName;
}

bool GetContentLength(string request , string& contentLen)
{
	size_t contentLengthStart = request.find("Content-Length: ", 0);
	if (contentLengthStart == string::npos)
	{
		return false;//no content length
	}

	contentLengthStart += 16;
	size_t contentLengthEnd = request.find("\r\n", contentLengthStart);
	if (contentLengthEnd == string::npos)
	{
		return false;//no end of content length
	}

	contentLen = request.substr(contentLengthStart, contentLengthEnd - contentLengthStart);
			
	try
	{
		int contentLength = stoi(contentLen);
		if (contentLength < 0)
		{
			return false;//content length is negative
		}
	}
	catch (const std::exception& e)
	{
		return false;
	}

	return true;
}

bool HandelGetOrHaedRequest(const string& method, string& path, string& lang, responseMessage* response)
{
	
	if (path == "/TimeString")
	{
		time_t timer;
		time(&timer);

		string currentTime = ctime(&timer);
		currentTime.erase(currentTime.length() - 1); // Remove the newline character
		if (method == "GET")
			response->responseData = currentTime;
		response->contentLength = "Content-Length: " + std::to_string(currentTime.length());
		return true;
	}
	else if (path == "/SecondsSince1970")
	{
		time_t timer;
		time(&timer);

		string secondsSince1970 = to_string((int)timer);
		if (method == "GET")
			response->responseData = secondsSince1970;
		response->contentLength = "Content-Length: " + std::to_string(secondsSince1970.length());
		return true;
	}
	else if (path == "/")
	{
		if (lang == "he")
			path = "/myfile-he.html";
		else if (lang == "en")
			path = "/myfile-en.html";
		else if (lang == "fr")
			path = "myfile-fr.html";
	}
	
	
	return ReadFromFile(method, path, response);
}

bool ReadFromFile(const string& method, const string& path, responseMessage* response)
{
	string fileName = GetFullPath(path);
	ifstream file(fileName, ios::binary);
	if (!file.is_open())
	{
		return false; // File not found
	}

	stringstream buffer;
	buffer << file.rdbuf();
	file.close();

	response->contentLength = "Content-Length: " + to_string(buffer.str().length());
	if(method == "GET")
		response->responseData = buffer.str();

	return true;
}

void WirteToFile(const string& path, const string& request, const string& method, responseMessage* response, string contentLen)
{
	string fileName = GetFullPath(path);
	FILE* file;

	if (method == "POST")
	{
		file = fopen(fileName.c_str(), "a");
	}
	else// is PUT
	{
		file = fopen(fileName.c_str(), "wb");
	}
	
	if (!file)
	{
		if (method == "POST")
		{
			response->statusCode = "404 Not Found";
			response->responseData = "File not found";
		}
		else
		{
			response->statusCode = "500 Internal Server Error";
			response->responseData = "Failed to open file";
		}
		response->contentLength = "Content-Length: " + to_string(response->responseData.length());
		response->connection = "Connection: close";

		return;
	}

	size_t startBody = request.find("\r\n\r\n", 0);//will always succeed because ParseRequest passed
	startBody += 4;

	size_t endBody = request.size() - startBody;
	string body = request.substr(startBody, endBody - startBody);

	if (!contentLen.empty() && stoi(contentLen) != body.size())
	{
		response->statusCode = "400 Bad Request";
		response->responseData = "Content-Length mismatch: The actual body length does not match the specified Content-Length.";
		response->contentLength = "Content-Length: " + to_string(response->responseData.length());

		fclose(file);
		return;
	}

	size_t bytesWritten = fwrite(body.c_str(), sizeof(char), body.size(), file);
	if (bytesWritten == body.size()) 
	{
		response->responseData = method + " Request went successfully, added the required data to " + fileName;
	}
	else
	{
		response->statusCode = "500 Internal Server Error";
		response->responseData = "Failed to write data to file.";
	}

	response->contentType = "Content-Type: text/plain";
	response->contentLength = "Content-Length: " + to_string(response->responseData.length());

	fclose(file);
}

void RelaseList(list<responseMessage*>& Responses)
{
	for (auto response : Responses)
	{
		delete response;
	}
	Responses.clear();
}
