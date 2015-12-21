#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <Iphlpapi.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <shellapi.h>
#include "zlib.h"
#include "windivert.h"

#define MAXBUF 0xFFFF
#define WM_MYMESSAGE (WM_USER + 1)
#define MAX_LOADSTRING 100
#define CMD_OPEN_MATCH 1001
#define CMD_COPY_LINK 1002
#define CMD_EXIT 1003
#define CMD_OPEN_HOME 1004

typedef DWORD (*GetTcpInfo)(PVOID, PDWORD, BOOL, ULONG, TCP_TABLE_CLASS, ULONG);
static DWORD CaptureThread(LPVOID arg);

HINSTANCE hInst;
TCHAR szTitle[MAX_LOADSTRING];
TCHAR szWindowClass[MAX_LOADSTRING];
char url[MAX_LOADSTRING];
wchar_t *titleWide;
NOTIFYICONDATA nid;
ATOM Class(HINSTANCE hInstance);
HWND InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

unsigned char header[] = {0x52, 0x52, 0xA0, 0x41, 0xFF, 0x5D, 0x46};
UINT lastPacket = 0;
UINT status = 0;

struct FFXIV {
	uint8_t u1[16];
	uint64_t timestamp;
	uint32_t len;
	uint8_t u2[2];
	uint16_t count;
	uint8_t flag1;
	uint8_t compressed;
	uint8_t u3[6];
	unsigned char *data;
};
UINT FFXIVLen = sizeof(struct FFXIV)-sizeof(unsigned char*);

struct FFXIV_msg {
	uint32_t size;
	uint64_t u1;
	uint16_t u2;
	uint16_t type;
};

struct SealRock {
	unsigned char **playerData;
	unsigned char *matchData;
	UINT players;
};

void *trayLoop() {
	HWND hWnd;
	HINSTANCE hInstance = GetModuleHandle(NULL);
	MSG msg;
	char title[] = "XIVPvP v1.0";
	titleWide = (wchar_t*)calloc(strlen(title) + 1, sizeof(wchar_t));
	mbstowcs(titleWide, title, strlen(title));
	wcscpy((wchar_t*)szTitle, titleWide);
	wcscpy((wchar_t*)szWindowClass, (wchar_t*)TEXT("class"));
	Class(hInstance);
	hWnd = InitInstance(hInstance, FALSE);
	if (!hWnd) {
		exit(1);
	}
	HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = 100;
	nid.uCallbackMessage = WM_MYMESSAGE;
	nid.hIcon = hIcon;
	strcpy(nid.szTip, title);
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	Shell_NotifyIcon(NIM_ADD, &nid);
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return NULL;
}

ATOM Class(HINSTANCE hInstance) {
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = 0;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	return RegisterClassEx(&wcex);
}

HWND InitInstance(HINSTANCE hInstance, int nCmdShow) {
	HWND hWnd;
	hInst = hInstance;
	hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	if (!hWnd) {
		return 0;
	}
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	return hWnd;
}

void ShowMenu(HWND hWnd) {
	HMENU hSubMenu = CreatePopupMenu();
	POINT p;
	GetCursorPos(&p);
	hSubMenu = CreatePopupMenu();
	AppendMenuW(hSubMenu, MF_STRING, CMD_OPEN_HOME, titleWide);
	switch (status) {
		case 0:
			AppendMenuW(hSubMenu, MF_STRING | MF_GRAYED, 0, L"status: FFXIV not running");
		break;
		case 1:
			AppendMenuW(hSubMenu, MF_STRING | MF_GRAYED, 0, L"status: capturing packets");
		break;
	}
	if(strlen(url)) {
		AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
		AppendMenuW(hSubMenu, MF_STRING, CMD_OPEN_MATCH, L"View last match");
		AppendMenuW(hSubMenu, MF_STRING, CMD_COPY_LINK, L"Copy link");
	}
	AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(hSubMenu, MF_STRING, CMD_EXIT, L"Exit");
	SetForegroundWindow(hWnd);
	TrackPopupMenu(hSubMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hWnd, NULL);
}

void Exit() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
	PostQuitMessage(0);
	exit(1);
}

