#ifndef TML_H
#define TML_H

#include <stddef.h>

typedef void (*tml_task_fn)(void *arg);

typedef struct {
    tml_task_fn fn;
    void *arg;
} tml_task_t;

int  tml_init(int workers);
int  tml_submit(const tml_task_t *task);
int  tml_submit_fn(tml_task_fn fn, void *arg);
void tml_wait_all(void);
void tml_shutdown(void);

#endif
