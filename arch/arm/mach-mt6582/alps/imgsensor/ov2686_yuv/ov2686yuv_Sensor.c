#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <asm/io.h>
//#include <asm/system.h>	 
#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "kd_camera_feature.h"
#include "ov2686yuv_Sensor.h"
#include "ov2686yuv_Camera_Sensor_para.h"
#include "ov2686yuv_CameraCustomized.h" 

#define OV2686YUV_DEBUG
#ifdef OV2686YUV_DEBUG
#define OV2686SENSORDB printk
#else
#define OV2686SENSORDB(x,...)
#endif

#define MT6572

static DEFINE_SPINLOCK(OV2686_drv_lock);

extern int iReadReg(u16 a_u2Addr , u8 * a_puBuff , u16 i2cId);
extern int iWriteReg(u16 a_u2Addr , u32 a_u4Data , u32 a_u4Bytes , u16 i2cId);

#define OV2686_write_cmos_sensor(addr, para) iWriteReg((u16) addr , (u32) para ,1,OV2686_WRITE_ID)
#define mDELAY(ms)  mdelay(ms)

static struct
{
	kal_uint8   IsPVmode;
	kal_uint32  PreviewDummyPixels;
	kal_uint32  PreviewDummyLines;
	kal_uint32  CaptureDummyPixels;
	kal_uint32  CaptureDummyLines;
	kal_uint32  PreviewPclk;
	kal_uint32  CapturePclk;
	kal_uint32  PreviewShutter;
	kal_uint32  SensorGain;
#ifdef MT6572
	kal_uint32  sceneMode;
	kal_uint32  SensorShutter;
	unsigned char 	isoSpeed;
#endif
	OV2686_SENSOR_MODE SensorMode;
} OV2686Sensor;

static kal_uint32 zoom_factor = 0; 
static kal_uint8 OV2686_Banding_setting = AE_FLICKER_MODE_50HZ;  //Wonder add
static kal_bool OV2686_AWB_ENABLE = KAL_TRUE; 
static kal_bool OV2686_AE_ENABLE = KAL_TRUE; 

MSDK_SENSOR_CONFIG_STRUCT OV2686SensorConfigData;



kal_uint16 OV2686_read_cmos_sensor(kal_uint32 addr)
{
	kal_uint16 get_byte=0;
    iReadReg((u16) addr ,(u8*)&get_byte,OV2686_WRITE_ID);
    
    return get_byte;
}



kal_uint32 OV2686_gain_check(kal_uint32 gain)
{
	gain = (gain > OV2686_MAX_GAIN) ? OV2686_MAX_GAIN : gain;
	gain = (gain < OV2686_MIN_GAIN) ? OV2686_MIN_GAIN : gain;

	return gain;
}

kal_uint32 OV2686_shutter_check(kal_uint32 shutter)
{
	if(OV2686Sensor.IsPVmode)
		shutter = (shutter > OV2686_MAX_SHUTTER_PREVIEW) ? OV2686_MAX_SHUTTER_PREVIEW : shutter;
	else
		shutter = (shutter > OV2686_MAX_SHUTTER_CAPTIRE) ? OV2686_MAX_SHUTTER_CAPTIRE: shutter;
	
	shutter = (shutter < OV2686_MIN_SHUTTER) ? OV2686_MIN_SHUTTER : shutter;

	return shutter;
}

static void OV2686_set_AE_mode(kal_bool AE_enable)
{
    kal_uint8 AeTemp;

	  AeTemp = OV2686_read_cmos_sensor(0x3503);

    if (AE_enable == KAL_TRUE)
    {
    	OV2686SENSORDB("[OV2686_set_AE_mode] enable\n");
        OV2686_write_cmos_sensor(0x3503, (AeTemp&(~0x03)));
    }
    else
    {
    	OV2686SENSORDB("[OV2686_set_AE_mode] disable\n");
      	OV2686_write_cmos_sensor(0x3503, (AeTemp|0x03));
    }
}

static void OV2686WriteShutter(kal_uint32 shutter)
{
	if (shutter<1)
	{
		  shutter=1;
	}	
	shutter*=16;
	
	OV2686_write_cmos_sensor(0x3502, shutter & 0x00FF);          
	OV2686_write_cmos_sensor(0x3501, ((shutter & 0x0FF00) >>8));  
	OV2686_write_cmos_sensor(0x3500, ((shutter & 0xFF0000) >> 16));		
}   

static void OV2686WriteSensorGain(kal_uint32 gain)
{
	kal_uint16 temp_reg = 0;

	OV2686SENSORDB("[OV2686WriteSensorGain] gain=%d\n", gain);
		
	//gain = OV2686_gain_check(gain);
	temp_reg = 0;
	temp_reg=gain&0xFF;
	
	OV2686_write_cmos_sensor(0x350B,temp_reg);
} 

void OV2686_night_mode(kal_bool enable)
{
	kal_uint16 night = OV2686_read_cmos_sensor(0x3A00); 
	
	if (enable)
	{
		OV2686SENSORDB("[OV2686_night_mode] enable\n");
		
       	OV2686_write_cmos_sensor(0x3A00,night|0x02); //30fps-5fps
       	OV2686_write_cmos_sensor(0x382a,0x08);//disable 0x00
       	OV2686_write_cmos_sensor(0x3a0a,0x0f); 
      	OV2686_write_cmos_sensor(0x3a0b,0x18);                         
      	OV2686_write_cmos_sensor(0x3a0c,0x0f); 
      	OV2686_write_cmos_sensor(0x3a0d,0x18);                    
    }
	else
	{   
		OV2686SENSORDB("[OV2686_night_mode] disable\n");
		
       	OV2686_write_cmos_sensor(0x3A00,night|0x02); //30fps-10fps   
        OV2686_write_cmos_sensor(0x382a,0x08);//disable 0x00
       	OV2686_write_cmos_sensor(0x3a0a,0x0a); 
      	OV2686_write_cmos_sensor(0x3a0b,0x92);                         
      	OV2686_write_cmos_sensor(0x3a0c,0x0a); 
      	OV2686_write_cmos_sensor(0x3a0d,0x92);  
    }
}	

#ifdef MT6572
void OV2686_set_contrast(UINT16 para)
{   
    OV2686SENSORDB("[OV2686_set_contrast]para=%d\n", para);
	
    switch (para)
    {
        case ISP_CONTRAST_HIGH:
			      
           	 break;
        case ISP_CONTRAST_MIDDLE:
			      
			       break;
		    case ISP_CONTRAST_LOW:
			      
             break;
        default:
             break;
    }
	
    return;
}

void OV2686_set_brightness(UINT16 para)
{
    OV2686SENSORDB("[OV5645MIPI_set_brightness]para=%d\n", para);
	
    switch (para)
    {
        case ISP_BRIGHT_HIGH:
        	            
                       break;
        case ISP_BRIGHT_MIDDLE:
        	              
			                 break;
		case ISP_BRIGHT_LOW:
			                		
                       break;
               default:
                       break;
    }
	
    return;
}

void OV2686_set_saturation(UINT16 para)
{
	OV2686SENSORDB("[OV5645MIPI_set_saturation]para=%d\n", para);
	
    switch (para)
    {
        case ISP_SAT_HIGH:
        	  	
            break;
        case ISP_SAT_MIDDLE:
        	  	
			      break;
		    case ISP_SAT_LOW:
		    	 
            break;
    default:
			      break;
    }
	
     return;
}

void OV2686_set_iso(UINT16 para)
{
	OV2686SENSORDB("[OV5645MIPI_set_iso]para=%d\n", para);
	
    spin_lock(&OV2686_drv_lock);
    OV2686Sensor.isoSpeed = para;
    spin_unlock(&OV2686_drv_lock);   
	
    switch (para)
    {
        case AE_ISO_100:
            	OV2686_write_cmos_sensor(0x3a13,0x38);
              break;
        case AE_ISO_200:
			        OV2686_write_cmos_sensor(0x3a13,0x58);
              break;
        case AE_ISO_400:
      		    OV2686_write_cmos_sensor(0x3a13,0xf8);
              break;
        default:
              break;
    }
    return;
}

void OV2686_scene_mode_PORTRAIT()
{
	//bright add
	
}

void OV2686_scene_mode_LANDSCAPE()
{
	//bright add
	
}

void OV2686_scene_mode_SUNSET()
{
	//bright add
	
}

void OV2686_scene_mode_SPORTS()
{
	//bright add
	
}

void OV2686_scene_mode_OFF()
{
	//bright add
	
}

BOOL OV2686_set_param_exposure_for_HDR(UINT16 para)
{
    kal_uint32 gain = 0, shutter = 0;
		
    OV2686_set_AE_mode(KAL_FALSE);
    
	  gain = OV2686Sensor.SensorGain;
    shutter = OV2686Sensor.SensorShutter;
	
	switch (para)
	{
	   case AE_EV_COMP_20:	
     case AE_EV_COMP_10:	
		       gain =gain<<1;
           shutter = shutter<<1;   		
		       break;
	   case AE_EV_COMP_00:
		       break;
	   case AE_EV_COMP_n10: 
	   case AE_EV_COMP_n20:
		       gain = gain >> 1;
           shutter = shutter >> 1;
		       break;
	   default:
		       break;
	}

  OV2686WriteSensorGain(gain);	
	OV2686WriteShutter(shutter);	
	
	return TRUE;
}

void OV2686_set_scene_mode(UINT16 para)
{
	OV2686SENSORDB("[OV2686_set_scene_mode]para=%d\n", para);
	
	spin_lock(&OV2686_drv_lock);
	OV2686Sensor.sceneMode=para;
	spin_unlock(&OV2686_drv_lock);
	
    switch (para)
    { 

		case SCENE_MODE_NIGHTSCENE:
          	 OV2686_night_mode(KAL_TRUE); 
			break;
        case SCENE_MODE_PORTRAIT:
			 OV2686_scene_mode_PORTRAIT();		 
             break;
        case SCENE_MODE_LANDSCAPE:
			 OV2686_scene_mode_LANDSCAPE();		 
             break;
        case SCENE_MODE_SUNSET:
			 OV2686_scene_mode_SUNSET();		 
            break;
        case SCENE_MODE_SPORTS:
            OV2686_scene_mode_SPORTS();		 
            break;
        case SCENE_MODE_HDR:
            break;
        case SCENE_MODE_OFF:
			OV2686_night_mode(KAL_FALSE);
			break;
        default:
			return KAL_FALSE;
            break;
    }
	
	return;
}

#endif