void OpenHome() {
	ShellExecuteA(NULL, "open", "https://xivpvp.com/", NULL, NULL, SW_SHOWDEFAULT);
}

void OpenMatch() {
	if(strlen(url)) {
		ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWDEFAULT);
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case CMD_EXIT:
					Exit();
					break;
				case CMD_OPEN_HOME:
					OpenHome();
					break;
				case CMD_OPEN_MATCH:
					OpenMatch();
					break;
				case CMD_COPY_LINK: {
						const size_t len = strlen(url) + 1;
						HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
						memcpy(GlobalLock(hMem), url, len);
						GlobalUnlock(hMem);
						OpenClipboard(0);
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hMem);
						CloseClipboard();
					}
				break;
			}
			break;
		case WM_DESTROY:
			Exit();
			break;
		case WM_MYMESSAGE:
			switch(lParam) {
				case WM_RBUTTONUP:
					ShowMenu(hWnd);
					break;
				case WM_LBUTTONUP:
					if(strlen(url)) {
						OpenMatch();
					} else {
						ShowMenu(hWnd);
					}
					break;
				default:
					return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

UINT SendData(char *location, char *data) {
	struct hostent *apiServer;
	struct sockaddr_in sock;
	char postStr[MAXBUF];
	char receiveStr[MAXBUF];
	WSADATA wsaData;
	SOCKET s;
	ssize_t n;
	sprintf(postStr, "POST /%s/ HTTP/1.0\r\nContent-Type: application/x-www-form-urlencoded\r\nUser-Agent: XIVPvP\r\nHost: client.xivpvp.com\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s", location, (unsigned)strlen(data), data);
	if(WSAStartup(0x0101, &wsaData) !=0) {
		return 0;
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	if(s == INVALID_SOCKET) {	
		return 0;
	}
	if((apiServer = gethostbyname("client.xivpvp.com")) == NULL) {
		return 0;
	}
	memcpy(&sock.sin_addr.s_addr, apiServer->h_addr, apiServer->h_length);
	sock.sin_family = AF_INET;
	sock.sin_port = htons(80);
	if((connect(s, (struct sockaddr *)&sock, sizeof(sock)))){
		return 0;
	}
	if(send(s, postStr , strlen(postStr), 0) == -1) {
		return 0;
	}
	while ((n = recv(s, receiveStr, MAXBUF, 0)) > 0) {
		receiveStr[n] = '\0';
	}
	if(strstr(receiveStr, "OKAY")) {
		return 1;
	}
	closesocket(s);
	WSACleanup();
	return 0;
}

void *SealRock(void *arguments) {
	struct SealRock *args = arguments;
	char buf[MAXBUF];
	UINT i, p;
	uint32_t timestamp;
	// send raw packet in hexadecimal
	sprintf(buf, "m=");
	for(i = 0; i < 64; i++) {
		sprintf(buf+strlen(buf), "%02x", args->matchData[i]);
	}
	memcpy(&timestamp, args->matchData, 4);
	free(args->matchData);
	for(p = 0; p < args->players; p++) {
		sprintf(buf+strlen(buf), "&p[%d]=", p);
		for(i = 0; i < 72; i++) {
			if(i > 30 && args->playerData[p][i] == 0x00) {
				break;
			}
			sprintf(buf+strlen(buf), "%02x", args->playerData[p][i]);
		}
		free(args->playerData[p]);
	}
	free(args->playerData);
	free(args);
	if(SendData("frontline", buf)) {
		snprintf(url, 46, "https://xivpvp.com/frontline/match/%d", timestamp);
	}
	return NULL;
}

void *WolvesDen(void *arguments) {
	unsigned char *data = arguments;
	char buf[MAXBUF];
	UINT i;
	uint32_t timestamp;
	sprintf(buf, "m=");
	// just send raw data, no point in making a struct yet in the client
	// we're still discovering how the packet is organized
	for(i = 0; i < 544; i++) {
		sprintf(buf+strlen(buf), "%02x", data[i]);
	}
	memcpy(&timestamp, data, 4);
	free(data);
	if(SendData("wolvesden", buf)) {
		snprintf(url, 46, "https://xivpvp.com/wolvesden/match/%d", timestamp);
	}
	return NULL;
}

int UncompressData(const unsigned char* abSrc, UINT nLenSrc, unsigned char* abDst, UINT nLenDst) {
	z_stream zInfo = {0};
	zInfo.total_in = zInfo.avail_in = nLenSrc;
	zInfo.avail_out = nLenDst;
	zInfo.next_in = (unsigned char*)abSrc;
	zInfo.next_out = abDst;
	int nErr, nRet = -1;
	nErr = inflateInit(&zInfo);
	if(nErr == Z_OK) {
		nErr= inflate( &zInfo, Z_FINISH);
		if(nErr == Z_STREAM_END) {
			nRet= zInfo.total_out;
		}
	}
	inflateEnd(&zInfo);
	return(nRet);
}

UINT ProcessBuffer(unsigned char *buf, UINT bufLen) {
	// packets we're interested in are way larger
	if(bufLen < 500) { 
		return 0;
	}
	unsigned char *tempData;
	struct FFXIV packet;
	struct FFXIV_msg msg;
	struct SealRock *SealRockArgs = NULL;
	uint8_t *msgPos;
	UINT i, k, found, dataLen;
	UINT players = 0;
	pthread_t pth;
	//printf("\nbuffer length %d\n", bufLen);
	// search for header
	for(i = 0; i < bufLen - sizeof(header); i++) {
		found = 0;
		for(k = 0; k < sizeof(header); k++) {
			if(header[k] != buf[i + k]) {
				break;
			}
			found++;
		}
		if(found == sizeof(header)) {
			// fragmented packet without enough length for a header
			if(bufLen-i < FFXIVLen) {
				return bufLen-i;
			}
			memcpy(&packet, buf+i, FFXIVLen);
			// large, fragmented packet
			if(packet.len > bufLen-i) {
				return bufLen-i;
			}
			dataLen = packet.len - FFXIVLen;
			packet.data = malloc(dataLen);
			memcpy(packet.data, buf+(i+FFXIVLen), dataLen);
			if(packet.compressed) {
				tempData = malloc(MAXBUF);
				dataLen = UncompressData(packet.data, dataLen, tempData, MAXBUF);
				if(dataLen == -1) {
					//printf("-- error decoding packet\n");
					free(packet.data);
					i += packet.len-1;
					continue;
				}
				free(packet.data);
				packet.data = tempData;
				//printf("inflated packet with %d messages, %d bytes\n", packet.count, dataLen);
			}
			// a network packet can have multiple "packets", and a "packet" can have multiple messages
			msgPos = packet.data;
			while(packet.count--) {
				memcpy(&msg, msgPos, sizeof(struct FFXIV_msg));
				//printf("decoded message 0x%04x\n", msg.type);
				// Frontline player message
				if(msg.type == 0x0355) {
					if(SealRockArgs == NULL) {
						SealRockArgs = malloc(sizeof(struct SealRock));
						SealRockArgs->playerData = malloc(72*72);
					}
					SealRockArgs->playerData[players] = malloc(72);
					memcpy(SealRockArgs->playerData[players], msgPos+32, msg.size-32);
					players++;
				}
				// Frontline summary message, always comes after player messages
				if(msg.type == 0x0354 && SealRockArgs != NULL) {
					SealRockArgs->players = players;
					SealRockArgs->matchData = malloc(100);
					memcpy(SealRockArgs->matchData, msgPos+32, msg.size-32);
					pthread_create(&pth, NULL, SealRock, (void *)SealRockArgs);
				}
				// Wolves den player + summary message, fixed length 544
				if(msg.type == 0x2df) {
					unsigned char *WDPacket = malloc(544);
					memcpy(WDPacket, msgPos+32, 544);
					pthread_create(&pth, NULL, WolvesDen, (void *)WDPacket);
				}
				msgPos += msg.size;
			}
			free(packet.data);
			i += packet.len-1;
		}
	}
	return 0;
}

static DWORD CaptureThread(LPVOID arg) {
	WINDIVERT_ADDRESS addr;
	HANDLE handle = (HANDLE)arg;
	UINT8 packet[MAXBUF];
	UINT8 buffer[MAXBUF];
	PVOID payload;
	UINT packetLen, fragmentedLen, payloadLen;
	UINT bufferLen = 0;
	PWINDIVERT_TCPHDR tcpHeader;
	while(TRUE) {
		lastPacket = (UINT)time(NULL);
		if(!WinDivertRecv(handle, packet, sizeof(packet), &addr, &packetLen)) {
			if(GetLastError() == 6) {
				lastPacket = 0;
				break;
			}
			continue;
		}
		WinDivertHelperParsePacket(packet, packetLen, NULL, NULL, NULL, NULL, &tcpHeader, NULL, &payload, &payloadLen);
		if(tcpHeader->Psh == 1) {
			if(bufferLen == 0) {
				ProcessBuffer(payload, payloadLen);
			} else {
				memcpy(buffer+bufferLen, payload, payloadLen);
				bufferLen += payloadLen;
				fragmentedLen = ProcessBuffer(buffer, bufferLen);
				// a large packet can have PSH true and still be fragmented :/
				if(fragmentedLen != 0) {
					memmove(buffer, buffer+(bufferLen-fragmentedLen), fragmentedLen);
					bufferLen = fragmentedLen;
				} else {
					bufferLen = 0;
				}
			}
		} else {
			// Some packets are large and have PSH false, buffer it for the next packet
			memcpy(buffer+bufferLen, payload, payloadLen);
			bufferLen += payloadLen;
		}
	}
	return NULL;
}

void GetPid(UINT *pid) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 process;
	ZeroMemory(&process, sizeof(process));
	process.dwSize = sizeof(process);
	if(Process32First(snapshot, &process)) {
		do {
			if(strcmp("ffxiv_dx11.exe", process.szExeFile) == 0 || strcmp("ffxiv.exe", process.szExeFile) == 0) {
				*pid = process.th32ProcessID;
				break;
			}
		} while(Process32Next(snapshot, &process));
	}
	CloseHandle(snapshot);
}

int BuildCaptureRule(UINT pid, char *str, size_t len) {
	unsigned char buf[MAXBUF];
	struct in_addr IpAddr;
	UINT i;
	MIB_TCPTABLE_OWNER_PID *ret;
	GetTcpInfo gettcp;
	HMODULE iphlp;
	DWORD dwSize = 0x1000;
	iphlp = LoadLibrary("iphlpapi.dll");
	gettcp = (GetTcpInfo) GetProcAddress(iphlp, "GetExtendedTcpTable");
	gettcp(buf, &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
	ret = (MIB_TCPTABLE_OWNER_PID *)buf;
	for(i = 0; i < ret->dwNumEntries; i++) {
		if(ret->table[i].dwOwningPid == pid) {
			IpAddr.S_un.S_addr = (u_long) ret->table[i].dwRemoteAddr;
			// we want only incoming packets to the client
			snprintf(str, len, "inbound and tcp.PayloadLength > 0 and ip.SrcAddr == %s", inet_ntoa(IpAddr));
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	HANDLE handle = NULL;
	char rule[256];
	UINT pid = 0;
	pthread_t tray;
	pthread_create(&tray, NULL, trayLoop, NULL);
	while(TRUE) {
		if((UINT)time(NULL) - lastPacket > 30) {
			if(lastPacket > 0) {
				lastPacket = 0;
				pid = 0;
				status = 0;
				if(!WinDivertClose(handle)) {
					//printf("error: failed to terminate old capture (%ld)\n", GetLastError());
				}
			}
			GetPid(&pid);
			if(pid != 0 && BuildCaptureRule(pid, rule, sizeof(rule)) != 0) {
				//printf("PID: %d, capture rule: %s\n", pid, rule);
				handle = WinDivertOpen(rule, WINDIVERT_LAYER_NETWORK, 0, WINDIVERT_FLAG_SNIFF);
				if(handle == INVALID_HANDLE_VALUE) {
					MessageBox(NULL, "Failed to open capture device.\nMake sure you running as an Administrator.", "Error", MB_ICONWARNING);
					Exit();
				}
				CreateThread(NULL, 1, (LPTHREAD_START_ROUTINE)CaptureThread, (LPVOID)handle, 0, NULL);
				status = 1;
			}
		}
		sleep(5);
	}
}
