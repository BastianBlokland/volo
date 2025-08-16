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
 * Current required glibc version: 2.17 (Ubuntu 14.04).
 *
 * NOTE: When adding additional libc dependencies they need to be manually listed here.
 */
__asm__(".symver __cxa_finalize,__cxa_finalize@GLIBC_2.2.5");
__asm__(".symver __errno_location,__errno_location@GLIBC_2.2.5");
__asm__(".symver __fxstat,__fxstat@GLIBC_2.2.5");
__asm__(".symver __sched_cpucount,__sched_cpucount@GLIBC_2.6");
__asm__(".symver __xstat,__xstat@GLIBC_2.2.5");
__asm__(".symver _setjmp,_setjmp@GLIBC_2.2.5");
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver atanf,atanf@GLIBC_2.2.5");
__asm__(".symver cbrtf,cbrtf@GLIBC_2.2.5");
__asm__(".symver clock_gettime,clock_gettime@GLIBC_2.17");
__asm__(".symver close,close@GLIBC_2.2.5");
__asm__(".symver closedir,closedir@GLIBC_2.2.5");
__asm__(".symver cos,cos@GLIBC_2.2.5");
__asm__(".symver cosf,cosf@GLIBC_2.2.5");
__asm__(".symver dlclose,dlclose@GLIBC_2.2.5");
__asm__(".symver dlerror,dlerror@GLIBC_2.2.5");
__asm__(".symver dlinfo,dlinfo@GLIBC_2.3.3");
__asm__(".symver dlopen,dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym,dlsym@GLIBC_2.2.5");
__asm__(".symver dup2,dup2@GLIBC_2.2.5");
__asm__(".symver execvp,execvp@GLIBC_2.2.5");
__asm__(".symver expf,expf@GLIBC_2.2.5");
__asm__(".symver fmod,fmod@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver fork,fork@GLIBC_2.2.5");
__asm__(".symver ftruncate,ftruncate@GLIBC_2.2.5");
__asm__(".symver getcwd,getcwd@GLIBC_2.2.5");
__asm__(".symver getenv,getenv@GLIBC_2.2.5");
__asm__(".symver getpagesize,getpagesize@GLIBC_2.2.5");
__asm__(".symver getpgid,getpgid@GLIBC_2.2.5");
__asm__(".symver inotify_add_watch,inotify_add_watch@GLIBC_2.4");
__asm__(".symver inotify_init1,inotify_init1@GLIBC_2.9");
__asm__(".symver isatty,isatty@GLIBC_2.2.5");
__asm__(".symver kill,kill@GLIBC_2.2.5");
__asm__(".symver killpg,killpg@GLIBC_2.2.5");
__asm__(".symver localtime,localtime@GLIBC_2.2.5");
__asm__(".symver log10f,log10f@GLIBC_2.2.5");
__asm__(".symver logf,logf@GLIBC_2.2.5");
__asm__(".symver longjmp,longjmp@GLIBC_2.2.5");
__asm__(".symver lseek,lseek@GLIBC_2.2.5");
__asm__(".symver memcpy,memcpy@GLIBC_2.14");
__asm__(".symver memset,memset@GLIBC_2.2.5");
__asm__(".symver mkdir,mkdir@GLIBC_2.2.5");
__asm__(".symver mmap,mmap@GLIBC_2.2.5");
__asm__(".symver munmap,munmap@GLIBC_2.2.5");
__asm__(".symver nanosleep,nanosleep@GLIBC_2.2.5");
__asm__(".symver open,open@GLIBC_2.2.5");
__asm__(".symver opendir,opendir@GLIBC_2.2.5");
__asm__(".symver posix_fadvise,posix_fadvise@GLIBC_2.2.5");
__asm__(".symver pow,pow@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver prctl,prctl@GLIBC_2.2.5");
__asm__(".symver pthread_attr_destroy,pthread_attr_destroy@GLIBC_2.2.5");
__asm__(".symver pthread_attr_init,pthread_attr_init@GLIBC_2.2.5");
__asm__(".symver pthread_attr_setstacksize,pthread_attr_setstacksize@GLIBC_2.2.5");
__asm__(".symver pthread_getattr_np,pthread_getattr_np@GLIBC_2.2.5");
__asm__(".symver pthread_getspecific,pthread_getspecific@GLIBC_2.2.5");
__asm__(".symver pthread_key_create,pthread_key_create@GLIBC_2.2.5");
__asm__(".symver pthread_self,pthread_self@GLIBC_2.2.5");
__asm__(".symver pthread_setspecific,pthread_setspecific@GLIBC_2.2.5");
__asm__(".symver raise,raise@GLIBC_2.2.5");
__asm__(".symver read,read@GLIBC_2.2.5");
__asm__(".symver readdir,readdir@GLIBC_2.2.5");
__asm__(".symver realpath,realpath@GLIBC_2.3");
__asm__(".symver rename,rename@GLIBC_2.2.5");
__asm__(".symver sched_getaffinity,sched_getaffinity@GLIBC_2.3.4");
__asm__(".symver sched_yield,sched_yield@GLIBC_2.2.5");
__asm__(".symver setenv,setenv@GLIBC_2.2.5");
__asm__(".symver setpriority,setpriority@GLIBC_2.2.5");
__asm__(".symver setsid,setsid@GLIBC_2.2.5");
__asm__(".symver sigaction,sigaction@GLIBC_2.2.5");
__asm__(".symver sigaddset,sigaddset@GLIBC_2.2.5");
__asm__(".symver sigemptyset,sigemptyset@GLIBC_2.2.5");
__asm__(".symver signal,signal@GLIBC_2.2.5");
__asm__(".symver sigprocmask,sigprocmask@GLIBC_2.2.5");
__asm__(".symver sin,sin@GLIBC_2.2.5");
__asm__(".symver sincos,sincos@GLIBC_2.2.5");
__asm__(".symver sincosf,sincosf@GLIBC_2.2.5");
__asm__(".symver syscall,syscall@GLIBC_2.2.5");
__asm__(".symver tanf,tanf@GLIBC_2.2.5");
__asm__(".symver time,time@GLIBC_2.2.5");
__asm__(".symver timegm,timegm@GLIBC_2.2.5");
__asm__(".symver unlink,unlink@GLIBC_2.2.5");
__asm__(".symver vsnprintf,vsnprintf@GLIBC_2.2.5");
__asm__(".symver waitpid,waitpid@GLIBC_2.2.5");
__asm__(".symver write,write@GLIBC_2.2.5");
