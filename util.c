/*
 * util.c
 *
 *  Created on: Apr 29, 2012
 *      Author: alpsayin
 */


#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"
#include "radiotftp.h"

void usage(void)
{
    printf("\nradiotftp sends or listens for a tftp request\n");
    printf("\nlegal invocations:\n");
    printf("\tradiotftp_uhf\n");
    printf("\tradiotftp_vhf\n");
    printf("Build Date: %s\n", BUILD);
#if ETHERNET_ENABLED==1
    printf("Uses ethernet link layer\n");
#elif AX25_ENABLED==1
    printf("Uses ax25 link layer\n");
#else
    printf("Doesn't use any link layer\n");
#endif
    printf("radiotftp [-dst] [-b] [-f] terminal [command filename] \n");
    printf(" -b run in background (EXPERIMENTAL, do NOT use this!)\n");
    printf(" -f defines a different local filename for put and get\n");
    printf("    and defines a different remote filename for append and appendline\n");
    printf(" -dstXXX.XXX.XXX.XXX.XXX.XXX sends the request to XXX.XXX.XXX.XXX.XXX.XXX\n"
    		"   default is broadcast");
    printf(" Local address settings are read from a 'radiotftp.conf' file\n");
    printf(" Valid baudrates 300, 600, 1200, 4800 (Default), 9600, 19200, 38400, 57600 bps\n");
    printf(" Examples: \n"
    		"    radiotftp_uhf384 /dev/ttyUSB0 \n"
    		"    radiotftp_uhf384 -flocalphoto.jpg /dev/ttyUSB0 get remotephoto.jpg\n"
    		"    radiotftp_uhf384 -flocalmusic.mp3 /dev/ttyUSB0 put remotemusic.mp3\n"
    		"    radiotftp_uhf384 -dst10.0.0.0.0.1 /dev/ttyUSB0 appendfile logfile.txt\n"
    		"    radiotftp_uhf384 -fremote_sensors.dat /dev/ttyUSB0 appendline '{TELEMETRY BATTERY=3.3V}'\n"
    		"\n");

    safe_exit(-1);
}
void print_time(char* prefix)
{
	struct timeval curtime;
    gettimeofday(&curtime, NULL);
    printf("%s %ld:%ld:%ld.%ld.%ld\n", prefix, GMT_OFFSET+(curtime.tv_sec%(3600*24))/3600,(curtime.tv_sec%3600)/60, curtime.tv_sec%60, curtime.tv_usec/1000, curtime.tv_usec%1000);
}
int sprint_time(char* outbuf, char* prefix, char* postfix)
{
	struct timeval curtime;
    gettimeofday(&curtime, NULL);
    return sprintf(outbuf, "%s-%ld:%ld:%ld.%ld.%ld%s\0", prefix, GMT_OFFSET+(curtime.tv_sec%(3600*24))/3600,(curtime.tv_sec%3600)/60, curtime.tv_sec%60, curtime.tv_usec/1000, curtime.tv_usec%1000, postfix);
}
uint8_t text_to_ip(uint8_t* in_and_out, uint8_t in_length)
{
	uint8_t i;
	uint8_t point=0;
	uint8_t sum=0;
	for(i=0; i<in_length; i++)
	{
		if(in_and_out[i]=='.' || in_and_out[i]==':' || in_and_out[i]==0x00)
		{
			in_and_out[point++]=sum;
			sum=0;
//			printf("\n");
		}
		else
		{
			sum = (sum*10) + (in_and_out[i]-'0');
//			printf("sum=%d ", sum);
		}
	}
	return 0;
}
uint8_t readnline(FILE* fptr, uint8_t* out, uint8_t length)
{
	uint8_t previ=0, i=0;
	do
	{
		previ=i;
		i+=fread(out+i, 1, 1, fptr);
	} while(out[i-1]!='\n' && i<length && previ!=i);
	out[i-1]=0;
	return 0;
}
void print_callsign(uint8_t* callsign)
{
	uint8_t i;
	for(i=0; i<6; i++)
	{
		if(callsign[i]>=32 && callsign[i]<=125 )
			putchar(callsign[i]);
	}
	printf("%d", callsign[6]);
	putchar('\n');
}
void print_addr_hex(uint8_t* addr)
{
	printf("%x:%x:%x:%x:%x:%x\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}
void print_addr_dec(uint8_t* addr)
{
	printf("%d.%d.%d.%d.%d.%d\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}
