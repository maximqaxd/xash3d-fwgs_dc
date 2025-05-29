/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

#if defined _MSC_VER && _MSC_VER >= 1400
	#ifndef _CRT_SECURE_NO_DEPRECATE
		#define _CRT_SECURE_NO_DEPRECATE
	#endif

	#pragma warning(disable: 4996) // deprecated functions
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
char **ppszFiles = NULL;
int nFiles = 0;
int nMaxFiles = 0;

int string_comparator(const void *string1, const void *string2) {
	char *s1 = *(char **)string1;
	char *s2 = *(char **)string2;
	return strcmp(s1, s2);
}

void PrintUsage(char *pname) {
	printf("\n\tusage: %s <source directory> <wadfile name> <script name> \n\n", pname);
	printf("\t%s is used to generate a bitmap name sorted 'qlumpy script'.\n", pname);
}

int main(int argc, char **argv) {
	char *pszdir;
	char *pszWadName;
	char *pszScriptName;
	char szBuf[1024];
	FILE *hScriptFile;
	struct dirent *entry;
	DIR *dp;
	int fWrite;

	printf("makels Copyright (c) 1998 Valve L.L.C., %s\n", __DATE__);

	if (argc != 4 || argv[1][0] == '/') {
		PrintUsage(argv[0]);
		exit(1);
	}

	pszdir = (char *)malloc(strlen(argv[1]) + 7);
	strcpy(pszdir, argv[1]);
	strcat(pszdir, "/*.pvr");

	pszWadName = (char *)malloc(strlen(argv[2]) + 5);
	strcpy(pszWadName, argv[2]);
	strcat(pszWadName, ".wad");

	pszScriptName = (char *)malloc(strlen(argv[3]));
	strcpy(pszScriptName, argv[3]);
	hScriptFile = fopen(pszScriptName, "w");

	if (!hScriptFile) {
		printf("\n---------- ERROR ------------------\n");
		printf(" Could not open the script file: %s\n", pszScriptName);
		exit(EXIT_FAILURE);
	}

	sprintf(szBuf, "$DEST    \"%s\"\n\n", pszWadName);
	fWrite = fputs(szBuf, hScriptFile);
	if (fWrite == EOF) {
		printf("\n---------- ERROR ------------------\n");
		printf(" Could not write to the script file: %s\n", pszScriptName);
		fclose(hScriptFile);
		exit(EXIT_FAILURE);
	}

	dp = opendir(argv[1]);
	if (dp != NULL) {
		while ((entry = readdir(dp))) {
			if (entry->d_type == DT_REG) {
				char szShort[256];
				strcpy(szShort, entry->d_name);
				for (char *p = szShort; *p; ++p) *p = toupper(*p);

				if ((szShort[1] == '_') && ((szShort[0] == 'N') || (szShort[0] == 'F'))) {
					printf("Skipping %s.\n", entry->d_name);
				} else {
					if (nFiles >= nMaxFiles) {
						nMaxFiles += 1000;
						ppszFiles = (char **)realloc(ppszFiles, nMaxFiles * sizeof(*ppszFiles));
						if (!ppszFiles) {
							printf("\n---------- ERROR ------------------\n");
							printf(" Could not realloc more filename pointer storage\n");
							exit(EXIT_FAILURE);
						}
					}
					ppszFiles[nFiles++] = strdup(szShort);
				}
			}
		}
		closedir(dp);
	}

	if (nFiles > 0) {
		qsort(ppszFiles, nFiles, sizeof(char *), string_comparator);

		for (int i = 0; i < nFiles; i++) {
			char *p;
			char szShort[256];
			char szFull[256];

			strcpy(szShort, argv[1]);
			strcat(szShort, "/");
			strcat(szShort, ppszFiles[i]);
			realpath(szShort, szFull);

			sprintf(szBuf, "$loadbmp    \"%s\"\n", szFull);
			fWrite = fputs(szBuf, hScriptFile);
			if (fWrite == EOF) {
				printf("\n---------- ERROR ------------------\n");
				printf(" Could not write to the script file: %s\n", pszScriptName);
				fclose(hScriptFile);
				exit(EXIT_FAILURE);
			}

			p = strchr(ppszFiles[i], '.');
			*p = '\0';

			sprintf(szBuf, "%s  miptex -1 -1 -1 -1\n\n", ppszFiles[i]);
			fWrite = fputs(szBuf, hScriptFile);
			if (fWrite == EOF) {
				printf("\n---------- ERROR ------------------\n");
				printf(" Could not write to the script file: %s\n", pszScriptName);
				fclose(hScriptFile);
				exit(EXIT_FAILURE);
			}

			free(ppszFiles[i]);
		}
	}

	printf("Processed %d files specified by %s\n", nFiles, argv[1]);

	fclose(hScriptFile);
	free(pszdir);
	free(pszWadName);
	free(pszScriptName);
	exit(0);
	return 0;
}
