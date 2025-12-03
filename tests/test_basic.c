// tests/test_basic.c
#include <stdio.h>
#include <stdlib.h>
#include "../include/tml.h"

void task(void *arg) {
    int *p = arg;
    printf("task %d\n", *p);
}

int main() {
    tml_init(2);
    for (int i=0;i<10;i++){
        int *v = malloc(sizeof(int));
        *v = i;
        tml_submit_fn(task, v);
    }
    sleep(1);
    tml_shutdown();
    return 0;
}
