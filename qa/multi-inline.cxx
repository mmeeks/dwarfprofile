#include <math.h>
#include <stdio.h>

void f1(int);
void f2(int);
void f3(int);
void f4(int);
void f5(int);

static void f0(int n) {
    int i;
    for (i = 0; i != n; ++i) {
        f0(i);
        printf("%d %e\n", i, sqrt((double) i));
    }
}

int main(int argc, char ** argv) {
    (void) argv;
    f0(argc); f1(argc); f2(argc); f3(argc); f4(argc); f5(argc);
    return 0;
}