static void OV2686SetDummy(kal_uint32 dummy_pixels, kal_uint32 dummy_lines)
{
	kal_uint32 temp_reg, temp_reg1, temp_reg2;

	OV2686SENSORDB("[OV2686SetDummy] dummy_pixels=%d, dummy_lines=%d\n", dummy_pixels, dummy_lines);
	
	if (dummy_pixels > 0)
	{ /*
		temp_reg1 = OV2686_read_cmos_sensor(0x380D);  
		temp_reg2 = OV2686_read_cmos_sensor(0x380C);  
		
		temp_reg = (temp_reg1 & 0xFF) | (temp_reg2 << 8);
		temp_reg += dummy_pixels;
	
		OV2686_write_cmos_sensor(0x380D,(temp_reg&0xFF));        
		OV2686_write_cmos_sensor(0x380C,((temp_reg&0xFF00)>>8));
		*/
	}

	if (dummy_lines > 0)
	{ /*
		temp_reg1 = OV2686_read_cmos_sensor(0x380F);    
		temp_reg2 = OV2686_read_cmos_sensor(0x380E);  
		
		temp_reg = (temp_reg1 & 0xFF) | (temp_reg2 << 8);
		temp_reg += dummy_lines;
	
		OV2686_write_cmos_sensor(0x380F,(temp_reg&0xFF));        
		OV2686_write_cmos_sensor(0x380E,((temp_reg&0xFF00)>>8)); 
		*/
	}
}

static kal_uint32 OV2686ReadShutter(void)
{
	kal_uint16 temp_reg1, temp_reg2 ,temp_reg3;
	
	temp_reg1 = OV2686_read_cmos_sensor(0x3500);   
	temp_reg2 = OV2686_read_cmos_sensor(0x3501);  
	temp_reg3 = OV2686_read_cmos_sensor(0x3502); 

	spin_lock(&OV2686_drv_lock);
	OV2686Sensor.PreviewShutter  = (temp_reg1 <<12)|(temp_reg2<<4)|(temp_reg3>>4);
	spin_unlock(&OV2686_drv_lock);

	OV2686SENSORDB("[OV2686ReadShutter] shutter=%d\n", OV2686Sensor.PreviewShutter);
	
	return OV2686Sensor.PreviewShutter;
}

static kal_uint32 OV2686ReadSensorGain(void)
{
	kal_uint32 sensor_gain = 0;
	
	sensor_gain=(OV2686_read_cmos_sensor(0x350B)&0xFF); 		

	OV2686SENSORDB("[OV2686ReadSensorGain] gain=%d\n", sensor_gain);

	return sensor_gain;
}  

static void OV2686_set_AWB_mode(kal_bool AWB_enable)
{
    kal_uint8 AwbTemp;
	
	  AwbTemp = OV2686_read_cmos_sensor(0x5180);

    if (AWB_enable == KAL_TRUE)
    {
    	OV2686SENSORDB("[OV2686_set_AWB_mode] enable\n");
		OV2686_write_cmos_sensor(0x5180, AwbTemp&0xfd); 
    }
    else
    {
    	OV2686SENSORDB("[OV2686_set_AWB_mode] disable\n");
		OV2686_write_cmos_sensor(0x5180, AwbTemp|0x02); 
    }
}
//extern char Back_Camera_Name[256];  //783608  modified by zhangxinghong 
static kal_uint32 OV2686_GetSensorID(kal_uint32 *sensorID)
{
	volatile signed char i;
	kal_uint32 sensor_id=0;
	
	OV2686_write_cmos_sensor(0x0103,0x01);
	mDELAY(10);
	
	for(i=0;i<3;i++)
	{
		sensor_id = (OV2686_read_cmos_sensor(0x300A) << 8) | OV2686_read_cmos_sensor(0x300B);
		 

		OV2686SENSORDB("[OV2686_GetSensorID] sensorID=%x\n", sensor_id);

		if(sensor_id != 0x2685 )  // PR 783608 modified by zhangxinghong
		{	
			*sensorID =0xffffffff;
			return ERROR_SENSOR_CONNECT_FAIL;
		}
	}
	*sensorID =sensor_id;
     //783608  modified by zhangxinghong 
//     sprintf(Back_Camera_Name,"Vender: Shengtai \nSensor ID:  0x%X \nSensor name:  OV2686 \nResolution:  2M ",
//				sensor_id);
    return ERROR_NONE;    
}   

