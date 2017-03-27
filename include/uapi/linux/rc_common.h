/*
 * include/uapi/linux/rc_common.h from the kernel directory, but it can be used
 * in kernel and user space.
 *
 * Written by Amlogic
 *
 */
#ifndef _UAPI_RC_COMMON_H_
#define _UAPI_RC_COMMON_H_

#include <linux/types.h>

#define MAX_KEYMAP_SIZE 256
#define CUSTOM_NAME_LEN 64

/*to ensure kernel and user spase use the same header file*/
#define SHARE_DATA_VERSION "v1.0.0"

union _codemap {
	struct ir_key_map {
		__u16 keycode;
		__u16 scancode;
		} map;
	__u32 code;
};
/**
 *struct ir_map_table - the IR key map table for different remote-control
 *
 *@custom_name: table name
 *@map_size: number of IR key
 *@custom_code: custom code, identify different key mapping table
 *@release_delay: release delay time
 *@codemap[0]: code for IR key
 */
struct ir_map_tab {
	char custom_name[CUSTOM_NAME_LEN];
	__u16 map_size;
	__u32 custom_code;
	__u32 release_delay;
	union _codemap codemap[0];
};

/**
 *struct ir_sw_decode_para - configuration parameters for software decode
 *
 *@max_frame_time: maximum frame time
 */
struct ir_sw_decode_para {
	unsigned int  max_frame_time;
};

/*IOCTL commands*/
#define REMOTE_IOC_SET_KEY_NUMBER        _IOW('I', 3, __u32)
#define REMOTE_IOC_SET_KEY_MAPPING_TAB   _IOW('I', 4, __u32)
#define REMOTE_IOC_SET_SW_DECODE_PARA    _IOW('I', 5, __u32)
#define REMOTE_IOC_GET_DATA_VERSION      _IOR('I', 121, __u32)

#endif