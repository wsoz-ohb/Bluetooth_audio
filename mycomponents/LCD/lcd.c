#include "lcd.h"
#include "lcdfont.h"
typedef struct
{
    const lcd_bus_ops_t *ops;
    void *user;
    lcd_bus_config_t config;
    u16 width;
    u16 height;
    int ready;
    int last_error;
} lcd_context_t;

static lcd_context_t lcd_ctx;

static void LCD_WR_DATA8(u8 dat);
static void LCD_WR_DATA(u16 dat);
static void LCD_WR_REG(u8 dat);
static void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2);

static int lcd_port_valid(const lcd_bus_ops_t *ops)
{
    return (ops != 0) &&
           (ops->write_command != 0) &&
           (ops->write_data != 0) &&
           (ops->delay_ms != 0);
}

static int lcd_set_error(int err)
{
    lcd_ctx.last_error = err;
    return err;
}

static void lcd_update_geometry(void)
{
    if (lcd_ctx.config.rotation == LCD_ROTATION_0 || lcd_ctx.config.rotation == LCD_ROTATION_180)
    {
        lcd_ctx.width = lcd_ctx.config.panel_width;
        lcd_ctx.height = lcd_ctx.config.panel_height;
    }
    else
    {
        lcd_ctx.width = lcd_ctx.config.panel_height;
        lcd_ctx.height = lcd_ctx.config.panel_width;
    }
}

static int lcd_write_command_buf(const uint8_t *data, size_t size)
{
    if (!lcd_port_valid(lcd_ctx.ops))
    {
        return lcd_set_error(LCD_ESTATE);
    }
    if (size == 0)
    {
        return LCD_EOK;
    }
    if (lcd_ctx.ops->write_command(lcd_ctx.user, data, size) != 0)
    {
        return lcd_set_error(LCD_EIO);
    }
    return LCD_EOK;
}

static int lcd_write_data_buf(const uint8_t *data, size_t size)
{
    if (!lcd_port_valid(lcd_ctx.ops))
    {
        return lcd_set_error(LCD_ESTATE);
    }
    if (size == 0)
    {
        return LCD_EOK;
    }
    if (lcd_ctx.ops->write_data(lcd_ctx.user, data, size) != 0)
    {
        return lcd_set_error(LCD_EIO);
    }
    return LCD_EOK;
}

static int lcd_write_command_u8(u8 cmd)
{
    return lcd_write_command_buf(&cmd, 1);
}

static int lcd_write_data_u8(u8 dat)
{
    return lcd_write_data_buf(&dat, 1);
}

static int lcd_write_data_u16(u16 dat)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(dat >> 8);
    buf[1] = (uint8_t)(dat & 0xFF);
    return lcd_write_data_buf(buf, sizeof(buf));
}

static void lcd_delay_ms(uint32_t ms)
{
    if (!lcd_port_valid(lcd_ctx.ops))
    {
        lcd_set_error(LCD_ESTATE);
        return;
    }
    lcd_ctx.ops->delay_ms(lcd_ctx.user, ms);
}

static void lcd_reset_pin(int level)
{
    if (lcd_ctx.ops != 0 && lcd_ctx.ops->reset != 0)
    {
        lcd_ctx.ops->reset(lcd_ctx.user, level);
    }
}

static void lcd_backlight(int on)
{
    if (lcd_ctx.ops != 0 && lcd_ctx.ops->backlight != 0)
    {
        lcd_ctx.ops->backlight(lcd_ctx.user, on);
    }
}

static u8 lcd_ili9341_rotation_value(lcd_rotation_t rotation)
{
    switch (rotation)
    {
    case LCD_ROTATION_0:
        return 0x08;
    case LCD_ROTATION_90:
        return 0xC8;
    case LCD_ROTATION_180:
        return 0x78;
    case LCD_ROTATION_270:
    default:
        return 0xA8;
    }
}

static u8 lcd_st7789_rotation_value(lcd_rotation_t rotation)
{
    /*
     * ST7789 MADCTL:
     * bit7 MY, bit6 MX, bit5 MV, bit3 RGB/BGR
     *
     * Current panel works in portrait mode with MADCTL = 0x00.
     * Therefore the 4-direction rotation mapping should be derived
     * from that native orientation. The previous 90-degree value
     * missed the MV bit, so software geometry became 320x240 while
     * the controller still interpreted coordinates as portrait.
     */
    switch (rotation)
    {
    case LCD_ROTATION_0:
        return 0x00;
    case LCD_ROTATION_90:
        return 0x60;
    case LCD_ROTATION_180:
        return 0xC0;
    case LCD_ROTATION_270:
    default:
        return 0xA0;
    }
}

