#ifndef SHARED_CONSTANTS_H
#define SHARED_CONSTANTS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define SHM_NAME "/sysTrace_shared_mem"
#define SYS_TRACE_ROOT_DIR "/home/sysTrace/"
    extern int global_stage_id;
    extern int global_stage_type;

    typedef struct
    {
        bool g_dump_L0;
        bool g_dump_L1;
        bool g_dump_L2;
        unsigned int g_dump_L1_interval;
        unsigned int g_dump_L2_interval;
        bool g_L1_timer_active;
        bool g_L2_timer_active;
        time_t g_L1_start_time;
        time_t g_L2_start_time;
        pthread_mutex_t g_trace_mutex;
    } SharedData;

    int init_shared_memory();

    SharedData *get_shared_data();

    void cleanup_shared_memory();
    bool checkAndUpdateTimer(int level);

#ifdef __cplusplus
}
#endif

#endif // SHARED_CONSTANTS_H