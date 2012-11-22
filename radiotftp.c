/*
 *
 *	TFTP Application to work with a Radiometrix Radio Module connected through serial port
 *	Alp Sayin
 *	20.04.2012
 *
 */

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

#include "radiotftp.h"
#include "lock.h"
#include "devtag-allinone.h"
#include "manchester.h"
#include "udp_ip.h"
#include "tftp.h"
#include "timers.h"
#include "ax25.h"
#include "util.h"
#include "datacollection.h"

#define END_OF_FILE 28
#define CTRLD  4
#define P_LOCK "/var/lock"

#define HELLO_WORLD_PORT 12345

FILE* tempFile;
char tempFileName[64];
char dial_tty[128];

const uint8_t my_ip_address[6]=
{ 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6 };

#define SYNC_LENGTH 4
const unsigned char syncword[SYNC_LENGTH]=
{ 0xAA, 0x55, 0xAA, 0x55 };

#define BYTE_ERROR_PROB 0 //0.0002 //FOR TESTING PURPOSES ONLY!!!
//reception
uint8_t command_buffer[256];
#if AX25_ENABLED==1
const uint8_t my_ax25_callsign[7]="SA0BXI\x0a";
uint8_t ax25_buffer[AX25_MAX_PAYLOAD_LENGTH + AX25_TOTAL_HEADERS_LENGTH];
uint8_t manchester_buffer[sizeof(ax25_buffer) * 2];
#else
uint8_t manchester_buffer[(UDP_MAX_PAYLOAD_LENGTH + UDP_TOTAL_HEADERS_LENGTH) * 2];
#endif
uint8_t transmit_buffer[sizeof(manchester_buffer) + 20 + SYNC_LENGTH + 2];
uint8_t udp_buffer[UDP_MAX_PAYLOAD_LENGTH + 20];
uint16_t transmit_length=0;

//transmission
#define IO_BUFSIZE 2048
uint8_t io_index=0;
uint8_t io[IO_BUFSIZE];
uint8_t buf[sizeof(manchester_buffer) + RADIOTFTP_MAX_PREAMBLE_LENGTH + SYNC_LENGTH + 2];

/* Default options */
int background=0;
long baud=-1;
int mode=-1;
int preamble_length=-1;

struct termios tp, old;
struct sigaction sa_io; //definition of signal action
struct sigaction sa_alarm;

int serialportFd;
int restore=0;
volatile uint8_t io_flag=0;
volatile uint8_t alarm_flag=0;
volatile uint8_t timer_flag=0;
volatile uint8_t queue_flag=0;
volatile uint8_t idle_flag=0;

uint8_t eth_src[6], eth_dst[6];
uint8_t udp_src[6], udp_dst[6];
uint16_t udp_src_prt, udp_dst_prt;

#if PREAMBLE_LENGTH > 15
#error preamble length cant be longer than 15
#endif

int createTempFile(char* prefix, char* postfix)
{
	int length;
	struct timeval curtime;
	gettimeofday(&curtime, NULL);
	length=sprintf(tempFileName, "%s-%ld:%ld:%ld%s\0", prefix, GMT_OFFSET + (curtime.tv_sec % (3600 * 24)) / 3600, (curtime.tv_sec % 3600) / 60, curtime.tv_sec % 60, postfix);
	tempFile=fopen(tempFileName, "wb");
	return length;
}
int deleteTempFile()
{
	if(tempFile != NULL)
	{
		fflush(tempFile);
		fclose(tempFile);
		while(remove(tempFileName))
			;
	}
	return 0;
}

uint8_t setRTS(uint8_t level)
{
	int status;

	if(level)
		registerEvent("TX", "enabled");
	else
		registerEvent("RX", "enabled");

	if(ioctl(serialportFd, TIOCMGET, &status) == -1)
	{
		//perror("setRTS()");
		return 0;
	}

	if(level)
		status|=TIOCM_RTS;
	else
		status&=~TIOCM_RTS;

	if(ioctl(serialportFd, TIOCMSET, &status) == -1)
	{
		//perror("setRTS()");
		return 0;
	}

	return 1;
}

TIMER_HANDLER_FUNCTION(idle_timer_handler)
{
	//printf("idle timer handler\n");
	idle_flag=1;
	return 0;
}

