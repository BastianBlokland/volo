/**
 * GLIBC (GNU libc as used on most Linux distributions) uses symbol versioning.
 * Documentation: https://www.man7.org/conf/lca2006/shared_libraries/slide19a.html
 *
 * Symbol versioning allows targeting older versions of libc functions even when running on a newer
 * libc library.
 *
 * By default the compiler targets the latest version of each symbol as supported by the installed
 * glibc. This means users would need a libc version that is greater or equal to the one we build
 * with, instead we manually target older versions of the libc symbols we use.
 *
 * Current required glibc version: 2.22 (Ubuntu 16.04).
 *
 * NOTE: When adding additional libc dependencies they need to be manually listed here.
 */
__asm__(".symver __cxa_finalize@GLIBC_2.2.5");
__asm__(".symver __errno_location@GLIBC_2.2.5");
__asm__(".symver __libc_start_main@GLIBC_2.2.5");
__asm__(".symver __sched_cpucount@GLIBC_2.6");
__asm__(".symver _setjmp@GLIBC_2.2.5");
__asm__(".symver acosf@GLIBC_2.2.5");
__asm__(".symver asinf@GLIBC_2.2.5");
__asm__(".symver atan2f@GLIBC_2.2.5");
__asm__(".symver atanf@GLIBC_2.2.5");
__asm__(".symver cbrtf@GLIBC_2.2.5");
__asm__(".symver clock_gettime@GLIBC_2.17");
__asm__(".symver close@GLIBC_2.2.5");
__asm__(".symver closedir@GLIBC_2.2.5");
__asm__(".symver cos@GLIBC_2.2.5");
__asm__(".symver cosf@GLIBC_2.2.5");
__asm__(".symver dlclose@GLIBC_2.2.5");
__asm__(".symver dlerror@GLIBC_2.2.5");
__asm__(".symver dlinfo@GLIBC_2.3.3");
__asm__(".symver dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym@GLIBC_2.2.5");
__asm__(".symver dup2@GLIBC_2.2.5");
__asm__(".symver execvp@GLIBC_2.2.5");
__asm__(".symver fmod@GLIBC_2.2.5");
__asm__(".symver fmodf@GLIBC_2.2.5");
__asm__(".symver fork@GLIBC_2.2.5");
__asm__(".symver fstat@GLIBC_2.2.5");
__asm__(".symver ftruncate@GLIBC_2.2.5");
__asm__(".symver getcwd@GLIBC_2.2.5");
__asm__(".symver getenv@GLIBC_2.2.5");
__asm__(".symver getpagesize@GLIBC_2.2.5");
__asm__(".symver getpgid@GLIBC_2.2.5");
__asm__(".symver inotify_add_watch@GLIBC_2.4");
__asm__(".symver inotify_init1@GLIBC_2.9");
__asm__(".symver isatty@GLIBC_2.2.5");
__asm__(".symver kill@GLIBC_2.2.5");
__asm__(".symver killpg@GLIBC_2.2.5");
__asm__(".symver localtime@GLIBC_2.2.5");
__asm__(".symver log10f@GLIBC_2.2.5");
__asm__(".symver logf@GLIBC_2.22");
__asm__(".symver longjmp@GLIBC_2.2.5");
__asm__(".symver lseek@GLIBC_2.2.5");
__asm__(".symver memcpy@GLIBC_2.14");
__asm__(".symver memset@GLIBC_2.2.5");
__asm__(".symver mkdir@GLIBC_2.2.5");
__asm__(".symver mmap@GLIBC_2.2.5");
__asm__(".symver munmap@GLIBC_2.2.5");
__asm__(".symver nanosleep@GLIBC_2.2.5");
__asm__(".symver open@GLIBC_2.2.5");
__asm__(".symver opendir@GLIBC_2.2.5");
__asm__(".symver posix_fadvise@GLIBC_2.2.5");
__asm__(".symver powf@GLIBC_2.22");
__asm__(".symver prctl@GLIBC_2.2.5");
__asm__(".symver pthread_attr_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_attr_getstack@GLIBC_2.2.5");
__asm__(".symver pthread_attr_init@GLIBC_2.2.5");
__asm__(".symver pthread_attr_setstacksize@GLIBC_2.2.5");
__asm__(".symver pthread_cond_broadcast@GLIBC_2.3.2");
__asm__(".symver pthread_cond_destroy@GLIBC_2.3.2");
__asm__(".symver pthread_cond_init@GLIBC_2.3.2");
__asm__(".symver pthread_cond_signal@GLIBC_2.3.2");
__asm__(".symver pthread_cond_wait@GLIBC_2.3.2");
__asm__(".symver pthread_create@GLIBC_2.2.5");
__asm__(".symver pthread_getattr_np@GLIBC_2.2.5");
__asm__(".symver pthread_join@GLIBC_2.2.5");
__asm__(".symver pthread_mutex_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_mutex_init@GLIBC_2.2.5");
__asm__(".symver pthread_mutex_lock@GLIBC_2.2.5");
__asm__(".symver pthread_mutex_unlock@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_init@GLIBC_2.2.5");
__asm__(".symver pthread_mutexattr_setrobust@GLIBC_2.12");
__asm__(".symver pthread_mutexattr_settype@GLIBC_2.2.5");
__asm__(".symver pthread_self@GLIBC_2.2.5");
__asm__(".symver raise@GLIBC_2.2.5");
__asm__(".symver read@GLIBC_2.2.5");
__asm__(".symver readdir@GLIBC_2.2.5");
__asm__(".symver realpath@GLIBC_2.3");
__asm__(".symver rename@GLIBC_2.2.5");
__asm__(".symver sched_getaffinity@GLIBC_2.3.4");
__asm__(".symver sched_yield@GLIBC_2.2.5");
__asm__(".symver setenv@GLIBC_2.2.5");
__asm__(".symver setpriority@GLIBC_2.2.5");
__asm__(".symver setsid@GLIBC_2.2.5");
__asm__(".symver sigaction@GLIBC_2.2.5");
__asm__(".symver sigaddset@GLIBC_2.2.5");
__asm__(".symver sigemptyset@GLIBC_2.2.5");
__asm__(".symver signal@GLIBC_2.2.5");
__asm__(".symver sigprocmask@GLIBC_2.2.5");
__asm__(".symver sin@GLIBC_2.2.5");
__asm__(".symver sincos@GLIBC_2.2.5");
__asm__(".symver sincosf@GLIBC_2.2.5");
__asm__(".symver stat@GLIBC_2.2.5");
__asm__(".symver syscall@GLIBC_2.2.5");
__asm__(".symver tanf@GLIBC_2.2.5");
__asm__(".symver time@GLIBC_2.2.5");
__asm__(".symver timegm@GLIBC_2.2.5");
__asm__(".symver unlink@GLIBC_2.2.5");
__asm__(".symver vsnprintf@GLIBC_2.2.5");
__asm__(".symver waitpid@GLIBC_2.2.5");
__asm__(".symver write@GLIBC_2.2.5");
