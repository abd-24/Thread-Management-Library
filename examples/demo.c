#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/tml.h"

void task_fn(void *arg) {
    int id = *(int*)arg;
    printf("Task %d executed\n", id);
    free(arg);
}

int main() {
    tml_init(4);

    for (int i = 0; i < 100; i++) {
        int *p = malloc(sizeof(int));
        *p = i;
        tml_submit_fn(task_fn, p);
    }

    sleep(1);
    tml_shutdown();
    return 0;
}