static int lcd_address_set_raw(u16 x1, u16 y1, u16 x2, u16 y2)
{
    int result;

    result = lcd_write_command_u8(0x2A);
    if (result != LCD_EOK) return result;
    result = lcd_write_data_u16(x1);
    if (result != LCD_EOK) return result;
    result = lcd_write_data_u16(x2);
    if (result != LCD_EOK) return result;
    result = lcd_write_command_u8(0x2B);
    if (result != LCD_EOK) return result;
    result = lcd_write_data_u16(y1);
    if (result != LCD_EOK) return result;
    result = lcd_write_data_u16(y2);
    if (result != LCD_EOK) return result;
    return lcd_write_command_u8(0x2C);
}

static int lcd_init_ili9341(void)
{
    int result;

#define LCD_CMD8(v)  do { result = lcd_write_command_u8((u8)(v)); if (result != LCD_EOK) return result; } while (0)
#define LCD_DATA8(v) do { result = lcd_write_data_u8((u8)(v)); if (result != LCD_EOK) return result; } while (0)

    LCD_CMD8(0x11);
    lcd_delay_ms(120);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;

    LCD_CMD8(0xCF); LCD_DATA8(0x00); LCD_DATA8(0xC1); LCD_DATA8(0x30);
    LCD_CMD8(0xED); LCD_DATA8(0x64); LCD_DATA8(0x03); LCD_DATA8(0x12); LCD_DATA8(0x81);
    LCD_CMD8(0xE8); LCD_DATA8(0x85); LCD_DATA8(0x00); LCD_DATA8(0x79);
    LCD_CMD8(0xCB); LCD_DATA8(0x39); LCD_DATA8(0x2C); LCD_DATA8(0x00); LCD_DATA8(0x34); LCD_DATA8(0x02);
    LCD_CMD8(0xF7); LCD_DATA8(0x20);
    LCD_CMD8(0xEA); LCD_DATA8(0x00); LCD_DATA8(0x00);
    LCD_CMD8(0xC0); LCD_DATA8(0x1D);
    LCD_CMD8(0xC1); LCD_DATA8(0x12);
    LCD_CMD8(0xC5); LCD_DATA8(0x33); LCD_DATA8(0x3F);
    LCD_CMD8(0xC7); LCD_DATA8(0x92);
    LCD_CMD8(0x3A); LCD_DATA8(0x55);
    LCD_CMD8(0x36); LCD_DATA8(lcd_ili9341_rotation_value(lcd_ctx.config.rotation));
    LCD_CMD8(0xB1); LCD_DATA8(0x00); LCD_DATA8(0x12);
    LCD_CMD8(0xB6); LCD_DATA8(0x0A); LCD_DATA8(0xA2);
    LCD_CMD8(0x44); LCD_DATA8(0x02);
    LCD_CMD8(0xF2); LCD_DATA8(0x00);
    LCD_CMD8(0x26); LCD_DATA8(0x01);
    LCD_CMD8(0xE0);
    LCD_DATA8(0x0F); LCD_DATA8(0x22); LCD_DATA8(0x1C); LCD_DATA8(0x1B); LCD_DATA8(0x08);
    LCD_DATA8(0x0F); LCD_DATA8(0x48); LCD_DATA8(0xB8); LCD_DATA8(0x34); LCD_DATA8(0x05);
    LCD_DATA8(0x0C); LCD_DATA8(0x09); LCD_DATA8(0x0F); LCD_DATA8(0x07); LCD_DATA8(0x00);
    LCD_CMD8(0xE1);
    LCD_DATA8(0x00); LCD_DATA8(0x23); LCD_DATA8(0x24); LCD_DATA8(0x07); LCD_DATA8(0x10);
    LCD_DATA8(0x07); LCD_DATA8(0x38); LCD_DATA8(0x47); LCD_DATA8(0x4B); LCD_DATA8(0x0A);
    LCD_DATA8(0x13); LCD_DATA8(0x06); LCD_DATA8(0x30); LCD_DATA8(0x38); LCD_DATA8(0x0F);
    LCD_CMD8(0x29);

#undef LCD_CMD8
#undef LCD_DATA8
    return LCD_EOK;
}

