/*
* Copyright (c) 2020 Anthony Beaucamp.
*
* This software is provided 'as-is', without any express or implied warranty.
* In no event will the authors be held liable for any damages arising from
* the use of this software.
*
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented * you must not
*   claim that you wrote the original software. If you use this software in a
*   product, an acknowledgment in the product documentation would be
*   appreciated but is not required.
*
*   2. Altered source versions must be plainly marked as such, and must not
*   be misrepresented as being the original software.
*
*   3. This notice may not be removed or altered from any distribution.
*
*   4. The names of this software and/or it's copyright holders may not be
*   used to endorse or promote products derived from this software without
*   specific prior written permission.
*/

#define _AFXDLL
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <afxwin.h>
#include <atlstr.h>
#include <winsock.h>
#include "hub.h"

unsigned char hubJoys[4] = { 255, 255, 255, 255 };
unsigned char hubMouse[2] = { 80, 100 };
packet_t* packetHead = NULL;
unsigned char packetID = 0;
boolean socketReady = false;
char* localip;
struct sockaddr_in tcpServer[HUB_SLOTS], tcpClient[HUB_SLOTS];
struct sockaddr_in udpServer[HUB_SLOTS], udpClient[HUB_SLOTS];
SOCKET tcpSocket[HUB_SLOTS] = { NULL };
SOCKET udpSocket[HUB_SLOTS] = { NULL };
int tcpLen[HUB_SLOTS], udpLen[HUB_SLOTS];
unsigned char tcpSlot = 0, udpSlot = 0;
SOCKET webSocket[2] = { NULL };	 // Server and Client
unsigned char webRxBuffer[256], webTxBuffer[65792];
unsigned int webhubLen, webcomLen, webTimeout;
boolean webBusy = false;
clock_t webTimer;
CFile hubFile[HUB_FILES];

// Hub timers
int	mNetworkEnable;
int	mMouseEnable;

// Hub stats
unsigned long mHubRX, mHubTX, mHubBAD;

////////////////////////////////
//      Control functions     //
////////////////////////////////
void HubFetchControls(unsigned char* joy1, unsigned char* joy2) {
	// Encode Joystick States
	hubJoys[0] = 255;
	if (joy1[0]) hubJoys[0] &= ~HUB_JOY_UP;
	if (joy1[1]) hubJoys[0] &= ~HUB_JOY_DOWN;
	if (joy1[2]) hubJoys[0] &= ~HUB_JOY_LEFT;
	if (joy1[3]) hubJoys[0] &= ~HUB_JOY_RIGHT;
	if (joy1[4]) hubJoys[0] &= ~HUB_JOY_FIRE1;
	hubJoys[1] = 255;
	if (joy2[0]) hubJoys[1] &= ~HUB_JOY_UP;
	if (joy2[1]) hubJoys[1] &= ~HUB_JOY_DOWN;
	if (joy2[2]) hubJoys[1] &= ~HUB_JOY_LEFT;
	if (joy2[3]) hubJoys[1] &= ~HUB_JOY_RIGHT;
	if (joy2[4]) hubJoys[1] &= ~HUB_JOY_FIRE1;

	// Temporary measure: Use joystick #1 also for mouse cursor
	if (joy1[0]) { if (hubMouse[1] > 2) { hubMouse[1] -= 3; } }
	if (joy1[1]) { if (hubMouse[1] < 197) { hubMouse[1] += 3; } }
	if (joy1[2]) { if (hubMouse[0] > 1) { hubMouse[0] -= 2; } }
	if (joy1[3]) { if (hubMouse[0] < 158) { hubMouse[0] += 2; } }
	if (joy1[4]) { hubJoys[0] &= ~HUB_MOUSE_LEFT; }
	else { hubJoys[0] |= HUB_MOUSE_LEFT; }
}

////////////////////////////////
//      PACKET functions      //
////////////////////////////////

