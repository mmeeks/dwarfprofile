#include <math.h>
#include <stdio.h>

inline void fi(int n) {
    int i;
    for (i = 0; i != n; ++i) {
        fi(i);
        printf("%d %e\n", i, sqrt((double) i));
    }
}

void f2(int n) {
    fi(n);
}
