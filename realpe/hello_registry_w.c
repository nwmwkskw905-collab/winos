#include <windows.h>
#include <wchar.h>
static int wcs_equal(const wchar_t* a, const wchar_t* b){ while(*a && *b){ if(*a!=*b) return 0; a++; b++; } return *a==*b; }
int main(void){
    HKEY hkey;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WinOS_TestW", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (r!=0) return 10;
    DWORD dw=0xAABBCCDD;
    r = RegSetValueExW(hkey, L"DW", 0, REG_DWORD, (BYTE*)&dw, sizeof(dw));
    if (r!=0) { RegCloseKey(hkey); return 11; }
    const wchar_t* ws = L"wide hello";
    r = RegSetValueExW(hkey, L"SZ", 0, REG_SZ, (BYTE*)ws, (DWORD)((wcslen(ws)+1)*2));
    if (r!=0) { RegCloseKey(hkey); return 12; }
    DWORD type, cb;
    DWORD outdw; cb=sizeof(outdw);
    r = RegQueryValueExW(hkey, L"DW", NULL, &type, (BYTE*)&outdw, &cb);
    if (r!=0) { RegCloseKey(hkey); return 13; }
    if (outdw!=0xAABBCCDD) { RegCloseKey(hkey); return 14; }
    wchar_t outws[64]; cb=sizeof(outws);
    r = RegQueryValueExW(hkey, L"SZ", NULL, &type, (BYTE*)outws, &cb);
    if (r!=0) { RegCloseKey(hkey); return 15; }
    if (!wcs_equal(outws, L"wide hello")) { RegCloseKey(hkey); return 16; }
    wchar_t name[64]; DWORD namelen=64;
    r = RegEnumValueW(hkey, 0, name, &namelen, NULL, NULL, NULL, NULL);
    if (r!=0) { RegCloseKey(hkey); return 17; }
    wchar_t sub[64]; DWORD sublen=64;
    HKEY hsub;
    r = RegCreateKeyExW(hkey, L"Sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hsub, NULL);
    if (r!=0) { RegCloseKey(hkey); return 18; }
    RegCloseKey(hsub);
    sublen=64;
    r = RegEnumKeyExW(hkey, 0, sub, &sublen, NULL, NULL, NULL, NULL);
    if (r!=0) { RegCloseKey(hkey); return 19; }
    if (!wcs_equal(sub, L"Sub")) { RegCloseKey(hkey); return 20; }
    RegDeleteValueW(hkey, L"DW");
    RegDeleteValueW(hkey, L"SZ");
    RegDeleteKeyW(hkey, L"Sub");
    RegCloseKey(hkey);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WinOS_TestW");
    return 86;
}
