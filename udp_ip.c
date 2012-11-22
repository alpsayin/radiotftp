
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "udp_ip.h"

static dataQueuerfptr_t mainDataQueuer;
static uint8_t local_ip_address[4]={127, 0, 0, 1};
static const uint8_t udp_broadcast_address[4]={ 255, 255, 255, 255};

static uint16_t udp_calculate_checksum(uint8_t* src_addr, uint8_t* dest_addr, uint8_t* payload, uint16_t udp_len)
{
    uint32_t i;
    uint8_t prot_udp=17;
    uint16_t word16;
    uint32_t sum;

    //initialize sum to zero
    sum=0;

    // make 16 bit words out of every two adjacent 8 bit words and
    // calculate the sum of all 16 vit words
    for (i=0; i<udp_len-8; i=i+2)
    {
        if(i+1<udp_len-8)
            word16 =((payload[i]<<8)&0xFF00)+(payload[i+1]&0x00FF);
        else
            word16 =((payload[i]<<8)&0xFF00);
        sum = sum + (uint32_t)word16;
    }

    // add the UDP pseudo header which contains the IP source and destination addresses
    for (i=0;i<IPV4_SOURCE_LENGTH;i=i+2)
    {
        word16 =((src_addr[i]<<8)&0xFF00)+(src_addr[i+1]&0xFF);
        sum=sum+word16;
    }
    for (i=0;i<IPV4_DESTINATION_LENGTH;i=i+2)
    {
        word16 =((dest_addr[i]<<8)&0xFF00)+(dest_addr[i+1]&0xFF);
        sum=sum+word16;
    }
    // the protocol number and the length of the UDP packet
    sum = sum + prot_udp + udp_len;

    // keep only the last 16 bits of the 32 bit calculated sum and add the carries
    while (sum>>16)
    {
        sum = (sum & 0xFFFF)+(sum >> 16);
    }

    // Take the one's complement of sum
    sum = ~sum;

    return ((uint16_t) (sum&0xFFFF));
}

dataQueuerfptr_t udp_get_data_queuer_fptr(void)
{
    return mainDataQueuer;
}

void udp_initialize_ip_network(uint8_t* myIpAddress, dataQueuerfptr_t dataQueuer)
{
    uint32_t i;
    for(i=0; i<IPV4_SOURCE_LENGTH; i++)
        local_ip_address[i] = myIpAddress[i];

    mainDataQueuer=dataQueuer;
}

uint8_t* udp_get_localhost_ip(uint8_t* ip_out)
{
    uint8_t i;

    if(ip_out!=NULL)
    {
        for(i=0; i<IPV4_SOURCE_LENGTH; i++)
            ip_out[i] = local_ip_address[i];
    }
    return local_ip_address;
}
uint8_t* udp_get_broadcast_ip(uint8_t* ip_out)
{
    uint8_t i;

    if(ip_out!=NULL)
    {
        for(i=0; i<IPV4_SOURCE_LENGTH; i++)
            ip_out[i] = udp_broadcast_address[i];
    }
    return udp_broadcast_address;
}

