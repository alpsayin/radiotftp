
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include "udp_ip.h"
#include "tftp.h"
#include "timers.h"
#include "radiotftp.h"
#include "util.h"
#include "datacollection.h"

static FILE* fptr=NULL;
static uint32_t fileLen;
static message_t lastMessage;
static timer_t tftp_timer;
static uint8_t status=TFTP_STATUS_IDLE;
static uint8_t timeouts=0;
static uint8_t delay_flag=0, isRequestOwner=0, read_timeout_flag=0;
static uint16_t blockNumber=0;
static uint16_t ackNumber=0;
static uint16_t tftp_dst_port=70;
static uint16_t tftp_src_port=71;
static uint8_t tftp_peer[6];
static dataQueuerfptr_t mainDataQueuer;
static uint32_t tftp_min_ack_timeout;
static uint32_t tftp_max_ack_timeout;
static uint32_t tftp_read_timeout;

uint8_t tftp_initialize(dataQueuerfptr_t dataQueuer, uint32_t min_ack_timeout, uint32_t max_ack_timeout, uint32_t read_timeout)
{
    mainDataQueuer=dataQueuer;
    srand(time(NULL));
    tftp_min_ack_timeout=min_ack_timeout;
    tftp_max_ack_timeout=max_ack_timeout;
    tftp_read_timeout=read_timeout;
    tftp_setStatus(TFTP_STATUS_IDLE);
    //for testing
    //timers_create_timer("TFTP Timer", &tftp_timer, 3, 0);
    return 0;
}
timer_t tftp_getTimerID(void)
{
    return tftp_timer;
}

