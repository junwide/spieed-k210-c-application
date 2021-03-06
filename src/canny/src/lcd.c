#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "sleep.h"
#include "lcd.h"
#include "st7789.h"
#include "font.h"

static lcd_ctl_t lcd_ctl;
static uint16_t* g_lcd_display_buff = NULL;
static uint16_t g_lcd_w = 0;
static uint16_t g_lcd_h = 0;
static bool g_lcd_init = false;


void lcd_polling_enable(void)
{
    lcd_ctl.mode = 0;
}

void lcd_interrupt_enable(void)
{
    lcd_ctl.mode = 1;
}

int lcd_init(lcd_para_t *lcd_para)
{
    uint8_t data = 0;
    lcd_ctl.dir = lcd_para->dir;
    lcd_ctl.width = lcd_para->width, lcd_ctl.height = lcd_para->height;
    lcd_ctl.start_offset_w0 = lcd_para->offset_w0;
    lcd_ctl.start_offset_h0 = lcd_para->offset_h0;
    lcd_ctl.start_offset_w1 = lcd_para->offset_w1;
    lcd_ctl.start_offset_h1 = lcd_para->offset_h1;

    if(g_lcd_w != lcd_para->width || g_lcd_h != lcd_para->height)
    {
        if(g_lcd_display_buff)
        {
            free(g_lcd_display_buff);
        }
        g_lcd_display_buff = (uint16_t*)malloc(lcd_para->width*lcd_para->height*2);
        if(!g_lcd_display_buff)
            return 12; //ENOMEM
        g_lcd_w = lcd_para->width;
        g_lcd_h = lcd_para->height;
    }

    tft_hard_init(lcd_para->freq, lcd_para->oct);
    /*soft reset*/
    tft_write_command(SOFTWARE_RESET);
    msleep(50);
    /*exit sleep*/
    tft_write_command(SLEEP_OFF);
    msleep(120);
    /*pixel format*/
    tft_write_command(PIXEL_FORMAT_SET);
    data = 0x55;
    tft_write_byte(&data, 1);
    msleep(10);

    g_lcd_init = true;
    lcd_set_direction(lcd_ctl.dir);
    if(lcd_para->invert)
    {
        tft_write_command(INVERSION_DISPALY_ON);
        msleep(10);
    }
    tft_write_command(NORMAL_DISPALY_ON);
    msleep(10);
    /*display on*/
    tft_write_command(DISPALY_ON);
    lcd_polling_enable();
    return 0;
}

void lcd_set_direction(lcd_dir_t dir)
{
    if(!g_lcd_init)
        return;
    //dir |= 0x08;
    lcd_ctl.dir = ((lcd_ctl.dir & DIR_RGB2BRG) == DIR_RGB2BRG) ? (dir | DIR_RGB2BRG) : dir;
    //lcd_ctl.dir = dir;
    if (lcd_ctl.dir & DIR_XY_MASK)
    {
        lcd_ctl.width = g_lcd_w - 1;
        lcd_ctl.height = g_lcd_h - 1;
        lcd_ctl.start_offset_w = lcd_ctl.start_offset_w1;
        lcd_ctl.start_offset_h = lcd_ctl.start_offset_h1;
    }
    else
    {
        lcd_ctl.width = g_lcd_h - 1;
        lcd_ctl.height = g_lcd_w - 1;
        lcd_ctl.start_offset_w = lcd_ctl.start_offset_w0;
        lcd_ctl.start_offset_h = lcd_ctl.start_offset_h0;
    }
    tft_write_command(MEMORY_ACCESS_CTL);
    tft_write_byte((uint8_t *)&lcd_ctl.dir, 1);
}

void lcd_deinit(void)
{
    if(g_lcd_display_buff)
    {
        free(g_lcd_display_buff);
        g_lcd_display_buff = NULL;
    }
    g_lcd_w = 0;
    g_lcd_h = 0;
}

uint32_t lcd_get_width_height(void)
{
    return g_lcd_w << 16 | g_lcd_h;
}

void lcd_clear(uint16_t color)
{
    #if LCD_SWAP_COLOR_BYTES
        color = SWAP_16(color);
    #endif
    uint32_t data = ((uint32_t)color << 16) | (uint32_t)color;
    lcd_set_area(0, 0, lcd_ctl.width, lcd_ctl.height);
    tft_fill_data(&data, g_lcd_h * g_lcd_w / 2);
}

static uint32_t lcd_freq = 20000000UL;
void lcd_set_freq(uint32_t freq)
{
    tft_set_clk_freq(freq);
    lcd_freq = freq;
}

uint32_t lcd_get_freq(void)
{
    return lcd_freq;
}

void lcd_set_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4] = {0};

    x1 += lcd_ctl.start_offset_w;
    x2 += lcd_ctl.start_offset_w;
    y1 += lcd_ctl.start_offset_h;
    y2 += lcd_ctl.start_offset_h;

    data[0] = (uint8_t)(x1 >> 8);
    data[1] = (uint8_t)(x1);
    data[2] = (uint8_t)(x2 >> 8);
    data[3] = (uint8_t)(x2);
    tft_write_command(HORIZONTAL_ADDRESS_SET);
    tft_write_byte(data, 4);

    data[0] = (uint8_t)(y1 >> 8);
    data[1] = (uint8_t)(y1);
    data[2] = (uint8_t)(y2 >> 8);
    data[3] = (uint8_t)(y2);
    tft_write_command(VERTICAL_ADDRESS_SET);
    tft_write_byte(data, 4);

    tft_write_command(MEMORY_WRITE);
}

void lcd_set_offset(uint16_t offset_w, uint16_t offset_h)
{
    lcd_ctl.start_offset_w = offset_w;
    lcd_ctl.start_offset_h = offset_h;
}

void lcd_bgr_to_rgb(bool enable)
{
    lcd_ctl.dir = enable ? (lcd_ctl.dir | DIR_RGB2BRG) : (lcd_ctl.dir & DIR_MASK);
    lcd_set_direction(lcd_ctl.dir);
}

void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_area(x, y, x, y);
    tft_write_half((uint8_t*)&color, 2);
}

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color)
{
    uint8_t i = 0;
    uint8_t j = 0;
    uint8_t data = 0;

    for (i = 0; i < 16; i++)
    {
        data = ascii0816[c * 16 + i];
        for (j = 0; j < 8; j++)
        {
            if (data & 0x80)
                lcd_draw_point(x + j, y, color);
            data <<= 1;
        }
        y++;
    }
}

void lcd_draw_string(uint16_t x, uint16_t y, char *str, uint16_t color)
{
    #if LCD_SWAP_COLOR_BYTES
        color = SWAP_16(color);
    #endif
    while (*str)
    {
        lcd_draw_char(x, y, *str, color);
        str++;
        x += 8;
    }
}

void lcd_draw_picture(uint16_t x1, uint16_t y1, uint16_t width, uint16_t height, uint32_t *ptr)
{
    lcd_set_area(x1, y1, x1 + width - 1, y1 + height - 1);
    tft_write_word(ptr, width * height / 2);
}

// static lcd_para_t lcd_default = {
// 	.width      = 320,
// 	.height     = 240,
// 	.dir        = 0,
// 	.extra_para = NULL,
// };