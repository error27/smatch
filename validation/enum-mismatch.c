#include "enum-common.c"

/*
 * check-name: -Wenum-mismatch
 * check-command: sparse -Wenum-mismatch -Wno-int-to-enum $file
 *
 * check-error-start
enum-common.c:57:45: warning: mixing different enum types
enum-common.c:57:45:     int enum ENUM_TYPE_B  versus
enum-common.c:57:45:     int enum ENUM_TYPE_A 
enum-common.c:58:45: warning: mixing different enum types
enum-common.c:58:45:     int enum ENUM_TYPE_B  versus
enum-common.c:58:45:     int enum ENUM_TYPE_A 
enum-common.c:54:22: warning: mixing different enum types
enum-common.c:54:22:     int enum ENUM_TYPE_B  versus
enum-common.c:54:22:     int enum ENUM_TYPE_A 
enum-common.c:55:22: warning: mixing different enum types
enum-common.c:55:22:     int enum <noident>  versus
enum-common.c:55:22:     int enum ENUM_TYPE_A 
enum-common.c:64:45: warning: mixing different enum types
enum-common.c:64:45:     int enum <noident>  versus
enum-common.c:64:45:     int enum ENUM_TYPE_A 
enum-common.c:65:45: warning: mixing different enum types
enum-common.c:65:45:     int enum <noident>  versus
enum-common.c:65:45:     int enum ENUM_TYPE_A 
enum-common.c:62:22: warning: mixing different enum types
enum-common.c:62:22:     int enum ENUM_TYPE_A  versus
enum-common.c:62:22:     int enum <noident> 
enum-common.c:69:17: warning: mixing different enum types
enum-common.c:69:17:     int enum ENUM_TYPE_B  versus
enum-common.c:69:17:     int enum ENUM_TYPE_A 
enum-common.c:70:17: warning: mixing different enum types
enum-common.c:70:17:     int enum <noident>  versus
enum-common.c:70:17:     int enum ENUM_TYPE_B 
enum-common.c:71:25: warning: mixing different enum types
enum-common.c:71:25:     int enum ENUM_TYPE_A  versus
enum-common.c:71:25:     int enum <noident> 
enum-common.c:74:17: warning: mixing different enum types
enum-common.c:74:17:     int enum ENUM_TYPE_B  versus
enum-common.c:74:17:     int enum ENUM_TYPE_A 
enum-common.c:75:17: warning: mixing different enum types
enum-common.c:75:17:     int enum <noident>  versus
enum-common.c:75:17:     int enum ENUM_TYPE_B 
enum-common.c:76:25: warning: mixing different enum types
enum-common.c:76:25:     int enum ENUM_TYPE_A  versus
enum-common.c:76:25:     int enum <noident> 
 * check-error-end
 */
