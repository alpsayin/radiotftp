/*
 * datacollection.c
 *
 *  Created on: Jun 26, 2012
 *      Author: alpsayin
 */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "datacollection.h"
#include "util.h"

static FILE* logfile=NULL;

void openLogFile(char* logfile_name)
{
	logfile = fopen(logfile_name,"a+");
	if(!logfile)
	{
		printf("Unable to open the log file!\n");
	}
}
void registerEvent(char* event, char* param)
{
	struct timeval tv;
	struct timezone tz;
	if(logfile)
	{
		gettimeofday(&tv, &tz);
		fprintf(logfile, "%d.%06d\t[%s->%s]\n", tv.tv_sec, tv.tv_usec, event,param);
		fflush(logfile);
	}
}
void closeLogFile()
{
	if(logfile)
	{
		fclose(logfile);
		logfile=NULL;
	}
}
