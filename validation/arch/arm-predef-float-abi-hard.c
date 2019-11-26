#if !defined(__ARM_PCS_VFP) || defined(__SOFTFP__) || defined(__ARM_PCS)
#error
#endif

/*
 * check-name: arm-predef-float-abi-hard
 * check-command: sparse --arch=arm -mfloat-abi=hard $file
 */
