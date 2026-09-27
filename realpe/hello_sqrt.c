#include <math.h>
int main(void){
    double x=24.75;
    double r=sqrt(x);
    // r ~4.9749, *100 = 497
    int ir = (int)(r*100.0);
    return ir; // should be 497
}
