#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <string>
#include <sstream>
#include <ctime>
using namespace std;


struct responseMessage
{
	string httpVersion = "HTTP/1.1";
	string statusCode = "200 OK";
	string date = "";
	string serverName = "Server: TCPNonBlockingServer/1.0";
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
	int sendSubType;	// Sending sub-type
	char buffer[BUFFER_SIZE];
	int len;
	clock_t responseTime;
	clock_t currentTime;
	responseMessage response;
	char* responseCharArray;
};
const int BUFFER_SIZE = 1024;
const int TIME_PORT = 27015;
const int MAX_SOCKETS = 60;
const int EMPTY = 0; //free socket
const int LISTEN = 1; //the socket how handel new clients
const int RECEIVE = 2; //a client sand message to this socket
const int IDLE = 3; //in the send at sockectState
const int SEND = 4; //the socket how need to send message
//option to send:
const int SEND_TIME = 1;
const int SEND_SECONDS = 2;
//HTTP methods
const int OPTIONS = 1;
const int GET = 2;
const int HEAD = 3;
const int POST = 4;
const int PUT = 5;
const int DEL = 6;
const int TRACE = 7;

//methods:
bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
char* ResponseToCharArray(responseMessage* response, bool isHead);
void RemoveCharsFromArray(char*& responseToSend, int numOfChars);
void RemoveCharsFromSocketBuff(int index, int numOfChars);
void HandleRequest(int index, const string& request, const string& timeStr);

void OptionsRequest(int index, string time);
void GetOrHeadRequest(int index, const string& request, string timeStr, bool isGet);
void PostRequest(int index, string timeStr);
void PutRequest(int index, string timeStr);
void DeleteRequest(int index, string timeStr);
void TraceRequest(int index, string timeStr);

//Data base:
struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;


void main()
{
	// Initialize Winsock
	WSAData wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Time Server: Error at WSAStartup()\n";
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
			cout << "Time Server: Error at select(): " << WSAGetLastError() << endl;
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

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				if (sockets[i].send == SEND)
					sendMessage(i);
			}
		}
	}

	// Closing connections and Winsock.
	cout << "Time Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}

bool addSocket(SOCKET id, int what)
{
	unsigned long flag = 1;
	if (ioctlsocket(id, FIONBIO, &flag) != 0)
	{
		cout << "Time Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
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
	delete sockets[index].responseCharArray;
	sockets[index].responseCharArray = nullptr;
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
		cout << "Time Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "Time Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

	if (addSocket(msgSocket, RECEIVE) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(id);
	}
	return;
}

void receiveMessage(int index)
{
	SOCKET msgSocket = sockets[index].id;
	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv)
	{
		cout << "Time Server: Error at recv(): " << WSAGetLastError() << endl;
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
		sockets[index].buffer[len + bytesRecv] = '\0';
		sockets[index].len += bytesRecv;

		cout << "Time Server: Received: " << bytesRecv << " bytes of the message - "
			<< &sockets[index].buffer[len] << endl;

		string request(sockets[index].buffer);
		size_t endOfFirstRequest = request.find("\r\n\r\n", 0);
		if (endOfFirstRequest == string::npos)
		{
			return;
		}

		string timeStr = getCurrentTimeString();
		//todo check if from the last byte pass 2 min => close socket
		request = request.substr(0, endOfFirstRequest);

		HandleRequest(index, request, timeStr);
	}
}

void sendMessage(int index)
{
	SOCKET msgSocket = sockets[index].id;
	bool isHead = (sockets[index].sendSubType == HEAD);

	char* responseToSend = sockets[index].responseCharArray;
	int bytesSent = 0;
	int partsOfMessage = 0;

	if (sockets[index].responseCharArray == nullptr)
	{
		responseToSend = ResponseToCharArray(&(sockets[index].response), isHead);
	}

	int totalLen = strlen(responseToSend);
	

	while (bytesSent < totalLen)
	{
		int result = send(msgSocket, responseToSend + bytesSent, totalLen - bytesSent, 0);
		if (result == SOCKET_ERROR)
		{
			if (WSAGetLastError() == WSAEWOULDBLOCK)
			{
				// Socket is not ready to send
				return;
			}
			else
			{
				cout << "Server: Error at send(): " << WSAGetLastError() << endl;
				closesocket(msgSocket);
				removeSocket(index);
				return;
			}
		}
		partsOfMessage++;
		bytesSent += result;
		cout << "Time Server: Sent: " << result << " of " << totalLen << " bytes message.\n";
	}

	if (bytesSent == totalLen)
	{
		sockets[index].send = IDLE;
		cout << "Time Server: Sent: " << partsOfMessage << " for message in socker number " << index << endl;
	}
	else
	{
		cout << "Time Server: Sent: " << partsOfMessage << " for message in socker number " << index;
		cout << " and remain " << totalLen-bytesSent << " bytes to sent\n";

		RemoveCharsFromArray(responseToSend, bytesSent);
	}
}

