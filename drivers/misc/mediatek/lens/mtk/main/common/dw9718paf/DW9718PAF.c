/*
 * Copyright (C) 2022 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * DW9718PAF voice coil motor driver
 *
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

#include "lens_info.h"

#define AF_DRVNAME        "DW9718PAF_DRV"
#define AF_I2C_SLAVE_ADDR 0x18

#define AF_DEBUG

#ifdef  AF_DEBUG
#define LOG_INF(format, args...) pr_err(AF_DRVNAME " [%s] " format, __func__, ##args)
#else
#define LOG_INF(format, args...)
#endif

static struct i2c_client *g_pstAF_I2Cclient;
static int               *g_pAF_Opened;
static spinlock_t        *g_pAF_SpinLock;
static unsigned long     g_u4AF_INF;
static unsigned long     g_u4AF_MACRO = 1023;
static unsigned long     g_u4TargetPosition;
static unsigned long     g_u4CurrPosition;

static int               g_bootPdFlag =0;
//extern char CamNameB_module_name[256];

static int i2c_read(u8 a_u2Addr, u8 *a_puBuff)
{
	int i4RetValue = 0;
	char puReadCmd[1] = { (char)(a_u2Addr) };

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR;
	g_pstAF_I2Cclient->addr = g_pstAF_I2Cclient->addr >> 1;

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puReadCmd, 1);
	if (i4RetValue < 0) {
		pr_err(" I2C write failed!!\n");
		return -1;
	}

	i4RetValue = i2c_master_recv(g_pstAF_I2Cclient, (char *)a_puBuff, 1);
	if (i4RetValue < 0) {
		pr_err(" I2C read failed!!\n");
		return -1;
	}

	return 0;
}

static u8 read_data(u8 addr)
{
	u8 get_byte = 0;

	i2c_read(addr, &get_byte);

	return get_byte;
}

static int s4DW9718PAF_ReadReg(unsigned short *a_pu2Result)
{
	*a_pu2Result = (read_data(0x03) << 8) + (read_data(0x04) & 0xff);

	return 0;
}

static int s4AF_WriteReg(u16 a_u2Data)
{
	int i4RetValue = 0;

	char puSendCmd[3] = {0x03,(char)(a_u2Data >> 8),(char)(a_u2Data & 0xFF) };

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR;
	g_pstAF_I2Cclient->addr = g_pstAF_I2Cclient->addr >> 1;

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd, 3);

	if (i4RetValue < 0) {
		pr_err("I2C send failed!!\n");
		return -1;
	}

	return 0;
}
// begin binchang.liang for passat 2021/11/15
  int DW9718PAF_PowerDown(struct i2c_client *pstAF_I2Cclient, int *pAF_Opened)
{
  	unsigned char uc_num;
  	int i4RetValue =0;
 	char pCmdArray[2][2] =
  	{
  		{0x02, 0x01},
  	//	{0x02, 0x00},
  		{0xFF, 0x01},//delay
  	};
  	LOG_INF("Start\n");
  	if(pstAF_I2Cclient ==NULL){
  		LOG_INF("pstAF_I2Cclient is null\n");
  		return -1;
  	}
  	g_pstAF_I2Cclient = pstAF_I2Cclient;
  	g_pAF_Opened = pAF_Opened;
	g_bootPdFlag =1;

  	if (*g_pAF_Opened > 0)
  		*g_pAF_Opened = 0;

  	for(uc_num =0; uc_num <2; uc_num++)
  	{
  		if(pCmdArray[uc_num][0] != 0xFF)
  		{
  			i4RetValue=i2c_master_send(g_pstAF_I2Cclient, pCmdArray[uc_num], 2);
  			if (i4RetValue <0) {
				g_bootPdFlag =2;
  				LOG_INF(" I2C write failed!!\n");
  			}
  		}else{
  			//mdelay(pCmdArray[uc_num][1]);
  		}
  	}

  	LOG_INF("End\n");

  	return 0;
}
// end binchang.liang for passat 2021/11/15
static inline int getAFInfo(__user struct stAF_MotorInfo *pstMotorInfo)
{
	struct stAF_MotorInfo stMotorInfo;

	stMotorInfo.u4MacroPosition   = g_u4AF_MACRO;
	stMotorInfo.u4InfPosition     = g_u4AF_INF;
	stMotorInfo.u4CurrentPosition = g_u4CurrPosition;
	stMotorInfo.bIsSupportSR      = 1;
	stMotorInfo.bIsMotorMoving    = 1;

	if (*g_pAF_Opened >= 1)
		stMotorInfo.bIsMotorOpen = 1;
	else
		stMotorInfo.bIsMotorOpen = 0;

	if (copy_to_user(pstMotorInfo, &stMotorInfo,sizeof(struct stAF_MotorInfo)))
		pr_err("copy to user failed when getting motor information\n");

	return 0;
}

static int initdrv(void)
{
	int i4RetValue = 0;
	char puSendCmd0[2] = { 0x02, 0x01 };
	char puSendCmd1[2] = { 0x02, 0x00 };
	char puSendCmd2[2] = { 0x02, 0x02 };
	char puSendCmd3[2] = { 0x06, 0x40 };
	char puSendCmd4[2] = { 0x07, 0x60 };
	char puSendCmd5[2] = { 0x03, 0x01 };
	char puSendCmd6[2] = { 0x04, 0x7A };

//    if(!strcmp(CamNameB_module_name, "HI846:TSP:8M:ASA8001132C1")||!strcmp(CamNameB_module_name, "GC08A3:UNIMAGE:8M:AWC2718M12C1"))
//    {
//        puSendCmd4[0] =0x07;
//		puSendCmd4[1] =0x0F;
//    }
	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd0, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd1, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}
	mdelay(1);

#if 0
	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd1, 2);
	return i4RetValue;
#endif

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd2, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}
	mdelay(1);

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd3, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd4, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd5, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd6, 2);
	if (i4RetValue < 0) {
		pr_err("%d i2c_master_send failed\n", __LINE__);
		return -1;
	}

	LOG_INF("%d g_bootPdFlag %d\n", __LINE__,g_bootPdFlag);
	return i4RetValue;
}

static inline int moveAF(unsigned long a_u4Position)
{
	int ret = 0;

	if ((a_u4Position > g_u4AF_MACRO) || (a_u4Position < g_u4AF_INF)) {
		pr_err("out of range\n");
		return -EINVAL;
	}

	if (*g_pAF_Opened == 1) {
		unsigned short InitPos;

		ret = s4DW9718PAF_ReadReg(&InitPos);

		if (initdrv() >= 0) {
			spin_lock(g_pAF_SpinLock);
			*g_pAF_Opened = 2;
			spin_unlock(g_pAF_SpinLock);
		} else {
			pr_err("VCM driver init fail\n");
		}

		if (ret == 0) {
			LOG_INF("Init Pos %6d\n", InitPos);

			spin_lock(g_pAF_SpinLock);
			g_u4CurrPosition = (unsigned long)InitPos;
			spin_unlock(g_pAF_SpinLock);

		} else {
			spin_lock(g_pAF_SpinLock);
			g_u4CurrPosition = 0;
			spin_unlock(g_pAF_SpinLock);
		}
	}

	if (g_u4CurrPosition == a_u4Position)
		return 0;

	spin_lock(g_pAF_SpinLock);
	g_u4TargetPosition = a_u4Position;
	spin_unlock(g_pAF_SpinLock);

	LOG_INF("move [curr] %d [target] %d\n", g_u4CurrPosition,g_u4TargetPosition);

	if (s4AF_WriteReg((unsigned short)g_u4TargetPosition) == 0) {
		spin_lock(g_pAF_SpinLock);
		g_u4CurrPosition = (unsigned long)g_u4TargetPosition;
		spin_unlock(g_pAF_SpinLock);
	} else {
		pr_err("set I2C failed when moving the motor\n");
		ret = -1;
	}

	return ret;
}

static inline int setAFInf(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_INF = a_u4Position;
	spin_unlock(g_pAF_SpinLock);
	return 0;
}

static inline int setAFMacro(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_MACRO = a_u4Position;
	spin_unlock(g_pAF_SpinLock);
	return 0;
}

/* ////////////////////////////////////////////////////////////// */
long DW9718PAF_Ioctl(struct file *a_pstFile, unsigned int a_u4Command,
		     unsigned long a_u4Param)
{
	long i4RetValue = 0;

	LOG_INF("%s\n", __func__);
	switch (a_u4Command) {
	case AFIOC_G_MOTORINFO:
		i4RetValue =
			getAFInfo((__user struct stAF_MotorInfo *)(a_u4Param));
		break;

	case AFIOC_T_MOVETO:
		i4RetValue = moveAF(a_u4Param);
		break;

	case AFIOC_T_SETINFPOS:
		i4RetValue = setAFInf(a_u4Param);
		break;

	case AFIOC_T_SETMACROPOS:
		i4RetValue = setAFMacro(a_u4Param);
		break;

	default:
		pr_err("No CMD\n");
		i4RetValue = -EPERM;
		break;
	}

	return i4RetValue;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
int DW9718PAF_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	LOG_INF("Start\n");

	if (*g_pAF_Opened == 2) {
		unsigned long af_step = 25;

		if (g_u4CurrPosition > g_u4AF_INF && g_u4CurrPosition <= g_u4AF_MACRO) {
			while (g_u4CurrPosition > 50) {
				if (g_u4CurrPosition > 400)
					af_step = 70;
				else if (g_u4CurrPosition > 180)
					af_step = 40;
				else
					af_step = 25;

				s4AF_WriteReg(g_u4CurrPosition - af_step);

				g_u4CurrPosition = g_u4CurrPosition - af_step;
				mdelay(10);

				if (g_u4CurrPosition <= 0 ||g_u4CurrPosition > 1023)
					break;
			}
		}

		g_u4CurrPosition = 0;

		LOG_INF("Wait\n");
	}

	if (*g_pAF_Opened) {
		LOG_INF("Free\n");
		spin_lock(g_pAF_SpinLock);
		*g_pAF_Opened = 0;
		spin_unlock(g_pAF_SpinLock);
	}

	if (*g_pAF_Opened == 0) {
		int  i4RetValue = 0;
		char puSendCmdPowerDown[2] = {0x02, 0x01};
		i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmdPowerDown, 2);
		LOG_INF("apply - %d\n", i4RetValue);
	}

	LOG_INF("End\n");

	return 0;
}

int DW9718PAF_SetI2Cclient(struct i2c_client *pstAF_I2Cclient,
			   spinlock_t *pAF_SpinLock, int *pAF_Opened)
{
	g_pstAF_I2Cclient = pstAF_I2Cclient;
	g_pAF_SpinLock = pAF_SpinLock;
	g_pAF_Opened = pAF_Opened;

	LOG_INF("%s *g_pAF_Opened:%d\n", __func__, *g_pAF_Opened);
	return 1;
}

int DW9718PAF_GetFileName(unsigned char *pFileName)
{
#if SUPPORT_GETTING_LENS_FOLDER_NAME
	char FilePath[256];
	char *FileString;

	sprintf(FilePath, "%s", __FILE__);
	FileString = strrchr(FilePath, '/');
	*FileString = '\0';
	FileString = (strrchr(FilePath, '/') + 1);
	strncpy(pFileName, FileString, AF_MOTOR_NAME);
	LOG_INF("FileName : %s\n", pFileName);
#else
	pFileName[0] = '\0';
#endif
	return 1;
}