void tftp_setStatus(uint8_t newStatus)
{
    status=newStatus;
}
uint16_t tftp_transfer_src_port(void)
{
    return tftp_src_port;
}
uint16_t tftp_transfer_dst_port(void)
{
    return tftp_dst_port;
}
uint8_t tftp_getRandomRetransmissionTime(void)
{
    uint8_t time = tftp_min_ack_timeout + (tftp_max_ack_timeout-tftp_min_ack_timeout+1)*(((float)rand())/((float)RAND_MAX));
    //printf("time=%d\n", time);
    return time;
}
uint8_t tftp_sendSingleBlockData(uint8_t* dst_ip, uint8_t* data_ptr, uint16_t data_len, uint8_t* remote_filename)
{
	uint8_t filenameCheck=0;

	if(data_len>450)
	{
		printf("input data too large, %d\n", data_len);
		safe_exit(-1);
	}
	printf("destination = ");
	print_addr_dec(dst_ip);
	printf("local-data-length = %d\n", data_len);
	filenameCheck |= (data_ptr==NULL);
	filenameCheck = filenameCheck<<1;
	printf("remote-filename = '%s'\n", remote_filename);
	filenameCheck |= (remote_filename==NULL || remote_filename[0]==0x00 );
	printf("filename-check = 0x%02x\n", filenameCheck);
	if(filenameCheck & 0x02)
	{
		printf("empty local data ptr\n");
		safe_exit(-1);
	}
	if(filenameCheck & 0x01)
	{
		printf("empty remote filename, using default filename = "TFTP_DEFAULT_FILENAME"\n");
		remote_filename=TFTP_DEFAULT_FILENAME;
	}
	if(filenameCheck == 0x03)
	{
		perror("empty input filenames");
		safe_exit(-1);
	}
    lastMessage.payloadLength=0;
    //put opcode in
    lastMessage.opcode=TFTP_OPCODE_WRQ_SINGLE;
    //put source ip in
    udp_get_localhost_ip(lastMessage.src);
    //put destination ip in
    memcpy(lastMessage.dst, dst_ip, 6);
    //select destination port
    lastMessage.dst_port=69;
    //select a random src port
    do
    {
        lastMessage.src_port= 65535*(((float)rand())/((float)RAND_MAX));
    }while(lastMessage.src_port==69 || lastMessage.src_port==0);
    //assign the tft_src_port
    tftp_src_port=lastMessage.src_port;
    printf("tftp src port = %d\n", tftp_src_port);
    //create the payload
    lastMessage.payload[lastMessage.payloadLength++] = 0x00;
    lastMessage.payload[lastMessage.payloadLength++] = TFTP_OPCODE_WRQ_SINGLE;
    memcpy(lastMessage.payload+lastMessage.payloadLength, remote_filename, strnlen(remote_filename, 16));
    lastMessage.payloadLength+=strnlen(remote_filename, 16);
    printf("remote_filename = '%s'\n", remote_filename);
    memcpy(lastMessage.payload+lastMessage.payloadLength, "\0netascii\0", 10);
    lastMessage.payloadLength+=10;
    lastMessage.append=0;
	//FIXME either I'm sending too much data, or I'm reading too much data while writing to file
    memcpy(&(lastMessage.payload[lastMessage.payloadLength]), data_ptr, data_len);
    lastMessage.payloadLength+=data_len;

    //put the block number in
    lastMessage.blockNumber = blockNumber=0;

    //set a timer to exit after a certain amount of time
	timers_create_timer(TFTP_SINGLE_BLOCK_WAIT_TIME, 0, 128000, 0);


    //reset acks
    ackNumber=-1;
    blockNumber=0;
    isRequestOwner=1;
    timeouts=0;
    return mainDataQueuer(udp_get_localhost_ip(NULL), lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);

}
uint8_t tftp_sendRequest(uint8_t opcode, uint8_t* dst_ip, uint8_t* local_filename, uint8_t* remote_filename, uint8_t remote_filename_len, uint8_t append)
{
	uint8_t filenameCheck=0;
	printf("destination = ");
	print_addr_dec(dst_ip);
	printf("local-filename = '%s'\n", local_filename);
	filenameCheck |= (local_filename[0]==0x00 || local_filename==NULL);
	filenameCheck = filenameCheck<<1;
	printf("remote-filename = '%s' -> %d\n", remote_filename, remote_filename_len);
	filenameCheck |= (remote_filename==NULL || remote_filename_len==0 || remote_filename[0]==0x00 );
	printf("filename-check = 0x%02x\n", filenameCheck);
	if(filenameCheck & 0x02)
	{
		printf("empty local filename, copying remote filename\n");
		local_filename=remote_filename;
	}
	if(filenameCheck & 0x01)
	{
		printf("empty remote filename, copying local filename\n");
		remote_filename=local_filename;
		remote_filename_len=strnlen(local_filename, 32);
	}
	if(filenameCheck == 0x03)
	{
		perror("empty input filenames");
		safe_exit(-1);
	}
    if(opcode == TFTP_OPCODE_RRQ)
    {
        tftp_setStatus(TFTP_STATUS_RECEIVING);
        if(append)
        {
        	fptr = fopen((char*)local_filename, "ab+");
        }
        else
        {
        	fptr = fopen((char*)local_filename, "wb+");
        }
        if(fptr == NULL)
        {
            perror("error opening file for write");
            safe_exit(-12);
        }
        fseek(fptr, 0, SEEK_END);
        fileLen=ftell(fptr);
        fseek(fptr, 0, SEEK_SET);
    }
    else if(opcode == TFTP_OPCODE_WRQ)
    {
        tftp_setStatus(TFTP_STATUS_SENDING);
        fptr = fopen((char*)local_filename, "rb+");
        if(fptr == NULL)
        {
            perror("error opening file for read");
            safe_exit(-13);
        }
        fseek(fptr, 0, SEEK_END);
        fileLen=ftell(fptr);
        fseek(fptr, 0, SEEK_SET);
    }
    else
    {
        return -10;
    }
    lastMessage.payloadLength=0;
    //put opcode in
    lastMessage.opcode=opcode;
    //put source ip in
    udp_get_localhost_ip(lastMessage.src);
    //put destination ip in
    memcpy(lastMessage.dst, dst_ip, 6);
    //select destination port
    lastMessage.dst_port=69;
    //select a random src port
    do
    {
        lastMessage.src_port= 65535*(((float)rand())/((float)RAND_MAX));
    }while(lastMessage.src_port==69 || lastMessage.src_port==0);
    //assign the tft_src_port
    tftp_src_port=lastMessage.src_port;
    printf("tftp src port = %d\n", tftp_src_port);
    //create the payload
    lastMessage.payload[lastMessage.payloadLength++] = 0x00;
    lastMessage.payload[lastMessage.payloadLength++] = opcode;
    memcpy(lastMessage.payload+lastMessage.payloadLength, remote_filename, remote_filename_len);
    lastMessage.payloadLength+=remote_filename_len;
    printf("remote_filename = '%s'\n", remote_filename);
    memcpy(lastMessage.payload+lastMessage.payloadLength, "\0netascii\0", 10);
    lastMessage.payloadLength+=10;
    if(append)
    {
    	memcpy(lastMessage.payload+lastMessage.payloadLength, "append\0", 7);
    	lastMessage.payloadLength+=7;
    	lastMessage.append=1;
    }
    else
    {
    	lastMessage.payload[lastMessage.payloadLength]='\0';
    	lastMessage.payloadLength++;
    	lastMessage.append=0;
    }
    //put the block number in
    lastMessage.blockNumber = blockNumber=0;
    	//set up retransmit timer
    if(opcode==TFTP_OPCODE_WRQ || opcode==TFTP_OPCODE_RRQ)
    {
    	timers_create_timer(tftp_getRandomRetransmissionTime()+1, 0, 128000, 0);
    }
    //reset acks
    ackNumber=-1;
    blockNumber=0;
    isRequestOwner=1;
    timeouts=0;
    return mainDataQueuer(udp_get_localhost_ip(NULL), lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);
}
uint8_t tftp_sendData(uint8_t* dst_ip, uint16_t blockNum)
{
    uint32_t curPos;
    lastMessage.payloadLength=0;
    //put opcode in
    lastMessage.opcode=TFTP_OPCODE_DATA;
    //put source ip in
    udp_get_localhost_ip(lastMessage.src);
    //put destination ip in
    memcpy(lastMessage.dst, dst_ip, 6);
    //select destination port
    lastMessage.dst_port=tftp_dst_port;
    //select a random src port
    lastMessage.src_port=tftp_src_port;
    //create the payload
    lastMessage.payload[lastMessage.payloadLength++] = 0x00;
    lastMessage.payload[lastMessage.payloadLength++] = lastMessage.opcode;
    lastMessage.payload[lastMessage.payloadLength++] = ((blockNum>>8)&0xFF);
    lastMessage.payload[lastMessage.payloadLength++] = blockNum&0xFF;
    //copy the data
    fseek(fptr, TFTP_MAX_BLOCK_SIZE*(blockNum-1), SEEK_SET);
    curPos=ftell(fptr);
    lastMessage.payloadLength+=fread(&(lastMessage.payload[lastMessage.payloadLength]), 1, TFTP_MAX_BLOCK_SIZE, fptr);

    //put the block number in
    lastMessage.blockNumber = blockNum;
    //set up retransmit timer
    timers_create_timer(tftp_getRandomRetransmissionTime(), 0, 128000, 0);
    printf("sent data size = %d\n", lastMessage.payloadLength);
    return mainDataQueuer(udp_get_localhost_ip(NULL), lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);
}
uint8_t tftp_sendError(uint8_t type, uint8_t* dst_ip, uint16_t dst_prt, uint8_t* additionalInfo, uint8_t infoLen)
{
    lastMessage.payloadLength=0;
    //put opcode in
    lastMessage.opcode=TFTP_OPCODE_ERROR;
    //put source ip in
    udp_get_localhost_ip(lastMessage.src);
    //put destination ip in
    memcpy(lastMessage.dst, dst_ip, 6);
    //select destination port
    lastMessage.dst_port=dst_prt;
    //select a random src port
    lastMessage.src_port=tftp_src_port;
    //create the payload
    lastMessage.payload[lastMessage.payloadLength++] = 0x00;
    lastMessage.payload[lastMessage.payloadLength++] = lastMessage.opcode;
    lastMessage.payload[lastMessage.payloadLength++] = 0x00;
    lastMessage.payload[lastMessage.payloadLength++] = type;
    //copy the info
    if(additionalInfo!=NULL)
    {
    	memcpy(lastMessage.payload+lastMessage.payloadLength, additionalInfo, infoLen);
        lastMessage.payloadLength+=infoLen;
    }
    //printf("sent error size = %d\n", lastMessage.payloadLength);
    return mainDataQueuer(udp_get_localhost_ip(NULL), lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);
}
uint8_t tftp_sendAck(uint8_t* dst_ip, uint16_t blockNum)
{
    uint16_t i=0;
    uint8_t buffer[4];
    lastMessage.payloadLength=0;
    //put opcode in
    lastMessage.opcode=TFTP_OPCODE_ERROR;
    //put source ip in
    udp_get_localhost_ip(lastMessage.src);
    //put destination ip in
    memcpy(lastMessage.dst, dst_ip, 6);
    //select destination port
    lastMessage.dst_port=tftp_dst_port;
    //select a random src port
    lastMessage.src_port=tftp_src_port;
    //create the payload
    buffer[i++]=0x00;
    buffer[i++]=TFTP_OPCODE_ACK;
    buffer[i++]= (blockNum>>8)&0xFF;
    buffer[i++]= (blockNum&0xFF);
    memcpy(lastMessage.payload, buffer, 4);
    lastMessage.payload[4]=0;
    //set ack length
    lastMessage.payloadLength=4;

    printf("sent ack size = %d\n", i);
    return mainDataQueuer(udp_get_localhost_ip(NULL), tftp_src_port, dst_ip, tftp_dst_port, buffer, i);
}