static int lcd_init_st7789(void)
{
    int result;

#define LCD_CMD8(v)  do { result = lcd_write_command_u8((u8)(v)); if (result != LCD_EOK) return result; } while (0)
#define LCD_DATA8(v) do { result = lcd_write_data_u8((u8)(v)); if (result != LCD_EOK) return result; } while (0)

    lcd_delay_ms(500);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;

    LCD_CMD8(0x11);
    lcd_delay_ms(100);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;

    LCD_CMD8(0x36); LCD_DATA8(lcd_st7789_rotation_value(lcd_ctx.config.rotation));
    LCD_CMD8(0x3A); LCD_DATA8(0x05);
    LCD_CMD8(0xB2); LCD_DATA8(0x0C); LCD_DATA8(0x0C); LCD_DATA8(0x00); LCD_DATA8(0x33); LCD_DATA8(0x33);
    LCD_CMD8(0xB7); LCD_DATA8(0x35);
    LCD_CMD8(0xBB); LCD_DATA8(0x35);
    LCD_CMD8(0xC0); LCD_DATA8(0x2C);
    LCD_CMD8(0xC2); LCD_DATA8(0x01);
    LCD_CMD8(0xC3); LCD_DATA8(0x13);
    LCD_CMD8(0xC4); LCD_DATA8(0x20);
    LCD_CMD8(0xC6); LCD_DATA8(0x0F);
    LCD_CMD8(0xCA); LCD_DATA8(0x0F);
    LCD_CMD8(0xC8); LCD_DATA8(0x08);
    LCD_CMD8(0x55); LCD_DATA8(0x90);
    LCD_CMD8(0xD0); LCD_DATA8(0xA4); LCD_DATA8(0xA1);

    LCD_CMD8(0xE0);
    LCD_DATA8(0xD0); LCD_DATA8(0x00); LCD_DATA8(0x06); LCD_DATA8(0x09); LCD_DATA8(0x0B);
    LCD_DATA8(0x2A); LCD_DATA8(0x3C); LCD_DATA8(0x55); LCD_DATA8(0x4B); LCD_DATA8(0x08);
    LCD_DATA8(0x16); LCD_DATA8(0x14); LCD_DATA8(0x19); LCD_DATA8(0x20);
    LCD_CMD8(0xE1);
    LCD_DATA8(0xD0); LCD_DATA8(0x00); LCD_DATA8(0x06); LCD_DATA8(0x09); LCD_DATA8(0x0B);
    LCD_DATA8(0x29); LCD_DATA8(0x36); LCD_DATA8(0x54); LCD_DATA8(0x4B); LCD_DATA8(0x0D);
    LCD_DATA8(0x16); LCD_DATA8(0x14); LCD_DATA8(0x21); LCD_DATA8(0x20);
    LCD_CMD8(0x29);

#undef LCD_CMD8
#undef LCD_DATA8
    return LCD_EOK;
}

void LCD_ConfigInitDefault(lcd_bus_config_t *config)
{
    if (config == 0)
    {
        return;
    }

    config->panel = LCD_PANEL_ST7789;
    config->rotation = LCD_ROTATION_0;
    config->panel_width = 240;
    config->panel_height = 320;
}

int LCD_Attach(const lcd_bus_ops_t *ops, void *user, const lcd_bus_config_t *config)
{
    lcd_bus_config_t default_config;

    if (!lcd_port_valid(ops))
    {
        return lcd_set_error(LCD_EINVAL);
    }

    LCD_ConfigInitDefault(&default_config);
    lcd_ctx.ops = ops;
    lcd_ctx.user = user;
    lcd_ctx.config = (config != 0) ? *config : default_config;
    lcd_ctx.ready = 0;
    lcd_ctx.last_error = LCD_EOK;
    lcd_update_geometry();
    return LCD_EOK;
}

int LCD_SetConfig(const lcd_bus_config_t *config)
{
    if (config == 0)
    {
        return lcd_set_error(LCD_EINVAL);
    }
    if (config->panel_width == 0 || config->panel_height == 0)
    {
        return lcd_set_error(LCD_EINVAL);
    }

    lcd_ctx.config = *config;
    lcd_ctx.ready = 0;
    lcd_update_geometry();
    return lcd_set_error(LCD_EOK);
}