void HubPushPacket(unsigned char cmd, signed char slot, unsigned char* data, unsigned char len) {
	// Create new packet
	packet_t *packet = (packet_t*)malloc(sizeof(packet_t));
	packet->next = NULL;

	// Assign ID & Timeout
	if (++packetID>15) { packetID = 1; }
	packet->ID = packetID;
	packet->timeout = (clock() * 1000) / CLOCKS_PER_SEC + HUB_TIMEOUT;

	// Copy data to packet
	packet->len = len+2;
	packet->data = (unsigned char*)malloc(len+2);
	packet->data[0] = cmd;
	packet->data[1] = slot;
	memcpy(&packet->data[2], data, len);

	// Append packet at packetTail of linked list
	if (!packetHead) {
		packetHead = packet;
	}
	else {
		packet_t *packetTail = packetHead;
		while (packetTail->next != NULL) {
			packetTail = packetTail->next;
		}
		packetTail->next = packet;
	}
}

void HubPopPacket(unsigned char ID) {
	// Remove packet at head of linked list
	if (packetHead && packetHead->ID == ID) {
		packet_t* next = packetHead->next;
		free(packetHead->data);
		free(packetHead);
		packetHead = next;
	}
}

void HubTimeoutPacket(void) {
	// Remove packets that have exceeded timeout
	while (packetHead && ((clock() * 1000) / CLOCKS_PER_SEC) > packetHead->timeout) {
		HubPopPacket(packetHead->ID);
	}
}

void HubReceiveNetwork(void) {
	unsigned char buffer[HUB_PACKET]; int len;
	unsigned long available;

	// Check for incoming UDP packets
	for (char i = 0; i<HUB_SLOTS; i++) {
		if (udpSocket[i]) {
			while ((len = recvfrom(udpSocket[i], (char*)buffer, 256, 0, (struct sockaddr *)&udpServer[i], &udpLen[i])) && len > 0) {
				// Store data into packet
				HubPushPacket(HUB_UDP_RECV, i, buffer, len);
			}
		}
	}

	// Check for incoming TCP packets
	for (char i = 0; i<HUB_SLOTS; i++) {
		if (tcpSocket[i]) {
			// Check if there is any data (recv() is blocking)
			ioctlsocket(tcpSocket[i], FIONREAD, &available);
			if (available) {
				// Store data into packet
				len = recv(tcpSocket[i], (char*)buffer, 256, 0);
				HubPushPacket(HUB_TCP_RECV, i, buffer, available);
			}
		}
	}

	// Check for incoming WEB packets
	if (webSocket[0]) {
		// If socket not open, look for new client
		if (!webSocket[1]) {
			webSocket[1] = accept(webSocket[0], NULL, NULL);
			if (webSocket[1] == INVALID_SOCKET) {
				webSocket[1] = 0;
			}
			else {
				webTimer = clock() + webTimeout;
				webRxBuffer[0] = 0;
				webhubLen = 0;
				webBusy = false;
			}
		}

		// If socket open, check timeout and process incoming data
		if (webSocket[1]) {
			if (clock() > webTimer) {
				closesocket(webSocket[1]);
				webSocket[1] = 0;
				webhubLen = 0;
				webBusy = false;
			}
			else
			if (!webBusy) {
				len = recv(webSocket[1], (char*)buffer, 256, 0);
				if (len > 0) {
					for (unsigned int c = 0; c < len; c++) {
						if (buffer[c] == '\n' || buffer[c] == '\r') {
							// Forward request?
							if (!strncmp((char*)webRxBuffer, "GET", 3)) {
								webRxBuffer[webhubLen++] = 0;
								HubPushPacket(HUB_WEB_RECV, -1, webRxBuffer, webhubLen);
								webBusy = true;
							}
							webRxBuffer[0] = 0;
							webhubLen = 0;
						}
						else {
							webRxBuffer[webhubLen++] = buffer[c];
						}
					}
				}
			}
		}
	}
}

//////////////////////////////
//		HUB Commands		//
//////////////////////////////

