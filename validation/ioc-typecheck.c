extern unsigned int __invalid_size_argument_for_IOC;
#define _IOC_TYPECHECK(t) \
                ((sizeof(t) == sizeof(t[1]) && \
                    sizeof(t) < (1 << 14)) ? \
                   sizeof(t) : __invalid_size_argument_for_IOC)

#define TEST_IOCTL (50 | (_IOC_TYPECHECK(unsigned) << 8))

static unsigned iocnrs[] = {
        [TEST_IOCTL & 0xff] = 1,
};
/*
 * check-name: correct handling of _IOC_TYPECHECK
 *
 * check-error-start
 * check-error-end
 */
