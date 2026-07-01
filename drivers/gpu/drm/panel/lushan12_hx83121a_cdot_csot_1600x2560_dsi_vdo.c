/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#include <linux/backlight.h>
#include <linux/delay.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

/*
** H_total = 1760 , V_total = 2694
** P_clk = 1760*2694*120 = 568,972,800
** data_rate = P_clk * 3  * 2  ÷ 2(Dual_dsi ÷ 2 ？)= P_clk * 3 =  1,706,918,400 
*/

#define PANEL_CLOCK_120hz 568972  // P_clk(Mhz) .clock=htotal*vtotal*fps 
#define PANEL_CLOCK_90hz 567388
#define PANEL_CLOCK_60hz 579110
#define PANEL_WIDTH  1600
#define PANEL_HEIGHT 2560

#define PHYSICAL_WIDTH 166244
#define PHYSICAL_HEIGHT 265958


#define DATA_RATE 980 //pll_clk * 2 = 490 * 2 = 980

#define HSA 40
#define HBP 60
#define VSA 4
#define VBP 18

/*Parameter setting for mode 0 Start*/
#define MODE_0_FPS 120
#define MODE_0_VFP 112
#define MODE_0_HFP 60
/*Parameter setting for mode 0 End*/

/*Parameter setting for mode 1 Start*/
#define MODE_1_FPS 90
#define MODE_1_VFP 1000
#define MODE_1_HFP 60
/*Parameter setting for mode 1 End*/

/*Parameter setting for mode 2 Start*/
#define MODE_2_FPS 60
#define MODE_2_VFP 2902
#define MODE_2_HFP 60
/*Parameter setting for mode 2 End*/

#include "include/tct-panel-lcd-power.h"
/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */

/* option function to read data from some panel address */
/* #define PANEL_SUPPORT_READBACK */

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	
//	struct gpio_desc *tp_reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	
	struct gpio_desc *vddi_18;
	struct gpio_desc *vdd_12;

	bool prepared;
	bool enabled;

	int error;
};

static char bl_tb0[] = {0x51, 0xf, 0xff};

#define lcm_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}

static struct LCD_setting_table lcm_suspend_setting[] = {
	{0x28, 2, {0x28, 0x00} },
	{REGFLAG_DELAY, 20, {} },
	{0x51, 2, {0x51, 0x00} },
	{0x10, 2, {0x10, 0x00} },
	{REGFLAG_DELAY, 100, {} },
};
}
#endif

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	pr_info("%s()+ \n", __func__);
/*  pull high TP rst & vddi_18 & vdd_12	 */
//  TP rst
/*	ctx->tp_reset_gpio =
		devm_gpiod_get(ctx->dev, "tp_reset",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->tp_reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get tp_reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->tp_reset_gpio));
		//return;
	}
//	gpiod_set_value(ctx->tp_reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->tp_reset_gpio);*/

//  vddi_18
	ctx->vddi_18 =
		devm_gpiod_get(ctx->dev, "vddi_18",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_18)) {
		dev_err(ctx->dev, "%s: cannot get vddi_18 %ld\n",
			__func__, PTR_ERR(ctx->vddi_18));
		//return;
	}
//	gpiod_set_value(ctx->vddi_18, 1);
	devm_gpiod_put(ctx->dev, ctx->vddi_18);
	
//  vdd_12
	ctx->vdd_12 =
		devm_gpiod_get(ctx->dev, "vdd_12",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd_12)) {
		dev_err(ctx->dev, "%s: cannot get vdd_12 %ld\n",
			__func__, PTR_ERR(ctx->vdd_12));
		//return;
	}
//	gpiod_set_value(ctx->vdd_12, 1);
	devm_gpiod_put(ctx->dev, ctx->vdd_12);
	
	mdelay(2);

//  bias
	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		//return;
	}
	
	//pr_info("%s() bias_pos=%d \n", __func__, gpiod_get_value(ctx->bias_pos));
        mdelay(5);

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		//return;
	}
	
	
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset",  GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		//return;
	}


	//start set sequence
	
	//gpiod_set_value(ctx->reset_gpio, 0);
	//mdelay(1);
	
	
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
	udelay(5 * 1000); //delay at least 3ms
	
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	udelay(1000);
	//pr_info("%s() bias_neg=%d \n", __func__, gpiod_get_value(ctx->bias_neg));
	
	_lcm_i2c_write_bytes(0x3, (1<<6));
	udelay(2000);
	_lcm_i2c_write_bytes(0x0, 0x10); //+5.6V
	udelay(1000);
	_lcm_i2c_write_bytes(0x1, 0x10); //-5.6V
	udelay(1000);


	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	mdelay(10);
	gpiod_set_value(ctx->reset_gpio, 1);
	mdelay(10);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
    mdelay(5);
	//udelay(10 * 1000);
	//pr_info("%s() reset_gpio=%d \n", __func__, gpiod_get_value(ctx->reset_gpio));

	//----------------------LCD initial code start----------------------//