extern "C" unsigned char* HubTxCallback(unsigned char data, unsigned char* joy1, unsigned char* joy2, unsigned char* out);

WSADATA wsaData;	// Used to open Windows connection

unsigned char* HubTxCallback(unsigned char data, unsigned char* joy1, unsigned char* joy2, unsigned char* out)
{
	static unsigned char rcvLen, comLen, comBuffer[256], hubLen, *hubBuffer, outLen, outBuffer[256];
	static unsigned char hasHeader, hasID, hasLen, comID = 0, hubID = 0;
	unsigned char checksum, i;

	int socket_buffer_size = 65536;
	u_long nonblocking_enabled = TRUE;
	static WSADATA w;	// Used to open Windows connection
	CString filepath;

	// Check for incoming packets
	HubReceiveNetwork();

	// Check header
	if (!hasHeader) {
		switch (data) {
		case 85:
			*out = outLen;
			return outBuffer;
		case 170:
			hasHeader = 1;
			return 0;
		default:
			return 0;
		}
	}

	// Check ID
	if (!hasID) {
		comID = data;
		hasID = 1;
		return 0;
	}

	// Check for length
	if (!hasLen) {
		comLen = data;
		hasLen = 1;
		rcvLen = 0;
		return 0;
	}

	// Add data to buffer
	comBuffer[rcvLen++] = data;

	// Check if packet was fully received (including extra byte for checksum)
	if (rcvLen < comLen+1) { return 0; }

	// Reset state
	hasHeader = 0;
	hasID = 0;
	hasLen = 0;

	// Verify checksum
	checksum = comID;
	for (unsigned char i = 0; i<comLen; i++)
		checksum += comBuffer[i];
	if (comBuffer[comLen] != checksum) {
		mHubBAD++; return 0;
	}

	// Try to pop last packet
	HubPopPacket(comID >> 4);

	// Fetch Joy/Mouse data
	HubFetchControls(joy1, joy2);

	// Process received data
	unsigned int offset;
	unsigned long length;
	unsigned char count, buffer[HUB_PACKET], slot, len = 0;
	struct sockaddr_in webServer;
	struct hostent *phe;
	struct in_addr addr;
	if (comLen) {
		// Record stats
		mHubTX++;

		// Check command code
		switch (comBuffer[0]) {
		case HUB_SYS_RESET:
			// Reset sockets
			for (char i=0; i<HUB_SLOTS; i++) {
				if (udpSocket[i]) {
					closesocket(udpSocket[i]);
					udpSocket[i] = 0;
				}
				if (tcpSocket[i]) {
					closesocket(tcpSocket[i]);
					tcpSocket[i] = 0;
				}
			}
			if (socketReady) WSACleanup();
			WSAStartup(0x0101, &wsaData);
			socketReady = true;

			// Reset packets, files and counters
			while (packetHead) {
				HubPopPacket(packetHead->ID);
			}
			for (i = 0; i < HUB_FILES; i++) {
				if (hubFile[i].m_hFile != CFile::hFileNull) {
					hubFile[i].Close();
				}
			}
			mHubBAD = 0;
			packetID = 0;

			// Get local ip address
			localip = "\0";
			if (gethostname((char*)buffer, sizeof(buffer)) == SOCKET_ERROR) break;
			phe = gethostbyname((char*)buffer);
			if (phe == 0) break;
			i = 0; while (phe->h_addr_list[i] != 0)
				memcpy(&addr, phe->h_addr_list[i++], sizeof(struct in_addr));
			localip = inet_ntoa(addr);
			break;

		case HUB_SYS_IP:
			HubPushPacket(HUB_SYS_IP, -1, (unsigned char*)localip, strlen(localip));
			break;

		case HUB_DIR_LS:
			// List current directory
			HANDLE hFind;
			WIN32_FIND_DATA FindData;
			hFind = FindFirstFile("microSD\\*.*", &FindData);	// .
			FindNextFile(hFind, &FindData);										// ..
			count = 0; len = 1;
			while (count < comBuffer[1] && FindNextFile(hFind, &FindData)) {
				memcpy(&buffer[len], (unsigned char*)FindData.cFileName, strlen(FindData.cFileName));
				len += strlen(FindData.cFileName);
				buffer[len++] = 0;
				buffer[len++] = (FindData.nFileSizeLow & 0xff);
				buffer[len++] = (FindData.nFileSizeLow >> 8);
				count++;
			}
			buffer[0] = count;
			HubPushPacket(HUB_DIR_LS, -1, buffer, len);
			FindClose(hFind);
			break;

		case HUB_FILE_OPEN:
			// Check if file was previously opened
			if (hubFile[comBuffer[1]].m_hFile != CFile::hFileNull) {
				hubFile[comBuffer[1]].Close();
			}

			// Open file (modes are 0:read, 1:write, 2:append)
			filepath = "microSD\\";
			filepath.Append((const char*)&comBuffer[3]);
			switch (comBuffer[2]) {
			case 0:
				hubFile[comBuffer[1]].Open(filepath, CFile::modeRead);
				break;
			case 1:
				hubFile[comBuffer[1]].Open(filepath, CFile::modeCreate | CFile::modeWrite);
				break;
			case 2:
				hubFile[comBuffer[1]].Open(filepath, CFile::modeWrite);
				hubFile[comBuffer[1]].SeekToEnd();
				break;
			}

			// Send back file size
			length = hubFile[comBuffer[1]].GetLength();
			memcpy(buffer, (char*)&length, 4);
			HubPushPacket(HUB_FILE_OPEN, comBuffer[1], buffer, 4);
			break;

		case HUB_FILE_SEEK:
			// Seek file position (offset from beginning)
			offset = (comBuffer[3] * 256) + comBuffer[2];
			if (hubFile[comBuffer[1]].m_hFile != CFile::hFileNull) {
				hubFile[comBuffer[1]].Seek(offset, CFile::begin);
			}
			break;

		case HUB_FILE_READ:
			// Read from file
			slot = comBuffer[1];
			if (hubFile[comBuffer[1]].m_hFile != CFile::hFileNull) {
				if ((len = hubFile[comBuffer[1]].Read(buffer, comBuffer[2])) && len > 0) {
					HubPushPacket(HUB_FILE_READ, slot, buffer, len);
				}
			}
			break;

		case HUB_FILE_WRITE:
			// Write to file
			if (hubFile[comBuffer[1]].m_hFile != CFile::hFileNull) {
				hubFile[comBuffer[1]].Write(&comBuffer[2], comLen - 3);
			}
			break;

		case HUB_FILE_CLOSE:
			// Close file
			if (hubFile[comBuffer[1]].m_hFile != CFile::hFileNull) {
				hubFile[comBuffer[1]].Close();
			}
			break;

		case HUB_UDP_SLOT:
			udpSlot = comBuffer[1];
			break;

		case HUB_TCP_SLOT:
			tcpSlot = comBuffer[1];
			break;

		case HUB_UDP_OPEN:
			// Open a datagram socket
			slot = udpSlot;
			udpSocket[slot] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (udpSocket[slot] == INVALID_SOCKET) {
				break;
			}

			// Set non-blocking and buffer size
			ioctlsocket(udpSocket[slot], FIONBIO, &nonblocking_enabled);
			if ((setsockopt(udpSocket[slot], SOL_SOCKET, SO_RCVBUF, (const char *)&socket_buffer_size, sizeof(int))) < 0) {
				closesocket(udpSocket[slot]);
				udpSocket[slot] = 0;
				break;
			}

			// Set server settings
			ZeroMemory(&udpServer[slot], sizeof(udpServer[slot]));
			udpServer[slot].sin_family = AF_INET;
			udpServer[slot].sin_addr.S_un.S_un_b.s_b1 = comBuffer[1];
			udpServer[slot].sin_addr.S_un.S_un_b.s_b2 = comBuffer[2];
			udpServer[slot].sin_addr.S_un.S_un_b.s_b3 = comBuffer[3];
			udpServer[slot].sin_addr.S_un.S_un_b.s_b4 = comBuffer[4];
			udpServer[slot].sin_port = htons(comBuffer[5] + comBuffer[6] * 256);

			// Set client settings
			memset((void *)&udpClient[slot], '\0', sizeof(struct sockaddr_in));
			udpClient[slot].sin_family = AF_INET;
			udpClient[slot].sin_addr.s_addr = htonl(INADDR_ANY);
			udpClient[slot].sin_port = htons(comBuffer[7] + comBuffer[8] * 256);

			// Bind local address to socket
			if (bind(udpSocket[slot], (struct sockaddr*)&udpClient[slot], sizeof(udpClient[slot])) == -1) {
				closesocket(udpSocket[slot]);
				udpSocket[slot] = 0;
				break;
			}

			break;

		case HUB_TCP_OPEN:
			// Open a datagram socket
			slot = tcpSlot;
			tcpSocket[slot] = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
			if (tcpSocket[slot] == INVALID_SOCKET) {
				break;
			}

			// Set server settings
			ZeroMemory(&tcpServer[slot], sizeof(tcpServer[slot]));
			tcpServer[slot].sin_family = AF_INET;
			tcpServer[slot].sin_addr.S_un.S_un_b.s_b1 = comBuffer[1];
			tcpServer[slot].sin_addr.S_un.S_un_b.s_b2 = comBuffer[2];
			tcpServer[slot].sin_addr.S_un.S_un_b.s_b3 = comBuffer[3];
			tcpServer[slot].sin_addr.S_un.S_un_b.s_b4 = comBuffer[4];
			tcpServer[slot].sin_port = htons(comBuffer[5] + comBuffer[6] * 256);

			// Try to connect
			if (connect(tcpSocket[slot], (struct sockaddr *)&tcpServer[slot], sizeof(struct sockaddr_in)) < 0) {
				closesocket(tcpSocket[slot]);
				tcpSocket[slot] = 0;
				break;
			}

			break;

		case HUB_WEB_OPEN:
			// Open a datagram socket
			webSocket[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (webSocket[0] == INVALID_SOCKET) {
				break;
			}

			// Set non-blocking and time-out
			ioctlsocket(webSocket[0], FIONBIO, &nonblocking_enabled);
			webTimeout = comBuffer[3] + comBuffer[4] * 256;
			webBusy = false;

			// Set server settings
			memset(&webServer, 0, sizeof(webServer));
			webServer.sin_family = AF_INET;
			webServer.sin_addr.s_addr = inet_addr(localip);
			webServer.sin_port = htons(comBuffer[1] + comBuffer[2] * 256);

			// Bind and setup listener
			if (bind(webSocket[0], (SOCKADDR *)&webServer, sizeof(webServer)) == SOCKET_ERROR) {
				closesocket(webSocket[0]);
				webSocket[0] = 0;
				break;
			}
			if (listen(webSocket[0], 1) == SOCKET_ERROR) {
				closesocket(webSocket[0]);
				webSocket[0] = 0;
				break;
			}

			break;

		case HUB_UDP_SEND:
			// Send packet to server
			slot = udpSlot;
			if (udpSocket[slot] > 0) {
				udpLen[slot] = sizeof(struct sockaddr_in);
				if (sendto(udpSocket[slot], (char*)&comBuffer[1], (int)(comLen - 1), 0, (struct sockaddr*)&udpServer[slot], udpLen[slot]) == -1) {
					closesocket(udpSocket[slot]);
					udpSocket[slot] = 0;
				}
			}
			break;

		case HUB_TCP_SEND:
			// Send packet to server
			slot = tcpSlot;
			if (tcpSocket[slot] > 0) {
				if (send(tcpSocket[slot], (char*)&comBuffer[1], (int)(comLen - 1), 0) == -1) {
					closesocket(tcpSocket[slot]);
					tcpSocket[slot] = 0;
				}
			}
			break;

		case HUB_WEB_HEADER:
			// Add header to contents
			if (webSocket[1] > 0) {
				memcpy((char*)&webTxBuffer[webcomLen], "HTTP/1.1 200 OK\r\nConnection: close\r\n", 36); webcomLen += 36;
				memcpy((char*)&webTxBuffer[webcomLen], (char*)&comBuffer[1], comLen - 1); webcomLen += (comLen - 1);
				memcpy((char*)&webTxBuffer[webcomLen], (char*)"\r\n\r\n", 4); webcomLen += 4;
				//send(webSocket[1], (char*)webTxBuffer, (int)webcomLen, 0);
				//webcomLen = 0;
			}
			break;

		case HUB_WEB_BODY:
			// Add body to contents
			if (webSocket[1] > 0) {
				memcpy((char*)&webTxBuffer[webcomLen], (char*)&comBuffer[1], comLen - 1); webcomLen += (comLen - 1);
				//send(webSocket[1], (char*)webTxBuffer, (int)webcomLen, 0);
				//webcomLen = 0;
			}
			break;

		case HUB_WEB_SEND:
			// Send to client and close connection
			if (webSocket[1] > 0) {
				memcpy((char*)&webTxBuffer[webcomLen], (char*)"\r\n\r\n", 4); webcomLen += 4;
				send(webSocket[1], (char*)webTxBuffer, (int)webcomLen, 0);
				webcomLen = 0;
				webBusy = false;
			}
			break;

		case HUB_UDP_CLOSE:
			slot = udpSlot;
			if (udpSocket[slot] > 0) {
				closesocket(udpSocket[slot]);
				udpSocket[slot] = 0;
			}
			break;

		case HUB_TCP_CLOSE:
			slot = tcpSlot;
			if (tcpSocket[slot] > 0) {
				closesocket(tcpSocket[slot]);
				tcpSocket[slot] = 0;
			}
			break;

		case HUB_WEB_CLOSE:
			// Close both incoming and outgoing sockets
			if (webSocket[0] > 0) {
				closesocket(webSocket[0]);
				webSocket[0] = 0;
			}
			if (webSocket[1] > 0) {
				closesocket(webSocket[1]);
				webSocket[1] = 0;
			}
			break;
		}
	}

	// Fetch next packet
	packet_t *packet = packetHead;
	if (packet) {
		hubID = packet->ID;
		hubLen = packet->len;
		hubBuffer = packet->data;
		mHubRX++;
	}
	else {
		hubLen = 0;
	}

	// Encode RX/TX ID
	unsigned char packetID = 0;
	packetID = (hubID << 4) + (comID & 0x0f);

	// Compute Checksum
	checksum = packetID;
	checksum += hubJoys[0];
	checksum += hubJoys[1];
	checksum += hubJoys[2];
	checksum += hubJoys[3];
	checksum += hubMouse[0];
	checksum += hubMouse[1];
	for (unsigned char i = 0; i<hubLen; i++)
		checksum += hubBuffer[i];

	// Prepare rx data
	outLen = 0;
	outBuffer[outLen++] = 170;
	outBuffer[outLen++] = packetID;
	outBuffer[outLen++] = hubJoys[0];
	outBuffer[outLen++] = hubJoys[1];
	outBuffer[outLen++] = hubJoys[2];
	outBuffer[outLen++] = hubJoys[3];
	outBuffer[outLen++] = hubMouse[0];
	outBuffer[outLen++] = hubMouse[1];
	outBuffer[outLen++] = hubLen;
	for (i=0; i<hubLen; i++)
		outBuffer[outLen++] = hubBuffer[i];
	outBuffer[outLen++] = checksum;

	// Timeout packets
	HubTimeoutPacket();
	return 0;
}

