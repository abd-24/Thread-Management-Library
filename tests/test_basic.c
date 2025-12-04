#include <stdlib.h>
#include <unistd.h>
#include "../include/tml.h"

void task(void *arg) {
    int *p = (int *)arg;
    free(p);
}

int main() {
    if (tml_init(2) != 0)
        return 1;

    for (int i = 0; i < 10; i++) {
        int *v = malloc(sizeof(int));
        if (!v)
            return 1;
        *v = i;

        if (tml_submit_fn(task, v) != 0) {
            free(v);
            return 1;
        }
    }

    tml_wait_all();
    tml_shutdown();
    return 0;
}
