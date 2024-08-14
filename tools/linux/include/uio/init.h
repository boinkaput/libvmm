#define pragma once

#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

bool insmod(const char *module_path);

int spawn_process_and_wait(const char *program, char *const *program_args,
                   int (*child_fn)(const char *, char *const *));

void shutdown(void);
