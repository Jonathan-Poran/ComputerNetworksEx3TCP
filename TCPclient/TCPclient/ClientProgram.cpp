#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h> 
#include <string.h>
#include <iomanip>

const int TIME_PORT = 27015;


void main()
{

	// Initialize Winsock (Windows Sockets).
	WSAData wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Time Client: Error at WSAStartup()\n";
		return;
	}

	// Client side:
	// Create a socket and connect to an internet address.
	SOCKET connSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == connSocket)
	{
		cout << "Time Client: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}

	sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = htons(TIME_PORT);

	// Connect to server.
	if (SOCKET_ERROR == connect(connSocket, (SOCKADDR*)&server, sizeof(server)))
	{
		cout << "Time Client: Error at connect(): " << WSAGetLastError() << endl;
		closesocket(connSocket);
		WSACleanup();
		return;
	}

	int bytesSent = 0;
	int bytesRecv = 0;
	char sendBuff[255];
	char recvBuff[255];
	int option = 0;

	while (option != 3)
	{
		cout << "\nPlease choose an option:\n";
		cout << "1 - Get current time.\n";
		cout << "2 - Get current time in seconds.\n";
		cout << "3 - Exit.\n";
		cout << ">> ";

		cin >> option;

		if (option == 1)
			strcpy(sendBuff, "TimeString");
		else if (option == 2)
			strcpy(sendBuff, "SecondsSince1970");
		else if (option == 3)
			strcpy(sendBuff, "Exit");

		// The send function sends data on a connected socket.
		bytesSent = send(connSocket, sendBuff, (int)strlen(sendBuff), 0);
		if (SOCKET_ERROR == bytesSent)
		{
			cout << "Time Client: Error at send(): " << WSAGetLastError() << endl;
			closesocket(connSocket);
			WSACleanup();
			return;
		}
		cout << "Time Client: Sent: " << bytesSent << "/" << strlen(sendBuff) << " bytes of \"" << sendBuff << "\" message.\n";

		// Gets the server's answer for options 1 and 2.
		if (option == 1 || option == 2)
		{
			bytesRecv = recv(connSocket, recvBuff, 255, 0);
			if (SOCKET_ERROR == bytesRecv)
			{
				cout << "Time Client: Error at recv(): " << WSAGetLastError() << endl;
				closesocket(connSocket);
				WSACleanup();
				return;
			}
			if (bytesRecv == 0)
			{
				cout << "Server closed the connection\n";
				return;
			}

			recvBuff[bytesRecv] = '\0'; //add the null-terminating to make it a string
			cout << "Time Client: Recieved: " << bytesRecv << " bytes of \"" << recvBuff << "\" message.\n";
		}
		else if (option == 3)
		{
			// Closing connections and Winsock.
			cout << "Time Client: Closing Connection.\n";
			closesocket(connSocket);
			WSACleanup();
		}
	}
}