static void OV2686InitialSetting(void)
{	
OV2686_write_cmos_sensor(0x3000, 0x03);
OV2686_write_cmos_sensor(0x3001, 0xff);
OV2686_write_cmos_sensor(0x3002, 0x1a);
OV2686_write_cmos_sensor(0x3011, 0x03);
OV2686_write_cmos_sensor(0x301d, 0xf0);
OV2686_write_cmos_sensor(0x3020, 0x00);
OV2686_write_cmos_sensor(0x3021, 0x23);

OV2686_write_cmos_sensor(0x3082, 0x2c); 
OV2686_write_cmos_sensor(0x3083, 0x00);
OV2686_write_cmos_sensor(0x3084, 0x07);
OV2686_write_cmos_sensor(0x3085, 0x03);
OV2686_write_cmos_sensor(0x3086, 0x01); //15fps 01=30fps
OV2686_write_cmos_sensor(0x3087, 0x00);
OV2686_write_cmos_sensor(0x3106, 0x01);

OV2686_write_cmos_sensor(0x3501, 0x26);
OV2686_write_cmos_sensor(0x3502, 0x40);
OV2686_write_cmos_sensor(0x3503, 0x03);
OV2686_write_cmos_sensor(0x350b, 0x36);
OV2686_write_cmos_sensor(0x3600, 0xb4);
OV2686_write_cmos_sensor(0x3603, 0x35);
OV2686_write_cmos_sensor(0x3604, 0x24);
OV2686_write_cmos_sensor(0x3605, 0x00);
OV2686_write_cmos_sensor(0x3620, 0x25);
OV2686_write_cmos_sensor(0x3621, 0x37);
OV2686_write_cmos_sensor(0x3622, 0x23);
OV2686_write_cmos_sensor(0x3628, 0x10);
OV2686_write_cmos_sensor(0x3701, 0x64);
OV2686_write_cmos_sensor(0x3705, 0x3c);
OV2686_write_cmos_sensor(0x370a, 0x23);
OV2686_write_cmos_sensor(0x370c, 0x50);
OV2686_write_cmos_sensor(0x370d, 0xc0);
OV2686_write_cmos_sensor(0x3717, 0x58);
OV2686_write_cmos_sensor(0x3718, 0x80);
OV2686_write_cmos_sensor(0x3720, 0x00);
OV2686_write_cmos_sensor(0x3721, 0x00);
OV2686_write_cmos_sensor(0x3722, 0x00);
OV2686_write_cmos_sensor(0x3723, 0x00);
OV2686_write_cmos_sensor(0x3738, 0x00);
OV2686_write_cmos_sensor(0x3781, 0x80);
OV2686_write_cmos_sensor(0x3789, 0x60);

OV2686_write_cmos_sensor(0x3800, 0x00);
OV2686_write_cmos_sensor(0x3801, 0x00);
OV2686_write_cmos_sensor(0x3802, 0x00);
OV2686_write_cmos_sensor(0x3803, 0x00);
OV2686_write_cmos_sensor(0x3804, 0x06);
OV2686_write_cmos_sensor(0x3805, 0x4f);
OV2686_write_cmos_sensor(0x3806, 0x04);
OV2686_write_cmos_sensor(0x3807, 0xbf);
OV2686_write_cmos_sensor(0x3808, 0x03);
OV2686_write_cmos_sensor(0x3809, 0x20);
OV2686_write_cmos_sensor(0x380a, 0x02);
OV2686_write_cmos_sensor(0x380b, 0x58);
OV2686_write_cmos_sensor(0x3810, 0x00);
OV2686_write_cmos_sensor(0x3811, 0x04);
OV2686_write_cmos_sensor(0x3812, 0x00);
OV2686_write_cmos_sensor(0x3813, 0x04);
OV2686_write_cmos_sensor(0x3814, 0x31);
OV2686_write_cmos_sensor(0x3815, 0x31);

OV2686_write_cmos_sensor(0x3820, 0xc0); //modified by zhangxinghong
OV2686_write_cmos_sensor(0x3821, 0x00);

OV2686_write_cmos_sensor(0x380c, 0x06);
OV2686_write_cmos_sensor(0x380d, 0xac);
OV2686_write_cmos_sensor(0x380e, 0x02);
OV2686_write_cmos_sensor(0x380f, 0x84);

OV2686_write_cmos_sensor(0x3a02, 0x90);//50hz 10=60hz
OV2686_write_cmos_sensor(0x3a06, 0x00);
OV2686_write_cmos_sensor(0x3a07, 0xc2);
OV2686_write_cmos_sensor(0x3a08, 0x00);
OV2686_write_cmos_sensor(0x3a09, 0xa1);

OV2686_write_cmos_sensor(0x3a0e, 0x02);
OV2686_write_cmos_sensor(0x3a0f, 0x46);
OV2686_write_cmos_sensor(0x3a10, 0x02);
OV2686_write_cmos_sensor(0x3a11, 0x84);

//night mode enable  0x3a00=0x43,0x382a=0x08
//night mode disable 0x3a00=0x41,0x382a=0x00
OV2686_write_cmos_sensor(0x3a00, 0x43);
OV2686_write_cmos_sensor(0x382a, 0x08);
OV2686_write_cmos_sensor(0x3a0a, 0x07);
OV2686_write_cmos_sensor(0x3a0b, 0x8c);
OV2686_write_cmos_sensor(0x3a0c, 0x07);
OV2686_write_cmos_sensor(0x3a0d, 0x8c);

OV2686_write_cmos_sensor(0x3a13,0x58);
OV2686_write_cmos_sensor(0x4000, 0x81);
OV2686_write_cmos_sensor(0x4001, 0x40);
OV2686_write_cmos_sensor(0x4008, 0x00);
OV2686_write_cmos_sensor(0x4009, 0x03);
OV2686_write_cmos_sensor(0x4300, 0x31);
OV2686_write_cmos_sensor(0x430e, 0x00);
OV2686_write_cmos_sensor(0x4602, 0x02);
OV2686_write_cmos_sensor(0x5000, 0xff);
OV2686_write_cmos_sensor(0x5001, 0x05);
OV2686_write_cmos_sensor(0x5002, 0x32);
OV2686_write_cmos_sensor(0x5003, 0x04);
OV2686_write_cmos_sensor(0x5004, 0xff);
OV2686_write_cmos_sensor(0x5005, 0x12);
OV2686_write_cmos_sensor(0x3784, 0x08);

OV2686_write_cmos_sensor(0x5180, 0xf4);
OV2686_write_cmos_sensor(0x5181, 0x11);
OV2686_write_cmos_sensor(0x5182, 0x41);
OV2686_write_cmos_sensor(0x5183, 0x42);
OV2686_write_cmos_sensor(0x5184, 0x6e);
OV2686_write_cmos_sensor(0x5185, 0x56);
OV2686_write_cmos_sensor(0x5186, 0xb4);
OV2686_write_cmos_sensor(0x5187, 0xb2);
OV2686_write_cmos_sensor(0x5188, 0x08);
OV2686_write_cmos_sensor(0x5189, 0x0e);
OV2686_write_cmos_sensor(0x518a, 0x0e);
OV2686_write_cmos_sensor(0x518b, 0x46);
OV2686_write_cmos_sensor(0x518c, 0x38);
OV2686_write_cmos_sensor(0x518d, 0xf8);
OV2686_write_cmos_sensor(0x518e, 0x04);
OV2686_write_cmos_sensor(0x518f, 0x7f);
OV2686_write_cmos_sensor(0x5190, 0x40);
OV2686_write_cmos_sensor(0x5191, 0x5f);
OV2686_write_cmos_sensor(0x5192, 0x40);
OV2686_write_cmos_sensor(0x5193, 0xff);
OV2686_write_cmos_sensor(0x5194, 0x40);
OV2686_write_cmos_sensor(0x5195, 0x07);
OV2686_write_cmos_sensor(0x5196, 0x04);
OV2686_write_cmos_sensor(0x5197, 0x04);
OV2686_write_cmos_sensor(0x5198, 0x00);
OV2686_write_cmos_sensor(0x5199, 0x05);
OV2686_write_cmos_sensor(0x519a, 0xd2);
OV2686_write_cmos_sensor(0x519b, 0x04);

OV2686_write_cmos_sensor(0x5200, 0x09);
OV2686_write_cmos_sensor(0x5201, 0x00);
OV2686_write_cmos_sensor(0x5202, 0x06);
OV2686_write_cmos_sensor(0x5203, 0x20);
OV2686_write_cmos_sensor(0x5204, 0x41);
OV2686_write_cmos_sensor(0x5205, 0x16);
OV2686_write_cmos_sensor(0x5206, 0x00);
OV2686_write_cmos_sensor(0x5207, 0x05);
OV2686_write_cmos_sensor(0x520b, 0x30);
OV2686_write_cmos_sensor(0x520c, 0x75);
OV2686_write_cmos_sensor(0x520d, 0x00);
OV2686_write_cmos_sensor(0x520e, 0x30);
OV2686_write_cmos_sensor(0x520f, 0x75);
OV2686_write_cmos_sensor(0x5210, 0x00);

OV2686_write_cmos_sensor(0x5280, 0x14);
OV2686_write_cmos_sensor(0x5281, 0x02);
OV2686_write_cmos_sensor(0x5282, 0x02);
OV2686_write_cmos_sensor(0x5283, 0x04);
OV2686_write_cmos_sensor(0x5284, 0x06);
OV2686_write_cmos_sensor(0x5285, 0x08);
OV2686_write_cmos_sensor(0x5286, 0x0c);
OV2686_write_cmos_sensor(0x5287, 0x10);

OV2686_write_cmos_sensor(0x5300, 0xc5);
OV2686_write_cmos_sensor(0x5301, 0xa0);
OV2686_write_cmos_sensor(0x5302, 0x06);
OV2686_write_cmos_sensor(0x5303, 0x0a);
OV2686_write_cmos_sensor(0x5304, 0x30);
OV2686_write_cmos_sensor(0x5305, 0x60);
OV2686_write_cmos_sensor(0x5306, 0x90);
OV2686_write_cmos_sensor(0x5307, 0xc0);
OV2686_write_cmos_sensor(0x5308, 0x82);
OV2686_write_cmos_sensor(0x5309, 0x00);
OV2686_write_cmos_sensor(0x530a, 0x26);
OV2686_write_cmos_sensor(0x530b, 0x02);
OV2686_write_cmos_sensor(0x530c, 0x02);
OV2686_write_cmos_sensor(0x530d, 0x00);
OV2686_write_cmos_sensor(0x530e, 0x0c);
OV2686_write_cmos_sensor(0x530f, 0x14);
OV2686_write_cmos_sensor(0x5310, 0x1a);
OV2686_write_cmos_sensor(0x5311, 0x20);
OV2686_write_cmos_sensor(0x5312, 0x80);
OV2686_write_cmos_sensor(0x5313, 0x4b);

OV2686_write_cmos_sensor(0x5380, 0x01);
OV2686_write_cmos_sensor(0x5381, 0x0c);
OV2686_write_cmos_sensor(0x5382, 0x00);
OV2686_write_cmos_sensor(0x5383, 0x16);
OV2686_write_cmos_sensor(0x5384, 0x00);
OV2686_write_cmos_sensor(0x5385, 0xb3);
OV2686_write_cmos_sensor(0x5386, 0x00);
OV2686_write_cmos_sensor(0x5387, 0x7e);
OV2686_write_cmos_sensor(0x5388, 0x00);
OV2686_write_cmos_sensor(0x5389, 0x07);
OV2686_write_cmos_sensor(0x538a, 0x01);
OV2686_write_cmos_sensor(0x538b, 0x35);
OV2686_write_cmos_sensor(0x538c, 0x00);

OV2686_write_cmos_sensor(0x5400, 0x0d);
OV2686_write_cmos_sensor(0x5401, 0x18);
OV2686_write_cmos_sensor(0x5402, 0x31);
OV2686_write_cmos_sensor(0x5403, 0x5a);
OV2686_write_cmos_sensor(0x5404, 0x65);
OV2686_write_cmos_sensor(0x5405, 0x6f);
OV2686_write_cmos_sensor(0x5406, 0x77);
OV2686_write_cmos_sensor(0x5407, 0x80);
OV2686_write_cmos_sensor(0x5408, 0x87);
OV2686_write_cmos_sensor(0x5409, 0x8f);
OV2686_write_cmos_sensor(0x540a, 0xa2);
OV2686_write_cmos_sensor(0x540b, 0xb2);
OV2686_write_cmos_sensor(0x540c, 0xcc);
OV2686_write_cmos_sensor(0x540d, 0xe4);
OV2686_write_cmos_sensor(0x540e, 0xf0);
OV2686_write_cmos_sensor(0x540f, 0xa0);
OV2686_write_cmos_sensor(0x5410, 0x6e);
OV2686_write_cmos_sensor(0x5411, 0x06);

OV2686_write_cmos_sensor(0x5480, 0x19);
OV2686_write_cmos_sensor(0x5481, 0x00);
OV2686_write_cmos_sensor(0x5482, 0x09);
OV2686_write_cmos_sensor(0x5483, 0x12);
OV2686_write_cmos_sensor(0x5484, 0x04);
OV2686_write_cmos_sensor(0x5485, 0x06);
OV2686_write_cmos_sensor(0x5486, 0x08);
OV2686_write_cmos_sensor(0x5487, 0x0c);
OV2686_write_cmos_sensor(0x5488, 0x10);
OV2686_write_cmos_sensor(0x5489, 0x18);

OV2686_write_cmos_sensor(0x5500, 0x02);
OV2686_write_cmos_sensor(0x5501, 0x03);
OV2686_write_cmos_sensor(0x5502, 0x04);
OV2686_write_cmos_sensor(0x5503, 0x05);
OV2686_write_cmos_sensor(0x5504, 0x06);
OV2686_write_cmos_sensor(0x5505, 0x08);
OV2686_write_cmos_sensor(0x5506, 0x00);
OV2686_write_cmos_sensor(0x5600, 0x02);
OV2686_write_cmos_sensor(0x5603, 0x40);
OV2686_write_cmos_sensor(0x5604, 0x28);
OV2686_write_cmos_sensor(0x5609, 0x20);
OV2686_write_cmos_sensor(0x560a, 0x60);

OV2686_write_cmos_sensor(0x5780, 0x3e);
OV2686_write_cmos_sensor(0x5781, 0x0f);
OV2686_write_cmos_sensor(0x5782, 0x04);
OV2686_write_cmos_sensor(0x5783, 0x02);
OV2686_write_cmos_sensor(0x5784, 0x01);
OV2686_write_cmos_sensor(0x5785, 0x01);
OV2686_write_cmos_sensor(0x5786, 0x00);
OV2686_write_cmos_sensor(0x5787, 0x04);
OV2686_write_cmos_sensor(0x5788, 0x02);
OV2686_write_cmos_sensor(0x5789, 0x00);
OV2686_write_cmos_sensor(0x578a, 0x01);
OV2686_write_cmos_sensor(0x578b, 0x02);
OV2686_write_cmos_sensor(0x578c, 0x03);
OV2686_write_cmos_sensor(0x578d, 0x03);
OV2686_write_cmos_sensor(0x578e, 0x08);
OV2686_write_cmos_sensor(0x578f, 0x0c);
OV2686_write_cmos_sensor(0x5790, 0x08);
OV2686_write_cmos_sensor(0x5791, 0x04);
OV2686_write_cmos_sensor(0x5792, 0x00);
OV2686_write_cmos_sensor(0x5793, 0x00);
OV2686_write_cmos_sensor(0x5794, 0x03);

OV2686_write_cmos_sensor(0x5800, 0x03);
OV2686_write_cmos_sensor(0x5801, 0x14);
OV2686_write_cmos_sensor(0x5802, 0x02);
OV2686_write_cmos_sensor(0x5803, 0x64);
OV2686_write_cmos_sensor(0x5804, 0x22);//0x1e
OV2686_write_cmos_sensor(0x5805, 0x05);
OV2686_write_cmos_sensor(0x5806, 0x2a);
OV2686_write_cmos_sensor(0x5807, 0x05);
OV2686_write_cmos_sensor(0x5808, 0x03);
OV2686_write_cmos_sensor(0x5809, 0x17);
OV2686_write_cmos_sensor(0x580a, 0x02);
OV2686_write_cmos_sensor(0x580b, 0x63);
OV2686_write_cmos_sensor(0x580c, 0x1a);
OV2686_write_cmos_sensor(0x580d, 0x05);
OV2686_write_cmos_sensor(0x580e, 0x1f);
OV2686_write_cmos_sensor(0x580f, 0x05);
OV2686_write_cmos_sensor(0x5810, 0x03);
OV2686_write_cmos_sensor(0x5811, 0x0c);
OV2686_write_cmos_sensor(0x5812, 0x02);
OV2686_write_cmos_sensor(0x5813, 0x5e);
OV2686_write_cmos_sensor(0x5814, 0x18);
OV2686_write_cmos_sensor(0x5815, 0x05);
OV2686_write_cmos_sensor(0x5816, 0x19);
OV2686_write_cmos_sensor(0x5817, 0x05);
OV2686_write_cmos_sensor(0x5818, 0x0d);
OV2686_write_cmos_sensor(0x5819, 0x40);
OV2686_write_cmos_sensor(0x581a, 0x04);
OV2686_write_cmos_sensor(0x581b, 0x0c);

OV2686_write_cmos_sensor(0x3106, 0x21);

OV2686_write_cmos_sensor(0x3a03, 0x4c);
OV2686_write_cmos_sensor(0x3a04, 0x40);

OV2686_write_cmos_sensor(0x3503, 0x00);

OV2686_write_cmos_sensor(0x0100, 0x01);

spin_lock(&OV2686_drv_lock);
OV2686Sensor.IsPVmode= 1;
OV2686Sensor.PreviewDummyPixels= 0;
OV2686Sensor.PreviewDummyLines= 0;
OV2686Sensor.PreviewPclk= 480;
OV2686Sensor.CapturePclk= 480;
//OV2686Sensor.PreviewShutter=0x0265;
OV2686Sensor.SensorGain=0x10;
spin_unlock(&OV2686_drv_lock);

}                                  

