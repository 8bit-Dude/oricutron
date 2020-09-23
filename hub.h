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

// 8bit-Hub Communication
#define HUB_TIMEOUT    1000  // Milliseconds
#define HUB_FILES		 16	 // Number of handles
#define HUB_SLOTS		 16	 // Number of tcp/udp handles
#define HUB_PACKET		256  // Max. byte size of packet

// 8bit-Hub Commands
#define HUB_SYS_ERROR     0
#define HUB_SYS_RESET     1
#define HUB_SYS_NOTIF     2
#define HUB_SYS_SCAN      3
#define HUB_SYS_CONNECT   4
#define HUB_SYS_IP        5
#define HUB_SYS_MOUSE     6
#define HUB_SYS_VERSION   7
#define HUB_SYS_UPDATE    8
#define HUB_DIR_LS       10
#define HUB_DIR_MK       11
#define HUB_DIR_RM       12
#define HUB_DIR_CD       13
#define HUB_FILE_OPEN    21
#define HUB_FILE_SEEK    22
#define HUB_FILE_READ    23
#define HUB_FILE_WRITE   24
#define HUB_FILE_CLOSE   25
#define HUB_UDP_OPEN     30
#define HUB_UDP_RECV     31
#define HUB_UDP_SEND     32
#define HUB_UDP_CLOSE    33
#define HUB_UDP_SLOT     34
#define HUB_TCP_OPEN     40
#define HUB_TCP_RECV     41
#define HUB_TCP_SEND     42
#define HUB_TCP_CLOSE    43
#define HUB_TCP_SLOT     44
#define HUB_WEB_OPEN     50
#define HUB_WEB_RECV     51
#define HUB_WEB_HEADER   52
#define HUB_WEB_BODY     53
#define HUB_WEB_SEND     54
#define HUB_WEB_CLOSE    55
#define HUB_HTTP_GET     60
#define HUB_HTTP_READ    61

// 8bit-Hub Joystick states
#define HUB_JOY_UP      1
#define HUB_JOY_DOWN    2
#define HUB_JOY_LEFT    4
#define HUB_JOY_RIGHT   8
#define HUB_JOY_FIRE1   16
#define HUB_JOY_FIRE2   32
#define HUB_MOUSE_LEFT  64
#define HUB_MOUSE_RIGHT 128

typedef struct packet {
	unsigned char ID;
	unsigned char len;
	unsigned char* data;
	unsigned int timeout;
	struct packet* next;
} packet_t;