//####120HZ  DSC   V5
//####Note: 1）gamma BFh    2）customer ID 

lcm_dcs_write_seq_static(ctx,0xB9,0x83,0x12,0x1A,0x55,0x00);
lcm_dcs_write_seq_static(ctx,0x51,0x08,0x00);
lcm_dcs_write_seq_static(ctx,0x53,0x24);
lcm_dcs_write_seq_static(ctx,0xB1,0x1C,0x6B,0x6B,0x27,0xE7,0x00,0x1B,0x11,0x20,0x20,0x2D,0x2D,0x17,0x33,0x31,0x40,0xCD,0xFF,0x1A,0x05,0x15,0x98,0x00,0x88,0xFF,0xFF,0xFF,0xCF,0x1A,0xCC,0x02,0x00);

lcm_dcs_write_seq_static(ctx,0xB2,0x00,0x6A,0x40,0x00,0x00,0x14,0x9E,0x60,0x3C,0x02,0x80,0x21,0x21,0x00,0x00,0xF0,0x27);
lcm_dcs_write_seq_static(ctx,0xB4,0x46,0x06,0x0C,0xBE,0x0C,0xBE,0x09,0x46,0x0F,0x57,0x0F,0x57,0x03,0x4A,0x00,0x00,0x05,0x0D,0x00,0x18,0x01,0x06,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0xFF,0x10,0x00,0x02,0x14,0x14,0x14,0x14);
lcm_dcs_write_seq_static(ctx,0xB6,0x8A,0x8A,0x03);
lcm_dcs_write_seq_static(ctx,0xBC,0x06,0x02);
lcm_dcs_write_seq_static(ctx,0xC0,0x23,0x23,0xCC,0x22,0x99,0xD8);

lcm_dcs_write_seq_static(ctx,0xC9,0x00,0x1E,0x80,0xA5,0x01);
lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x13,0x38,0x00,0x05,0x7A);

