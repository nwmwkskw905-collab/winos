#include <stdio.h>
int main(void){
    double x=24.75;
    printf("x=%.2f\n", x);
    double y=30.0;
    printf("y=%.1f\n", y);
    printf("both x=%.2f y=%.1f\n", x, y);
    return 42;
}