PACKET_HANDLER_FUNCTION(tftp_negotiate)
{
	int writed=0;
    uint8_t result=0;
    uint8_t filename[32];
    uint8_t mode[16];
    uint8_t comment[32];
    uint8_t filename_length=0, mode_length=0, comment_length=0;
    uint16_t opcode, i=0;

    //printf("%s\n", payload);
    //read in the opcode
    opcode = payload[i++] & 0xFF;
    opcode <<= 8;
    opcode |= payload[i++] & 0xFF;

    //check the opcode
    if(opcode!=TFTP_OPCODE_RRQ && opcode!=TFTP_OPCODE_WRQ && opcode!=TFTP_OPCODE_WRQ_SINGLE) //if not RRQ or WRQ
    {
        return -1;
    }
    //check if we are available
    //send busy message, unless it's the same guy who wants to open a connection
    //if it's a single block operation, we don't care if we are busy or not
    if(opcode!=TFTP_OPCODE_WRQ_SINGLE)
    {
		if(status!=TFTP_STATUS_IDLE && strncmp(tftp_peer, src, 6))
		{
			printf("I'm busy with %d.%d.%d.%d -> %d.%d.%d.%d : %d\n", tftp_peer[0], tftp_peer[1], tftp_peer[2], tftp_peer[3], src[0], src[1], src[2], src[3], src_port);
			result=tftp_sendError(TFTP_ERROR_SEE_MESSAGE, src, src_port, "I'm busy.\0", strlen("I'm busy.\0"));
			return -2;
		}
		else if(status!=TFTP_STATUS_IDLE && !memcmp(tftp_peer, src, 6))
		{
//			printf("fptr = [0x%x]\n", (int)fptr);
			if(fptr!=NULL)
			{
				fflush(fptr);
				fclose(fptr);
				fptr=NULL;
			}
			registerEvent("connection_cancel","override");
			printf("Previous connection canceled -> %d.%d.%d.%d : %d\n", src[0], src[1], src[2], src[3], src_port);
		}
    }
    //read in the filename
    while(payload[i]!=0x00 && i<34) //2+32
    {
        filename[filename_length++]=payload[i++];
    }

    filename[filename_length++]=0;
    //check the filename length and skip the null byte
    if(i==34 || payload[i++]!=0x00)
    {
        return -2;
    }
    //read in the mode, but ignore it
    while(payload[i]!=0x00 && i<51) //2+32+1+16
    {
        mode[mode_length++]=payload[i++];
    }
    mode[mode_length++]=0x00;
    //skip the null byte
    if(payload[i++]!=0x00)
    {
        return -3;
    }

    if(opcode==TFTP_OPCODE_WRQ_SINGLE)
    {
    	//for actual negotiation packets this section contains comment
    	//but for the single block operations this section contains data//open the file
        printf("opening file for single block write %s\n", filename);
        registerEvent("wrq_single_request",filename);
        fptr=fopen(filename, "ab+");
        if(fptr==NULL)
        {
        	printf("error opening file for append\n");
        	return -4;
        }
        //handle the data
        writed=fwrite(&(payload[i]), 1, len-i, fptr);
        fflush(fptr);
        while(writed<len-i)
        {
        	printf("missing write\n");
        	writed+=fwrite(&(payload[i+writed]), 1, len-i-writed, fptr);
            fflush(fptr);
        }
        printf("single block with length=%d written=%d\n", len, writed);
        if(fptr!=NULL)
        {
			fclose(fptr);
			fptr=NULL;
        }
        //send a finish error for courtesy, not really necessary
        //only send if we are not busy, we wouldn't want this error message to record itself as lastMessage
		if(status!=TFTP_STATUS_IDLE && strncmp(tftp_peer, src, 6))
		{
			result=tftp_sendError(TFTP_ERROR_SEE_MESSAGE, src, src_port, "TRANSMISSION COMPLETE: Closing NOW\0", strlen("TRANSMISSION COMPLETE: Closing NOW\0"));
		}
		registerEvent("connectionClose","success");
        printf("Connection closed\n");
        return 0;
    }
    //read in the comment
    while(payload[i]!=0x00 && i<85) //2+32+1+16+1+32
    {
    	comment[comment_length++]=payload[i++];
    }
    comment[comment_length++]=0;
    if(payload[i++]!=0x00)
    {
    	return -4;
    }
    printf("filename: '%s'\nmode: %s\ncomment: %s\n", filename, mode, comment);
    //set the active transfer ports
    tftp_dst_port=src_port;
    //select a random src port
    do
    {
        tftp_src_port= 65535*(((float)rand())/((float)RAND_MAX));
    }while(tftp_src_port==69 || tftp_src_port==0);

    printf("tftp src port = %d\n", tftp_src_port);
    if(opcode==TFTP_OPCODE_RRQ) //RRQ
    {
        //send in first data
        print_time("tftp RRQ connection received ");
        registerEvent("rrq_request",filename);
        //reset the block number
        blockNumber=0;
        ackNumber=0;
        //open the file
        printf("opening '%s'\n", filename);
        fptr=fopen(filename, "rb");
        if(fptr == NULL)
        {
            //perror("error opening the file");
            tftp_sendError(TFTP_ERROR_FILE_NOT_FOUND, src, tftp_dst_port, "Could not open file for read.\0", strlen("Could not open file for read.\0"));
            return -5;
        }
        fseek(fptr, 0, SEEK_END);
        fileLen=ftell(fptr);
        fseek(fptr, 0, SEEK_SET);
        memcpy(tftp_peer, src, 6);
        //send in the first part of file
        result=tftp_sendData(src, 1);
        status=TFTP_STATUS_SENDING;
        timeouts=0;
        if(result)
        {
            printf("!!! couldn't send data #%d\n", blockNumber);
        }
    }
    else if(opcode==TFTP_OPCODE_WRQ) //WRQ
    {
        //send in ACK 0
        print_time("tftp WRQ connection received ");
        registerEvent("wrq_request", filename);
        //reset the block number
        blockNumber=0;
        ackNumber=0;
        //open the file
        printf("opening %s\n", filename);
        if(!strncmp(comment, "append", 6)) //if comment says append
        {
        	fptr=fopen(filename, "ab+");
//        	fwrite("\n", 1, 1, fptr);
        }
        else
        {
        	fptr=fopen(filename, "wb");
        }
        if(fptr == NULL)
        {
        	tftp_sendError(TFTP_ERROR_ILLEGAL_OPERATION, src, tftp_dst_port, "Could not open file for write.\0", strlen("Could not open file for write.\0"));
//        	perror("error opening the file");
        	return -6;
        }
        fseek(fptr, 0, SEEK_END);
        fileLen=ftell(fptr);
        fseek(fptr, 0, SEEK_SET);
        //send ack
        memcpy(tftp_peer, src, 6);
        result=tftp_sendAck(src, blockNumber);
        tftp_setStatus(TFTP_STATUS_RECEIVING);
        timeouts=0;
        if(result)
        {
            printf("!!! couldn't send ack #%d\n", blockNumber);
        }
    }
    else
    {
        printf("shouldn't happen\n");
        return -7;
    }
    return result;
}
PACKET_HANDLER_FUNCTION(tftp_transfer)
{
    uint8_t result=0;
    uint16_t opcode, block, error, i=0;

    //printf("%s\n", payload);
    //read in the opcode
    opcode = payload[i++] & 0xFF;
    opcode <<= 8;
    opcode |= payload[i++] & 0xFF;

    if(lastMessage.opcode==TFTP_OPCODE_WRQ_SINGLE)
    {
    	if(opcode==TFTP_OPCODE_ERROR)
    	{
			//read in the error code
			error = payload[i++] & 0xFF;
			error <<= 8;
			error |= payload[i++] & 0xFF;

			if(error==TFTP_ERROR_SEE_MESSAGE)
			{
				printf("tftp error received -> %s\n", payload+i);
			}
			else
			{
				printf("tftp error received %d\n", error);
			}
			//return to pending status
			status=TFTP_STATUS_IDLE;
			if(isRequestOwner)
				safe_exit(-16);
    	}
    }
    //check the opcode
    if(status==TFTP_STATUS_RECEIVING)
    {
        if(opcode==TFTP_OPCODE_DATA)
        {
            //read in the block #
            block = payload[i++] & 0xFF;
            block <<= 8;
            block |= payload[i++] & 0xFF;
            tftp_dst_port=src_port;
            //whether or not the data is right unset the read_timeout_flag
            if(block==blockNumber+1) //if this is the next data packet
            {
            	int writed=0;
                //handle the data
                writed=fwrite(&(payload[i]), 1, len-4, fptr);
                fflush(fptr);
                while(writed<len-4)
                {
                	printf("missing write\n");
                	writed+=fwrite(&(payload[i+writed]), 1, len-4-writed, fptr);
                    fflush(fptr);
                }
                printf("block #%d length=%d written=%d\n", block, len, writed);
                //increment the block number
                blockNumber++;
            }
            else
            {
                //do nothing
                printf("duplicate block #%d length=%d\n", block, len);
            }
            //send an ack or an error in any case
        	timeouts=0;
            if(len < (TFTP_MAX_BLOCK_SIZE+TFTP_DATA_HEADER_SIZE))
            {
            	if(fptr!=NULL)
            	{
            		fclose(fptr);
    				fptr=NULL;
            	}
            	printf("size=%d -> EOF\n", len);
                result=tftp_sendError(TFTP_ERROR_SEE_MESSAGE, src, tftp_dst_port, "TRANSMISSION COMPLETE: Closing in 10 seconds\0", strlen("TRANSMISSION COMPLETE: Closing in 10 seconds\0"));
                delay_flag=1;
                registerEvent("connectionclose","timeout");
                timers_create_timer(TFTP_COMPLETE_TIMEOUT, 0, 0, 0);
                return 0;
            }
            else
            {
            	result=tftp_sendAck(src, blockNumber);
            	timers_create_timer(tftp_read_timeout, 0, 0, 0);
            	read_timeout_flag=block;
				if(result)
				{
					printf("!!! couldn't send ack #%d\n", blockNumber);
				}
            }
        }
        else if(opcode==TFTP_OPCODE_ERROR)
        {
            //read in the error code
            error = payload[i++] & 0xFF;
            error <<= 8;
            error |= payload[i++] & 0xFF;

            if(error==TFTP_ERROR_SEE_MESSAGE)
            {
                printf("tftp error received -> %s\n", payload+i);
                registerEvent("error", payload+i);
            }
            else
            {
                printf("tftp error received %d\n", error);
            }
            //return to pending status
            status=TFTP_STATUS_IDLE;
            if(isRequestOwner)
            	safe_exit(-16);
        }
        else
        {
            //silent discard
            return 0;
        }
    }
    else if(status==TFTP_STATUS_SENDING)
    {
        if(opcode==TFTP_OPCODE_ACK)
        {
            if(timers_cancel_timer())
                printf("couldnt cancel timer\n");
            block = payload[i++] & 0xFF;
            block <<= 8;
            block |= payload[i++] & 0xFF;
            ackNumber = block;
            printf("tftp wrq ack #%d received\n", block);
            //prepare and send next packet

            tftp_dst_port=src_port;
            timeouts=0;
            result=tftp_sendData(src, ackNumber+1);
            if(result)
            {
                printf("!!! couldn't send data #%d\n", ackNumber+1);
            }
            return 0;
        }
        else if(opcode==TFTP_OPCODE_ERROR)
        {
            //read in the error code
            error = payload[i++] & 0xFF;
            error <<= 8;
            error |= payload[i++] & 0xFF;

            if(error==TFTP_ERROR_SEE_MESSAGE)
            {
                printf("tftp error received -> %s\n", payload+i);
                if(!strncmp("TRANSMISSION COMPLETE", payload+i, strlen("TRANSMISSION COMPLETE")))
                {
                    if(isRequestOwner)
                        safe_exit(0);
                    ackNumber=lastMessage.blockNumber;
                }
                else
                {
                    registerEvent("error", payload+i);
                }
            }
            else
            {
                printf("tftp error received %d\n", error);
            }
            //return to pending status
            status=TFTP_STATUS_IDLE;
            if(isRequestOwner)
            	safe_exit(-16);
        }
        else
        {
            //silent discard
            return 0;
        }
    }
    else
    {
        //silent discard
        return 0;
    }

    return result;
}
TIMER_HANDLER_FUNCTION(tftp_timer_handler)
{
	//TODO something is really weird here with the control statements
	if(lastMessage.opcode==TFTP_OPCODE_WRQ_SINGLE)
	{
		printf("connection closed\n");
		if(isRequestOwner)
			safe_exit(0);
	}
	else
	{
		if(status==TFTP_STATUS_RECEIVING)
		{
			if(delay_flag)
			{
				blockNumber=0;
				ackNumber=0;
				tftp_setStatus(TFTP_STATUS_IDLE);
				delay_flag=0;
				printf("connection closed\n");
				if(isRequestOwner)
					safe_exit(0);
			}
		}
		if(status==TFTP_STATUS_SENDING)
		{
			//if the last taken block number is less than the last transmitted ack number
			//or if we sent a write request and couldn't get an ack yet
			if( (lastMessage.blockNumber>ackNumber) || (lastMessage.opcode==TFTP_OPCODE_WRQ))
			{
				timeouts++;
				printf("tftp ack timer timeout %d, timeouts=%d\n", ackNumber, timeouts);

				if(timeouts>=TFTP_MAX_TIMEOUTS)
				{
					tftp_setStatus(TFTP_STATUS_IDLE);
					blockNumber=0;
					ackNumber=0;
					printf("connection canceled\n");
					if(isRequestOwner)
						safe_exit(-18);
					return 0;
				}

				//set up retransmit timer
				timers_create_timer(tftp_getRandomRetransmissionTime(), 0, 128000, 0);
				//retransmit
				registerEvent("RETRANSMIT","data");
				return mainDataQueuer(lastMessage.src, lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);
			}
		}
		else if(status==TFTP_STATUS_RECEIVING)
		{
			//if we sent a read request and couldn't get any data yet
			if( (lastMessage.opcode==TFTP_OPCODE_RRQ && blockNumber==0) || read_timeout_flag==blockNumber)
			{
				timeouts++;
				printf("tftp rrq timer timeout %d, timeouts=%d, read_timeout_flag=%d\n", blockNumber, timeouts, read_timeout_flag);

				if(timeouts>=TFTP_MAX_TIMEOUTS)
				{
					tftp_setStatus(TFTP_STATUS_IDLE);
					blockNumber=0;
					ackNumber=0;
					printf("connection canceled\n");
					if(lastMessage.opcode==TFTP_OPCODE_RRQ)
						safe_exit(-14);
					return 0;
				}

				//set up retransmit timer
				timers_create_timer(tftp_getRandomRetransmissionTime(), 0, 128000, 0);
				//retransmit
				registerEvent("RETRANSMIT","ack");
				return mainDataQueuer(lastMessage.src, lastMessage.src_port, lastMessage.dst, lastMessage.dst_port, lastMessage.payload, lastMessage.payloadLength);
			}
		}
	}
	return 0;
}