string getCurrentTimeString()
{
	time_t timeOfNow;
	time(&timeOfNow);
	string timeStr = ctime(&timeOfNow);
	timeStr.pop_back(); // ???? ???? ?????
	return timeStr;
}

void RemoveCharsFromSocketBuff(int index, int numOfChars)
{
	memcpy(sockets[index].buffer, &sockets[index].buffer[numOfChars], sockets[index].len - numOfChars);
	sockets[index].len -= numOfChars;
}

void RemoveCharsFromArray(char*& responseToSend, int numOfChars)
{
	if (responseToSend == nullptr)
		return;

	int length = strlen(responseToSend);
	if (numOfChars >= length)
	{
		delete[] responseToSend;
		responseToSend = nullptr; 
		return;
	}

	memmove(responseToSend, responseToSend + numOfChars, length - numOfChars + 1);
}

char* ResponseToCharArray(responseMessage* response, bool isHead)
{
	ostringstream responseStream;
	responseStream << response->httpVersion << " "
		<< response->statusCode << "\r\n"
		<< response->date << "\r\n"
		<< response->serverName << "\r\n"
		<< response->contentType << "\r\n"
		<< "Content-Length: " << response->responseData.size() << "\r\n"
		<< response->connection << "\r\n\r\n";

	if (isHead && !response->responseData.empty())
		responseStream << response->responseData;

	string responseStr = responseStream.str();
	char* responseArray = new char[responseStr.size() + 1];
	strcpy(responseArray, responseStr.c_str());

	return responseArray;
}

void HandleRequest(int index, const string& request, const string& timeStr)
{	
	string method = GeMethodFromRequest(request);
	
	if (method == "OPTIONS")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = OPTIONS;
		OptionsRequest(index, timeStr);
	}
	else if (method == "GET")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = GET;
		GetOrHeadRequest(index, timeStr, request, true);
	}
	else if (method == "HEAD")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = HEAD;
		// GetOrHeadRequest(index, timeStr, false);
	}
	else if (method == "POST")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = POST;
		// PostRequest(index, timeStr);
	}
	else if (method == "PUT")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = PUT;
		// PutRequest(index, timeStr);
	}
	else if (method == "DELETE")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = DEL;
		// DeleteRequest(index, timeStr);
	}
	else if (method == "TRACE")
	{
		sockets[index].send = SEND;
		sockets[index].sendSubType = TRACE;
		// TraceRequest(index, timeStr);
	}
	else if (method == "Exit")
	{
		SOCKET msgSocket = sockets[index].id;
		closesocket(msgSocket);
		removeSocket(index);
	}
	else
	{
		cout << "Time Server: Unknown request type: " << method << endl;
	}
}

