#include <stdio.h>
#include <unistd.h>
#include "sysctl.h"
//GPIO and BSP 
#include "fpioa.h"
#include "bsp.h"
#include "plic.h"
/*camera*/
#include "ov2640.h"
#include "dvp.h"
#include "iomem.h"
/*LCD 320*240*/
#include "lcd.h"
#include "st7789.h"
//ai and algorithm
#include "ai.h"
#include "image_process.h"
//#include "pch.h"  //这里包含常见的边缘算法

#define PLL0_OUTPUT_FREQ 800000000UL  //800Mhz
#define PLL1_OUTPUT_FREQ 400000000UL  //400Mhz



volatile uint8_t g_dvp_finish_flag; //获取图像的标志
static uint32_t lcd_gram[(320 * 240)/2] __attribute__((aligned(32)));

/*dvp的中断回调*/
static int on_irq_dvp(void* ctx)
{
    if (dvp_get_interrupt(DVP_STS_FRAME_FINISH))
    {
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
        dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
        g_dvp_finish_flag = 1;
    }   
    else
    {
        dvp_start_convert();
        dvp_clear_interrupt(DVP_STS_FRAME_START);
    }
    return 0;
}


static void io_mux_init(void)
{

   //DVP
    fpioa_set_function(42, FUNC_CMOS_RST);
    fpioa_set_function(44, FUNC_CMOS_PWDN);
    fpioa_set_function(46, FUNC_CMOS_XCLK);
    fpioa_set_function(43, FUNC_CMOS_VSYNC);
    fpioa_set_function(45, FUNC_CMOS_HREF);
    fpioa_set_function(47, FUNC_CMOS_PCLK);
    fpioa_set_function(41, FUNC_SCCB_SCLK);
    fpioa_set_function(40, FUNC_SCCB_SDA);
    //LCD
    fpioa_set_function(38, FUNC_GPIOHS0 + DCX_GPIONUM);
    fpioa_set_function(36, FUNC_SPI0_SS3);
    fpioa_set_function(39, FUNC_SPI0_SCLK);
    fpioa_set_function(37, FUNC_GPIOHS0 + RST_GPIONUM);
    sysctl_set_spi0_dvp_data(1);
    
    //Power
    sysctl_set_power_mode(SYSCTL_POWER_BANK0, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK1, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK2, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK3, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK4, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK5, SYSCTL_POWER_V33);
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
}

static lcd_para_t lcd_default = {
	.width      = 320,
	.height     = 240,
	.dir        = 0,
	.extra_para = NULL,
    .freq       = 20000000UL,
    .oct        = true,
    .invert     = false,
};

static void camera_init(void)
{
    dvp_init(8);
    dvp_set_xclk_rate(24000000);
    dvp_enable_burst();
    dvp_set_image_format(DVP_CFG_RGB_FORMAT);
    dvp_set_image_size(320, 240);
    dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
    dvp_disable_auto();

    /* DVP interrupt config */
    printf("DVP interrupt config\n");
    plic_set_priority(IRQN_DVP_INTERRUPT, 1);
    plic_irq_register(IRQN_DVP_INTERRUPT, on_irq_dvp, NULL);
    plic_irq_enable(IRQN_DVP_INTERRUPT);
    
    ov2640_init();
}


int main(void)
{

    sysctl_cpu_set_freq(PLL1_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL0, PLL0_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL1, PLL1_OUTPUT_FREQ);
    sysctl_clock_enable(SYSCTL_CLOCK_AI);
    plic_init();
    
    io_mux_init();
    lcd_init(&lcd_default);
    lcd_set_direction(DIR_YX_LRUD);
    lcd_clear(WHITE);
	lcd_draw_string(136, 70, "DEMO", BLACK);
	lcd_draw_string(104, 150, "canny detection", BLACK);

    /* DVP init */
    camera_init();

    /*AI init */
    ai_init(NULL);
    /* enable global interrupt */
    sysctl_enable_irq();

    /* system start */
    printf("system start\n");
    while (1)
    {
        g_dvp_finish_flag = 0;
        dvp_clear_interrupt(DVP_STS_FRAME_START | DVP_STS_FRAME_FINISH);
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 1);
        while (g_dvp_finish_flag == 0)
            ;
		ai_run(lcd_gram);
		lcd_draw_picture(0, 0, 320, 240, lcd_gram);
    }
}

