#pragma once
#include <pthread.h>

/* Send SIGINT to specified thread every second until alarm_stop().
 * This function uses SIGALRM inside. */
void alarm_thread(pthread_t);

// Stop send SIGINT to specified thread
void alarm_stop(pthread_t);

// Should be called at program startup
void init_signals();
