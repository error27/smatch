/*	
 * Mingw Compatibility functions	
 *	
 *	
 *  Licensed under the Open Software License version 1.1	
 */	
	
	
	
#include <stdarg.h>	
#include <windef.h>	
#include <winbase.h>	
#include <stdlib.h>	
#include <string.h>	
	
#include "lib.h"
#include "token.h"
	
void *blob_alloc(unsigned long size)	
{	
	void *ptr;	
	ptr = malloc(size);	
	if (ptr != NULL)	
		memset(ptr, 0, size);	
	return ptr;	
}	
	
void blob_free(void *addr, unsigned long size)	
{	
	free(addr);	
}	
	
long double string_to_ld(const char *nptr, char **endptr) 	
{	
	return strtod(nptr, endptr);	
}	
	
int identical_files(struct stream* s, struct stat *st, const char * name) 	
{	
	HANDLE file1 =CreateFile(s->name,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);	
	if(file1==INVALID_HANDLE_VALUE) 	
		return 0;	
	HANDLE file2=CreateFile(name,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);	
	if(file2==INVALID_HANDLE_VALUE) {	
		CloseHandle(file1);	
		return 0;	
	}	
	BY_HANDLE_FILE_INFORMATION info1;	
	BY_HANDLE_FILE_INFORMATION info2;	
	int same=0;	
	if(GetFileInformationByHandle(file1,&info1) && GetFileInformationByHandle(file2,&info2)){	
		if(info1.nFileIndexLow==info2.nFileIndexLow &&	
		   info1.nFileIndexHigh==info2.nFileIndexHigh &&	
		   info1.dwVolumeSerialNumber==info2.dwVolumeSerialNumber) 	
			same=1;	
	}	
	CloseHandle(file1);	
	CloseHandle(file2);	
	return same;	
}	
