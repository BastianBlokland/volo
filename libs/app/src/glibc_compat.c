#ifdef VOLO_LINUX
/**
 * Hack to support old GLIBC versions.
 * In GLIBC 2.34 a new version of the '__libc_start_main' symbol was added due to a potential
 * incompatibility with the 'init' parameter (which does not affect us).
 */

// Old (2.2.5) '__libc_start_main' version from GLIBC (which we will always use).
__asm__(".symver __libc_start_main_old,__libc_start_main@GLIBC_2.2.5");
int __libc_start_main_old(
    int (*main)(int, char**, char**),
    int            argc,
    char**         argv,
    __typeof(main) init,
    void (*fini)(void),
    void (*rtld_fini)(void),
    void* stack_end);

// Wrapped '__libc_start_main' version (which just calls the old version) that we will link instead.
int __wrap___libc_start_main(
    int (*main)(int, char**, char**),
    int            argc,
    char**         argv,
    __typeof(main) init,
    void (*fini)(void),
    void (*rtld_fini)(void),
    void* stack_end) {
  return __libc_start_main_old(main, argc, argv, init, fini, rtld_fini, stack_end);
}
#endif