int LCD_Init(void)
{
    int result;

    if (!lcd_port_valid(lcd_ctx.ops))
    {
        return lcd_set_error(LCD_ESTATE);
    }
    if (lcd_ctx.config.panel_width == 0 || lcd_ctx.config.panel_height == 0)
    {
        return lcd_set_error(LCD_EINVAL);
    }

    lcd_ctx.ready = 0;
    lcd_ctx.last_error = LCD_EOK;
    lcd_update_geometry();

    lcd_reset_pin(0);
    lcd_delay_ms(100);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;
    lcd_reset_pin(1);
    lcd_delay_ms(100);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;
    lcd_backlight(1);
    lcd_delay_ms(100);
    if (lcd_ctx.last_error != LCD_EOK) return lcd_ctx.last_error;

    switch (lcd_ctx.config.panel)
    {
    case LCD_PANEL_ILI9341:
        result = lcd_init_ili9341();
        break;
    case LCD_PANEL_ST7789:
        result = lcd_init_st7789();
        break;
    default:
        return lcd_set_error(LCD_EUNSUPPORTED);
    }

    if (result != LCD_EOK)
    {
        return result;
    }

    lcd_ctx.ready = 1;
    return lcd_set_error(LCD_EOK);
}

int LCD_IsReady(void)
{
    return lcd_ctx.ready;
}

int LCD_GetLastError(void)
{
    return lcd_ctx.last_error;
}

u16 LCD_GetWidth(void)
{
    return lcd_ctx.width;
}

u16 LCD_GetHeight(void)
{
    return lcd_ctx.height;
}

static void LCD_WR_DATA8(u8 dat)
{
    (void)lcd_write_data_u8(dat);
}

static void LCD_WR_DATA(u16 dat)
{
    (void)lcd_write_data_u16(dat);
}

static void LCD_WR_REG(u8 dat)
{
    (void)lcd_write_command_u8(dat);
}

static void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2)
{
    (void)lcd_address_set_raw(x1, y1, x2, y2);
}

#define LCD_RETURN_IF_NOT_READY() \
    do \
    { \
        if (!LCD_IsReady()) \
        { \
            lcd_set_error(LCD_ESTATE); \
            return; \
        } \
    } while (0)


/******************************************************************************
      函数说明：指定区域填充颜色
      输入参数：xsta, ysta  起始坐标
                xend, yend  终止坐标
                color       要填充的颜色
      返回值：  无
******************************************************************************/
void LCD_Fill(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color)
{          
	LCD_RETURN_IF_NOT_READY();
	u16 i,j; 
	LCD_Address_Set(xsta,ysta,xend-1,yend-1);//设置显示范围
	for(i=ysta;i<yend;i++)
	{													   	 	
		for(j=xsta;j<xend;j++)
		{
			LCD_WR_DATA(color);
		}
	} 					  	    
}

/******************************************************************************
      函数说明：在指定位置画点
      输入参数：x, y   点坐标
                color  画点颜色
      返回值：  无
******************************************************************************/
void LCD_DrawPoint(u16 x,u16 y,u16 color)
{
	LCD_RETURN_IF_NOT_READY();
	LCD_Address_Set(x,y,x,y);//设置光标位置
	LCD_WR_DATA(color);
} 


/******************************************************************************
      函数说明：画线
      输入参数：x1, y1  起始坐标
                x2, y2  终止坐标
                color   线条颜色
      返回值：  无
******************************************************************************/
void LCD_DrawLine(u16 x1,u16 y1,u16 x2,u16 y2,u16 color)
{
	LCD_RETURN_IF_NOT_READY();
	u16 t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //计算 x 方向增量
	delta_y=y2-y1;
	uRow=x1;//当前绘制点 x 坐标
	uCol=y1;
	if(delta_x>0)incx=1; //设置 x 方向步进
	else if (delta_x==0)incx=0;//垂直线
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//水平线
	else {incy=-1;delta_y=-delta_y;}
	if(delta_x>delta_y)distance=delta_x; //取增量较大的方向作为步数
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		LCD_DrawPoint(uRow,uCol,color);//画点
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}


/******************************************************************************
      函数说明：画矩形
      输入参数：x1, y1  起始坐标
                x2, y2  终止坐标
                color   矩形颜色
      返回值：  无
******************************************************************************/
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2,u16 color)
{
	LCD_RETURN_IF_NOT_READY();
	LCD_DrawLine(x1,y1,x2,y1,color);
	LCD_DrawLine(x1,y1,x1,y2,color);
	LCD_DrawLine(x1,y2,x2,y2,color);
	LCD_DrawLine(x2,y1,x2,y2,color);
}


