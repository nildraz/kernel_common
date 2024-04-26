#ifndef AVI_WOL_H
#define AVI_WOL_H

#define  WOL_ADD_TCP_FRAME        0
#define  WOL_DEL_TCP_FRAME        1
#define  WOL_ADD_UDP_FRAME        2
#define  WOL_DEL_UDP_FRAME        3
#define  WOL_ADD_UDP_BCAST_FRAME  4
#define  WOL_DEL_UDP_BCAST_FRAME  5
#define  WOL_ENABLE_ARP_FRAME     6
#define  WOL_DISABLE_ARP_FRAME    7
#define  WOL_FUNCTION_ONOFF       8
#define  WOL_LINK_CHANGE_ONOFF    9
#define  WOL_MAGIC_PACKET_ONOFF  10
#define  WOL_UNICAST_ONOFF       11
#define  WOL_DEL_ALL_FRAMES      12
#define  WOL_SET_FRAME           13

typedef struct _WakeupFrame
{
	unsigned short Mask[8];
	unsigned short pattern_len;
	unsigned char  Pattern[128];
} ST_wakeupFrame;

struct wake_frame_req {
	int cmd;
   union
	{
   	int port;
		ST_wakeupFrame frame;
	} param;
};

#endif /* AVI_WOL_H */