uint16_t udp_create_packet(uint8_t* src_in, uint16_t src_port, uint8_t* dst_in, uint16_t dst_port, uint8_t* payload_in, uint16_t payload_length, uint8_t* packet_out)
{
    uint16_t len=0, udp_checksum=0, total_length=0, tmp_len;
    uint32_t header_checksum=0;
    uint16_t *checksumPtr;
    uint8_t i;

    //check for input errors
    if(payload_length > UDP_MAX_PAYLOAD_LENGTH || packet_out==NULL)
        return 0;

    //IPv4 Headers

    //version and header length
    //version 4
    //length 5x32
    packet_out[IPV4_VERSIONnIHL_OFFSET]= ((0x04)<<4) | (0x05);
    len+=IPV4_VERSIONnIHL_LENGTH;

    //differentiated services code point and explicit congestion notification
    packet_out[IPV4_DSCPnECN_OFFSET] = (0x03<<4) | (0x00);
    len+=IPV4_DSCPnECN_LENGTH;

    //total length
    //add udp header length + ip header length
    total_length = payload_length+8+20;
    packet_out[IPV4_TOTAL_LENGTH_OFFSET]=((total_length>>8) & 0xFF);
    packet_out[IPV4_TOTAL_LENGTH_OFFSET+1]=(total_length & 0xFF);
    len+=IPV4_TOTAL_LENGTH_LENGTH;

    //fragment identification
    packet_out[IPV4_IDENTIFICATION_OFFSET]=0;
    packet_out[IPV4_IDENTIFICATION_OFFSET+1]=0;
    len+=IPV4_IDENTIFICATION_LENGTH;

    //flags and fragment offset
    //bit 1 for flags means dont fragment
    packet_out[IPV4_FLAGSnFRAGMENT_OFFSET_OFFSET]= (0x01<<5) | (0x00);
    packet_out[IPV4_FLAGSnFRAGMENT_OFFSET_OFFSET+1]= 0x00;
    len+=IPV4_FLAGSnFRAGMENT_OFFSET_LENGTH;

    //time to live
    packet_out[IPV4_TIME_TO_LIVE_OFFSET] = IPV4_TTL_LIMIT;
    len+=IPV4_TIME_TO_LIVE_LENGTH;

    //protocol
    packet_out[IPV4_PROTOCOL_OFFSET] = UDP_IPV4_PROTOCOL_NUMBER;
    len+=IPV4_PROTOCOL_LENGTH;

    //header checksum
    {
		tmp_len = len;
		checksumPtr = packet_out;
		while(tmp_len > 1)
		{
			header_checksum += *((uint16_t*) checksumPtr)++;
			if(header_checksum & 0x80000000)   /* if high order bit set, fold */
				header_checksum = (header_checksum & 0xFFFF) + (header_checksum >> 16);
			tmp_len -= 2;
		}

		if(tmp_len)       /* take care of left over byte */
		{
			header_checksum += (uint16_t) *(uint8_t *)checksumPtr;
		}

		while(header_checksum>>16)
		{
			header_checksum = (header_checksum & 0xFFFF) + (header_checksum >> 16);
		}
    }
    packet_out[IPV4_HEADER_CHECKSUM_OFFSET] = (header_checksum >> 8) & 0xFF;
    packet_out[IPV4_HEADER_CHECKSUM_OFFSET+1] = header_checksum & 0xFF;
    len+=IPV4_HEADER_CHECKSUM_LENGTH;

    //source address
    for(i=0; i<IPV4_SOURCE_LENGTH; i++)
        packet_out[IPV4_SOURCE_OFFSET+i]=src_in[i];
    len+=IPV4_SOURCE_LENGTH;

    //paste the destination address
    for(i=0; i<IPV4_DESTINATION_LENGTH; i++)
        packet_out[IPV4_DESTINATION_OFFSET+i]=dst_in[i];
    len+=IPV4_DESTINATION_LENGTH;

    //UDP Headers

    //source port
    packet_out[UDP_SOURCE_PORT_OFFSET]=((src_port>>8) & 0xFF);
    packet_out[UDP_SOURCE_PORT_OFFSET+1]=(src_port & 0xFF);
    len+=UDP_SOURCE_PORT_LENGTH;

    //destination port
    packet_out[UDP_DESTINATION_PORT_OFFSET]=((dst_port>>8) & 0xFF);
    packet_out[UDP_DESTINATION_PORT_OFFSET+1]=(dst_port & 0xFF);
    len+=UDP_DESTINATION_PORT_LENGTH;

    //udp length = data+udp headers
    //we've already added 8 to payload_length above, we don't do it again
    packet_out[UDP_LENGTH_OFFSET]=((payload_length>>8) & 0xFF);
    packet_out[UDP_LENGTH_OFFSET+1]=(payload_length & 0xFF);
    len+=UDP_LENGTH_LENGTH;

    //udp checksum
    //we've already added 8 to payload_length above, we don't do it again
    udp_checksum=udp_calculate_checksum(src_in, dst_in, payload_in, payload_length);
    packet_out[UDP_CHECKSUM_OFFSET]=((udp_checksum>>8) & 0xFF);
    packet_out[UDP_CHECKSUM_OFFSET+1]=(udp_checksum & 0xFF);
    len+=UDP_CHECKSUM_LENGTH;

    memcpy(packet_out+UDP_PAYLOAD_OFFSET, payload_in, payload_length-8);
    
    len+=payload_length-8;
    
    return len;
}

uint8_t udp_check_destination(uint8_t* my_dst, uint8_t* packet_dst, uint8_t* packet_in)
{
    uint8_t result;
    
    //check for address match
    result=memcmp(my_dst, packet_in+IPV4_DESTINATION_OFFSET, IPV4_DESTINATION_LENGTH);
    if(result)
    {
        result=memcmp(udp_broadcast_address, packet_in+IPV4_DESTINATION_OFFSET, IPV4_DESTINATION_LENGTH);
    }

    //copy the destination address in the packet
    if(packet_dst!=NULL)
    	memcpy(packet_dst, packet_in+IPV4_DESTINATION_OFFSET, IPV4_DESTINATION_LENGTH);
    
    return result;
}

