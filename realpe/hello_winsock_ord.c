#include <winsock2.h>

int main(void) {
    if (htonl(0x12345678u) != 0x78563412u)
        return 10;

    if (ntohl(0x12345678u) != 0x78563412u)
        return 11;

    if ((u_short)htons(0x1234u) != 0x3412u)
        return 12;

    if ((u_short)ntohs(0x1234u) != 0x3412u)
        return 13;

    if (htonl(ntohl(0xAABBCCDDu)) != 0xAABBCCDDu)
        return 14;

    return 42;
}