/******************************************************************************
      函数说明：画圆
      输入参数：x0, y0  圆心坐标
                r       半径
                color   圆的颜色
      返回值：  无
******************************************************************************/
void Draw_Circle(u16 x0,u16 y0,u8 r,u16 color)
{
	LCD_RETURN_IF_NOT_READY();
	int a,b;
	a=0;b=r;	  
	while(a<=b)
	{
		LCD_DrawPoint(x0-b,y0-a,color);             //3           
		LCD_DrawPoint(x0+b,y0-a,color);             //0           
		LCD_DrawPoint(x0-a,y0+b,color);             //1                
		LCD_DrawPoint(x0-a,y0-b,color);             //2             
		LCD_DrawPoint(x0+b,y0+a,color);             //4               
		LCD_DrawPoint(x0+a,y0-b,color);             //5
		LCD_DrawPoint(x0+a,y0+b,color);             //6 
		LCD_DrawPoint(x0-b,y0+a,color);             //7
		a++;
		if((a*a+b*b)>(r*r))//判断是否需要减小 y 偏移
		{
			b--;
		}
	}
}

/******************************************************************************
      函数说明：显示汉字串
      输入参数：x, y   显示起点
                *s     要显示的汉字串
                fc     字体颜色
                bc     背景颜色
                sizey  字号，可选 12/16/24/32
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChinese(u16 x,u16 y,u8 *s,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	while(*s!=0)
	{
		if(sizey==12) LCD_ShowChinese12x12(x,y,s,fc,bc,sizey,mode);
		else if(sizey==16) LCD_ShowChinese16x16(x,y,s,fc,bc,sizey,mode);
		else if(sizey==24) LCD_ShowChinese24x24(x,y,s,fc,bc,sizey,mode);
		else if(sizey==32) LCD_ShowChinese32x32(x,y,s,fc,bc,sizey,mode);
		else return;
		s+=2;
		x+=sizey;
	}
}

/******************************************************************************
      函数说明：显示单个 12x12 汉字
      输入参数：x, y   显示起点
                *s     要显示的汉字
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChinese12x12(u16 x,u16 y,u8 *s,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	u8 i,j,m=0;
	u16 k;
	u16 HZnum;//汉字数目
	u16 TypefaceNum;//单个字符占用的字节数
	u16 x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	                         
	HZnum=sizeof(tfont12)/sizeof(typFNT_GB12);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if((tfont12[k].Index[0]==*(s))&&(tfont12[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //继续查找下一项
	}
} 

/******************************************************************************
      函数说明：显示单个 16x16 汉字
      输入参数：x, y   显示起点
                *s     要显示的汉字
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChinese16x16(u16 x,u16 y,u8 *s,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	u8 i,j,m=0;
	u16 k;
	u16 HZnum;//汉字数目
	u16 TypefaceNum;//单个字符占用的字节数
	u16 x0=x;
  TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont16)/sizeof(typFNT_GB16);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont16[k].Index[0]==*(s))&&(tfont16[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //继续查找下一项
	}
} 


/******************************************************************************
      函数说明：显示单个 24x24 汉字
      输入参数：x, y   显示起点
                *s     要显示的汉字
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChinese24x24(u16 x,u16 y,u8 *s,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	u8 i,j,m=0;
	u16 k;
	u16 HZnum;//汉字数目
	u16 TypefaceNum;//单个字符占用的字节数
	u16 x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont24)/sizeof(typFNT_GB24);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont24[k].Index[0]==*(s))&&(tfont24[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //继续查找下一项
	}
} 

/******************************************************************************
      函数说明：显示单个 32x32 汉字
      输入参数：x, y   显示起点
                *s     要显示的汉字
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChinese32x32(u16 x,u16 y,u8 *s,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	u8 i,j,m=0;
	u16 k;
	u16 HZnum;//汉字数目
	u16 TypefaceNum;//单个字符占用的字节数
	u16 x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont32)/sizeof(typFNT_GB32);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont32[k].Index[0]==*(s))&&(tfont32[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //继续查找下一项
	}
}


/******************************************************************************
      函数说明：显示单个字符
      输入参数：x, y   显示起点
                num    要显示的字符
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowChar(u16 x,u16 y,u8 num,u16 fc,u16 bc,u8 sizey,u8 mode)
{
	LCD_RETURN_IF_NOT_READY();
	u8 temp,sizex,t,m=0;
	u16 i,TypefaceNum;//单个字符占用的字节数
	u16 x0=x;
	sizex=sizey/2;
	TypefaceNum=(sizex/8+((sizex%8)?1:0))*sizey;
	num=num-' ';    //转换为字库偏移
	LCD_Address_Set(x,y,x+sizex-1,y+sizey-1);  //设置光标位置
	for(i=0;i<TypefaceNum;i++)
	{ 
		if(sizey==12)temp=ascii_1206[num][i];		       //调用 6x12 字模
		else if(sizey==16)temp=ascii_1608[num][i];		 //调用 8x16 字模
		else if(sizey==24)temp=ascii_2412[num][i];		 //调用 12x24 字模
		else if(sizey==32)temp=ascii_3216[num][i];		 //调用 16x32 字模
		else return;
		for(t=0;t<8;t++)
		{
			if(!mode)//非叠加模式
			{
				if(temp&(0x01<<t))LCD_WR_DATA(fc);
				else LCD_WR_DATA(bc);
				m++;
				if(m%sizex==0)
				{
					m=0;
					break;
				}
			}
			else//叠加模式
			{
				if(temp&(0x01<<t))LCD_DrawPoint(x,y,fc);//画一个点
				x++;
				if((x-x0)==sizex)
				{
					x=x0;
					y++;
					break;
				}
			}
		}
	}   	 	  
}


/******************************************************************************
      函数说明：显示字符串
      输入参数：x, y   显示起点
                *p     要显示的字符串
                fc     字体颜色
                bc     背景颜色
                sizey  字号
                mode   0 为非叠加模式，1 为叠加模式
      返回值：  无
******************************************************************************/
void LCD_ShowString(u16 x,u16 y,const u8 *p,u16 fc,u16 bc,u8 sizey,u8 mode)
{         
	LCD_RETURN_IF_NOT_READY();
	while(*p!='\0')
	{       
		LCD_ShowChar(x,y,*p,fc,bc,sizey,mode);
		x+=sizey/2;
		p++;
	}  
}