void ParseRequest(string request, string& path, string& version, string& host, string& contentType, string& contentLength)
{
	size_t endOfRequest = request.find("\r\n\r\n", 0);
	string headers = request.substr(0, endOfRequest);

	size_t firstLineEnd = headers.find("\r\n");
	string firstLine = headers.substr(0, firstLineEnd);

	size_t methodEnd = firstLine.find(" ");
	size_t pathEnd = firstLine.find(" ", methodEnd + 1);

	path = firstLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
	version = firstLine.substr(pathEnd + 1);

	// ?? ?? ???? Host, ???? ?????? ????? ?? ????? ?-HTTP/1.1
	size_t hostStart = headers.find("Host: ");
	if (hostStart != string::npos)
	{
		hostStart += 6; // ???? "Host: "
		size_t hostEnd = headers.find("\r\n", hostStart);
		host = headers.substr(hostStart, hostEnd - hostStart);
	}
	else
	{
		// Host ??? ????? ???? ?-HTTP/1.1
		throw runtime_error("400 Bad Request: Missing Host header");
	}

	// ?? ?? ???? Content-Type, ???? ????? ?? ????? ?????
	size_t contentTypeStart = headers.find("Content-Type: ");
	if (contentTypeStart != string::npos)
	{
		contentTypeStart += 14; // ???? "Content-Type: "
		size_t contentTypeEnd = headers.find("\r\n", contentTypeStart);
		contentType = headers.substr(contentTypeStart, contentTypeEnd - contentTypeStart);
	}
	else
	{
		contentType = "text/plain"; // ????? ???? ?? ?? ????
	}

	// ?? ?? ???? Content-Length, ?? ???? ????? ??????
	size_t contentLengthStart = headers.find("Content-Length: ");
	if (contentLengthStart != string::npos)
	{
		contentLengthStart += 15; // ???? "Content-Length: "
		size_t contentLengthEnd = headers.find("\r\n", contentLengthStart);
		contentLength = headers.substr(contentLengthStart, contentLengthEnd - contentLengthStart);
	}
	else
	{
		contentLength = "0"; // ?? ?? ????, ?? ???? ?????. ???? ????? ???? ??? ?????
	}
}



string GeMethodFromRequest(string request)
{
	size_t methodEnd = request.find(' ');
	if (methodEnd != string::npos)
	{
		return request.substr(0, methodEnd);
	}
	return "";
}

void OptionsRequest(int index, string timeStr)
{
	RemoveCharsFromSocketBuff(index, 8); //remove options(7) + " "(1) 
	responseMessage* response = &sockets[index].response;

	response->date = "Date: " + timeStr;
	response->responseData = "Allow: OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE";
	response->contentType = "Content-Type: text/html";
	response->contentLength = "Content-Length: " + to_string(response->responseData.size());
}

void GetOrHeadRequest(int index, const string& request, string timeStr, bool isGet)
{
	//
}

void PostRequest(int index, string timeStr)
{
	char* bufferSocket = sockets[index].buffer;
	string path;
	string version;
	string contentLen;

	string request(bufferSocket);
	size_t endOfRequest = request.find("\r\n\r\n", 0);
	request = request.substr(0, endOfRequest);
	responseMessage* response = &sockets[index].response;

	if (endOfRequest != string::npos)
	{
		if (GetContentLength(request, contentLen) && )
		{
			int contentLength = stoi(contentLen);

		}
		else
		{
			response->statusCode = "400 Bad Request";
			response->contentLength = "Content-Length: 0";
		}
	}
}

void PutRequest(int index, string timeStr)
{

}

void DeleteRequest(int index, string timeStr)
{

}

void TraceRequest(int index, string timeStr)
{

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

bool GetPath(string request, string& path)
{
	size_t methodEnd = request.find(' ');
	if (methodEnd != string::npos)
	{
		size_t pathStart = methodEnd + 1;
		size_t pathEnd = request.find(' ', pathStart);

		if (pathEnd != string::npos)
		{
			path = request.substr(pathStart, pathEnd - pathStart); // ???? ?? ?-path (???? ?????)
			return true;
		}
	}
	return false; // ?? ?? ?????? ????? ?? ?-path
}

void GetQueryString(string request, string& queryString)
{
	size_t queryStart = request.find("?");
	if (queryStart != string::npos)
	{
		queryString = request.substr(queryStart + 1);
	}
}

bool GetVersion(string request, string& version)
{
	size_t versionStart = request.find("HTTP/");
	if (versionStart != string::npos)
	{
		version = request.substr(versionStart); // ???? ?? ?-version (???? HTTP/1.1)
		return true;
	}
	return false; // ?? ?? ?????? ????? ?? ?????
}

bool GetBody(string request, string& body)
{
	size_t bodyStart = request.find("\r\n\r\n");
	if (bodyStart != string::npos)
	{
		body = request.substr(bodyStart + 4); // body ????? ???? "\r\n\r\n"
		return true;
	}
	return false; // ?? ?? ?????? ????? ?? ?-body
}