//lcm_dcs_write_seq_static(ctx,0xCC,0x02);
lcm_dcs_write_seq_static(ctx,0xCC,0x0A);
lcm_dcs_write_seq_static(ctx,0xD1,0x37,0x03,0x0C,0xFD);
lcm_dcs_write_seq_static(ctx,0xD3,0x00,0xC0,0x08,0x08,0x08,0x04,0x04,0x04,0x16,0x02,0x07,0x07,0x07,0x31,0x13,0x19,0x12,0x12,0x03,0x03,0x03,0x32,0x10,0x18,0x00,0x11,0x32,0x10,0x03,0x00,0x03,0x32,0x10,0x03,0x00,0x03,0x00,0x00,0xFF,0x00);
lcm_dcs_write_seq_static(ctx,0xD5,0x19,0x19,0x18,0x18,0x02,0x02,0x03,0x03,0x04,0x04,0x05,0x05,0x06,0x06,0x07,0x07,0x00,0x00,0x01,0x01,0x18,0x18,0x40,0x40,0x20,0x20,0x18,0x18,0x18,0x18,0x40,0x40,0x18,0x18,0x2F,0x2F,0x31,0x31,0x2F,0x2F,0x31,0x31,0x18,0x18,0x41,0x41,0x41,0x41);
lcm_dcs_write_seq_static(ctx,0xD6,0x40,0x40,0x18,0x18,0x05,0x05,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x00,0x00,0x07,0x07,0x06,0x06,0x18,0x18,0x19,0x19,0x20,0x20,0x18,0x18,0x18,0x18,0x40,0x40,0x18,0x18,0x2F,0x2F,0x31,0x31,0x2F,0x2F,0x31,0x31,0x18,0x18,0x41,0x41,0x41,0x41);
lcm_dcs_write_seq_static(ctx,0xE1,0x11,0x00,0x00,0x89,0x30,0x80,0x0A,0x00,0x03,0x20,0x00,0x14,0x03,0x20,0x03,0x20,0x02,0x00,0x02,0x91,0x00,0x20,0x02,0x47,0x00,0x0B,0x00,0x0C,0x05,0x0E,0x03,0x68,0x18,0x00,0x10,0xE0,0x03,0x0C,0x20,0x00,0x06,0x0B,0x0B,0x33,0x0E,0x1C,0x2A,0x38,0x46,0x54,0x62,0x69,0x70,0x77,0x79,0x7B,0x7D,0x7E,0x01,0x02,0x01,0x00,0x09);
lcm_dcs_write_seq_static(ctx,0xE7,0x29,0x08,0x08,0x39,0x4E,0x1E,0x00,0x23,0x5D,0x02,0x02,0x00,0x00,0x00,0x00,0x12,0x05,0x02,0x02,0x07,0x10,0x10,0x00,0x1D,0xB9,0x23,0xB9,0x00,0x33,0x03,0x88);
lcm_dcs_write_seq_static(ctx,0xBD,0x01);
lcm_dcs_write_seq_static(ctx,0xB1,0x01,0x23,0x00);
lcm_dcs_write_seq_static(ctx,0xD8,0x20,0x00,0x02,0x22,0x00,0x00,0x20,0x00,0x02,0x22,0x00,0x00,0x20,0x00,0x02,0x22,0x00,0x00,0x20,0x00,0x02,0x22,0x00,0x00,0x20,0x00,0x02,0x22,0x00,0x00,0x20,0x00,0x02,0x22,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE1,0x40,0x09,0xBE,0x19,0xFC,0x19,0xFA,0x19,0xF8,0x1A,0x38,0x1A,0x78,0x1A,0xB6,0x2A,0xF6,0x2B,0x34,0x2B,0x74,0x3B,0x74,0x63,0xF4);
lcm_dcs_write_seq_static(ctx,0xE7,0x02,0x00,0xC2,0x01,0x55,0x07,0x48,0x08,0x48,0x14,0xFD,0x26);
lcm_dcs_write_seq_static(ctx,0xCB,0x1F,0x55,0x02,0x28,0x0D,0x08,0x0A);
lcm_dcs_write_seq_static(ctx,0xD6,0x02,0x04,0x21,0x02,0x04,0x21);
lcm_dcs_write_seq_static(ctx,0xBD,0x02);
lcm_dcs_write_seq_static(ctx,0xD8,0xAF,0xFF,0xFA,0xFA,0xBF,0xEA,0xAF,0xFF,0xFA,0xFA,0xBF,0xEA);
lcm_dcs_write_seq_static(ctx,0xE7,0x08,0x08,0x01,0x03,0x01,0x03,0x07,0x02,0x02,0x47,0x00,0x47,0x81,0x02,0x40,0x00,0x18,0x4A,0x06,0x05,0x04,0x03,0x02,0x01,0x00,0x00,0x03,0x02,0x01,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xBD,0x03);
lcm_dcs_write_seq_static(ctx,0xD8,0xAA,0xAA,0xAA,0xAB,0xBF,0xEA,0xAA,0xAA,0xAA,0xAB,0xBF,0xEA,0xAF,0xFF,0xFA,0xFA,0xBF,0xEA,0xAF,0xFF,0xFA,0xFA,0xBF,0xEA);
lcm_dcs_write_seq_static(ctx,0xE1,0x01,0x3F);

//##0424  L0=0.2V
lcm_dcs_write_seq_static(ctx,0xBD,0x00);
lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x10,0x2A,0x30,0x38,0x71,0x86,0x8E,0x91,0x93,0x94,0x95,0x93,0x94,0x95,0x94,0x97,0x9A,0x9D,0xB7,0xC8,0x61,0x7F,0x00,0x10,0x2A,0x30,0x38,0x71,0x86,0x8E,0x91,0x93,0x94,0x95,0x93,0x94,0x95,0x94,0x97,0x9A,0x9D,0xB7,0xC8,0x61,0x7F);


lcm_dcs_write_seq_static(ctx,0xD8,0xEA,0xAA,0xAA,0xAE,0xAA,0xAF,0xEA,0xAA,0xAA,0xAE,0xAA,0xAF,0xE0,0x00,0x0A,0x2E,0x80,0x2F,0xE0,0x00,0x0A,0x2E,0x80,0x2F,0xE0,0x00,0x0A,0x2E,0x80,0x2F,0xE0,0x00,0x0A,0x2E,0x80,0x2F);

