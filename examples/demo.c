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
    if (tml_init(4) != 0)
        return 1;

    for (int i = 0; i < 100; i++) {
        int *p = malloc(sizeof(int));
        if (!p)
            return 1;

        *p = i;

        if (tml_submit_fn(task_fn, p) != 0) {
            free(p);
            return 1;
        }
    }

    tml_wait_all();
    tml_shutdown();
    return 0;
}