static void OV2686PreviewSetting(void)
{
OV2686_write_cmos_sensor(0x3500,((OV2686Sensor.PreviewShutter*16)>>16)&0xff); 
OV2686_write_cmos_sensor(0x3501,((OV2686Sensor.PreviewShutter*16)>>8)&0xff); 
OV2686_write_cmos_sensor(0x3502,(OV2686Sensor.PreviewShutter*16)&0xff); 
OV2686_write_cmos_sensor(0x350B, OV2686Sensor.SensorGain);           

OV2686_write_cmos_sensor(0x370a, 0x23);

OV2686_write_cmos_sensor(0x3808, 0x03);
OV2686_write_cmos_sensor(0x3809, 0x20);
OV2686_write_cmos_sensor(0x380a, 0x02);
OV2686_write_cmos_sensor(0x380b, 0x58);
OV2686_write_cmos_sensor(0x3810, 0x00);
OV2686_write_cmos_sensor(0x3811, 0x04);
OV2686_write_cmos_sensor(0x3812, 0x00);
OV2686_write_cmos_sensor(0x3813, 0x04);
OV2686_write_cmos_sensor(0x3814, 0x31);
OV2686_write_cmos_sensor(0x3815, 0x31);

OV2686_write_cmos_sensor(0x3086, 0x01);

OV2686_write_cmos_sensor(0x380c, 0x06);
OV2686_write_cmos_sensor(0x380d, 0xac);
OV2686_write_cmos_sensor(0x380e, 0x02);
OV2686_write_cmos_sensor(0x380f, 0x84);

OV2686_write_cmos_sensor(0x3820, 0xc0); //modified by zhangxinghong
OV2686_write_cmos_sensor(0x3821, 0x00);

OV2686_write_cmos_sensor(0x3a06, 0x00);
OV2686_write_cmos_sensor(0x3a07, 0xc2);
OV2686_write_cmos_sensor(0x3a08, 0x00);
OV2686_write_cmos_sensor(0x3a09, 0xa1);

OV2686_write_cmos_sensor(0x3a0e, 0x02);
OV2686_write_cmos_sensor(0x3a0f, 0x46);
OV2686_write_cmos_sensor(0x3a10, 0x02);
OV2686_write_cmos_sensor(0x3a11, 0x84);

OV2686_write_cmos_sensor(0x3a0a, 0x0a);
OV2686_write_cmos_sensor(0x3a0b, 0x92);
OV2686_write_cmos_sensor(0x3a0c, 0x0a);
OV2686_write_cmos_sensor(0x3a0d, 0x10);

OV2686_write_cmos_sensor(0x4008, 0x00);
OV2686_write_cmos_sensor(0x4009, 0x03);

OV2686_write_cmos_sensor(0x3503, 0x00);
OV2686_write_cmos_sensor(0x3a00, 0x43);
	
spin_lock(&OV2686_drv_lock);
OV2686Sensor.IsPVmode = KAL_TRUE;
OV2686Sensor.PreviewPclk= 480;
OV2686Sensor.SensorMode= SENSOR_MODE_PREVIEW;
spin_unlock(&OV2686_drv_lock);

}

static void OV2686FullSizeCaptureSetting(void)
{
OV2686_write_cmos_sensor(0x3503, 0x03);
OV2686_write_cmos_sensor(0x3a00, 0x41);

OV2686_write_cmos_sensor(0x370a, 0x21);

OV2686_write_cmos_sensor(0x3808, 0x06);
OV2686_write_cmos_sensor(0x3809, 0x40);
OV2686_write_cmos_sensor(0x380a, 0x04);
OV2686_write_cmos_sensor(0x380b, 0xb0);
OV2686_write_cmos_sensor(0x3810, 0x00);
OV2686_write_cmos_sensor(0x3811, 0x08);
OV2686_write_cmos_sensor(0x3812, 0x00);
OV2686_write_cmos_sensor(0x3813, 0x08);
OV2686_write_cmos_sensor(0x3814, 0x11);
OV2686_write_cmos_sensor(0x3815, 0x11);

OV2686_write_cmos_sensor(0x3086, 0x01);

OV2686_write_cmos_sensor(0x380c, 0x06);
OV2686_write_cmos_sensor(0x380d, 0xa4);
OV2686_write_cmos_sensor(0x380e, 0x05);
OV2686_write_cmos_sensor(0x380f, 0x0e);

OV2686_write_cmos_sensor(0x3820, 0xc0); //modified by zhangxinghong
OV2686_write_cmos_sensor(0x3821, 0x00);

OV2686_write_cmos_sensor(0x3a06, 0x00);
OV2686_write_cmos_sensor(0x3a07, 0xc2);
OV2686_write_cmos_sensor(0x3a08, 0x00);
OV2686_write_cmos_sensor(0x3a09, 0xa1);

OV2686_write_cmos_sensor(0x3a0e, 0x04);
OV2686_write_cmos_sensor(0x3a0f, 0x8c);
OV2686_write_cmos_sensor(0x3a10, 0x05);
OV2686_write_cmos_sensor(0x3a11, 0x08);

OV2686_write_cmos_sensor(0x4008, 0x02);
OV2686_write_cmos_sensor(0x4009, 0x09);
	
spin_lock(&OV2686_drv_lock);
OV2686Sensor.IsPVmode = KAL_FALSE;
OV2686Sensor.CapturePclk= 585;
//OV2686Sensor.SensorMode= SENSOR_MODE_CAPTURE;
spin_unlock(&OV2686_drv_lock);

}

static void OV2686FullSizeCaptureSetting_ZSD(void)
{
OV2686_write_cmos_sensor(0x3503, 0x00);
OV2686_write_cmos_sensor(0x3a00, 0x41);
OV2686_write_cmos_sensor(0x3a0a, 0x05);
OV2686_write_cmos_sensor(0x3a0b, 0x0e);
OV2686_write_cmos_sensor(0x3a0c, 0x05);
OV2686_write_cmos_sensor(0x3a0d, 0x0e);

OV2686_write_cmos_sensor(0x370a, 0x21);

OV2686_write_cmos_sensor(0x3808, 0x06);
OV2686_write_cmos_sensor(0x3809, 0x40);
OV2686_write_cmos_sensor(0x380a, 0x04);
OV2686_write_cmos_sensor(0x380b, 0xb0);
OV2686_write_cmos_sensor(0x3810, 0x00);
OV2686_write_cmos_sensor(0x3811, 0x08);
OV2686_write_cmos_sensor(0x3812, 0x00);
OV2686_write_cmos_sensor(0x3813, 0x08);
OV2686_write_cmos_sensor(0x3814, 0x11);
OV2686_write_cmos_sensor(0x3815, 0x11);

OV2686_write_cmos_sensor(0x3086, 0x01);

OV2686_write_cmos_sensor(0x380c, 0x06);
OV2686_write_cmos_sensor(0x380d, 0xa4);
OV2686_write_cmos_sensor(0x380e, 0x05);
OV2686_write_cmos_sensor(0x380f, 0x0e);

OV2686_write_cmos_sensor(0x3820, 0xc0);  //modified by zhangxinghong
OV2686_write_cmos_sensor(0x3821, 0x00);

OV2686_write_cmos_sensor(0x3a06, 0x00);
OV2686_write_cmos_sensor(0x3a07, 0xc2);
OV2686_write_cmos_sensor(0x3a08, 0x00);
OV2686_write_cmos_sensor(0x3a09, 0xa1);

OV2686_write_cmos_sensor(0x3a0e, 0x04);
OV2686_write_cmos_sensor(0x3a0f, 0x8c);
OV2686_write_cmos_sensor(0x3a10, 0x05);
OV2686_write_cmos_sensor(0x3a11, 0x08);

OV2686_write_cmos_sensor(0x4008, 0x02);
OV2686_write_cmos_sensor(0x4009, 0x09);
	
spin_lock(&OV2686_drv_lock);
OV2686Sensor.IsPVmode = KAL_FALSE;
OV2686Sensor.CapturePclk= 585;
//OV2686Sensor.SensorMode= SENSOR_MODE_CAPTURE;
spin_unlock(&OV2686_drv_lock);

}

UINT32 OV2686Open(void)
{
	volatile signed int i;
	kal_uint16 sensor_id = 0;
	
	OV2686SENSORDB("[OV2686Open]\n");
	
	OV2686_write_cmos_sensor(0x0103,0x01);
  mDELAY(10);

	for(i=0;i<3;i++)
	{
		sensor_id = (OV2686_read_cmos_sensor(0x300A) << 8) | OV2686_read_cmos_sensor(0x300B);
		
		OV2686SENSORDB("[OV2686Open]SensorId=%x\n", sensor_id);
  

		if(sensor_id != 0x2685)  // PR 783608 modified by zhangxinghong
		{
			return ERROR_SENSOR_CONNECT_FAIL;
		}
	}
	
	spin_lock(&OV2686_drv_lock);
	OV2686Sensor.CaptureDummyPixels = 0;
  	OV2686Sensor.CaptureDummyLines = 0;
	OV2686Sensor.PreviewDummyPixels = 0;
  	OV2686Sensor.PreviewDummyLines = 0;
	OV2686Sensor.SensorMode= SENSOR_MODE_INIT;
	#ifdef MT6572
	OV2686Sensor.isoSpeed = 100;
	#endif
	spin_unlock(&OV2686_drv_lock);

	OV2686InitialSetting();

	return ERROR_NONE;
}

