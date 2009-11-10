/* stdint.h */
typedef int uint32_t;
typedef long long uint64_t;

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>
#include <math.h>

#if _DEBUG
#include <iostream>
#endif

#include "../mumble_plugin.h"

HANDLE h;
uint32_t p_playerBase;
uint64_t g_playerGUID;

#define STATIC_REALMNAME  0x0127046E

static DWORD getProcess(const wchar_t *exename) {
	PROCESSENTRY32 pe;
	DWORD pid = 0;

	pe.dwSize = sizeof(pe);
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap != INVALID_HANDLE_VALUE) {
		BOOL ok = Process32First(hSnap, &pe);

		while (ok) {
			if (wcscmp(pe.szExeFile, exename)==0) {
				pid = pe.th32ProcessID;
				break;
			}
			ok = Process32Next(hSnap, &pe);
		}
		CloseHandle(hSnap);
	}
	return pid;
}

static bool peekProc(VOID *base, VOID *dest, SIZE_T len) {
	SIZE_T r;
	BOOL ok=ReadProcessMemory(h, base, dest, len, &r);
	return (ok && (r == len));
}

static void about(HWND h) {
	::MessageBox(h, L"Reads audio position information from World of Warcraft", L"Mumble WoW Plugin", MB_OK);
}

uint32_t getInt32(uint32_t ptr) {
	uint32_t result;
	SIZE_T r;
	BOOL ok=ReadProcessMemory(h, (void *)ptr, &result, sizeof(uint32_t), &r);
	if (ok && (r == sizeof(uint32_t))) {
		return result;
	} else {
		return 0xffffffff;
	}
}

uint64_t getInt64(uint32_t ptr) {
	uint64_t result;
	SIZE_T r;
	BOOL ok=ReadProcessMemory(h, (void *)ptr, &result, sizeof(uint64_t), &r);
	if (ok && (r == sizeof(uint64_t))) {
		return result;
	} else {
		return 0xffffffffffffffff;
	}
}

float getFloat(uint32_t ptr) {
	float result;
	SIZE_T r;
	BOOL ok=ReadProcessMemory(h, (void *)ptr, &result, sizeof(float), &r);
	if (ok && (r == sizeof(float))) {
		return result;
	} else {
		return (float)0xffffffff;
	}
}

int getCStringN(uint32_t ptr, char *buffer, size_t buffersize) {
	SIZE_T r;
	BOOL ok = ReadProcessMemory (h, (void *)ptr, buffer, buffersize, &r);

	/* safety net, just in case we didn't get a string back at all */
	buffer[buffersize-1] = '\0';

	if (ok && (r == buffersize)) {
		return strlen (buffer);
	} else {
		return 0;
	}
}

int getString (uint32_t ptr, std::string &buffer)
{
	char buf[1024];
	int bufLength;

	bufLength = getCStringN (ptr, buf, sizeof(buf));
	buffer = buf;

	return bufLength;
}

int getWString (uint32_t ptr, std::wstring &buffer)
{
	char buf[1024];
	int bufLength;
	wchar_t wbuf[1024];
	int wbufLength;

	bufLength = getCStringN (ptr, buf, sizeof(buf));
	wbufLength = MultiByteToWideChar (CP_UTF8, 0,
		buf, bufLength,
		wbuf, 1024);
	buffer.assign (wbuf, wbufLength);

	return 0;
}


void getDebug16(uint32_t ptr) {
#ifdef _DEBUG
	unsigned char buf[16];
	SIZE_T r;
	BOOL ok=ReadProcessMemory(h, (void *)ptr, &buf, sizeof(buf), &r);
	if (ok && (r == sizeof (buf))) {
		printf ("%08x: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
			ptr,
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7],
			buf[8], buf[9], buf[10], buf[11],
			buf[12], buf[13], buf[14], buf[15]
		);
	}
#endif
}

uint32_t getPlayerBase() {
	uint32_t gClientConnection;
	uint32_t sCurMgr;
	uint32_t curObj;
	uint64_t playerGUID;
	uint32_t playerBase;

	uint32_t nextObj;
	uint64_t GUID;

	playerBase=0;

	gClientConnection=getInt32(0x012705B0);
	sCurMgr=getInt32(gClientConnection + 0x2d94);
	if (sCurMgr != 0) {
		playerGUID=getInt64(sCurMgr+0xC0);
		if (playerGUID != 0) {
			g_playerGUID = playerGUID;
			curObj=getInt32(sCurMgr+0xAC);
			while (curObj != 0) {
				nextObj=getInt32(curObj + 0x3C);
				GUID=getInt64(curObj + 0x30);
				if (playerGUID == GUID) {
					playerBase = curObj;
					break;
				} else if (curObj == nextObj) {
					break;
				} else {
					curObj = nextObj;
				}
			}
		}
	}

	return playerBase;
}