//###  60HZ VFP adjustfinal                                                             PA12            PA15
lcm_dcs_write_seq_static(ctx,0xBD,0x01);
lcm_dcs_write_seq_static(ctx,0xE2,0x46,0x06,0x0C,0xBE,0x03,0x3F,0x0F,0x57,0x01,0x45,0x10,0x07,0x18,0x01,0x06,0x00,0x00,0x14,0x14,0x14,0x14,0x00,0x00,0x00,0x29,0x14,0x14,0x39,0x4C,0x1C,0x00,0x22,0xBD,0x02,0x02,0x00,0x00,0x93,0x01,0x0D,0x49,0x0D,0x4A,0x00,0x18,0x45,0x60,0x3E,0x00,0x05,0x7A,0x00);

lcm_dcs_write_seq_static(ctx,0xBD,0x02);
lcm_dcs_write_seq_static(ctx,0xE2,0x46,0x06,0x0C,0xBE,0x09,0x46,0x0F,0x57,0x03,0x4A,0x04,0x0C,0x18,0x01,0x08,0x00,0x00,0x14,0x14,0x14,0x14,0x00,0x00,0x00,0x29,0x08,0x08,0x39,0x4E,0x1E,0x00,0x23,0x5D,0x02,0x02,0x00,0x00,0xC2,0x01,0x55,0x47,0x48,0x48,0x00,0x18,0x4A,0x60,0x3E,0x00,0x05,0x7A,0x00);



lcm_dcs_write_seq_static(ctx,0xBD,0x02);
lcm_dcs_write_seq_static(ctx,0xBF,0x72);
lcm_dcs_write_seq_static(ctx,0xBD,0x00);
lcm_dcs_write_seq_static(ctx,0xBF,0xFD,0x00,0x80,0x9C,0x10,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xD0,0x07,0xC0,0x08,0x03,0x11,0x00);
lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE9,0xCF);
lcm_dcs_write_seq_static(ctx,0xBA,0x03);
lcm_dcs_write_seq_static(ctx,0xE9,0x3F);
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x8A);
lcm_dcs_write_seq_static(ctx,0xBD,0x02);
lcm_dcs_write_seq_static(ctx,0xB4,0xDE,0x00,0x80,0x00);
lcm_dcs_write_seq_static(ctx,0xBD,0x00);
lcm_dcs_write_seq_static(ctx,0xD9,0x00,0x0C,0x02);
lcm_dcs_write_seq_static(ctx,0xC8,0x00,0x04,0x04,0x00,0x00,0x83,0x17,0xFF,0x77,0x70);
lcm_dcs_write_seq_static(ctx,0xBE,0x80,0x00,0xC6);

lcm_dcs_write_seq_static(ctx,0xE9,0xE2);
lcm_dcs_write_seq_static(ctx,0xE7,0x49);
lcm_dcs_write_seq_static(ctx,0xE9,0x3F);


//####customer  ID
lcm_dcs_write_seq_static(ctx,0xBD,0x00);
lcm_dcs_write_seq_static(ctx,0xC3,0xC5,0x42);

//######60HZ   DSC on ####