UINT32 OV2686Close(void)
{
	OV2686SENSORDB("[OV2686Close]\n");
	
	return ERROR_NONE;
}

UINT32 OV2686Preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{	
	OV2686SENSORDB("[OV2686Preview]enter\n");
	
	OV2686PreviewSetting();
	//OV2686ReadShutter();
	//OV2686ReadSensorGain();	
	//OV2686_set_AE_mode(KAL_TRUE);
	//OV2686_set_AWB_mode(KAL_TRUE);
	
	mDELAY(200);
	
	OV2686SENSORDB("[OV2686Preview]exit\n");
	
 	return TRUE ;
}	

UINT32 OV2686Capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	kal_uint32 shutter = 0, prev_line_len = 0, cap_line_len = 0, temp = 0;

	OV2686SENSORDB("[OV2686Capture]enter\n");
	
	OV2686_write_cmos_sensor(0x3503,OV2686_read_cmos_sensor(0x3503)|0x03);	
	OV2686_write_cmos_sensor(0x3a00,OV2686_read_cmos_sensor(0x3a00)&0xfd);  
	
	if(SENSOR_MODE_PREVIEW == OV2686Sensor.SensorMode )
	{
		OV2686SENSORDB("[OV2686Capture]Normal Capture\n ");

		//OV2686_set_AE_mode(KAL_FALSE);
		//OV2686_set_AWB_mode(KAL_FALSE);	
		shutter=OV2686ReadShutter();
		temp =OV2686ReadSensorGain();	
		
		mDELAY(30);
		OV2686FullSizeCaptureSetting();
		
		spin_lock(&OV2686_drv_lock);
		OV2686Sensor.SensorMode= SENSOR_MODE_CAPTURE;
		//OV2686Sensor.CaptureDummyPixels = 0;
  	//OV2686Sensor.CaptureDummyLines = 0;
		spin_unlock(&OV2686_drv_lock);
		
  		//OV2686SetDummy(OV2686Sensor.CaptureDummyPixels, OV2686Sensor.CaptureDummyLines);

		//prev_line_len = OV2686_PV_PERIOD_PIXEL_NUMS + OV2686Sensor.PreviewDummyPixels;
  		//cap_line_len = OV2686_FULL_PERIOD_PIXEL_NUMS + OV2686Sensor.CaptureDummyPixels;
  		//shutter = (shutter * OV2686Sensor.CapturePclk) / OV2686Sensor.PreviewPclk;
  		//shutter = (shutter * prev_line_len) / cap_line_len;	

  	OV2686WriteShutter(shutter);
  	mDELAY(150);
  		//OV2686WriteSensorGain(OV2686Sensor.SensorGain);

		spin_lock(&OV2686_drv_lock);
  		OV2686Sensor.SensorGain= temp;
		OV2686Sensor.SensorShutter = shutter;
		spin_unlock(&OV2686_drv_lock);
	}
	else if(SENSOR_MODE_ZSD == OV2686Sensor.SensorMode)
	{
		//for zsd hdr use
		shutter=OV2686ReadShutter();
		temp =OV2686ReadSensorGain();
		
		spin_lock(&OV2686_drv_lock);
  		OV2686Sensor.SensorGain= temp;
		OV2686Sensor.SensorShutter = shutter;
		spin_unlock(&OV2686_drv_lock);
	}
	
	OV2686SENSORDB("[OV2686Capture]exit\n");
	
	return ERROR_NONE; 
}

UINT32 OV2686ZSDPreview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	OV2686SENSORDB("[OV2686ZSDPreview]enter\n");
	
	if(SENSOR_MODE_PREVIEW == OV2686Sensor.SensorMode || OV2686Sensor.SensorMode == SENSOR_MODE_INIT)
	{
		OV2686FullSizeCaptureSetting_ZSD();
	}

	spin_lock(&OV2686_drv_lock);
	OV2686Sensor.SensorMode= SENSOR_MODE_ZSD;
	spin_unlock(&OV2686_drv_lock);
	
	OV2686_set_AE_mode(KAL_TRUE);
	//OV2686_set_AWB_mode(KAL_TRUE);
	
	OV2686SENSORDB("[OV2686ZSDPreview]exit\n");
	
	return ERROR_NONE; 
}

UINT32 OV2686GetResolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *pSensorResolution)
{
	OV2686SENSORDB("[OV2686GetResolution]\n");

	pSensorResolution->SensorPreviewWidth= OV2686_IMAGE_SENSOR_SVGA_WIDTH - 1 * 8;
	pSensorResolution->SensorPreviewHeight= OV2686_IMAGE_SENSOR_SVGA_HEIGHT - 1 * 8;
	pSensorResolution->SensorFullWidth= OV2686_IMAGE_SENSOR_UVGA_WITDH - 2 * 8;  
	pSensorResolution->SensorFullHeight= OV2686_IMAGE_SENSOR_UVGA_HEIGHT - 2 * 8;
	pSensorResolution->SensorVideoWidth=OV2686_IMAGE_SENSOR_SVGA_WIDTH - 1 * 8;
	pSensorResolution->SensorVideoHeight=OV2686_IMAGE_SENSOR_SVGA_HEIGHT - 1 * 8;
	return ERROR_NONE;
}

UINT32 OV2686GetInfo(MSDK_SCENARIO_ID_ENUM ScenarioId,
					  MSDK_SENSOR_INFO_STRUCT *pSensorInfo,
					  MSDK_SENSOR_CONFIG_STRUCT *pSensorConfigData)
{
	OV2686SENSORDB("[OV2686GetInfo]\n");

	pSensorInfo->SensorPreviewResolutionX= OV2686_IMAGE_SENSOR_SVGA_WIDTH - 1 * 8;
	pSensorInfo->SensorPreviewResolutionY= OV2686_IMAGE_SENSOR_SVGA_HEIGHT - 1 * 8;
	pSensorInfo->SensorFullResolutionX= OV2686_IMAGE_SENSOR_UVGA_WITDH - 2 * 8;
	pSensorInfo->SensorFullResolutionY= OV2686_IMAGE_SENSOR_UVGA_HEIGHT - 2 * 8;
	pSensorInfo->SensorCameraPreviewFrameRate=30;
	pSensorInfo->SensorVideoFrameRate=30;
	pSensorInfo->SensorStillCaptureFrameRate=10;
	pSensorInfo->SensorWebCamCaptureFrameRate=15;
	pSensorInfo->SensorResetActiveHigh=FALSE;
	pSensorInfo->SensorResetDelayCount=1;
	pSensorInfo->SensorOutputDataFormat=SENSOR_OUTPUT_FORMAT_YUYV;
	pSensorInfo->SensorClockPolarity=SENSOR_CLOCK_POLARITY_LOW;	
	pSensorInfo->SensorClockFallingPolarity=SENSOR_CLOCK_POLARITY_LOW;
	pSensorInfo->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	pSensorInfo->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	pSensorInfo->SensorInterruptDelayLines = 1;
	pSensorInfo->SensroInterfaceType=SENSOR_INTERFACE_TYPE_PARALLEL;
	pSensorInfo->CaptureDelayFrame = 2;
	pSensorInfo->PreviewDelayFrame = 4; 
	pSensorInfo->VideoDelayFrame = 4; 		
	pSensorInfo->SensorMasterClockSwitch = 0; 
  pSensorInfo->SensorDrivingCurrent = ISP_DRIVING_8MA;   		

	switch (ScenarioId)
	{
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			pSensorInfo->SensorClockFreq=26;
			pSensorInfo->SensorClockDividCount=	3;
			pSensorInfo->SensorClockRisingCount= 0;
			pSensorInfo->SensorClockFallingCount= 2;
			pSensorInfo->SensorPixelClockCount= 3;
			pSensorInfo->SensorDataLatchCount= 2;
            pSensorInfo->SensorGrabStartX = 2; 
            pSensorInfo->SensorGrabStartY = 2;             			
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			pSensorInfo->SensorClockFreq=26;
			pSensorInfo->SensorClockDividCount=	3;
			pSensorInfo->SensorClockRisingCount= 0;
			pSensorInfo->SensorClockFallingCount= 2;
			pSensorInfo->SensorPixelClockCount= 3;
			pSensorInfo->SensorDataLatchCount= 2;
            pSensorInfo->SensorGrabStartX = 2; 
            pSensorInfo->SensorGrabStartY = 2;             
			break;
		default:
			pSensorInfo->SensorClockFreq=26;
			pSensorInfo->SensorClockDividCount=3;
			pSensorInfo->SensorClockRisingCount=0;
			pSensorInfo->SensorClockFallingCount=2;
			pSensorInfo->SensorPixelClockCount=3;
			pSensorInfo->SensorDataLatchCount=2;
            pSensorInfo->SensorGrabStartX = 2; 
            pSensorInfo->SensorGrabStartY = 2;             
			break;
	}

	memcpy(pSensorConfigData, &OV2686SensorConfigData, sizeof(MSDK_SENSOR_CONFIG_STRUCT));
	
	return ERROR_NONE;
}

UINT32 OV2686Control(MSDK_SCENARIO_ID_ENUM ScenarioId, MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *pImageWindow,
					  MSDK_SENSOR_CONFIG_STRUCT *pSensorConfigData)
{
	OV2686SENSORDB("[OV2686Control]\n");
		
	switch (ScenarioId)
	{
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			OV2686Preview(pImageWindow, pSensorConfigData);
			break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:			
			OV2686Capture(pImageWindow, pSensorConfigData);
			break;
		case MSDK_SCENARIO_ID_CAMERA_ZSD:			
			OV2686ZSDPreview(pImageWindow, pSensorConfigData);
			break;
		default:
		    break; 
	}
	
	return TRUE;
}

