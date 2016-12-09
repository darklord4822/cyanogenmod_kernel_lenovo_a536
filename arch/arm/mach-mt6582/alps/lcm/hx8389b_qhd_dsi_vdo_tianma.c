#ifndef BUILD_LK
#include <linux/string.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
    #include <platform/mt_gpio.h>
    #include <string.h>
#elif defined(BUILD_UBOOT)
    #include <asm/arch/mt_gpio.h>
#else
    #include <linux/string.h>
    #include <mach/mt_gpio.h>
#endif

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  (480)
#define FRAME_HEIGHT (854)

#ifndef TRUE
    #define TRUE  1
#endif

#ifndef FALSE
    #define FALSE 0
#endif

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v) (lcm_util.set_reset_pin((v)))

#define UDELAY(n)        (lcm_util.udelay(n))
#define MDELAY(n)        (lcm_util.mdelay(n))

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V3(para_tbl,size,force_update)      lcm_util.dsi_set_cmdq_V3(para_tbl,size,force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)    lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)                                   lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)               lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)                                    lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)            lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

static LCM_setting_table_V3 lcm_initialization_setting[] = {
	/*
	Note:
	Data ID will depends on the following rule.
	    count of parameters > 1 ==> DataID=0x39
	    count of parameters = 1 ==> DataID=0x15
	    count of parameters = 0 ==> DataID=0x05
	Structure format:
	    {DataID, DCS command, count of parameters, {parameter list}},
	    {REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, milliseconds, {}},
	*/

	// Set extension command
	{0x39, 0xB9,  3, {0xFF,0x83,0x89}},

	// Set MIPI control
	{0x39, 0xBA,  7, {0x41,0x93,0x00,0x16,0xA4,0x10,0x18}},

	{0x15, 0xC6,  1, {0x08}},

	// Set power (VGH=VSP*2-VSN, VGL=VSN*2-VSP)
	{0x39, 0xB1, 19, {0x00,0x00,0x07,0xEF,0x97,0x10,0x11,0x94,0xF1,0x26,
	                  0x2E,0x3F,0x3F,0x42,0x01,0x32,0xF7,0x20,0x80}},

	// Set power option
	{0x39, 0xDE,  3, {0x05,0x58,0x10}},

	// Set display related register
	{0x39, 0xB2,  7, {0x00,0x00,0x78,0x0E,0x03,0x3F,0x80}},

	// Set panel driving timing
	{0x39, 0xB4, 23, {0x80,0x08,0x00,0x32,0x10,0x07,0x32,0x10,0x03,0x32,
	                  0x10,0x07,0x27,0x01,0x5A,0x0B,0x37,0x05,0x40,0x14,
	                  0x50,0x58,0x0A}},

	// Set GIP (Gate In Panel)
	{0x39, 0xD5, 48, {0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x01,0x60,0x00,
	                  0x99,0x88,0x88,0x88,0x88,0x23,0x88,0x01,0x88,0x67,
	                  0x88,0x45,0x01,0x23,0x23,0x45,0x88,0x88,0x88,0x88,
	                  0x99,0x88,0x88,0x88,0x54,0x88,0x76,0x88,0x10,0x88,
	                  0x32,0x32,0x10,0x88,0x88,0x88,0x88,0x88}},

	// Set Gamma
	{0x39, 0xE0, 34, {0x00,0x18,0x1F,0x3B,0x3E,0x3F,0x2F,0x4A,0x07,0x0E,
	                  0x0F,0x13,0x16,0x13,0x13,0x0F,0x19,0x00,0x18,0x1F,
	                  0x3B,0x3E,0x3F,0x2F,0x4A,0x07,0x0E,0x0F,0x13,0x16,
	                  0x13,0x13,0x0F,0x19}},

	// Set DGC-LUT (Digital Gamma Curve Look-up Table)
	{0x39, 0xC1,127, {0x01,0x00,0x08,0x10,0x18,0x20,0x28,0x30,0x38,0x40,
	                  0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,0x88,0x90,
	                  0x98,0xA0,0xA8,0xB0,0xB8,0xC0,0xC8,0xD0,0xD8,0xE0,
	                  0xE8,0xF0,0xF8,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,
	                  0x00,0x00,0x00,0x00,0x08,0x10,0x18,0x20,0x28,0x30,
	                  0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,
	                  0x88,0x90,0x98,0xA0,0xA8,0xB0,0xB8,0xC0,0xC8,0xD0,
	                  0xD8,0xE0,0xE8,0xF0,0xF8,0xFF,0x00,0x00,0x00,0x00,
	                  0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x10,0x17,0x20,
	                  0x29,0x31,0x39,0x42,0x4A,0x53,0x5B,0x63,0x6B,0x74,
	                  0x7C,0x84,0x8C,0x94,0x9C,0xA4,0xAC,0xB5,0xBB,0xC3,
	                  0xCB,0xD3,0xDB,0xE2,0xEA,0xF2,0xF8,0xFF,0x00,0x00,
	                  0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	// Set VCOM = -0.990 V
	{0x39, 0xB6,  4, {0x00,0x93,0x00,0x93}},

	// Set panel (Normal black; Scan direction; RGB/BGR source driver direction)
	{0x15, 0xCC,  1, {0x02}},

	// Set internal TE (Tear Effect)
	{0x39, 0xB7,  3, {0x00,0x00,0x50}},

	// --- Resume ------------------------------------
	// Do resume within initial is NOT recommended!

	/*
	// Sleep out
	{0x05, 0x11, 0, {}},
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 120, {}},

	// Display on
	{0x05, 0x29, 0, {}},
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 20, {}},
	*/
};


static LCM_setting_table_V3 lcm_suspend_setting[] = {
	// Display off
	//{0x05, 0x28, 0, {}},
	//{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 20, {}},
	// Note:
	// Remove the Display off command is a workaround
	// to reduce the idle currect of Himax HX8389-B driver IC.

	// Sleep in
	{0x05, 0x10, 0, {}},
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 120, {}},
};


static LCM_setting_table_V3 lcm_resume_setting[] = {
	// Sleep out
	{0x05, 0x11, 0, {}},
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 120, {}},

	// Display on
	{0x05, 0x29, 0, {}},
	{REGFLAG_ESCAPE_ID, REGFLAG_DELAY_MS_V3, 20, {}},
};

// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------
static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;
	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.LANE_NUM           = LCM_TWO_LANE;
	params->dsi.PS                 = LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;
	params->dsi.vertical_sync_active    = 4;
	params->dsi.vertical_backporch      = 16;
	params->dsi.vertical_frontporch     = 20;
	params->dsi.vertical_active_line    = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active  = 60;
	params->dsi.horizontal_backporch    = 80;
	params->dsi.horizontal_frontporch   = 80;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.PLL_CLOCK = 220; // Range: 25..625 MHz
}


static void lcm_init(void)
{
	#ifdef BUILD_LK
	  printf("[lk][lcm] %s \n", __func__);
  #else
    printk("[kernel][lcm] %s \n", __func__);
  #endif
	SET_RESET_PIN(1);
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(20);
	SET_RESET_PIN(1);
	MDELAY(120);

	dsi_set_cmdq_V3(lcm_initialization_setting, sizeof(lcm_initialization_setting)/sizeof(lcm_initialization_setting[0]), 1);
	dsi_set_cmdq_V3(lcm_resume_setting, sizeof(lcm_resume_setting)/sizeof(lcm_resume_setting[0]), 1);
}


static void lcm_suspend(void)
{
	dsi_set_cmdq_V3(lcm_suspend_setting, sizeof(lcm_suspend_setting)/sizeof(lcm_suspend_setting[0]), 1);
}


static void lcm_resume(void)
{
	dsi_set_cmdq_V3(lcm_resume_setting, sizeof(lcm_resume_setting)/sizeof(lcm_resume_setting[0]), 1);
}

LCM_DRIVER hx8389b_qhd_dsi_vdo_tianma_lcm_drv =
{
	.name           = "hx8389b_qhd_dsi_vdo_tianma",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
};
