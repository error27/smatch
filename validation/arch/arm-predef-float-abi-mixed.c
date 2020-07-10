#if defined(__ARM_PCS_VFP) || defined(__SOFTFP__) || !defined(__ARM_PCS)
#error
#endif

/*
 * check-name: arm-predef-float-abi-mixed
 * check-command: sparse --arch=arm -mfloat-abi=softfp $file
 */