BOOL OV2686_set_param_wb(UINT16 para)
{
	OV2686SENSORDB("[OV2686_set_param_wb]para=%d\n", para);	

    switch (para)
    {
        case AWB_MODE_OFF:
		         spin_lock(&OV2686_drv_lock);
             OV2686_AWB_ENABLE = KAL_FALSE; 
			       spin_unlock(&OV2686_drv_lock);
             OV2686_set_AWB_mode(OV2686_AWB_ENABLE);
             break;                    
        case AWB_MODE_AUTO:
			       spin_lock(&OV2686_drv_lock);
             OV2686_AWB_ENABLE = KAL_TRUE; 
             spin_unlock(&OV2686_drv_lock);
             OV2686_set_AWB_mode(KAL_TRUE);
			       break;
        case AWB_MODE_CLOUDY_DAYLIGHT:
        	   OV2686_set_AWB_mode(KAL_FALSE); 
        	   OV2686_write_cmos_sensor(0x3208, 0x00); 
        	 
			       OV2686_write_cmos_sensor(0x5195, 0x07);
			       OV2686_write_cmos_sensor(0x5196, 0xdc);
			       OV2686_write_cmos_sensor(0x5197, 0x04);
			       OV2686_write_cmos_sensor(0x5198, 0x00);
			       OV2686_write_cmos_sensor(0x5199, 0x05);
			       OV2686_write_cmos_sensor(0x519a, 0xd3);
			 
             OV2686_write_cmos_sensor(0x3208, 0x10); 
             OV2686_write_cmos_sensor(0x3208, 0xa0); 
             break;	
        case AWB_MODE_DAYLIGHT:
        	   OV2686_set_AWB_mode(KAL_FALSE); 
             OV2686_write_cmos_sensor(0x3208, 0x00);   
                                     
			       OV2686_write_cmos_sensor(0x5195, 0x07);
			       OV2686_write_cmos_sensor(0x5196, 0x9c);
			       OV2686_write_cmos_sensor(0x5197, 0x04);
			       OV2686_write_cmos_sensor(0x5198, 0x00);
			       OV2686_write_cmos_sensor(0x5199, 0x05);
			       OV2686_write_cmos_sensor(0x519a, 0xf3);
			 
             OV2686_write_cmos_sensor(0x3208, 0x10); 
             OV2686_write_cmos_sensor(0x3208, 0xa0); 
			       break;
        case AWB_MODE_INCANDESCENT:
        	   OV2686_set_AWB_mode(KAL_FALSE); 
             OV2686_write_cmos_sensor(0x3208, 0x00); 
             
			       OV2686_write_cmos_sensor(0x5195, 0x06);
			       OV2686_write_cmos_sensor(0x5196, 0xb8);
			       OV2686_write_cmos_sensor(0x5197, 0x04);
			       OV2686_write_cmos_sensor(0x5198, 0x00);
			       OV2686_write_cmos_sensor(0x5199, 0x06);
			       OV2686_write_cmos_sensor(0x519a, 0x5f);
			                                          
             OV2686_write_cmos_sensor(0x3208, 0x10); 
             OV2686_write_cmos_sensor(0x3208, 0xa0); 
			       break;
        case AWB_MODE_TUNGSTEN:
			       OV2686_set_AWB_mode(KAL_FALSE); 
             OV2686_write_cmos_sensor(0x3208, 0x00);  
                                      
             OV2686_write_cmos_sensor(0x5195, 0x04);
			       OV2686_write_cmos_sensor(0x5196, 0x90);
			       OV2686_write_cmos_sensor(0x5197, 0x04);
			       OV2686_write_cmos_sensor(0x5198, 0x00);
			       OV2686_write_cmos_sensor(0x5199, 0x09);
			       OV2686_write_cmos_sensor(0x519a, 0x20); 
                          
             OV2686_write_cmos_sensor(0x3208, 0x10); 
             OV2686_write_cmos_sensor(0x3208, 0xa0); 
			       break;
        case AWB_MODE_FLUORESCENT:
			       OV2686_set_AWB_mode(KAL_FALSE); 
             OV2686_write_cmos_sensor(0x3208, 0x00);  
                                                    
             OV2686_write_cmos_sensor(0x5195, 0x06);
			       OV2686_write_cmos_sensor(0x5196, 0x30);
			       OV2686_write_cmos_sensor(0x5197, 0x04);
			       OV2686_write_cmos_sensor(0x5198, 0x00);
			       OV2686_write_cmos_sensor(0x5199, 0x04);
			       OV2686_write_cmos_sensor(0x519a, 0x30);
                                              
             OV2686_write_cmos_sensor(0x3208, 0x10); 
             OV2686_write_cmos_sensor(0x3208, 0xa0); 
			       break;
        default:
             return FALSE;
    }

    return TRUE;
}

BOOL OV2686_set_param_effect(UINT16 para)
{
	  OV2686SENSORDB("[OV2686_set_param_effect]para=%d\n", para);
    switch (para)
    {
        case MEFFECT_OFF:
          	OV2686_write_cmos_sensor(0x3208,0x00);           	          
          
            OV2686_write_cmos_sensor(0x5600,0x06); 
           	OV2686_write_cmos_sensor(0x5603,0x40); 
            OV2686_write_cmos_sensor(0x5604,0x28);  
                     
           	OV2686_write_cmos_sensor(0x3208,0x10); 
            OV2686_write_cmos_sensor(0x3208,0xa0); 
			      break;
        case MEFFECT_SEPIA:
            OV2686_write_cmos_sensor(0x3208,0x00); 
                      
           	OV2686_write_cmos_sensor(0x5600,0x1e); 
           	OV2686_write_cmos_sensor(0x5603,0x40); 
            OV2686_write_cmos_sensor(0x5604,0xa0);
           	         
          	OV2686_write_cmos_sensor(0x3208,0x10); 
           	OV2686_write_cmos_sensor(0x3208,0xa0); 
		      	break;
        case MEFFECT_NEGATIVE:
          	OV2686_write_cmos_sensor(0x3208,0x00); 
          	          
            OV2686_write_cmos_sensor(0x5600,0x46);            	
           	          
            OV2686_write_cmos_sensor(0x3208,0x10); 
          	OV2686_write_cmos_sensor(0x3208,0xa0); 
			      break;
        case MEFFECT_SEPIAGREEN:
           	OV2686_write_cmos_sensor(0x3208,0x00); 
           	         
           	OV2686_write_cmos_sensor(0x5600,0x1e); 
           	OV2686_write_cmos_sensor(0x5603,0x60); 
            OV2686_write_cmos_sensor(0x5604,0x60);
           	          
           	OV2686_write_cmos_sensor(0x3208,0x10); 
           	OV2686_write_cmos_sensor(0x3208,0xa0); 
            break;
        case MEFFECT_SEPIABLUE:
          	OV2686_write_cmos_sensor(0x3208,0x00); 
          	          
            OV2686_write_cmos_sensor(0x5600,0x1e); 
           	OV2686_write_cmos_sensor(0x5603,0xa0); 
            OV2686_write_cmos_sensor(0x5604,0x40);
           	         
         	  OV2686_write_cmos_sensor(0x3208,0x10); 
           	OV2686_write_cmos_sensor(0x3208,0xa0); 
            break;
		case MEFFECT_MONO:
          	OV2686_write_cmos_sensor(0x3208,0x00); 
          	
            OV2686_write_cmos_sensor(0x5600,0x1e); 
           	OV2686_write_cmos_sensor(0x5603,0x80); 
            OV2686_write_cmos_sensor(0x5604,0x80); 
           	
          	OV2686_write_cmos_sensor(0x3208,0x10); 
           	OV2686_write_cmos_sensor(0x3208,0xa0); 
			      break;
     default:
            return KAL_FALSE;
    }

    return KAL_TRUE;
} 

BOOL OV2686_set_param_banding(UINT16 para)
{
    kal_uint8 banding;
	  kal_uint16 temp_reg = 0;
  	kal_uint32 base_shutter = 0, max_shutter_step = 0, exposure_limitation = 0;
  	kal_uint32 line_length = 0, sensor_pixel_clock = 0;

	OV2686SENSORDB("[OV2686_set_param_banding]para=%d\n", para);	
  
	if (OV2686Sensor.IsPVmode == KAL_TRUE)
	{
		line_length = OV2686_PV_PERIOD_PIXEL_NUMS + OV2686Sensor.PreviewDummyPixels;
		exposure_limitation = OV2686_PV_PERIOD_LINE_NUMS + OV2686Sensor.PreviewDummyLines;
		sensor_pixel_clock = OV2686Sensor.PreviewPclk * 100 * 1000;
	}
	else
	{
		line_length = OV2686_FULL_PERIOD_PIXEL_NUMS + OV2686Sensor.CaptureDummyPixels;
		exposure_limitation = OV2686_FULL_PERIOD_LINE_NUMS + OV2686Sensor.CaptureDummyLines;
		sensor_pixel_clock = OV2686Sensor.CapturePclk * 100 * 1000;
	}
	  line_length = line_length * 2;
	  
    banding = OV2686_read_cmos_sensor(0x3A02);
	
    switch (para)
    {
        case AE_FLICKER_MODE_50HZ:
			spin_lock(&OV2686_drv_lock);
			OV2686_Banding_setting = AE_FLICKER_MODE_50HZ;
			spin_unlock(&OV2686_drv_lock);
			OV2686_write_cmos_sensor(0x3a02, banding|0x80);	
			OV2686_write_cmos_sensor(0x3a0a, 0x0a);
			OV2686_write_cmos_sensor(0x3a0b, 0x92);
			
					
			break;
        case AE_FLICKER_MODE_60HZ:			
			spin_lock(&OV2686_drv_lock);
            OV2686_Banding_setting = AE_FLICKER_MODE_60HZ;
			spin_unlock(&OV2686_drv_lock);
			OV2686_write_cmos_sensor(0x3a02, banding&0x7f);
			OV2686_write_cmos_sensor(0x3a0c, 0x0a);
			OV2686_write_cmos_sensor(0x3a0d, 0x10);
			break;
        default:
			return FALSE;
    }

    return TRUE;
}

BOOL OV2686_set_param_exposure(UINT16 para)
{
    kal_uint8 EvTemp0 = 0x00, EvTemp1 = 0x00, temp_reg= 0x00;

	  OV2686SENSORDB("[OV2686_set_param_exposure]para=%d\n", para);

//modified by zhangxinghong
#if 0
	if (SCENE_MODE_HDR == OV2686Sensor.sceneMode)
   {
       OV2686_set_param_exposure_for_HDR(para);
       return TRUE;
   }
#endif
	
	  temp_reg=OV2686_read_cmos_sensor(0x5608);
	  OV2686_write_cmos_sensor(0x5600,OV2686_read_cmos_sensor(0x5600)|0x04);

    switch (para)
    {	
		case AE_EV_COMP_30:
			                   EvTemp0= 0x30;        //modified by zhangxinghong
			EvTemp1= temp_reg&0xf7;
			break;
	    	case AE_EV_COMP_20:
			                   EvTemp0= 0x20;
			EvTemp1= temp_reg&0xf7;
			break;
		case AE_EV_COMP_10:
			                   EvTemp0= 0x10;
			EvTemp1= temp_reg&0xf7;
			break;
		case AE_EV_COMP_00:
			                   EvTemp0= 0x00;
			EvTemp1= temp_reg&0xf7;
			break;
		case AE_EV_COMP_n10:
			                   EvTemp0= 0x10;
			EvTemp1= temp_reg|0x08;	
			break;
               case AE_EV_COMP_n20:
			                   EvTemp0= 0x20;
			EvTemp1= temp_reg|0x08;	
			break;	
		case AE_EV_COMP_n30:
			                   EvTemp0= 0x30;
			EvTemp1= temp_reg|0x08;	              ////modified by zhangxinghong
			break;		

	
    default:
            return FALSE;
    }
    OV2686_write_cmos_sensor(0x3208, 0x00); 
    
	  OV2686_write_cmos_sensor(0x5607, EvTemp0);
	  OV2686_write_cmos_sensor(0x5608, EvTemp1);
	  	
    OV2686_write_cmos_sensor(0x3208, 0x10); 
    OV2686_write_cmos_sensor(0x3208, 0xa0); 
    return TRUE;
}