lcm_dcs_write_seq_static(ctx,0xBD,0x00);
lcm_dcs_write_seq_static(ctx,0xE2,0x10);
	//----------------------LCD initial code End----------------------//
	//SLPOUT and DISPON
	lcm_dcs_write_seq_static(ctx, 0x11);
	mdelay(120);
	lcm_dcs_write_seq_static(ctx, 0x29);
	mdelay(50);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_info("%s\n", __func__);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_info("%s()+ %d,%d,%d \n", __func__, ctx->prepared, ctx->enabled, ctx->error);

	if (!ctx->prepared)
		return 0;

	//lcm_dcs_write_seq_static(ctx, 0x26,0x08);
	
	lcm_dcs_write_seq_static(ctx, 0x28);
	mdelay(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	mdelay(120);
	
	ctx->error = 0;
	ctx->prepared = false;




	udelay(1000);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	//gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	
/*	ctx->tp_reset_gpio =
		devm_gpiod_get(ctx->dev, "tp_reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->tp_reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get tp_reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->tp_reset_gpio));
		return PTR_ERR(ctx->tp_reset_gpio);
	}

	//gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);*/
	
	udelay(5000);

	//pr_info("%s() tp_reset_gpio=%d reset_gpio=%d \n", __func__, gpiod_get_value(ctx->tp_reset_gpio),gpiod_get_value(ctx->reset_gpio));
	
	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}

	gpiod_set_value(ctx->bias_neg, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);

	udelay(5000);
	pr_info("%s() bias_neg=%d \n", __func__, gpiod_get_value(ctx->bias_neg));

	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}

	gpiod_set_value(ctx->bias_pos, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	udelay(5000);
	pr_info("%s() bias_pos=%d \n", __func__, gpiod_get_value(ctx->bias_pos)); 


	pr_info("%s-\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {

	.clock       = PANEL_CLOCK_120hz,
	.hdisplay    = PANEL_WIDTH,
	.hsync_start = PANEL_WIDTH  + MODE_0_HFP,
	.hsync_end   = PANEL_WIDTH  + MODE_0_HFP + HSA,
	.htotal      = PANEL_WIDTH  + MODE_0_HFP + HSA + HBP,
	.vdisplay    = PANEL_HEIGHT,
	.vsync_start = PANEL_HEIGHT + MODE_0_VFP,
	.vsync_end   = PANEL_HEIGHT + MODE_0_VFP + VSA,
	.vtotal      = PANEL_HEIGHT + MODE_0_VFP + VSA + VBP,
	.vrefresh    = MODE_0_FPS,
};

static const struct drm_display_mode performance_mode_90hz = {

	.clock       = PANEL_CLOCK_90hz,
	.hdisplay    = PANEL_WIDTH,
	.hsync_start = PANEL_WIDTH  + MODE_1_HFP,
	.hsync_end   = PANEL_WIDTH  + MODE_1_HFP + HSA,
	.htotal      = PANEL_WIDTH  + MODE_1_HFP + HSA + HBP,
	.vdisplay    = PANEL_HEIGHT,
	.vsync_start = PANEL_HEIGHT + MODE_1_VFP,
	.vsync_end   = PANEL_HEIGHT + MODE_1_VFP + VSA,
	.vtotal      = PANEL_HEIGHT + MODE_1_VFP + VSA + VBP,
	.vrefresh    = MODE_1_FPS,
};

static const struct drm_display_mode performance_mode_60hz = {

	.clock       = PANEL_CLOCK_60hz,
	.hdisplay    = PANEL_WIDTH,
	.hsync_start = PANEL_WIDTH  + MODE_2_HFP,
	.hsync_end   = PANEL_WIDTH  + MODE_2_HFP + HSA,
	.htotal      = PANEL_WIDTH  + MODE_2_HFP + HSA + HBP,
	.vdisplay    = PANEL_HEIGHT,
	.vsync_start = PANEL_HEIGHT + MODE_2_VFP,
	.vsync_end   = PANEL_HEIGHT + MODE_2_VFP + VSA,
	.vtotal      = PANEL_HEIGHT + MODE_2_VFP + VSA + VBP,
	.vrefresh    = MODE_2_FPS,
};


#if defined(CONFIG_MTK_PANEL_EXT)

static struct mtk_panel_params ext_params = {
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,  //128
		.pic_height = 2560,
		.pic_width = 800,
		.slice_height = 20,
		.slice_width = 800,
		.chunk_size = 800,
		.xmit_delay = 512,
		.dec_delay = 657,
		.scale_value = 32,
		.increment_interval = 583,
		.decrement_interval = 11,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 872,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		
		.rc_buf_thresh[ 0] =  14,
		.rc_buf_thresh[ 1] =  28,
		.rc_buf_thresh[ 2] =  42,
		.rc_buf_thresh[ 3] =  56,
		.rc_buf_thresh[ 4] =  70,
		.rc_buf_thresh[ 5] =  84,
		.rc_buf_thresh[ 6] =  98,
		.rc_buf_thresh[ 7] = 105,
		.rc_buf_thresh[ 8] = 112,
		.rc_buf_thresh[ 9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,

		.rc_range_parameters[ 0].range_min_qp =       0,
		.rc_range_parameters[ 0].range_max_qp =       4,
		.rc_range_parameters[ 0].range_bpg_offset =   2,
		.rc_range_parameters[ 1].range_min_qp =       0,
		.rc_range_parameters[ 1].range_max_qp =       4,
		.rc_range_parameters[ 1].range_bpg_offset =   0,
		.rc_range_parameters[ 2].range_min_qp =       1,
		.rc_range_parameters[ 2].range_max_qp =       5,
		.rc_range_parameters[ 2].range_bpg_offset =   0,
		.rc_range_parameters[ 3].range_min_qp =       1,
		.rc_range_parameters[ 3].range_max_qp =       6,
		.rc_range_parameters[ 3].range_bpg_offset =  -2,
		.rc_range_parameters[ 4].range_min_qp =       3,
		.rc_range_parameters[ 4].range_max_qp =       7,
		.rc_range_parameters[ 4].range_bpg_offset =  -4,
		.rc_range_parameters[ 5].range_min_qp =       3,
		.rc_range_parameters[ 5].range_max_qp =       7,
		.rc_range_parameters[ 5].range_bpg_offset =  -6,
		.rc_range_parameters[ 6].range_min_qp =       3,
		.rc_range_parameters[ 6].range_max_qp =       7,
		.rc_range_parameters[ 6].range_bpg_offset =  -8,
		.rc_range_parameters[ 7].range_min_qp =       3,
		.rc_range_parameters[ 7].range_max_qp =       8,
		.rc_range_parameters[ 7].range_bpg_offset =  -8,
		.rc_range_parameters[ 8].range_min_qp =       3,
		.rc_range_parameters[ 8].range_max_qp =       9,
		.rc_range_parameters[ 8].range_bpg_offset =  -8,
		.rc_range_parameters[ 9].range_min_qp =       3,
		.rc_range_parameters[ 9].range_max_qp =      10,
		.rc_range_parameters[ 9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp =       5,
		.rc_range_parameters[10].range_max_qp =      11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp =       5,
		.rc_range_parameters[11].range_max_qp =      12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp =       5,
		.rc_range_parameters[12].range_max_qp =      13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp =       7,
		.rc_range_parameters[13].range_max_qp =      13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp =      13,
		.rc_range_parameters[14].range_max_qp =      13,
		.rc_range_parameters[14].range_bpg_offset = -12,
	},
	.dyn = {
		.switch_en = 0,
		.data_rate = DATA_RATE,
	},
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = MODE_0_FPS,
//		.lfr_enable = 1,
//		.lfr_minimum_fps = 60,
	},
	.data_rate = DATA_RATE,
	.pll_clk=490, 
};

static struct mtk_panel_params ext_params_90hz = {
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,  //128
		.pic_height = 2560,
		.pic_width = 800,
		.slice_height = 20,
		.slice_width = 800,
		.chunk_size = 800,
		.xmit_delay = 512,
		.dec_delay = 657,
		.scale_value = 32,
		.increment_interval = 583,
		.decrement_interval = 11,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 872,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		
		.rc_buf_thresh[ 0] =  14,
		.rc_buf_thresh[ 1] =  28,
		.rc_buf_thresh[ 2] =  42,
		.rc_buf_thresh[ 3] =  56,
		.rc_buf_thresh[ 4] =  70,
		.rc_buf_thresh[ 5] =  84,
		.rc_buf_thresh[ 6] =  98,
		.rc_buf_thresh[ 7] = 105,
		.rc_buf_thresh[ 8] = 112,
		.rc_buf_thresh[ 9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,

		.rc_range_parameters[ 0].range_min_qp =       0,
		.rc_range_parameters[ 0].range_max_qp =       4,
		.rc_range_parameters[ 0].range_bpg_offset =   2,
		.rc_range_parameters[ 1].range_min_qp =       0,
		.rc_range_parameters[ 1].range_max_qp =       4,
		.rc_range_parameters[ 1].range_bpg_offset =   0,
		.rc_range_parameters[ 2].range_min_qp =       1,
		.rc_range_parameters[ 2].range_max_qp =       5,
		.rc_range_parameters[ 2].range_bpg_offset =   0,
		.rc_range_parameters[ 3].range_min_qp =       1,
		.rc_range_parameters[ 3].range_max_qp =       6,
		.rc_range_parameters[ 3].range_bpg_offset =  -2,
		.rc_range_parameters[ 4].range_min_qp =       3,
		.rc_range_parameters[ 4].range_max_qp =       7,
		.rc_range_parameters[ 4].range_bpg_offset =  -4,
		.rc_range_parameters[ 5].range_min_qp =       3,
		.rc_range_parameters[ 5].range_max_qp =       7,
		.rc_range_parameters[ 5].range_bpg_offset =  -6,
		.rc_range_parameters[ 6].range_min_qp =       3,
		.rc_range_parameters[ 6].range_max_qp =       7,
		.rc_range_parameters[ 6].range_bpg_offset =  -8,
		.rc_range_parameters[ 7].range_min_qp =       3,
		.rc_range_parameters[ 7].range_max_qp =       8,
		.rc_range_parameters[ 7].range_bpg_offset =  -8,
		.rc_range_parameters[ 8].range_min_qp =       3,
		.rc_range_parameters[ 8].range_max_qp =       9,
		.rc_range_parameters[ 8].range_bpg_offset =  -8,
		.rc_range_parameters[ 9].range_min_qp =       3,
		.rc_range_parameters[ 9].range_max_qp =      10,
		.rc_range_parameters[ 9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp =       5,
		.rc_range_parameters[10].range_max_qp =      11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp =       5,
		.rc_range_parameters[11].range_max_qp =      12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp =       5,
		.rc_range_parameters[12].range_max_qp =      13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp =       7,
		.rc_range_parameters[13].range_max_qp =      13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp =      13,
		.rc_range_parameters[14].range_max_qp =      13,
		.rc_range_parameters[14].range_bpg_offset = -12,
	},
	.dyn = {
		.switch_en = 0,
		.data_rate = DATA_RATE,

	},
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = MODE_1_FPS,
//		.lfr_enable = 1,
//		.lfr_minimum_fps = 60,
	},
	.data_rate = DATA_RATE,
};

static struct mtk_panel_params ext_params_60hz = {
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,  //128
		.pic_height = 2560,
		.pic_width = 800,
		.slice_height = 20,
		.slice_width = 800,
		.chunk_size = 800,
		.xmit_delay = 512,
		.dec_delay = 657,
		.scale_value = 32,
		.increment_interval = 583,
		.decrement_interval = 11,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 872,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		
		.rc_buf_thresh[ 0] =  14,
		.rc_buf_thresh[ 1] =  28,
		.rc_buf_thresh[ 2] =  42,
		.rc_buf_thresh[ 3] =  56,
		.rc_buf_thresh[ 4] =  70,
		.rc_buf_thresh[ 5] =  84,
		.rc_buf_thresh[ 6] =  98,
		.rc_buf_thresh[ 7] = 105,
		.rc_buf_thresh[ 8] = 112,
		.rc_buf_thresh[ 9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,

		.rc_range_parameters[ 0].range_min_qp =       0,
		.rc_range_parameters[ 0].range_max_qp =       4,
		.rc_range_parameters[ 0].range_bpg_offset =   2,
		.rc_range_parameters[ 1].range_min_qp =       0,
		.rc_range_parameters[ 1].range_max_qp =       4,
		.rc_range_parameters[ 1].range_bpg_offset =   0,
		.rc_range_parameters[ 2].range_min_qp =       1,
		.rc_range_parameters[ 2].range_max_qp =       5,
		.rc_range_parameters[ 2].range_bpg_offset =   0,
		.rc_range_parameters[ 3].range_min_qp =       1,
		.rc_range_parameters[ 3].range_max_qp =       6,
		.rc_range_parameters[ 3].range_bpg_offset =  -2,
		.rc_range_parameters[ 4].range_min_qp =       3,
		.rc_range_parameters[ 4].range_max_qp =       7,
		.rc_range_parameters[ 4].range_bpg_offset =  -4,
		.rc_range_parameters[ 5].range_min_qp =       3,
		.rc_range_parameters[ 5].range_max_qp =       7,
		.rc_range_parameters[ 5].range_bpg_offset =  -6,
		.rc_range_parameters[ 6].range_min_qp =       3,
		.rc_range_parameters[ 6].range_max_qp =       7,
		.rc_range_parameters[ 6].range_bpg_offset =  -8,
		.rc_range_parameters[ 7].range_min_qp =       3,
		.rc_range_parameters[ 7].range_max_qp =       8,
		.rc_range_parameters[ 7].range_bpg_offset =  -8,
		.rc_range_parameters[ 8].range_min_qp =       3,
		.rc_range_parameters[ 8].range_max_qp =       9,
		.rc_range_parameters[ 8].range_bpg_offset =  -8,
		.rc_range_parameters[ 9].range_min_qp =       3,
		.rc_range_parameters[ 9].range_max_qp =      10,
		.rc_range_parameters[ 9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp =       5,
		.rc_range_parameters[10].range_max_qp =      11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp =       5,
		.rc_range_parameters[11].range_max_qp =      12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp =       5,
		.rc_range_parameters[12].range_max_qp =      13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp =       7,
		.rc_range_parameters[13].range_max_qp =      13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp =      13,
		.rc_range_parameters[14].range_max_qp =      13,
		.rc_range_parameters[14].range_bpg_offset = -12,
	},
	.dyn = {
		.switch_en = 0,
		.data_rate = DATA_RATE,
	},
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = MODE_2_FPS,
//		.lfr_enable = 1,
//		.lfr_minimum_fps = 60,
	},
	.data_rate = DATA_RATE,
};

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	if (level > 255)
		level = 255;

	level = level * 4095 / 255;
	bl_tb0[1] = ((level >> 8) & 0xf);
	bl_tb0[2] = (level & 0xff);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			 unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;

	if (mode == 0)
		ext->params = &ext_params;
	else if (mode == 1)
		ext->params = &ext_params_90hz;
	else if (mode == 2)
		ext->params = &ext_params_60hz;
	else
		ret = 1;

	return ret;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
    pr_info("lcm %s+\n", __func__);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	pr_info("lcm %s-\n", __func__);
	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
//	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.reset = panel_ext_reset,
	.ext_param_set = mtk_panel_ext_param_set,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *           become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *          display the first valid frame after starting to receive
	 *          video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *           turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *             to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode_90hz;
	struct drm_display_mode *mode_60hz;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	mode_90hz = drm_mode_duplicate(panel->drm, &performance_mode_90hz);
	if (!mode_90hz) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode_90hz.hdisplay,
			performance_mode_90hz.vdisplay,
			performance_mode_90hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_90hz);
	mode_90hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode_90hz);


	mode_60hz = drm_mode_duplicate(panel->drm, &performance_mode_60hz);
	if (!mode_60hz) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode_60hz.hdisplay,
			performance_mode_60hz.vdisplay,
			performance_mode_60hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_60hz);
	mode_60hz->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode_60hz);

	panel->connector->display_info.width_mm = 166;
	panel->connector->display_info.height_mm = 266;

	return 3;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;

	pr_err("%s+\n", __func__);
	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
