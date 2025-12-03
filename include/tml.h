#ifndef TML_H
#define TML_H

typedef void (*tml_task_fn)(void *arg);

typedef struct tml_task {
    tml_task_fn fn;
    void *arg;
} tml_task_t;

int tml_init(int workers);
void tml_submit(tml_task_t *task);
void tml_submit_fn(tml_task_fn fn, void *arg);
void tml_shutdown(void);

#endif