static const unsigned long nameStorePtr        = 0x12542D8 + 0x8;  // 0x012541C8, 0x011AE3D0+8 , 0x00D29BA8+8 , Player name database
static const unsigned long nameMaskOffset      = 0x024;  // Offset for the mask used with GUID to select a linked list
static const unsigned long nameBaseOffset      = 0x01c;  // Offset for the start of the name linked list
static const unsigned long nameStringOffset    = 0x020;  // Offset to the C string in a name structure

void getPlayerName(std::wstring &identity) {
	unsigned long mask, base, offset, current, shortGUID, testGUID;

	mask = getInt32 (nameStorePtr + nameMaskOffset);
	base = getInt32 (nameStorePtr + nameBaseOffset);

	shortGUID = g_playerGUID & 0xffffffff;  // Only half the guid is used to check for a hit
	if (mask == 0xffffffff) {
		identity.clear();
		return;
	}
	offset = 12 * (mask & shortGUID);  // select the appropriate linked list
	current=getInt32(base + offset + 8);   // ptr to lower half of GUID of first element
	offset = getInt32(base + offset);  // this plus 4 is the offset for the next element
	if ((current == 0) || (current & 0x1)) {
		identity.clear();
		return;
	}
	testGUID=getInt32(current);

	while (testGUID != shortGUID) {
		current=getInt32(current + offset + 4);
		if ((current == 0) || (current & 0x1)) {
			identity.clear();
			return;
		}
		testGUID=getInt32(current);
	}
	getWString(current + nameStringOffset, identity);
}

void getRealmName(std::string &context)
{
	getString (STATIC_REALMNAME, context);
}

void getCamera(float camera_pos[3], float camera_front[3], float camera_top[3]) {
	uint32_t ptr1, ptr2;
	float buf[4][3];

	ptr1 = getInt32(0x10e1824);
	ptr2 = getInt32(ptr1+0x7da0);

	peekProc((BYTE *) ptr2+0x08, buf, sizeof(buf));

	/* camera postition */
	camera_pos[0] = -buf[0][1];
	camera_pos[1] =  buf[0][2];
	camera_pos[2] =  buf[0][0];

	/* camera front vector */
	camera_front[0] = -buf[1][1];
	camera_front[1] =  buf[1][2];
	camera_front[2] =  buf[1][0];

	/* camera right vector */
#if 0 /* gets calculated in mumble client */
	camera_right[0] = -buf[2][1];
	camera_right[1] =  buf[2][2];
	camera_right[2] =  buf[2][0];
#endif

	/* camera top vector */
	camera_top[0] = -buf[3][1];
	camera_top[1] =  buf[3][2];
	camera_top[2] =  buf[3][0];
}


typedef class WowData {
	std::wstring nameAvatar;
	bool nameAvatarValid;

	std::string nameRealm;
	bool nameRealmValid;

	uint64_t playerGUID;
	uint32_t pointerPlayerObject;

public:
	WowData::WowData () {
		refresh ();
	}

	void WowData::updateAvatarName () {
		getPlayerName (nameAvatar);
		if (!nameAvatar.empty ()) {
			int temp = nameAvatar.length();
			nameAvatarValid = true;
		} else {
			nameAvatarValid = false;
		}
	}

	void WowData::updateRealmName () {
		getRealmName (nameRealm);
		if (!nameRealm.empty ()) {
			nameRealmValid = true;
		} else {
			nameRealmValid = false;
		}
	}

	std::wstring getNameAvatar () {
		if (!nameAvatarValid) {
			updateAvatarName ();
		}

		return nameAvatar;
	}

	std::string getNameRealm () {
		if (!nameRealmValid) {
			updateRealmName ();
		}

		return nameRealm;
	}

	void refresh () {
		nameAvatarValid = false;
		nameRealmValid = false;
	}
} WowData_t;

WowData_t wow;


