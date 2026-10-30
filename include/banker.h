/*
 * banker.h - FR5 Banker's safety algorithm.
 */
#ifndef CC_BANKER_H
#define CC_BANKER_H

#define BANKER_MAX_P   20
#define BANKER_MAX_R   10
#define BANKER_MAX_VAL 1000

/* Runs the safety check on a state file.
 * Returns 0 on success (sets *is_safe), non-zero on input error. */
int banker_run_file(const char *path, int *is_safe);
int cmd_banker(const char *path);

#endif
