#ifndef _PROJECT_INFO_H_
#define _PROJECT_INFO_H_ 1
typedef		__u32		uint32;
typedef     __u8        uint8;

/*******SECURE_BOOTn = 0x00786078+ 0x4*n, n=[1..14]******/
#define SECURE_BOOT_BASE		0x00786078
#define SECURE_BOOT1			(SECURE_BOOT_BASE + 0x4*1)
#define BUF_SIZE		64

#define SMEM_PROJECT_INFO 136
#define NONDEFINE  -1

#include <linux/string.h>
#include <linux/kallsyms.h>

struct project_info {
    char project_name[8];
    char project_codename[20];
    char reservename[12];
    uint32  prj_version;
    uint32  hw_version;
    uint32  rf_v1;
    uint32  rf_v2;
    uint32  rf_v3;
    uint32  uart_boot_mode;
    uint32  platform_id;
    uint32  ddr_manufacture_info;
    uint32  ddr_row;
    uint32  ddr_column;
    uint32  ddr_fw_version;
    uint32  ddr_reserve_info;
    uint32  reserve01; /*reserve for feture use*/
    uint32  reserve02;
    uint32  reserve03;
    uint32  reserve04;
    uint32  reserve05;
    uint32  reserve06;
    uint32  reserve07;
    uint32  reserve08;
    uint32  reserve09;
};

struct component_info {
	char *version;
	char *manufacture;
};

enum{
	HW_VERSION__UNKNOWN,
	HW_VERSION__11 = 11,//all EVB
	HW_VERSION__12,	//T0
};

enum COMPONENT_TYPE {
	DDR,
	EMMC,
	F_CAMERA,
        SECOND_F_CAMERA,
	R_CAMERA,
	SECOND_R_CAMERA,
	THIRD_R_CAMERA,
        FORTH_R_CAMERA,
	R_MODULE,
	F_MODULE,
	R_OIS,
	SECOND_R_OIS,
	TP,
	LCD,
	WCN,
	I_SENSOR,
	G_SENSOR,
	M_SENSOR,
	GYRO,
	BACKLIGHT,
	MAINBOARD,
	/*Add new component here*/
	FINGERPRINTS,
	TOUCH_KEY,
	UFS,
	ABOARD,
	NFC,
	FAST_CHARGE,
	WIRELESS_CHARGE,
	CPU,
    RF_VERSION,
	COMPONENT_MAX,
};

int push_component_info(enum COMPONENT_TYPE type,
	char *version, char *manufacture);
int reset_component_info(enum COMPONENT_TYPE type);
uint32 get_hw_version(void);

#endif