UINT32 OV2686YUVSensorSetting(FEATURE_ID iCmd, UINT32 iPara)
{
	OV2686SENSORDB("[OV2686YUVSensorSetting]icmd=%d, ipara=%d\n", iCmd, iPara);

	switch (iCmd) {
		case FID_SCENE_MODE:
			#ifdef  MT6572
				OV2686_set_scene_mode(iPara); 
			#else
	    		if (iPara == SCENE_MODE_OFF)
	       			OV2686_night_mode(KAL_FALSE); 
	    		else if (iPara == SCENE_MODE_NIGHTSCENE)
          			OV2686_night_mode(KAL_TRUE);
			#endif
	    	break; 	    
		case FID_AWB_MODE: 	    
        	OV2686_set_param_wb(iPara);
			break;
		case FID_COLOR_EFFECT:	    	    
         	OV2686_set_param_effect(iPara);
		 	break;
		case FID_AE_EV:    	    
         	OV2686_set_param_exposure(iPara);
		 	break;
		case FID_AE_FLICKER:    	    	    
         	OV2686_set_param_banding(iPara);
		 	break;
    	case FID_AE_SCENE_MODE: 
         	if (iPara == AE_MODE_OFF) {
				spin_lock(&OV2686_drv_lock);
		 		OV2686_AE_ENABLE = KAL_FALSE; 
				spin_unlock(&OV2686_drv_lock);
         	}
         	else 
			{
				spin_lock(&OV2686_drv_lock);
		 		OV2686_AE_ENABLE = KAL_TRUE; 
				spin_unlock(&OV2686_drv_lock);
	     	}
         	OV2686_set_AE_mode(OV2686_AE_ENABLE);
         	break; 
   	 	case FID_ZOOM_FACTOR:	    
			spin_lock(&OV2686_drv_lock);
	     	zoom_factor = iPara; 
			spin_unlock(&OV2686_drv_lock);
         	break; 
#ifdef MT6572
		case FID_ISP_CONTRAST:
            OV2686_set_contrast(iPara);
            break;
        case FID_ISP_BRIGHT:
            OV2686_set_brightness(iPara);
            break;
        case FID_ISP_SAT:
            OV2686_set_saturation(iPara);
            break;
		case FID_AE_ISO:
            OV2686_set_iso(iPara);
            break;
#endif
		default:
		 	break;
	}
	
	return TRUE;
}

UINT32 OV2686YUVSetVideoMode(UINT16 u2FrameRate)
{
	if (u2FrameRate == 30)
	{
      OV2686_write_cmos_sensor(0x3086, 0x01);

      OV2686_write_cmos_sensor(0x380c, 0x06);
      OV2686_write_cmos_sensor(0x380d, 0xac);
      OV2686_write_cmos_sensor(0x380e, 0x02);
      OV2686_write_cmos_sensor(0x380f, 0x84);
      
      OV2686_write_cmos_sensor(0x3820, 0xc0); //modified by zhangxinghong
      OV2686_write_cmos_sensor(0x3821, 0x00);
      
      OV2686_write_cmos_sensor(0x3a06, 0x00);
      OV2686_write_cmos_sensor(0x3a07, 0xc2);
      OV2686_write_cmos_sensor(0x3a08, 0x00);
      OV2686_write_cmos_sensor(0x3a09, 0xa1);
      
      OV2686_write_cmos_sensor(0x3a0e, 0x02);
      OV2686_write_cmos_sensor(0x3a0f, 0x46);
      OV2686_write_cmos_sensor(0x3a10, 0x02);
      OV2686_write_cmos_sensor(0x3a11, 0x84);
      
      OV2686_write_cmos_sensor(0x3a00, 0x41);
      OV2686_write_cmos_sensor(0x3a0a, 0x02);
      OV2686_write_cmos_sensor(0x3a0b, 0x84);
      OV2686_write_cmos_sensor(0x3a0c, 0x02);
      OV2686_write_cmos_sensor(0x3a0d, 0x84);
	}
  else if (u2FrameRate == 15)   
	{
      OV2686_write_cmos_sensor(0x3086, 0x03);

      OV2686_write_cmos_sensor(0x380c, 0x06);
      OV2686_write_cmos_sensor(0x380d, 0xac);
      OV2686_write_cmos_sensor(0x380e, 0x02);
      OV2686_write_cmos_sensor(0x380f, 0x84);
      
      OV2686_write_cmos_sensor(0x3820, 0xc0); //modified by zhangxinghong
      OV2686_write_cmos_sensor(0x3821, 0x00);
      
      OV2686_write_cmos_sensor(0x3a06, 0x00);
      OV2686_write_cmos_sensor(0x3a07, 0x61);
      OV2686_write_cmos_sensor(0x3a08, 0x00);
      OV2686_write_cmos_sensor(0x3a09, 0x51);
      
      OV2686_write_cmos_sensor(0x3a0e, 0x02);
      OV2686_write_cmos_sensor(0x3a0f, 0x46);
      OV2686_write_cmos_sensor(0x3a10, 0x02);
      OV2686_write_cmos_sensor(0x3a11, 0x84);
      
      OV2686_write_cmos_sensor(0x3a00, 0x41);
      OV2686_write_cmos_sensor(0x3a0a, 0x02);
      OV2686_write_cmos_sensor(0x3a0b, 0x84);
      OV2686_write_cmos_sensor(0x3a0c, 0x02);
      OV2686_write_cmos_sensor(0x3a0d, 0x84); 
	}
  else 
  {
     printk("Wrong frame rate setting \n");
  }   
	mDELAY(30);

  return TRUE;
}

UINT32 OV2686SetMaxFramerateByScenario(MSDK_SCENARIO_ID_ENUM scenarioId, MUINT32 frameRate) {
	kal_uint32 pclk;
	kal_int16 dummyLine;
	kal_uint16 lineLength,frameHeight;
		
	switch (scenarioId) 
	{
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
			pclk = 480/10;
			lineLength = OV2686_PV_PERIOD_PIXEL_NUMS;
			frameHeight = (10 * pclk)/frameRate/lineLength;
			dummyLine = frameHeight - OV2686_PV_PERIOD_LINE_NUMS;
			OV2686SENSORDB("[OV2686SetMaxFramerateByScenario][preview]framerate=%d, dummy_line=%d\n",frameRate, dummyLine);
			OV2686SetDummy(0, dummyLine);			
			break;			
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			pclk = 480/10;
			lineLength = OV2686_PV_PERIOD_PIXEL_NUMS;
			frameHeight = (10 * pclk)/frameRate/lineLength;
			dummyLine = frameHeight - OV2686_PV_PERIOD_LINE_NUMS;
			OV2686SENSORDB("[OV2686SetMaxFramerateByScenario][video]framerate=%d, dummy_line=%d\n",frameRate, dummyLine);
			OV2686SetDummy(0, dummyLine);			
			break;			
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		case MSDK_SCENARIO_ID_CAMERA_ZSD:			
			pclk = 480/10;
			lineLength = OV2686_FULL_PERIOD_PIXEL_NUMS;
			frameHeight = (10 * pclk)/frameRate/lineLength;
			dummyLine = frameHeight - OV2686_FULL_PERIOD_LINE_NUMS;
			OV2686SENSORDB("[OV2686SetMaxFramerateByScenario][capture/zsd]framerate=%d, dummy_line=%d\n",frameRate, dummyLine);
			OV2686SetDummy(0, dummyLine);			
			break;		
        case MSDK_SCENARIO_ID_CAMERA_3D_PREVIEW: 
        case MSDK_SCENARIO_ID_CAMERA_3D_VIDEO:
        case MSDK_SCENARIO_ID_CAMERA_3D_CAPTURE:  
			break;		
		default:
			break;
	}	
	
	return ERROR_NONE;
}

UINT32 OV2686GetDefaultFramerateByScenario(MSDK_SCENARIO_ID_ENUM scenarioId, MUINT32 *pframeRate) 
{
	OV2686SENSORDB("[OV2686GetDefaultFramerateByScenario]\n");
	
	switch (scenarioId) 
	{
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			 *pframeRate = 300;
			 break;
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		case MSDK_SCENARIO_ID_CAMERA_ZSD:
			 *pframeRate = 300;
			break;		
        case MSDK_SCENARIO_ID_CAMERA_3D_PREVIEW: 
        case MSDK_SCENARIO_ID_CAMERA_3D_VIDEO:
        case MSDK_SCENARIO_ID_CAMERA_3D_CAPTURE: 
			 *pframeRate = 300;
			break;		
		default:
			break;
	}

	return ERROR_NONE;
}

#ifdef MT6572
UINT32 OV2686SetTestPatternMode(kal_bool bEnable)
{	
	OV2686SENSORDB("[OV2686SetTestPatternMode]bEnable=%d\n",bEnable);	

	if(bEnable)	
		OV2686_write_cmos_sensor(0x5080,0x80);		
	else	
		OV2686_write_cmos_sensor(0x5080,0x00);		

	return ERROR_NONE;
}

void OV2686Set3ACtrl(ACDK_SENSOR_3A_LOCK_ENUM action)
{
	OV2686SENSORDB("[OV2686Set3ACtrl]action=%d\n",action);	

   switch (action)
   {
      case SENSOR_3A_AE_LOCK:
          OV2686_set_AE_mode(KAL_FALSE);
      break;
      case SENSOR_3A_AE_UNLOCK:
          OV2686_set_AE_mode(KAL_TRUE);
      break;
      case SENSOR_3A_AWB_LOCK:
          OV2686_set_AWB_mode(KAL_FALSE);
      break;
      case SENSOR_3A_AWB_UNLOCK:
          OV2686_set_AWB_mode(KAL_TRUE);
      break;
      default:
      	break;
   }
   
   return;
}