/******************************************************************************
      函数说明：幂函数
      输入参数：m 为底数，n 为指数
      返回值：  m 的 n 次幂
******************************************************************************/
u32 mypow(u8 m,u8 n)
{
	u32 result=1;	 
	while(n--)result*=m;
	return result;
}


/******************************************************************************
      函数说明：显示整数数字
      输入参数：x, y   显示起点
                num    要显示的整数
                len    要显示的位数
                fc     字体颜色
                bc     背景颜色
                sizey  字号
      返回值：  无
******************************************************************************/
void LCD_ShowIntNum(u16 x,u16 y,u16 num,u8 len,u16 fc,u16 bc,u8 sizey)
{         	
	LCD_RETURN_IF_NOT_READY();
	u8 t,temp;
	u8 enshow=0;
	u8 sizex=sizey/2;
	for(t=0;t<len;t++)
	{
		temp=(num/mypow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				LCD_ShowChar(x+t*sizex,y,' ',fc,bc,sizey,0);
				continue;
			}else enshow=1; 
		 	 
		}
	 	LCD_ShowChar(x+t*sizex,y,temp+48,fc,bc,sizey,0);
	}
} 


/******************************************************************************
      函数说明：显示两位小数数字
      输入参数：x, y   显示起点
                num    要显示的小数
                len    要显示的总位数
                fc     字体颜色
                bc     背景颜色
                sizey  字号
      返回值：  无
******************************************************************************/
void LCD_ShowFloatNum1(u16 x,u16 y,float num,u8 len,u16 fc,u16 bc,u8 sizey)
{         	
	LCD_RETURN_IF_NOT_READY();
	u8 t,temp,sizex;
	u16 num1;
	sizex=sizey/2;
	num1=num*100;
	for(t=0;t<len;t++)
	{
		temp=(num1/mypow(10,len-t-1))%10;
		if(t==(len-2))
		{
			LCD_ShowChar(x+(len-2)*sizex,y,'.',fc,bc,sizey,0);
			t++;
			len+=1;
		}
	 	LCD_ShowChar(x+t*sizex,y,temp+48,fc,bc,sizey,0);
	}
}


/******************************************************************************
      函数说明：显示图片
      输入参数：x, y    显示起点
                length  图片长度
                width   图片宽度
                pic[]   图片数据
      返回值：  无
******************************************************************************/
void LCD_ShowPicture(u16 x,u16 y,u16 length,u16 width,const u8 pic[])
{
	LCD_RETURN_IF_NOT_READY();
	u16 i,j;
	u32 k=0;
	LCD_Address_Set(x,y,x+length-1,y+width-1);
	for(i=0;i<length;i++)
	{
		for(j=0;j<width;j++)
		{
			LCD_WR_DATA8(pic[k*2]);
			LCD_WR_DATA8(pic[k*2+1]);
			k++;
		}
	}			
}


