#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <time.h> 
#include <pthread.h>
    extern int global_stage_id;
    extern int global_stage_type;
    extern bool g_dump_L0;
    extern bool g_dump_L1;
    extern bool g_dump_L2;
    //extern int g_dump_L0_interval;
    extern unsigned int g_dump_L1_interval;
    extern unsigned int g_dump_L2_interval;
    extern bool g_L1_timer_active;
    extern bool g_L2_timer_active;
    extern time_t g_L1_start_time;
    extern time_t g_L2_start_time;
    extern pthread_mutex_t g_trace_mutex; 
#define SYS_TRACE_ROOT_DIR "/home/sysTrace/"

#ifdef __cplusplus
}
#endif