static void OV2686GetCurAeAwbInfo(UINT32 pSensorAEAWBCurStruct)
{
	OV2686SENSORDB("[OV2686GetCurAeAwbInfo]\n");	
	
	PSENSOR_AE_AWB_CUR_STRUCT Info = (PSENSOR_AE_AWB_CUR_STRUCT)pSensorAEAWBCurStruct;
	Info->SensorAECur.AeCurShutter=OV2686ReadShutter();
	Info->SensorAECur.AeCurGain=OV2686ReadSensorGain() * 2;
	Info->SensorAwbGainCur.AwbCurRgain=OV2686_read_cmos_sensor(0x504c);
	Info->SensorAwbGainCur.AwbCurBgain=OV2686_read_cmos_sensor(0x504e);
}

void OV2686_get_AEAWB_lock(UINT32 *pAElockRet32, UINT32 *pAWBlockRet32)
{
	OV2686SENSORDB("[OV2686_get_AEAWB_lock]\n");	
	
	*pAElockRet32 =1;
	*pAWBlockRet32=1;
}

void OV2686_GetDelayInfo(UINT32  delayAddr)
{
	OV2686SENSORDB("[OV2686_GetDelayInfo]\n");	
	
	SENSOR_DELAY_INFO_STRUCT *pDelayInfo=(SENSOR_DELAY_INFO_STRUCT*)delayAddr;
	pDelayInfo->InitDelay=0;
	pDelayInfo->EffectDelay=0;
	pDelayInfo->AwbDelay=0;
	pDelayInfo->AFSwitchDelayFrame=50;
}

void OV2686_AutoTestCmd(UINT32 *cmd,UINT32 *para)
{
	OV2686SENSORDB("[OV2686_AutoTestCmd]\n");
	
	switch(*cmd)
	{
		case YUV_AUTOTEST_SET_SHADDING:
		case YUV_AUTOTEST_SET_GAMMA:
		case YUV_AUTOTEST_SET_AE:
		case YUV_AUTOTEST_SET_SHUTTER:
		case YUV_AUTOTEST_SET_GAIN:
		case YUV_AUTOTEST_GET_SHUTTER_RANGE:
			break;
		default:
			break;	
	}
}

void OV2686GetExifInfo(UINT32  exifAddr)
{
	OV2686SENSORDB("[OV2686_AutoTestCmd]\n");
	
	SENSOR_EXIF_INFO_STRUCT* pExifInfo = (SENSOR_EXIF_INFO_STRUCT*)exifAddr;
    pExifInfo->FNumber = 28;
    pExifInfo->AEISOSpeed = OV2686Sensor.isoSpeed;
    pExifInfo->FlashLightTimeus = 0;
    pExifInfo->RealISOValue = OV2686Sensor.isoSpeed;
}
#endif



//PR 817954  add by zhangxinghong 
#define FLASH_BV_THRESHOLD 0x42     //打闪阈\E5\80?
static void OV2686_DVP_FlashTriggerCheck(unsigned int *pFeatureReturnPara32)
{
    unsigned int NormBr;

    NormBr =OV2686_read_cmos_sensor(0x3a19);
    printk("OV2686_DVP_FlashTriggerCheck_%x\n",NormBr);
    if (NormBr > FLASH_BV_THRESHOLD)
    {
       *pFeatureReturnPara32 = 0;
        return;
    }
    *pFeatureReturnPara32 = 1;
    return;
}


UINT32 OV2686FeatureControl(MSDK_SENSOR_FEATURE_ENUM FeatureId,
							 UINT8 *pFeaturePara,UINT32 *pFeatureParaLen)
{
	UINT16 *pFeatureReturnPara16=(UINT16 *) pFeaturePara;
	UINT16 *pFeatureData16=(UINT16 *) pFeaturePara;
	UINT32 *pFeatureReturnPara32=(UINT32 *) pFeaturePara;
	UINT32 *pFeatureData32=(UINT32 *) pFeaturePara;

//	unsigned long long  *pFeatureReturnPara32=(UINT32 *) pFeaturePara;
//	unsigned long long  *pFeatureData32=(UINT32 *) pFeaturePara;

	MSDK_SENSOR_CONFIG_STRUCT *pSensorConfigData=(MSDK_SENSOR_CONFIG_STRUCT *) pFeaturePara;
	MSDK_SENSOR_REG_INFO_STRUCT *pSensorRegData=(MSDK_SENSOR_REG_INFO_STRUCT *) pFeaturePara;
	UINT32 Tony_Temp1 = 0;
	UINT32 Tony_Temp2 = 0;
	Tony_Temp1 = pFeaturePara[0];
	Tony_Temp2 = pFeaturePara[1];
	
	switch (FeatureId)
	{
		case SENSOR_FEATURE_GET_RESOLUTION:
			*pFeatureReturnPara16++=OV2686_IMAGE_SENSOR_UVGA_WITDH;
			*pFeatureReturnPara16=OV2686_IMAGE_SENSOR_UVGA_HEIGHT;
			*pFeatureParaLen=4;
			break;
		case SENSOR_FEATURE_GET_PERIOD:
			*pFeatureReturnPara16++=OV2686_PV_PERIOD_PIXEL_NUMS + OV2686Sensor.PreviewDummyPixels;
			*pFeatureReturnPara16=OV2686_PV_PERIOD_LINE_NUMS + OV2686Sensor.PreviewDummyLines;
			*pFeatureParaLen=4;
			break;
		case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
			*pFeatureReturnPara32 = OV2686Sensor.PreviewPclk * 1000 *100;
			*pFeatureParaLen=4;
			break;
		case SENSOR_FEATURE_SET_NIGHTMODE:
			OV2686_night_mode((BOOL) *pFeatureData16);
			break;
       		case SENSOR_FEATURE_GET_TRIGGER_FLASHLIGHT_INFO:        //PR 817954  add by zhangxinghong 
            		OV2686_DVP_FlashTriggerCheck(pFeatureData32);
//             		printk("[OV2686] F_GET_TRIGGER_FLASHLIGHT_INFO: %d\n", pFeatureData32);
             		break;

		case SENSOR_FEATURE_SET_REGISTER:
			OV2686_write_cmos_sensor(pSensorRegData->RegAddr, pSensorRegData->RegData);
			break;
		case SENSOR_FEATURE_GET_REGISTER:
			pSensorRegData->RegData = OV2686_read_cmos_sensor(pSensorRegData->RegAddr);
			break;
		case SENSOR_FEATURE_GET_CONFIG_PARA:
			memcpy(pSensorConfigData, &OV2686SensorConfigData, sizeof(MSDK_SENSOR_CONFIG_STRUCT));
			*pFeatureParaLen=sizeof(MSDK_SENSOR_CONFIG_STRUCT);
			break;
		case SENSOR_FEATURE_GET_GROUP_COUNT:
                       *pFeatureReturnPara32++=0;
                       *pFeatureParaLen=4;	   
		        break; 
		case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
			*pFeatureReturnPara32=LENS_DRIVER_ID_DO_NOT_CARE;
			*pFeatureParaLen=4;
			break;
		case SENSOR_FEATURE_CHECK_SENSOR_ID:
			OV2686_GetSensorID(pFeatureData32);
			break;
		case SENSOR_FEATURE_SET_YUV_CMD:
			OV2686YUVSensorSetting((FEATURE_ID)*pFeatureData32, *(pFeatureData32+1));
			break;
		case SENSOR_FEATURE_SET_VIDEO_MODE:
		    OV2686YUVSetVideoMode(*pFeatureData16);
		    break; 
		case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
			OV2686SetMaxFramerateByScenario((MSDK_SCENARIO_ID_ENUM)*pFeatureData32, *(pFeatureData32+1));
			break;
		case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
			OV2686GetDefaultFramerateByScenario((MSDK_SCENARIO_ID_ENUM)*pFeatureData32, (MUINT32 *)(*(pFeatureData32+1)));
			break;
		#ifdef MT6572
		case SENSOR_FEATURE_SET_TEST_PATTERN:            			
			OV2686SetTestPatternMode((BOOL)*pFeatureData16);            			
			break;
		case SENSOR_FEATURE_SET_YUV_3A_CMD:
            OV2686Set3ACtrl((ACDK_SENSOR_3A_LOCK_ENUM)*pFeatureData32);
            break;
		case SENSOR_FEATURE_GET_SHUTTER_GAIN_AWB_GAIN:
			OV2686GetCurAeAwbInfo(*pFeatureData32);			
			break;
		case SENSOR_FEATURE_GET_AE_AWB_LOCK_INFO:
			OV2686_get_AEAWB_lock(*pFeatureData32, *(pFeatureData32+1));
			break;
		case SENSOR_FEATURE_GET_DELAY_INFO:
			OV2686_GetDelayInfo(*pFeatureData32);
			break;
		case SENSOR_FEATURE_AUTOTEST_CMD:
			OV2686_AutoTestCmd(*pFeatureData32,*(pFeatureData32+1));
			break;
		case SENSOR_FEATURE_GET_EXIF_INFO:
            OV2686GetExifInfo(*pFeatureData32);
            break;
		#endif
		case SENSOR_FEATURE_SET_GAIN:
		case SENSOR_FEATURE_SET_FLASHLIGHT:
		case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
		case SENSOR_FEATURE_SET_ESHUTTER:
		case SENSOR_FEATURE_SET_CCT_REGISTER:
		case SENSOR_FEATURE_GET_CCT_REGISTER:
		case SENSOR_FEATURE_SET_ENG_REGISTER:
		case SENSOR_FEATURE_GET_ENG_REGISTER:
		case SENSOR_FEATURE_GET_REGISTER_DEFAULT:
		case SENSOR_FEATURE_CAMERA_PARA_TO_SENSOR:
		case SENSOR_FEATURE_SENSOR_TO_CAMERA_PARA:
		case SENSOR_FEATURE_GET_GROUP_INFO:
		case SENSOR_FEATURE_GET_ITEM_INFO:
		case SENSOR_FEATURE_SET_ITEM_INFO:
		case SENSOR_FEATURE_GET_ENG_INFO:
			break;
		default:
			break;			
	}
	return ERROR_NONE;
}

SENSOR_FUNCTION_STRUCT	SensorFuncOV2686=
{
	OV2686Open,
	OV2686GetInfo,
	OV2686GetResolution,
	OV2686FeatureControl,
	OV2686Control,
	OV2686Close
};

UINT32 OV2686_YUV_SensorInit(PSENSOR_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc!=NULL)
		*pfFunc=&SensorFuncOV2686;

	return ERROR_NONE;
}