void safe_exit(int retVal)
{
	if(restore)
	{
		if(tcsetattr(serialportFd, TCSANOW, &old) < 0)
		{
			perror("Couldn't restore term attributes");
			exit(-1);
		}
	}
	printf("exiting...\n");
	registerEvent("exit", "");
	closeLogFile();
	deleteTempFile();
	lockfile_remove();
	exit(retVal);
}

void sigINT_handler(int sig)
{
	signal(sig, SIG_IGN);
	safe_exit(0);
}

uint64_t signalCount=0;
void sigIO_handler(int status)
{
	//fputs("sigio\n", stdout);
	io_flag=1;
	signalCount++;
}

void sigRTALRM_handler(int sig)
{
	//printf("main timer handler\n");
	timer_flag=1;

}

uint8_t queueSerialData(uint8_t* src, uint16_t src_port, uint8_t* dst, uint16_t dst_port, uint8_t* dataptr, uint16_t datalen)
{
	uint16_t idx=0, len=0, j=0;
	const uint8_t null_address[6]=
	{ 0, 0, 0, 0, 0, 0 };

	if(queue_flag)
	{
		return -1;
	}

	memset(transmit_buffer, 0x55, preamble_length);
	idx+=preamble_length;

	memcpy(transmit_buffer + idx, syncword, SYNC_LENGTH);
	idx+=SYNC_LENGTH;

	if(!memcmp(src, null_address, 6))
	{
		printf("WARNING! ABOUT TO SEND AN EMPTY PACKET!\n");
		printf("src=");
		print_addr_dec(src);
		printf("src_port=%d\n", src_port);
		printf("dst=");
		print_addr_dec(dst);
		printf("dst_port=%d\n", dst_port);
		printAsciiHex(dataptr, datalen);
	}

	//printf("udp payload: %s\n", dataptr);
	len=udp_create_packet(src, src_port, dst, dst_port, dataptr, datalen, udp_buffer);
	if(len == 0)
	{
		fprintf(stderr, "couldn't prepare udp packet\n");
		return -2;
	}

#if AX25_ENABLED==1
	//printf("ax25 payload: %s\n", udp_buffer);
	len=ax25_create_ui_packet(ax25_get_local_callsign(NULL), ax25_get_broadcast_callsign(NULL), udp_buffer, len, ax25_buffer);
	if(len == 0)
	{
		fprintf(stderr, "couldn't prepare ax25 packet\n");
		return -3;
	}
	len=manchester_encode(ax25_buffer, manchester_buffer, len);
#else
	len=manchester_encode(udp_buffer, manchester_buffer, len);
#endif

//	transmit_buffer[idx++] = (len&0xFF); //further work
//	transmit_buffer[idx++] = ((len>>8)&0xFF); //further work

	memcpy(&transmit_buffer[idx], manchester_buffer, len);
	idx+=len;

	transmit_buffer[idx++]=END_OF_FILE;
	transmit_buffer[idx++]=0;

	queue_flag=1;
	transmit_length=idx;

	//print_time("data queued");

	return 0;
}
uint16_t transmitSerialData(void)
{
	uint16_t i, res=0;
	int fd_flags=0;
	struct sigaction save_buffer[2];

	fd_flags=fcntl(serialportFd, F_GETFL);
	fcntl(serialportFd, F_SETFL, O_RDWR | O_SYNC);

//	tcflush(serialportFd, TCIFLUSH);
//	tcflush(serialportFd, TCOFLUSH);
//
//	if(tcsetattr(serialportFd, TCSANOW, &tp)<0)
//	{
//		perror("Couldn't set term attributes");
//		return -1;
//	}

	sigaction(SIGALRM, NULL, save_buffer);

	setRTS(1);
	switch(mode)
	{
		case RADIOTFTP_MODE_UHF:
			usleep(RADIOTFTP_BIM2A_TX_PREDELAY);
			break;
		case RADIOTFTP_MODE_VHF:
			usleep(RADIOTFTP_UHX1_TX_PREDELAY);
			break;
		default:
			perror("unknown baud rate");
			safe_exit(-1);
			break;
	}
	{
#if 0
		res = write(serialportFd, transmit_buffer, transmit_length);
#else
		printf("transmit_length=%d\n", transmit_length);
		srand(time(NULL));
		for(i=0; i < transmit_length; i++)
		{
			double myFloatRandom=(((float) rand()) / (float) RAND_MAX);
			if(myFloatRandom < BYTE_ERROR_PROB)
			{
				printf("%f < %f [0x%02X]=", myFloatRandom, BYTE_ERROR_PROB, transmit_buffer[i]);
				transmit_buffer[i]^=0xFF;
				printf("[0x%02X]\n", transmit_buffer[i]);
			}
			res=write(serialportFd, transmit_buffer + i, 1);
			//fprintf(stdout, "%c", io[i]);
			// Potential preamble problem??
			//usleep(10000);
			if(res < 0)
			{
				printf("serial write failed\n");
				return -3;
			}
		}
#endif

		if(res < 0)
		{
			return -2;
		}
		//wait for the buffer to be flushed
		tcdrain(serialportFd);
		switch(mode)
		{
			case RADIOTFTP_MODE_UHF:
				usleep(RADIOTFTP_BIM2A_TX_POSTDELAY);
				break;
			case RADIOTFTP_MODE_VHF:
				usleep(RADIOTFTP_UHX1_TX_POSTDELAY);
				break;
			default:
				perror("unknown baud rate");
				safe_exit(-1);
				break;
		}
	}

	sigaction(SIGALRM, save_buffer, NULL);

	setRTS(0);

	fcntl(serialportFd, F_GETFL);
	fcntl(serialportFd, F_SETFL, fd_flags | O_RDWR | O_ASYNC);

//	print_time("data sent");

	return 0;
}
uint8_t udp_packet_demultiplexer(uint8_t* src, uint16_t src_port, uint8_t* dst, uint16_t dst_port, uint8_t* payload, uint16_t len)
{
	uint8_t retVal;
//    printf("== udp packet handler ==\n");
//    printf("== SRC: %02x.%02x.%02x.%02x.%02x.%02x:%d ==\n", src[0], src[1], src[2], src[3], src[4], src[5], src_port);
//    printf("== DST: %02x.%02x.%02x.%02x.%02x.%02x:%d ==\n", dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst_port);

	uint8_t different=0;

	//check for address match
	different=memcmp(udp_get_localhost_ip(NULL), dst, IPV4_DESTINATION_LENGTH);
	if(different)
	{
		different=memcmp(udp_get_broadcast_ip(NULL), dst, IPV4_DESTINATION_LENGTH);
	}

	if(!different)
	{
		if(dst_port == 69)
		{
//			printf("tftp negotiate port\n");
			retVal=tftp_negotiate(src, src_port, dst, dst_port, payload, len - 8);
			if(retVal)
				printf("tftp_negotiate(), error=%d\n", retVal);
		}
		else if(dst_port == tftp_transfer_src_port())
		{
//			printf("tftp transfer port\n");
			retVal=tftp_transfer(src, src_port, dst, dst_port, payload, len - 8);
			if(retVal)
				printf("tftp_transfer(), error=%d\n", retVal);
		}
		else if(dst_port == HELLO_WORLD_PORT)
		{
			printf("New neighbour:\nIP = %x.%x.%x.%x.%x.%x\n", src[0], src[1], src[2], src[3], src[4], src[5]);
		}
		else
		{
			printf("packet received for unassigned port: %d from ", dst_port);
			print_addr_dec(src);
		}
	}
	else
	{
		//nothing to do here yet
		//may be for routing purposes...
		printf("packet for someone else: ");
		print_addr_dec(dst);
		printf("port: %d\n", dst_port);

	}
	return 0;
}
int main(int ac, char *av[])
{
	//TODO extract the serial port codes into another source file, this is too ugly!
	uint16_t i, j, len;
	int16_t res=0, result=0;
	uint8_t destination_ip[32];
	uint8_t outbuf[512];
	uint8_t linebuf[32];
	uint8_t local_filename[32]="\0";
	FILE* sptr;

	if(ac == 1)
		usage();

	if(!strcmp("radiotftp_uhf", av[0]) || !strcmp("./radiotftp_uhf", av[0]))
	{
		mode=RADIOTFTP_MODE_UHF;
		preamble_length=RADIOTFTP_BIM2A_PREAMBLE_LENGTH;
		baud=RADIOTFTP_BIM2A_BAUD_RATE;
		printf("Running with UHF band 19200 baud version\n");
		openLogFile(RADIOTFTP_BIM2A_EVENTLOG);
	}
	else if(!strcmp("radiotftp_vhf", av[0]) || !strcmp("./radiotftp_vhf", av[0]))
	{
		mode=RADIOTFTP_MODE_VHF;
		preamble_length=RADIOTFTP_UHX1_PREAMBLE_LENGTH;
		baud=RADIOTFTP_UHX1_BAUD_RATE;
		printf("Running with VHF band 2400 baud version\n");
		openLogFile(RADIOTFTP_UHX1_EVENTLOG);
	}
	else
	{
		mode=RADIOTFTP_MODE_VHF;
		preamble_length=RADIOTFTP_UHX1_PREAMBLE_LENGTH;
		baud=RADIOTFTP_UHX1_BAUD_RATE;
		printf("Running with VHF band 2400 baud version\n");
		openLogFile(RADIOTFTP_UHX1_EVENTLOG);
	}

	//setting defaults
	udp_get_broadcast_ip(destination_ip);
	strcpy(dial_tty, "/dev/ttyUSB0");

	for(i=1; (i < ac) && (av[i][0] == '-'); i++)
	{
		if(strcmp(av[i], "-b") == 0)
		{
			background=1;
		}
		else if(strcmp(av[i], "-f") == 0)
		{
			strncpy(local_filename, av[i] + 2, 32);
			printf("different filename = '%s'\n", local_filename);
		}
		else if(strncmp(av[i], "-dst", 4) == 0)
		{
			memset(destination_ip, 0, 32);
			memcpy(destination_ip, av[i] + 4, strlen(av[i]) - 4);
			/*! convert text ip to numerical */
			text_to_ip(destination_ip, strlen(av[i]) - 4 + 1);
			printf("Destination: ");
			print_addr_dec(destination_ip);
		}
		else
			usage();

	}
	res=0;
	result=0;

	strncpy(dial_tty, devtag_get(av[i]), sizeof(dial_tty));

	while(!get_lock(dial_tty))
	{
		if(decrementLockRetries() == 0)
			exit(-1);
		sleep(1);
	}

	if((serialportFd=open(devtag_get(av[i]), O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
	{
		perror("bad terminal device, try another");
		exit(-1);
	}
	signal(SIGIO, SIG_IGN);
	signal(SIGINT, sigINT_handler);
	// Make the file descriptor asynchronous (the manual page says only
	// O_APPEND and O_NONBLOCK, will work with F_SETFL...)
	fcntl(serialportFd, F_GETFL);
	fcntl(serialportFd, F_SETFL, O_RDWR | O_SYNC);

	if(tcgetattr(serialportFd, &tp) < 0)
	{
		perror("Couldn't get term attributes");
		exit(-1);
	}
	old=tp;

	/*
	 SANE is a composite flag that sets the following parameters from termio(M):

	 CREAD BRKINT IGNPAR ISTRIP ICRNL IXON ISIG ICANON
	 ECHO ECHOK OPOST ONLCR

	 SANE also clears the following modes:

	 CLOCAL
	 IGNBRK PARMRK INPCK INLCR IUCLC IXOFF
	 XCASE ECHOE ECHONL NOFLSH
	 OLCUC OCRNL ONOCR ONLRET OFILL OFDEL NLDLY CRDLY
	 TABDLY BSDLY VTDLY FFDLY

	 */
	/* 8 bits + baud rate + local control */
	tp.c_cflag=baud | CS8 | CLOCAL | CREAD;
	tp.c_cflag&=~PARENB;
	tp.c_cflag&=~CSTOPB;
	tp.c_cflag&=~CSIZE;
	tp.c_cflag|=CS8;
	tp.c_cflag|=CRTSCTS;
	tp.c_oflag=0; /* Raw Input */
	tp.c_lflag=0; /* No conoical */
	tp.c_cc[VTIME]=0;
	tp.c_cc[VMIN]=1;

	/* ignore CR, ignore parity */
	//ISTRIP is a dangerous flag, it strips the 8th bit of bytes
	//tp.c_iflag= ~( ISTRIP ) | IGNPAR | IGNBRK;
	tp.c_iflag=~(IGNBRK | PARMRK | INPCK | ISTRIP | INLCR | IUCLC | IXOFF) | BRKINT | IGNPAR | ICRNL | IXON | ISIG | ICANON;

	/* set output and input baud rates */

	cfsetospeed(&tp, baud);
	cfsetispeed(&tp, baud);

	tcflush(serialportFd, TCIFLUSH);
	tcflush(serialportFd, TCOFLUSH);

	if(tcsetattr(serialportFd, TCSANOW, &tp) < 0)
	{
		perror("Couldn't set term attributes");
		goto error;
	}

	restore=1;

	//TODO go over the background codes
	if(background)
	{
		int i;
		if(getppid() == 1)
			return 0; /* Already a daemon */

		i=fork();

		if(i < 0)
			exit(1); /* error */

		if(i > 0)
			_exit(0); /* parent exits */

		/* child */

		setsid(); /* obtain a new process group */
		for(i=getdtablesize(); i >= 0; --i)
		{
			if(i == serialportFd)
				continue;
			if(i == 1)
				continue;
			close(i); /* close all descriptors */
		}

		i=open("/dev/null", O_RDWR);
		dup(i);
		dup(i); /* handle standard I/O */
		umask(027); /* set newly created file permissions */
		chdir("/"); /* change running directory */

	}

	srand((unsigned) time(NULL));

	j=0;
	i++;
	for(; i < ac; i++)
	{
		len=strlen(av[i]);
		memcpy(command_buffer + j, av[i], len);
		j+=len;
		if(i != ac - 1)
		{
			command_buffer[j++]=' ';
		}
	}

	//process the escape characters
	for(i=0; i < j; i++)
	{
		if(command_buffer[i] == '\\')
		{
			if(i + 1 < j)
			{
				if(command_buffer[i + 1] == 'n')
				{
					command_buffer[i]='\r';
					command_buffer[i + 1]='\n';
				}
			}
		}
	}

	timers_initialize(&sigRTALRM_handler);

	/*! read settings from radiotftp.conf file */
	sptr=fopen("radiotftp.conf", "r");
	if(sptr != NULL)
	{
		readnline(sptr, linebuf, 32);
		linebuf[6]=atoi(linebuf + 6);
		linebuf[8]=0;
		printf("AX.25 Callsign: ");
		print_callsign(linebuf);
#if AX_25_ENABLED==1
		ax25_initialize_network(linebuf);
		printf("USING AX25 LINK LAYER!!!\n");
#endif
		readnline(sptr, linebuf, 32);
		text_to_ip(linebuf, 32);
		printf("Eth Address: ");
		print_addr_hex(linebuf);
#if AX25_ENABLED==0
		printf("NOT USING ANY LINK LAYER!!!\n");
#endif
		readnline(sptr, linebuf, 32);
		text_to_ip(linebuf, 32);
		printf("IPv4 Address: ");
		print_addr_dec(linebuf);
		udp_initialize_ip_network(linebuf, &queueSerialData);
	}
	else
	{
#if AX25_ENABLED==1
		ax25_initialize_network(my_ax25_callsign);
		printf("AX.25 CALLSIGN = ");
		print_callsign(ax25_get_local_callsign(NULL));
#endif
		udp_initialize_ip_network(my_ip_address, &queueSerialData);
		printf("IPv4 Address = ");
		print_addr_dec(udp_get_localhost_ip(NULL));
	}

	switch(mode)
	{
		case RADIOTFTP_MODE_UHF:
			tftp_initialize(udp_get_data_queuer_fptr(), RADIOTFTP_BIM2A_ACK_TIMEOUT_MIN, RADIOTFTP_BIM2A_ACK_TIMEOUT_MAX, RADIOTFTP_BIM2A_READ_TIMEOUT);
			break;
		case RADIOTFTP_MODE_VHF:
			tftp_initialize(udp_get_data_queuer_fptr(), RADIOTFTP_UHX1_ACK_TIMEOUT_MIN, RADIOTFTP_UHX1_ACK_TIMEOUT_MAX, RADIOTFTP_UHX1_READ_TIMEOUT);
			break;
		default:
			perror("unknown baud rate");
			safe_exit(-1);
			break;
	}

	if(!strncasecmp(RADIOTFTP_COMMAND_PUT, command_buffer, strlen(RADIOTFTP_COMMAND_PUT)))
	{
		printf(RADIOTFTP_COMMAND_PUT"\n");
		registerEvent(RADIOTFTP_COMMAND_PUT, command_buffer + strlen(RADIOTFTP_COMMAND_PUT) + 1);
		if((res=tftp_sendRequest(TFTP_OPCODE_WRQ, destination_ip, local_filename, command_buffer + strlen(RADIOTFTP_COMMAND_PUT) + 1, j - strlen(RADIOTFTP_COMMAND_PUT) - 1, 0)))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}
	else if(!strncasecmp(RADIOTFTP_COMMAND_BEACON, command_buffer, strlen(RADIOTFTP_COMMAND_BEACON)))
	{
		printf(RADIOTFTP_COMMAND_BEACON"\n");
		registerEvent(RADIOTFTP_COMMAND_BEACON, command_buffer + strlen(RADIOTFTP_COMMAND_BEACON) + 1);
		if((res=tftp_sendSingleBlockData(destination_ip, command_buffer + strlen(RADIOTFTP_COMMAND_BEACON) + 1, j - strlen(RADIOTFTP_COMMAND_BEACON) - 1, local_filename)))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}
	else if(!strncasecmp(RADIOTFTP_COMMAND_APPEND_LINE, command_buffer, strlen(RADIOTFTP_COMMAND_APPEND_LINE)))
	{
		printf(RADIOTFTP_COMMAND_APPEND_LINE"\n");
		registerEvent(RADIOTFTP_COMMAND_APPEND_LINE, command_buffer + strlen(RADIOTFTP_COMMAND_APPEND_LINE) + 1);
		i=createTempFile(TEMPFILE_PREFIX, TEMPFILE_POSTFIX);
		if(tempFile == NULL)
		{
			perror("couldn't create temp file to store the line feed");
			goto error;
		}
		fwrite(command_buffer + strlen(RADIOTFTP_COMMAND_APPEND_LINE) + 1, 1, j - strlen(RADIOTFTP_COMMAND_APPEND_LINE) - 1, tempFile);
		fflush(tempFile);
		fclose(tempFile);
		if((res=tftp_sendRequest(TFTP_OPCODE_WRQ, destination_ip, tempFileName, local_filename, strlen(local_filename), 1)))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}
	else if(!strncasecmp(RADIOTFTP_COMMAND_APPEND_FILE, command_buffer, strlen(RADIOTFTP_COMMAND_APPEND_FILE)))
	{
		printf(RADIOTFTP_COMMAND_APPEND_FILE"\n");
		registerEvent(RADIOTFTP_COMMAND_APPEND_FILE, command_buffer + strlen(RADIOTFTP_COMMAND_APPEND_FILE) + 1);
		if((res=tftp_sendRequest(TFTP_OPCODE_WRQ, destination_ip, local_filename, command_buffer + strlen(RADIOTFTP_COMMAND_APPEND_FILE) + 1, j - strlen(RADIOTFTP_COMMAND_APPEND_FILE) - 1, 1)))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}
	else if(!strncasecmp(RADIOTFTP_COMMAND_GET, command_buffer, strlen(RADIOTFTP_COMMAND_GET)))
	{
		printf(RADIOTFTP_COMMAND_GET"\n");
		registerEvent(RADIOTFTP_COMMAND_GET, command_buffer + strlen(RADIOTFTP_COMMAND_GET) + 1);
		if((res=tftp_sendRequest(TFTP_OPCODE_RRQ, destination_ip, local_filename, command_buffer + strlen(RADIOTFTP_COMMAND_GET) + 1, j - strlen(RADIOTFTP_COMMAND_GET) - 1, 0)))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}
	else
	{
		printf("hello radio world!\n");
		registerEvent("helloWorld", "");
		if((res=queueSerialData(udp_get_localhost_ip(NULL), HELLO_WORLD_PORT, udp_get_broadcast_ip(NULL), HELLO_WORLD_PORT, "hello world\n\0x00", strlen("hello world\n\0x00"))))
		{
			printf("%d\n", res);
			perror("tftp request fail");
			goto error;
		}
	}

	int sync_counter=0;
	int sync_passed=0;
	int save_index=0;

	//entering the main while loop
	printf("started listening...\n");
	while(1)
	{
		if(timer_flag)
		{
			idle_timer_handler();
			tftp_timer_handler();
			timer_flag=0;
		}
		if(idle_flag)
		{
			//print_time("System Idle");
			idle_flag=0;
		}
		if(queue_flag)
		{
//			printf("queue flag\n");
			if(!sync_passed && sync_counter < 1)
			{
				//wait_some
				switch(mode)
				{
					case RADIOTFTP_MODE_UHF:
						usleep(RADIOTFTP_BIM2A_TX_MESSAGE_INTERVAL);
						break;
					case RADIOTFTP_MODE_VHF:
						usleep(RADIOTFTP_UHX1_TX_MESSAGE_INTERVAL);
						break;
					default:
						perror("unknown baud rate");
						safe_exit(-1);
						break;
				}
				transmitSerialData();
				queue_flag=0;
			}
		}
		if((res=read(serialportFd, io, IO_BUFSIZE)) > 0)
		{
			io_index=0;
			//printf("# of bytes read = %d\n", res);
			for(i=0; i < res; i++)
			{
				//putchar(io[i]);
				//printf("%\n",io[i]);
				if(sync_counter < SYNC_LENGTH && io[i] == syncword[sync_counter])
				{
					sync_counter++; /* sync continued */
					//printf("sync counting %d\n",sync_counter);
				}
				else
				{
					//printf("sync reset %d\n",sync_counter);
					sync_counter=0; /* not a preamble, reset counter */
				}
				if(sync_counter >= SYNC_LENGTH && sync_passed == 0)
				{ /* preamble passed */
					sync_passed=1;
				}
				if(sync_passed)
				{
					//printf("getting data '%c'\n", io[i]);
					if(io[i] == END_OF_FILE && !isManchester_encoded(io[i]))
					{
//							printf("non-manchester character received\n");
						outbuf[0]=0;
						result=manchester_decode(buf + 1, manchester_buffer, save_index);
#if AX25_ENABLED==1
						result=ax25_open_ui_packet(NULL, NULL, ax25_buffer, manchester_buffer, result);
#else
						result=1;
#endif
						if(result)
						{
//								printf("opened link-layer packet\n");
#if AX25_ENABLED==1
							result=udp_open_packet(udp_src, &udp_src_prt, udp_dst, &udp_dst_prt, udp_buffer, ax25_buffer);
#else
							result = udp_open_packet(udp_src, &udp_src_prt, udp_dst, &udp_dst_prt, udp_buffer, manchester_buffer);
#endif
							if(result)
							{
//									printf("opened transport-layer packet\n");
								udp_packet_demultiplexer(udp_src, udp_src_prt, udp_dst, udp_dst_prt, udp_buffer, result);
							}
							else
							{
								strcat(outbuf, "!udp discarded!");
								if(write(1, outbuf, strlen(outbuf)) <= 0)
								{
									fputs("couldn't write to tty\n", stderr);
								}
								if(write(1, "\n", 1) <= 0)
								{
									fputs("couldn't write to tty\n", stderr);
								}
							}
						}
						else
						{
							strcat(outbuf, "!ax25 discarded!");
							if(write(1, outbuf, strlen(outbuf)) <= 0)
							{
								fputs("couldn't write to tty\n", stderr);
							}
							if(write(1, "\n", 1) <= 0)
							{
								fputs("couldn't write to tty\n", stderr);
							}
						}
						sync_passed=0;
						sync_counter=0;
						save_index=0;
					}
					else
					{
//						printf("saved data '%c'\n", io[i]);
//						printf("save_index=%d/%d\n",save_index, sizeof(buf));
						if(save_index >= sizeof(buf))
						{
							sync_passed=0;
							sync_counter=0;
							save_index=0;
						}
						buf[save_index++]=io[i];
						buf[save_index + 1]=0;
//						printf("-\n%s\n-\n", buf);
					}
				}
			}
			io_flag=0;
		}
	}

	safe_exit(0);
	error: safe_exit(-1);

	return -2;
}
