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

/* Flash */
#include "w25qxx.h"
/*AI*/
#include "image_process.h"
#include "region_layer.h"

#define PLL0_OUTPUT_FREQ 800000000UL  //800Mhz
#define PLL1_OUTPUT_FREQ 400000000UL  //400Mhz

#define 	CLASS_NUMBER 			1
#define 	ANCHOR_NUM 				5

/* Data Struct */
#define KMODEL_SIZE (1860 * 1024)   
uint8_t* model_data;

volatile uint8_t g_dvp_finish_flag; //获取图像的标志
volatile uint32_t g_ai_done_flag; //图像处理完标志

static image_t kpu_image, display_image;  //处理图像与显示图像

static obj_info_t fire_detect_info;     //对象信息
kpu_model_context_t fire_detect_task;   //检测人物
static region_layer_t fire_detect_rl;   //检测实体

//预设achor 与训练时一致即可
static float anchor[ANCHOR_NUM * 2] = {0.57273, 0.677385, 1.87446, 2.06253, 3.33843, 5.47434, 7.88282, 3.52778,9.77052,9.16828};

/*Ai 处理的回调*/
static void ai_done(void * ctx)
{
    g_ai_done_flag = 1;
}
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
    dvp_set_image_size(224, 224);    
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
	{ "fire", GREEN }
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

//框出对象
static void drawboxes(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t class, float prob)
{
	if (x1 >= 272) x1 = 271;
    if (x1 <= 47) x1 = 48;
	if (x2 >= 272) x2 = 271;
    if (x2 <= 47) x2 = 48;

	if (y1 >= 232) y1 = 231;
    if (y1 <= 7) y1 = 8;
	if (y2 >= 232) y2 = 231;
    if (y2 <= 7) y2 = 8;

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

    printf("flash init\n");
    w25qxx_init(3, 0);
    w25qxx_enable_quad_mode();
    model_data = (uint8_t*)malloc(KMODEL_SIZE + 255);
    if(!model_data)
    {
        printf("malloc errror");
        return -1;
    }
    uint8_t *model_data_align = (uint8_t*)(((uintptr_t)model_data+255)&(~255));
    w25qxx_read_data(0xA00000, model_data_align, KMODEL_SIZE, W25QXX_QUAD_FAST);  //加载SRAM中的model

    lcd_init(&lcd_default);
    lcd_set_direction(DIR_YX_LRUD);
    lcd_clear(WHITE);
	lcd_draw_string(136, 70, "DEMO", BLACK);
	lcd_draw_string(104, 150, "Fire detection", BLACK);
    /* DVP init */
    camera_init();
    //KPU 处理信息，与训练的input对齐
    kpu_image.pixel = 3;
    kpu_image.width = 224;    
    kpu_image.height = 224;   
    image_init(&kpu_image);
    memset(kpu_image.addr, 127, kpu_image.pixel * kpu_image.width * kpu_image.height);
    dvp_set_ai_addr((uint32_t)kpu_image.addr, (uint32_t)(kpu_image.addr + 224 * 224 ), (uint32_t)(kpu_image.addr + 224 * 224 * 2));
    dvp_set_output_enable(0, 1);
    //显示图像信息
    display_image.pixel = 2;
    display_image.width = 224;    
    display_image.height = 224;   
    image_init(&display_image);
    dvp_set_display_addr((uint32_t)display_image.addr);
    dvp_set_output_enable(1, 1);

    /* init face detect model */
    if (kpu_load_kmodel(&fire_detect_task, model_data_align) != 0)
    {
        printf("\nmodel init error\n");
        while (1);
    }
    //检测实体的信息
    fire_detect_rl.anchor_number = ANCHOR_NUM;
    fire_detect_rl.anchor = anchor;
    fire_detect_rl.threshold = 0.7;
    fire_detect_rl.nms_value = 0.3;
    fire_detect_rl.classes = CLASS_NUMBER;
    region_layer_init(&fire_detect_rl, 7, 7, 30, kpu_image.width, kpu_image.height);
    /* enable global interrupt */
    sysctl_enable_irq();
    /* system start */
    printf("system start\n");
    
    while (1)
    {
        /* ai cal finish*/
        g_dvp_finish_flag = 0;
        dvp_clear_interrupt(DVP_STS_FRAME_START | DVP_STS_FRAME_FINISH);
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 1);
        while (g_dvp_finish_flag == 0)
            ;
        g_ai_done_flag = 0;
        printf("Tag one");
        kpu_run_kmodel(&fire_detect_task, kpu_image.addr, DMAC_CHANNEL5, ai_done, NULL);
        while (!g_ai_done_flag);
        float *output;
        size_t output_size;
        kpu_get_output(&fire_detect_task, 0, (uint8_t **)&output, &output_size);
        fire_detect_rl.input = output;
        region_layer_run(&fire_detect_rl, &fire_detect_info);
		/* display pic*/
		lcd_draw_picture(48, 8, 224, 224, (uint32_t *)display_image.addr);   // 48 8
		/* draw boxs */
		region_layer_draw_boxes(&fire_detect_rl, drawboxes);         
    }
    return 0;
}