static int fetch(float *avatar_pos, float *avatar_front, float *avatar_top, float *camera_pos, float *camera_front, float *camera_top, std::string &context, std::wstring &identity) {
	/* clear position */
	for (int i=0; i<3; i++) {
		avatar_pos[i]=avatar_front[i]=avatar_top[i]=camera_pos[i]=camera_front[i]=camera_top[i]=0.0;
	}

	/* are we still looking at the right object? */
	uint64_t peekGUID, tempGUID;
	peekGUID=getInt64(p_playerBase+0x30);
	if (g_playerGUID != peekGUID) {
		/* no? Try to resynch to the new address. Happens when walking through portals quickly (aka no or short loading screen) */
		tempGUID = g_playerGUID;
		p_playerBase=getPlayerBase();
		if (tempGUID != g_playerGUID) {
			/* GUID of actor changed, likely a character and/or realm change */
			wow.refresh ();
		}
		peekGUID=getInt64(p_playerBase+0x30);
		if (g_playerGUID != peekGUID) {
			/* no? we are still getting the expected GUID for our avatar, but we don't have it's current position */
			return true;
		}
	}
	context = wow.getNameRealm ();
	identity = wow.getNameAvatar ();

	BOOL ok = true;

	// Wow stores as
	// North/South (+ North)
	// East/West (+ West)
	// Up/Down (+Up)
	// ... which isn't a right-hand coordinate system.

	float pos[3];
	ok = ok && peekProc((BYTE *) p_playerBase + 0x798, pos, sizeof(float)*3);
	if (! ok) {
		if (g_playerGUID == 0xffffffffffffffff) {
			return false;
		} else if (g_playerGUID == 0) {
			return true;
		} else {
			/* FIXME need a better way to mark PlayerBase invalid */
			g_playerGUID=0;
			return true; /* we got a good reference for an avatar, but no good position */
		}
	}

	/* convert wow -> right hand coordinate system */
	avatar_pos[0] = -pos[1];
	avatar_pos[1] = pos[2];
	avatar_pos[2] = pos[0];

	float heading=0.0;
	ok = ok && peekProc((BYTE *) p_playerBase + 0x7A8, &heading, sizeof(heading));
	if (! ok)
		return false;

	float pitch=0.0;
	ok = ok && peekProc((BYTE *) p_playerBase + 0x7AC, &pitch, sizeof(pitch));
	if (! ok)
		return false;

	/* TODO use yaw (heading) and pitch angles */
	/* FIXME sin/cos (heading) is right from the numbers, but (-heading) is right from the sound position */
	avatar_front[0]=-sin(heading);
	avatar_front[1]=0.0;
	avatar_front[2]=cos(heading);

	// Dummy top vector, can't tilt your head sideways in wow.
	avatar_top[0]= 0.0;
	avatar_top[1]= 1.0;
	avatar_top[2]= 0.0;

	getCamera(camera_pos, camera_front, camera_top);

	if (! ok)
		return false;

	//	printf("P %f %f %f -- %f %f %f \n", avatar_pos[0], avatar_pos[1], avatar_pos[2], avatar_front[0], avatar_front[1], avatar_front[2]);

	// is it a unit length vector?
	if (fabs((avatar_front[0]*avatar_front[0]+avatar_front[1]*avatar_front[1]+avatar_front[2]*avatar_front[2])-1.0)>0.5) {
		//		printf("F %f %f %f\n", front[0], front[1], front[2]);
		return false;
	}

	// are we around 0/0/0
	if ((fabs(avatar_pos[0])<0.1) && (fabs(avatar_pos[1])<0.1) && (fabs(avatar_pos[2])<0.1)) {
		//		printf("P %f %f %f\n", avatar_pos[0], avatar_pos[1], avatar_pos[2]);
		return false;
	}

	return true;
}

static int trylock() {
	h = NULL;

	DWORD pid=getProcess(L"Wow.exe");
	if (!pid)
		return false;

	h=OpenProcess(PROCESS_VM_READ, false, pid);
	if (!h) {
#ifdef _DEBUG
		DWORD dwError;
		wchar_t errBuf[256];

		dwError = GetLastError();
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)errBuf, sizeof(errBuf),NULL);
		std::cout << "Error in OpenProcess: ";
		std::wcout << errBuf << std::endl;
#endif
		return false;
	}

	p_playerBase=getPlayerBase();
	if (p_playerBase != 0) {
		float apos[3], afront[3], atop[3], cpos[3], cfront[3], ctop[3];
		std::string context;
		std::wstring identity;

		if (fetch(apos, afront, atop, cpos, cfront, ctop, context, identity)) {
			return true;
		}
	}

	CloseHandle(h);
	h = NULL;
	return false;
}

static void unlock() {
	if (h) {
		CloseHandle(h);
		h = NULL;
	}
	return;
}

static const std::wstring longdesc() {
	return std::wstring(L"Supports World of Warcraft 3.2.2 (10505) (Release) (Euro). With context and identity support.");
}

static std::wstring description(L"World of Warcraft 3.2.2 (Euro)");

static std::wstring shortname(L"World of Warcraft");

static MumblePlugin wowplug = {
	MUMBLE_PLUGIN_MAGIC,
	description,
	shortname,
	about,
	NULL,
	trylock,
	unlock,
	longdesc,
	fetch
};

extern "C" __declspec(dllexport) MumblePlugin *getMumblePlugin() {
	return &wowplug;
}