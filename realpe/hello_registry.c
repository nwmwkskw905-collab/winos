#include <windows.h>
#include <string.h>

int main(void) {
    HKEY hkey;
    LONG r;

    /* Create key */
    r = RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinOS_Test", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (r != 0) return 10;

    /* Set DWORD */
    DWORD dw = 0x12345678;
    r = RegSetValueExA(hkey, "TestDWORD", 0, REG_DWORD, (BYTE*)&dw, sizeof(dw));
    if (r != 0) { RegCloseKey(hkey); return 11; }

    /* Set SZ */
    const char* sz = "hello registry";
    r = RegSetValueExA(hkey, "TestSZ", 0, REG_SZ, (BYTE*)sz, (DWORD)strlen(sz)+1);
    if (r != 0) { RegCloseKey(hkey); return 12; }

    /* Query DWORD */
    DWORD type, cb;
    DWORD out_dw;
    cb = sizeof(out_dw);
    r = RegQueryValueExA(hkey, "TestDWORD", NULL, &type, (BYTE*)&out_dw, &cb);
    if (r != 0) { RegCloseKey(hkey); return 13; }
    if (type != REG_DWORD) { RegCloseKey(hkey); return 14; }
    if (out_dw != 0x12345678) { RegCloseKey(hkey); return 15; }

    /* Query SZ */
    char out_sz[64];
    cb = sizeof(out_sz);
    r = RegQueryValueExA(hkey, "TestSZ", NULL, &type, (BYTE*)out_sz, &cb);
    if (r != 0) { RegCloseKey(hkey); return 16; }
    if (type != REG_SZ) { RegCloseKey(hkey); return 17; }
    if (strcmp(out_sz, "hello registry") != 0) { RegCloseKey(hkey); return 18; }

    /* Enum values */
    char name[64];
    DWORD name_len = sizeof(name);
    r = RegEnumValueA(hkey, 0, name, &name_len, NULL, NULL, NULL, NULL);
    if (r != 0) { RegCloseKey(hkey); return 19; }
    /* should be one of our values */
    int found_dword=0, found_sz=0;
    if (strcmp(name, "TestDWORD")==0) found_dword=1;
    if (strcmp(name, "TestSZ")==0) found_sz=1;
    name_len = sizeof(name);
    r = RegEnumValueA(hkey, 1, name, &name_len, NULL, NULL, NULL, NULL);
    if (r != 0) { RegCloseKey(hkey); return 20; }
    if (strcmp(name, "TestDWORD")==0) found_dword=1;
    if (strcmp(name, "TestSZ")==0) found_sz=1;
    if (!found_dword || !found_sz) { RegCloseKey(hkey); return 21; }

    /* Create subkey */
    HKEY hsub;
    r = RegCreateKeyExA(hkey, "SubKey", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hsub, NULL);
    if (r != 0) { RegCloseKey(hkey); return 22; }
    RegCloseKey(hsub);

    /* Enum keys */
    char subname[64];
    DWORD sublen = sizeof(subname);
    r = RegEnumKeyExA(hkey, 0, subname, &sublen, NULL, NULL, NULL, NULL);
    if (r != 0) { RegCloseKey(hkey); return 23; }
    if (strcmp(subname, "SubKey") != 0) { RegCloseKey(hkey); return 24; }

    /* Delete value */
    r = RegDeleteValueA(hkey, "TestDWORD");
    if (r != 0) { RegCloseKey(hkey); return 25; }
    cb = sizeof(out_dw);
    r = RegQueryValueExA(hkey, "TestDWORD", NULL, NULL, NULL, &cb);
    if (r == 0) { RegCloseKey(hkey); return 26; }

    /* Delete subkey */
    r = RegDeleteKeyA(hkey, "SubKey");
    if (r != 0) { RegCloseKey(hkey); return 27; }

    /* Cleanup */
    RegDeleteValueA(hkey, "TestSZ");
    RegCloseKey(hkey);
    /* Delete main key */
    r = RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\WinOS_Test");
    if (r != 0) return 28;

    /* Test invalid handle */
    SetLastError(0);
    r = RegCloseKey((HKEY)0x1234);
    if (r == 0) return 29;

    /* Test traversal blocked */
    r = RegCreateKeyExA(HKEY_CURRENT_USER, "..\\outside", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (r == 0) { RegCloseKey(hkey); return 30; }

    return 85;
}