/*  pull high TP rst & vddi_18 & vdd_12	 */
//  TP rst
/*	ctx->tp_reset_gpio =
		devm_gpiod_get(ctx->dev, "tp_reset",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->tp_reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get tp_reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->tp_reset_gpio));
		return PTR_ERR(ctx->tp_reset_gpio);
	}
	devm_gpiod_put(ctx->dev, ctx->tp_reset_gpio);*/

//  vddi_18
	ctx->vddi_18 =
		devm_gpiod_get(ctx->dev, "vddi_18",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_18)) {
		dev_err(ctx->dev, "%s: cannot get vddi_18 %ld\n",
			__func__, PTR_ERR(ctx->vddi_18));
		return PTR_ERR(ctx->vddi_18);
	}
	devm_gpiod_put(ctx->dev, ctx->vddi_18);
	
//  vdd_12
	ctx->vdd_12 =
		devm_gpiod_get(ctx->dev, "vdd_12",  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd_12)) {
		dev_err(ctx->dev, "%s: cannot get vdd_12 %ld\n",
			__func__, PTR_ERR(ctx->vdd_12));
		return PTR_ERR(ctx->vdd_12);
	}
	devm_gpiod_put(ctx->dev, ctx->vdd_12);
	
#ifndef CONFIG_LEDS_MTK_I2C
	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(dev, "%s: cannot get bias-pos 0 %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(dev, "%s: cannot get bias-neg 1 %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);
#endif
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0) {
		dev_err(dev, "lcm drm_panel_add fail\n");
		return ret;
	}

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "lcm mipi_dsi_attach fail\n");
		drm_panel_remove(&ctx->panel);
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0) {
		dev_err(dev, "lcm mtk_panel_ext_create fail\n");
		return ret;
	}
#endif

	pr_info("%s()-\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
	pr_info("%s()+ \n", __func__);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "hera,hx83121a_cdot_csot_wqxga_dsi_vdo", },
	//{ .compatible = "hera,hx83121a", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "hera_hx83121a_cdot_csot_wqxga_dsi_vdo_lcm_drv",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Elon Hsu <elon.hsu@mediatek.com>");
MODULE_DESCRIPTION("HX83121A CDOT CSOT WQXGA VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");

