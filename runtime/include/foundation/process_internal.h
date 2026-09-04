#ifndef FOUNDATION_PROCESS_INTERNAL_H
#define FOUNDATION_PROCESS_INTERNAL_H

#include "foundation/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>
#endif

typedef struct fdn_process fdn_process;

const fdn_process *fdn_process_from_handle(uint64_t handle);
bool fdn_process_environment_block_required(const fdn_process *process);

#if defined(_WIN32)
int32_t fdn_process_windows_status(DWORD error);
wchar_t *fdn_process_windows_program(const fdn_process *process);
wchar_t *fdn_process_windows_command(const fdn_process *process);
wchar_t *fdn_process_windows_environment(const fdn_process *process);
int32_t fdn_process_windows_cwd(const fdn_process *process,
                                wchar_t **working_directory);
#else
int32_t fdn_process_posix_status(int error);
char **fdn_process_posix_argv(const fdn_process *process);
char **fdn_process_posix_environment(const fdn_process *process);
int32_t fdn_process_posix_cwd(const fdn_process *process, char **working_directory);
void fdn_process_posix_argv_close(const fdn_process *process, char **values);
void fdn_process_posix_environment_close(const fdn_process *process, char **values);
#endif

#endif
