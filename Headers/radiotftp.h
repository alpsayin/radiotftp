/* 
 * File:   tty_talk.h
 * Author: alpsayin
 *
 * Created on April 9, 2012, 8:36 PM
 */

#ifndef 	RADIOTFTP_H
#define		RADIOTFTP_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termio.h>
#include <time.h>
#include <sys/fcntl.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdint.h>

#include "lock.h"
#include "devtag-allinone.h"
#include "manchester.h"
#include "udp_ip.h"
#include "tftp.h"
#include "timers.h"
#include "ax25.h"
#include "util.h"

#ifdef	__cplusplus
extern "C" {
#endif

//#define AX25_ENABLED 1
#define ETHERNET_ENABLED 0

#define TEMPFILE_PREFIX "linefeed\0"
#define TEMPFILE_POSTFIX ".txt\0"
#define RADIOTFTP_COMMAND_PUT	"put"
#define RADIOTFTP_COMMAND_GET	"get"
#define RADIOTFTP_COMMAND_APPEND_FILE	"appendfile"
#define RADIOTFTP_COMMAND_APPEND_LINE	"appendline"
#define RADIOTFTP_COMMAND_BEACON "beacon"

#define RADIOTFTP_MODE_UHF						433925
#define RADIOTFTP_BIM2A_BAUD_RATE 				B19200
#define RADIOTFTP_BIM2A_TX_PREDELAY 			5000ul
#define RADIOTFTP_BIM2A_TX_POSTDELAY        	75000ul
#define RADIOTFTP_BIM2A_TX_MESSAGE_INTERVAL 	100000ul
#define RADIOTFTP_BIM2A_ACK_TIMEOUT_MIN 		1
#define RADIOTFTP_BIM2A_ACK_TIMEOUT_MAX 		3
#define RADIOTFTP_BIM2A_READ_TIMEOUT			5
#define RADIOTFTP_BIM2A_PREAMBLE_LENGTH			8
#define RADIOTFTP_BIM2A_EVENTLOG				"radiotftp_uhf-event.log"

#define RADIOTFTP_MODE_VHF						144075
#define RADIOTFTP_UHX1_BAUD_RATE 				B4800
#define RADIOTFTP_UHX1_TX_PREDELAY 				60000ul
#define RADIOTFTP_UHX1_TX_POSTDELAY 			250000ul
#define RADIOTFTP_UHX1_TX_MESSAGE_INTERVAL 		100000ul
#define RADIOTFTP_UHX1_ACK_TIMEOUT_MIN 			2
#define RADIOTFTP_UHX1_ACK_TIMEOUT_MAX 			4
#define RADIOTFTP_UHX1_READ_TIMEOUT				6
#define RADIOTFTP_UHX1_PREAMBLE_LENGTH			10
#define RADIOTFTP_UHX1_EVENTLOG					"radiotftp_vhf-event.log"

#define RADIOTFTP_MODE_SERIAL					999999
#define RADIOTFTP_SERIAL_BAUD_RATE 				__MAX_BAUD
#define RADIOTFTP_SERIAL_TX_PREDELAY 			60ul
#define RADIOTFTP_SERIAL_TX_POSTDELAY 			250ul
#define RADIOTFTP_SERIAL_TX_MESSAGE_INTERVAL	100ul
#define RADIOTFTP_SERIAL_ACK_TIMEOUT_MIN 		1
#define RADIOTFTP_SERIAL_ACK_TIMEOUT_MAX 		2
#define RADIOTFTP_SERIAL_READ_TIMEOUT			3
#define RADIOTFTP_SERIAL_PREAMBLE_LENGTH		1
#define RADIOTFTP_SERIAL_EVENTLOG				"radiotftp_serial-event.log"

#define RADIOTFTP_MAX_PREAMBLE_LENGTH 			20

uint8_t setRTS(uint8_t level);

TIMER_HANDLER_FUNCTION_PROTO(idle_timer_handler);
void safe_exit(int retVal);
void sigINT_handler(int sig);
void sigIO_handler(int status);
void sigRTALRM_handler( int sig);
uint8_t queueSerialData(uint8_t* src, uint16_t src_port, uint8_t* dst, uint16_t dst_port, uint8_t* dataptr, uint16_t datalen);
uint16_t transmitSerialData(void);
uint8_t udp_packet_demultiplexer(uint8_t* src, uint16_t src_port, uint8_t* dst, uint16_t dst_port, uint8_t* payload, uint16_t len);
int main(int ac, char *av[]);



#ifdef	__cplusplus
}
#endif

#endif	/* TTY_TALK_H */