uint16_t udp_open_packet_extended(uint8_t* src_out, uint16_t* src_port_out,
                                    uint8_t* dst_out, uint16_t* dst_port_out,
                                    uint8_t* payload_out,
                                    uint8_t* packet_in,
									uint8_t* version_out,
									uint8_t* headerlength_out,
									uint8_t* dscp_out,
									uint8_t* ecn_out,
									uint16_t* totallength_out,
									uint16_t* fragmentidentification_out,
									uint8_t* flags_out,
									uint16_t* fragmentoffset_out,
									uint8_t* ttl_out,
									uint8_t* protocol_out,
									uint8_t* headerchecksum_out,
									)
{
    uint16_t len=0;
    uint16_t udp_len_from_ip=0;
    uint16_t udp_len_from_udp=0;
    uint16_t udp_checksum=0;
    uint16_t calculated_checksum=0;

    //copy version and priority
    if(version_out!=NULL)
        *version_out=packet_in[IPV6_VERSION_PRIORITY_OFFSET] & 0xF0;

    if(priority_out!=NULL)
        *priority_out=packet_in[IPV6_VERSION_PRIORITY_OFFSET] & 0x0F;

    //copy flow label
    if(flow_label_out!=NULL)
    	memcpy(flow_label_out, packet_in+IPV6_FLOW_LABEL_OFFSET, IPV6_FLOW_LABEL_LENGTH);

    udp_len_from_ip = (packet_in[IPV6_PAYLOAD_LENGTH_OFFSET] & 0x00FF);
    udp_len_from_ip = udp_len_from_ip<<8;
    udp_len_from_ip |= (packet_in[IPV6_PAYLOAD_LENGTH_OFFSET+1] & 0x00FF);

    if(next_header_out!=NULL)
    	memcpy(next_header_out, packet_in+IPV6_NEXT_HEADER_OFFSET, IPV6_NEXT_HEADER_LENGTH);

    if(hop_limit_out!=NULL)
    	memcpy(hop_limit_out, packet_in+IPV6_HOP_LIMIT_OFFSET, IPV6_HOP_LIMIT_LENGTH);

    //copy source address
    if(src_out!=NULL)
    	memcpy(src_out, packet_in+IPV6_SOURCE_OFFSET, IPV6_SOURCE_LENGTH);

    //copy destination address
    if(dst_out!=NULL)
    	memcpy(dst_out, packet_in+IPV6_DESTINATION_OFFSET, IPV6_DESTINATION_LENGTH);

    //copy source port
    if(src_port_out!=NULL)
    {
        *src_port_out=packet_in[UDP_SOURCE_PORT_OFFSET] & 0xFF;
        *src_port_out<<=8;
        *src_port_out|=packet_in[UDP_SOURCE_PORT_OFFSET+1] & 0xFF;
    }

    //copy destination port
    if(dst_port_out!=NULL)
    {
        *dst_port_out=packet_in[UDP_DESTINATION_PORT_OFFSET] & 0xFF;
        *dst_port_out<<=8;
        *dst_port_out|=packet_in[UDP_DESTINATION_PORT_OFFSET+1] & 0xFF;
    }

    //copy and check udp length
    udp_len_from_udp = packet_in[UDP_LENGTH_OFFSET];
    udp_len_from_udp = udp_len_from_udp<<8;
    udp_len_from_udp |= packet_in[UDP_LENGTH_OFFSET+1] & 0xFF;


    if(udp_len_from_ip != udp_len_from_udp)
        return 0;
    else
        len = udp_len_from_udp;

    //copy checksum and check
    udp_checksum = (packet_in[UDP_CHECKSUM_OFFSET] & 0xFF);
    udp_checksum = udp_checksum<<8;
    udp_checksum |= (packet_in[UDP_CHECKSUM_OFFSET+1] & 0xFF);

    calculated_checksum=udp_calculate_checksum(packet_in+IPV6_SOURCE_OFFSET, packet_in+IPV6_DESTINATION_OFFSET, packet_in+UDP_PAYLOAD_OFFSET, len);
    
    if(udp_checksum != calculated_checksum)
        return 0;
    //finally copy the payload itself
    if(payload_out != NULL)
        memcpy(payload_out, packet_in+UDP_PAYLOAD_OFFSET, len);

    return len;
}

uint16_t udp_open_packet(uint8_t* src_out, uint16_t* src_port_out,
                                    uint8_t* dst_out, uint16_t* dst_port_out,
                                    uint8_t* payload_out,
                                    uint8_t* packet_in
                            )
{
    return udp_open_packet_extended(src_out, src_port_out, dst_out, dst_port_out, payload_out, packet_in, NULL, NULL, NULL, NULL, NULL);
}



