#include <stdio.h>
#include <unistd.h>
#include "sysctl.h"
//GPIO and BSP 
#include "fpioa.h"
#include "bsp.h"
#include "plic.h"
#include "w25qxx.h"
/*camera*/
#include "ov2640.h"
#include "dvp.h"
#include "iomem.h"
/*LCD 320*240*/
#include "lcd.h"
#include "st7789.h"
//ai and algorithm
#include "image_process.h"
#include "region_layer.h"
#include "w25qxx.h"

#define PLL0_OUTPUT_FREQ 800000000UL  //800Mhz
#define PLL1_OUTPUT_FREQ 400000000UL  //400Mhz

#define 	CLASS_NUMBER 			2
#define 	ANCHOR_NUM 				5  	//5 or 9

#if ANCHOR_NUM == 5
#define KMODEL_SIZE (543 * 1024)
static float anchor[ANCHOR_NUM * 2] = {0.156250, 0.222548,0.361328, 0.489583,0.781250, 0.983133,1.621094, 1.964286,3.574219, 3.940000};
#elif ANCHOR_NUM == 9
#define KMODEL_SIZE (571 * 1024)
static float anchor[ANCHOR_NUM * 2] = {0.117188, 0.166667,0.224609, 0.312500,0.390625, 0.531250, 0.664062, 0.838196,0.878906, 1.485714,1.269531, 1.125714, 1.728516, 2.096633,2.787402, 3.200000,4.382959, 4.555469};
#endif

uint8_t model_data[KMODEL_SIZE];

volatile uint8_t g_dvp_finish_flag; //获取图像的标志
volatile uint32_t g_ai_done_flag;   //图像处理完标志
static image_t kpu_image, display_image; 

kpu_model_context_t obj_detect_task;
static region_layer_t obj_detect_rl;
static obj_info_t obj_detect_info;


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
/*Ai 处理的回调*/
static void ai_done(void *ctx)
{
    g_ai_done_flag = 1;
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
//label 标签
typedef struct
{
	char *str;
	uint16_t color;
	uint16_t height;
	uint16_t width;
	uint32_t *ptr;
} class_lable_t;

class_lable_t class_lable[CLASS_NUMBER] =
{
	{ "no_mask", NAVY },
	{ "mask", DARKGREEN }
};

static uint32_t lable_string_draw_ram[115 * 16 * 8 / 2];

//标签初始化
static void lable_init(void)
{
	uint8_t index;

	class_lable[0].height = 16;
	class_lable[0].width = 8 * strlen(class_lable[0].str);
	class_lable[0].ptr = lable_string_draw_ram;
	lcd_ram_draw_string(class_lable[0].str, class_lable[0].ptr, BLACK, class_lable[0].color);
	for (index = 1; index < CLASS_NUMBER; index++) {
		class_lable[index].height = 16;
		class_lable[index].width = 8 * strlen(class_lable[index].str);
		class_lable[index].ptr = class_lable[index - 1].ptr + class_lable[index - 1].height * class_lable[index - 1].width / 2;
		lcd_ram_draw_string(class_lable[index].str, class_lable[index].ptr, BLACK, class_lable[index].color);
	}
}

static void drawboxes(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t class, float prob)
{
	if (x1 >= 320)
		x1 = 319;
	if (x2 >= 320)
		x2 = 319;
	if (y1 >= 240)
		y1 = 239;
	if (y2 >= 240)
		y2 = 239;

	lcd_draw_rectangle(x1, y1, x2, y2, 2, class_lable[class].color);
	lcd_draw_picture(x1 + 1, y1 + 1, class_lable[class].width, class_lable[class].height, class_lable[class].ptr);
}


int main(void)
{

    sysctl_cpu_set_freq(PLL1_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL0, PLL0_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL1, PLL1_OUTPUT_FREQ);
    sysctl_clock_enable(SYSCTL_CLOCK_AI);
    plic_init();
    
    io_mux_init();
    lable_init();

    w25qxx_init(3, 0);
    w25qxx_enable_quad_mode();
    w25qxx_read_data(0xA00000, model_data, KMODEL_SIZE, W25QXX_QUAD_FAST);  //加载SRAM中的model

    lcd_init(&lcd_default);
    lcd_set_direction(DIR_YX_LRUD);
    lcd_clear(WHITE);
	lcd_draw_string(136, 70, "DEMO", BLACK);
	lcd_draw_string(104, 150, "face mask detection", BLACK);
  
    /* DVP init */
    camera_init();
    kpu_image.pixel = 3;
    kpu_image.width = 320;
    kpu_image.height = 256;
    image_init(&kpu_image);
    memset(kpu_image.addr, 127, kpu_image.pixel * kpu_image.width * kpu_image.height);
    dvp_set_ai_addr((uint32_t)(kpu_image.addr + 8 * 320), (uint32_t)(kpu_image.addr + 320 * 256 + 8 * 320), (uint32_t)(kpu_image.addr + 320 * 256 * 2 + 8 * 320));
    dvp_set_output_enable(0, 1);
	
    display_image.pixel = 2;
    display_image.width = 320;
    display_image.height = 240;
    image_init(&display_image);   
    dvp_set_display_addr((uint32_t)display_image.addr);
    dvp_set_output_enable(1, 1);

    /* init obj detect model */
    if (kpu_load_kmodel(&obj_detect_task, model_data) != 0)
    {
        printf("\nmodel init error\n");
        while (1);
    }
    obj_detect_rl.anchor_number = ANCHOR_NUM;
    obj_detect_rl.anchor = anchor;
    obj_detect_rl.threshold = 0.7;
    obj_detect_rl.nms_value = 0.4;
    obj_detect_rl.classes = CLASS_NUMBER;
    region_layer_init(&obj_detect_rl, 10, 8, (4 + 2 + 1) * ANCHOR_NUM, kpu_image.width, kpu_image.height);

    /* enable global interrupt */
    sysctl_enable_irq();
	
    /* system start */
    printf("System start\n");

    while (1)
    {
        g_dvp_finish_flag = 0;
        dvp_clear_interrupt(DVP_STS_FRAME_START | DVP_STS_FRAME_FINISH);
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 1);
        while (g_dvp_finish_flag == 0)
            ;
        /* run obj detect */
        g_ai_done_flag = 0;
        kpu_run_kmodel(&obj_detect_task, kpu_image.addr, DMAC_CHANNEL5, ai_done, NULL);
        while(!g_ai_done_flag);
        float *output;
        size_t output_size;
        kpu_get_output(&obj_detect_task, 0, (uint8_t **)&output, &output_size);
        obj_detect_rl.input = output;
        region_layer_run(&obj_detect_rl, &obj_detect_info);
		/* display pic*/
		lcd_draw_picture(0, 0, 320, 240, display_image.addr);
		/* draw boxs */
		region_layer_draw_boxes(&obj_detect_rl, drawboxes); 
		
    }
}

