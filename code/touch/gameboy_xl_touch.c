// Joe Ostrander
// Gameboy XL Touch
// 2023.05.06

// Note about scanvideo:
// When scaling, there is a bug in scanvideo.c that prevents line zero from repeating
// see my fix --> https://github.com/joeostrander/pico-extras/commit/3ed9467f0203acd9bedfdbb08bed8f31b61b320c

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for memcmp
#include "time.h"
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/sync.h"
#include "hardware/vreg.h"
#include "pico/stdio.h"
#include "osd.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "colors.h"
#include "touch.h"


#define SDA_PIN                     12
#define SCL_PIN                     13
i2c_inst_t* i2cHandle = i2c0;

#define TOUCH_INTERFACE

#define MIN_RUN 3

#define ONBOARD_LED_PIN             25
#define LCD_DISP_PIN                10
#define BACKLIGHT_PWM_PIN           11  // PWM_B[5]
#define BACKLIGHT_PWM_MAX           0xFFFF

#define DMG_READING_DPAD_PIN        20      // P14
#define DMG_READING_BUTTONS_PIN     19      // P15
#define DMG_OUTPUT_LEFT_B_PIN       26      // P11
#define DMG_OUTPUT_DOWN_START_PIN   22      // P13
#define DMG_OUTPUT_UP_SELECT_PIN    21      // P12
#define DMG_OUTPUT_RIGHT_A_PIN      27      // P10

// GAMEBOY VIDEO INPUT (From level shifter)
#define VSYNC_PIN                   18
#define PIXEL_CLOCK_PIN             17
#define HSYNC_PIN                   16
#define DATA_0_PIN                  15
#define DATA_1_PIN                  14

// at 3x Game area will be 480x432 
#define DMG_PIXELS_X                160
#define DMG_PIXELS_Y                144
#define DMG_PIXEL_COUNT             (DMG_PIXELS_X*DMG_PIXELS_Y)

typedef enum
{
    BUTTON_A = 0,
    BUTTON_B,
    BUTTON_SELECT,
    BUTTON_START,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_HOME,
    BUTTON_COUNT
} controller_button_t;

typedef enum
{
    OSD_LINE_COLOR_SCHEME = 0,
    OSD_LINE_BACK_COLOR,
    OSD_LINE_BACKLIGHT,
    OSD_LINE_EXIT,
    OSD_LINE_COUNT
} osd_line_t;

const scanvideo_timing_t vga_timing_800x480 =
{
        .clock_freq = 24000000,

        .h_active = 800,
        .v_active = 480,

        .h_front_porch = 40,
        .h_pulse = 12,
        .h_total = 960,
        .h_sync_polarity = 1,

        .v_front_porch = 2,
        .v_pulse = 2,
        .v_total = 500, 
        .v_sync_polarity = 1,

        .enable_clock = 1,
        .clock_polarity = 0,

        .enable_den = 0
};

const scanvideo_mode_t vga_mode_tft_800x480_3x_scale =
{
        .default_timing = &vga_timing_800x480,
        .pio_program = &video_24mhz_composable,
        .width = 800,
        .height = 480,
        .xscale = 3,
        .yscale = 3,
};

typedef struct rectangle_t
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} rectangle_t;

typedef enum
{
    BUTTON_STATE_PRESSED = 0,
    BUTTON_STATE_UNPRESSED
} button_state_t;

#define VGA_MODE        vga_mode_tft_800x480_3x_scale
#define LINE_LENGTH     ((uint16_t)(((VGA_MODE.width * 100.0)/VGA_MODE.xscale) + 50) / 100)

static rectangle_t rect_gamewindow;
static rectangle_t rect_osd;
static uint16_t background_color;
static int backlight_level = 10;    // 1 to 10

static semaphore_t video_initted;
static uint8_t button_states[BUTTON_COUNT];
static uint8_t button_states_previous[BUTTON_COUNT];
static color_scheme_t* color_scheme;
static control_scheme_t* control_scheme;

static uint8_t framebuffer[DMG_PIXEL_COUNT];
static uint8_t* osd_framebuffer = NULL;

// The controls buffer is a 160x120 area at 10x scale
static uint8_t framebuffer_controls_8bit[] = 
{
    0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,2,2,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,2,2,0,
    0,1,1,0,0,1,1,0,0,0,2,2,0,0,0,0,
    0,1,1,0,0,1,1,0,0,0,2,2,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Touch areas for buttons
static const rectangle_t scaled_home   = {.x = 146, .y = 70,  .width = 10, .height = 20};
static const rectangle_t scaled_start  = {.x = 146, .y = 10,  .width = 10, .height = 20}; 
static const rectangle_t scaled_select = {.x = 146, .y = 130, .width = 10, .height = 20};
static const rectangle_t scaled_a      = {.x = 186,  .y = 10, .width = 20, .height = 20};       // maybe x 183... 
static const rectangle_t scaled_b      = {.x = 206,  .y = 40, .width = 20, .height = 20};       // maybe x 203...
static const rectangle_t scaled_up     = {.x = 186,  .y = 110,  .width = 20, .height = 20};     // maybe x 183...
static const rectangle_t scaled_down   = {.x = 226,  .y = 110,  .width = 20, .height = 20};     // maybe x 223...
static const rectangle_t scaled_left   = {.x = 206,  .y = 130,  .width = 20, .height = 20};     // maybe x 203...
static const rectangle_t scaled_right  = {.x = 206,  .y = 90,  .width = 20, .height = 20};      // maybe x 203...

static void core1_func(void);
static void render_scanline(scanvideo_scanline_buffer_t *buffer);
static void initialize_gpio(void);
static void gpio_callback(uint gpio, uint32_t events);
static void gpio_callback_VIDEO(uint gpio, uint32_t events);
static bool button_is_pressed(controller_button_t button);
static bool button_was_released(controller_button_t button);
static void set_button(controller_button_t button, button_state_t state);
static void __no_inline_not_in_flash_func(command_check)(void);
static void update_osd(void);
static void blink(uint8_t count, uint16_t millis_on, uint16_t millis_off);
static void change_backlight_level(int direction);
static void set_orientation(void);
static bool rect_contains_point(const rectangle_t* rect, uint16_t x, uint16_t y);
static void touchup(uint16_t x, uint16_t y);
static void touchdown(uint16_t x, uint16_t y);

int32_t single_solid_line(uint32_t *buf, size_t buf_length, uint16_t color);
int32_t single_scanline(uint32_t *buf, size_t buf_length, uint8_t line_index);

int main(void) 
{
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    sleep_ms(10);

    //set_sys_clock_khz(300000, true);
    set_sys_clock_khz(240000, true);

    // Create a semaphore to be posted when video init is complete.
    sem_init(&video_initted, 0, 1);

    // Launch all the video on core 1.
    multicore_launch_core1(core1_func);

    // Wait for initialization of video to be complete.
    sem_acquire_blocking(&video_initted);

    initialize_gpio();

    // prevent false trigger of OSD on start -- set all previous button states to 1 (unpressed)
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        button_states[i] = BUTTON_STATE_UNPRESSED;
        button_states_previous[i] = BUTTON_STATE_UNPRESSED;
    }

    set_background_color(COLOR_LIGHT_GREY);
    background_color = rgb888_to_rgb222(get_background_color());

    color_scheme = get_scheme();
    control_scheme = get_control_scheme();
    
    osd_framebuffer = OSD_get_framebuffer();
    update_osd();

    set_orientation();

    TOUCH_init(i2cHandle);
    TOUCH_set_touchup_callback(&touchup);
    TOUCH_set_touchdown_callback(&touchdown);

    gpio_set_irq_enabled_with_callback(VSYNC_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback_VIDEO);

    while (true) 
    {
        #ifdef TOUCH_INTERFACE
        if (TOUCH_tasks())
        {
            command_check();
        }
        #endif
        
        //blink(3, 100, 2000);
    }
}

int32_t single_scanline(uint32_t *buf, size_t buf_length, uint8_t line_index)
{
    uint16_t* p16 = (uint16_t *) buf;
    uint16_t* first_pixel;
    uint16_t pixel_count = 0;

    // GAME WINDOW
    *p16++ = COMPOSABLE_RAW_RUN;
    first_pixel = p16;
    *p16++ = 0; // replaced later - first pixel
    *p16++ = rect_gamewindow.width - MIN_RUN;
    
    uint8_t *pbuff = &framebuffer[line_index * rect_gamewindow.width];

    uint16_t pos = 0;
    uint16_t x;
    uint16_t i = 0;
    uint16_t color = 0;

    uint16_t idx;
    uint16_t rot_x;
    uint16_t rot_y;
    for (x = 0; x < rect_gamewindow.width; x++)
    {
        if (x == 0 && i == 0)
        {
            *first_pixel = rgb888_to_rgb222( *((uint32_t*)color_scheme + *pbuff) ) ;
        }
        else
        {
            if ( OSD_is_enabled() 
                && (line_index >= rect_osd.y)
                && (line_index <= (rect_osd.y+rect_osd.height))
                && (x >= rect_osd.x)
                && (x <= (rect_osd.x+rect_osd.width)))
            {
                // ROTATE 270
                rot_x = rect_osd.height - 1 - (line_index-rect_osd.y);
                rot_y = x - rect_osd.x;
               
                idx = rot_x + (rot_y * OSD_WIDTH);
                if (osd_framebuffer != NULL && idx < (OSD_WIDTH*OSD_HEIGHT))
                    color = (uint16_t)(osd_framebuffer[idx]);
            }
            else
            {
                color = rgb888_to_rgb222( *((uint32_t*)color_scheme + *pbuff) ) ;
            }

            *p16++ = color; 
        }

        pbuff++;
        pixel_count++;
    }
    
    uint16_t remaining = (VGA_MODE.width/VGA_MODE.xscale) - rect_gamewindow.width;
    if (remaining > MIN_RUN)
    {
        *p16++ = COMPOSABLE_RAW_RUN;
        *p16++ = rgb888_to_rgb222(control_scheme->background_color);  // TODO: use control scheme backcolor instead
        pixel_count++;
        *p16++ = remaining - MIN_RUN;
        
        uint16_t clr = 0;
        uint16_t left_pixels = remaining;
        uint16_t y = line_index;

        // alternative using uint16 binary-literal array
        uint8_t rot_xxx;
        uint8_t rot_yyy;
        uint8_t xxx10;
        uint8_t yyy10 = line_index / 10;
        uint8_t posss;
        for (uint16_t x = 1; x < left_pixels; x++)   // start at 1... already did a pixel
        {
            
            xxx10 = x / 10;
             rot_xxx = 16 - 1 - yyy10;      // 270
             rot_yyy = xxx10;               // 270
            if (rot_yyy < 12 )
            {
                posss = rot_xxx + (rot_yyy * 16);
                clr = rgb888_to_rgb222( *((uint32_t*)control_scheme + framebuffer_controls_8bit[posss]) );
            }
            else
            {
                clr = rgb888_to_rgb222(control_scheme->background_color);
            }
            
            *p16++ = clr;
            pixel_count++;
        }
    }
   
    if (pixel_count*VGA_MODE.xscale < VGA_MODE.width)
    {
        uint16_t remaining = (VGA_MODE.width - (pixel_count*VGA_MODE.xscale))/VGA_MODE.xscale;
        
        //TESTING!!!
        while (remaining % 3 != 0)
        {
            remaining++;
        }

        // RIGHT BORDER
        if (remaining > MIN_RUN)
        {
            *p16++ = COMPOSABLE_COLOR_RUN;
            *p16++ = rgb888_to_rgb222(control_scheme->background_color); 
            *p16++ = remaining - MIN_RUN;
        }
    }

    // black pixel to end line
    *p16++ = COMPOSABLE_RAW_1P;
    *p16++ = 0;

    *p16++ = COMPOSABLE_EOL_ALIGN;  // TODO... how to determine when to do skip align

    return ((uint32_t *) p16) - buf;
}

int32_t single_solid_line(uint32_t *buf, size_t buf_length, uint16_t color)
{
    uint16_t *p16 = (uint16_t *) buf;

    *p16++ = COMPOSABLE_COLOR_RUN;
    *p16++ = color; 
    *p16++ = LINE_LENGTH - MIN_RUN;

    // black pixel to end line
    *p16++ = COMPOSABLE_RAW_1P;
    *p16++ = 0;

    *p16++ = COMPOSABLE_EOL_ALIGN;
    
    return ((uint32_t *) p16) - buf;
}

static void render_scanline(scanvideo_scanline_buffer_t *dest) 
{
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;
    int line_num = scanvideo_scanline_number(dest->scanline_id);
    int line_start = rect_gamewindow.y;
    int line_end = rect_gamewindow.y + rect_gamewindow.height;
    
    if (line_num < line_start || line_num > line_end)
    {
        dest->data_used = single_solid_line(buf, buf_length, background_color);
    }
    else
    {
        dest->data_used = single_scanline(buf, buf_length, (uint8_t)(line_num - rect_gamewindow.y));
    }

    dest->status = SCANLINE_OK;
}

static void core1_func(void) 
{
    
    hard_assert(VGA_MODE.width + 4 <= PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS * 2);    

    // Initialize video and interrupts on core 1.
    scanvideo_setup(&VGA_MODE);
    scanvideo_timing_enable(true);
    sem_release(&video_initted);

    gpio_set_irq_enabled_with_callback(DMG_READING_DPAD_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(DMG_READING_BUTTONS_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    while (true) 
    {
        scanvideo_scanline_buffer_t *scanline_buffer = scanvideo_begin_scanline_generation(true);
        render_scanline(scanline_buffer);
        scanvideo_end_scanline_generation(scanline_buffer);
    }
}

static void initialize_gpio(void)
{    
    //Onboard LED
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    gpio_put(ONBOARD_LED_PIN, 0);

    // LCD DISP PIN
    gpio_init(LCD_DISP_PIN);
    gpio_set_dir(LCD_DISP_PIN, GPIO_OUT);
    gpio_put(LCD_DISP_PIN, 1);

    // LCD Backlight
    gpio_set_function(BACKLIGHT_PWM_PIN, GPIO_FUNC_PWM);
    uint sliceNum=pwm_gpio_to_slice_num (BACKLIGHT_PWM_PIN); 
    pwm_config config = pwm_get_default_config();
    pwm_init(sliceNum, &config, true);
    change_backlight_level(0);  // 0 - no change

    // Gameboy video signal inputs
    gpio_init(VSYNC_PIN);
    gpio_init(PIXEL_CLOCK_PIN);
    gpio_init(DATA_0_PIN);
    gpio_init(DATA_1_PIN);
    gpio_init(HSYNC_PIN);

    // Initialize I2C port at 400 kHz
    i2c_init(i2cHandle, 400 * 1000);

    // Initialize I2C pins
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SCL_PIN);
    gpio_pull_up(SDA_PIN);

    gpio_init(DMG_OUTPUT_RIGHT_A_PIN);
    gpio_set_dir(DMG_OUTPUT_RIGHT_A_PIN, GPIO_OUT);
    gpio_put(DMG_OUTPUT_RIGHT_A_PIN, 1);

    gpio_init(DMG_OUTPUT_LEFT_B_PIN);
    gpio_set_dir(DMG_OUTPUT_LEFT_B_PIN, GPIO_OUT);
    gpio_put(DMG_OUTPUT_LEFT_B_PIN, 1);

    gpio_init(DMG_OUTPUT_UP_SELECT_PIN);
    gpio_set_dir(DMG_OUTPUT_UP_SELECT_PIN, GPIO_OUT);
    gpio_put(DMG_OUTPUT_UP_SELECT_PIN, 1);

    gpio_init(DMG_OUTPUT_DOWN_START_PIN);
    gpio_set_dir(DMG_OUTPUT_DOWN_START_PIN, GPIO_OUT);
    gpio_put(DMG_OUTPUT_DOWN_START_PIN, 1);

    gpio_init(DMG_READING_DPAD_PIN);
    gpio_set_dir(DMG_READING_DPAD_PIN, GPIO_IN);

    gpio_init(DMG_READING_BUTTONS_PIN);
    gpio_set_dir(DMG_READING_BUTTONS_PIN, GPIO_IN);

}

static void gpio_callback(uint gpio, uint32_t events) 
{
    // Prevent controller input to game if OSD is visible
    if (OSD_is_enabled())
        return;

    if(gpio==DMG_READING_DPAD_PIN)
    {
        if (events & GPIO_IRQ_EDGE_FALL)   // Send DPAD states on low
        {
            gpio_put(DMG_OUTPUT_RIGHT_A_PIN, button_states[BUTTON_RIGHT]);
            gpio_put(DMG_OUTPUT_LEFT_B_PIN, button_states[BUTTON_LEFT]);
            gpio_put(DMG_OUTPUT_UP_SELECT_PIN, button_states[BUTTON_UP]);
            gpio_put(DMG_OUTPUT_DOWN_START_PIN, button_states[BUTTON_DOWN]);
        }

        //TODO: it might be best to read BUTTONS when this goes high
    }

    if(gpio==DMG_READING_BUTTONS_PIN)
    {
        if (events & GPIO_IRQ_EDGE_FALL)   // Send BUTTON states on low
        {
            gpio_put(DMG_OUTPUT_RIGHT_A_PIN, button_states[BUTTON_A]);
            gpio_put(DMG_OUTPUT_LEFT_B_PIN, button_states[BUTTON_B]);
            gpio_put(DMG_OUTPUT_UP_SELECT_PIN, button_states[BUTTON_SELECT]);
            gpio_put(DMG_OUTPUT_DOWN_START_PIN, button_states[BUTTON_START]);

            // Prevent in-game reset lockup
            // If A,B,Select and Start are all pressed, release them!
            if ((button_states[BUTTON_A] | button_states[BUTTON_B] | button_states[BUTTON_SELECT]| button_states[BUTTON_START])==0)
            {
                button_states[BUTTON_A] = 1;
                button_states[BUTTON_B] = 1;
                button_states[BUTTON_SELECT] = 1;
                button_states[BUTTON_START] = 1;
            }
        }

        // When P15 pin goes high, read cycle is complete, send all high
        if(events & GPIO_IRQ_EDGE_RISE)
        {
            gpio_put(DMG_OUTPUT_RIGHT_A_PIN, 1);
            gpio_put(DMG_OUTPUT_LEFT_B_PIN, 1);
            gpio_put(DMG_OUTPUT_UP_SELECT_PIN, 1);
            gpio_put(DMG_OUTPUT_DOWN_START_PIN, 1);
        }
    }
}

static bool button_is_pressed(controller_button_t button)
{
    return button_states[button] == BUTTON_STATE_PRESSED;
}

static bool button_was_released(controller_button_t button)
{
    return button_states[button] == BUTTON_STATE_UNPRESSED && button_states_previous[button] == BUTTON_STATE_PRESSED;
}

static void set_button(controller_button_t button, button_state_t state)
{
    button_states[button] = state;
    if (button == BUTTON_A)
    {
        framebuffer_controls_8bit[16*4 + 13] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*4 + 14] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*5 + 13] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*5 + 14] = state == BUTTON_STATE_PRESSED ? 3 : 2;
    }
    else if (button == BUTTON_B)
    {
        framebuffer_controls_8bit[16*6 + 10] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*6 + 11] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*7 + 10] = state == BUTTON_STATE_PRESSED ? 3 : 2;
        framebuffer_controls_8bit[16*7 + 11] = state == BUTTON_STATE_PRESSED ? 3 : 2;
    }
    else if (button == BUTTON_RIGHT)
    {
        framebuffer_controls_8bit[16*6 + 5] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*6 + 6] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*7 + 5] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*7 + 6] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_LEFT)
    {
        framebuffer_controls_8bit[16*6 + 1] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*6 + 2] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*7 + 1] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*7 + 2] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_UP)
    {
        framebuffer_controls_8bit[16*4 + 3] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*4 + 4] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*5 + 3] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*5 + 4] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_DOWN)
    {
        framebuffer_controls_8bit[16*8 + 3] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*8 + 4] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*9 + 3] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[16*9 + 4] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_SELECT)
    {
        framebuffer_controls_8bit[1] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[2] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_HOME)
    {
        framebuffer_controls_8bit[7] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[8] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
    else if (button == BUTTON_START)
    {
        framebuffer_controls_8bit[13] = state == BUTTON_STATE_PRESSED ? 3 : 1;
        framebuffer_controls_8bit[14] = state == BUTTON_STATE_PRESSED ? 3 : 1;
    }
}

static void __no_inline_not_in_flash_func(command_check)(void)
{
    if (memcmp(button_states, button_states_previous, sizeof(button_states)) == 0)
        return;

    // Home pressed
    if (button_was_released(BUTTON_HOME))
    {
        OSD_toggle();
    }
    else
    {
        if (OSD_is_enabled())
        {
            if (button_was_released(BUTTON_DOWN))
            {
                OSD_change_line(1);
            }
            else if (button_was_released(BUTTON_UP))
            {
                OSD_change_line(-1);
            }
            else if (button_was_released(BUTTON_RIGHT) 
                    || button_was_released(BUTTON_LEFT)
                    || button_was_released(BUTTON_A))
            {
                bool leftbtn = button_was_released(BUTTON_LEFT);
                uint8_t line = OSD_get_active_line();
                if (line == OSD_LINE_COLOR_SCHEME)
                {
                    change_color_scheme_index(leftbtn ? -1 : 1);
                    color_scheme = get_scheme();
                    update_osd();
                }
                else if (line == OSD_LINE_BACK_COLOR)
                {
                    change_control_scheme_index(leftbtn ? -1 : 1);
                    control_scheme = get_control_scheme();
                    update_osd();
                }
                else if (line == OSD_LINE_BACKLIGHT)
                {
                    change_backlight_level(leftbtn ? -1 : 1);
                    update_osd();
                }
                else
                {
                    OSD_toggle();
                }
            }
        }
    }

    for (int i = 0; i < BUTTON_COUNT; i++) 
    {
        button_states_previous[i] = button_states[i];
    }
    //OR... memcpy(button_states_previous, button_states, BUTTON_COUNT);
}

static void update_osd(void)
{
    char buff[32];
    sprintf(buff, "COLOR SCHEME:% 5d", get_scheme_index());
    OSD_set_line_text(OSD_LINE_COLOR_SCHEME, buff);

    sprintf(buff, "BACK COLOR:% 7d", get_control_scheme_index());
    OSD_set_line_text(OSD_LINE_BACK_COLOR, buff);

    sprintf(buff, "BACKLIGHT:% 8d", backlight_level);
    OSD_set_line_text(OSD_LINE_BACKLIGHT, buff);

    OSD_set_line_text(OSD_LINE_EXIT, "EXIT");

    OSD_update();
}

static void blink(uint8_t count, uint16_t millis_on, uint16_t millis_off)
{
  static uint8_t blink_count = 0;
  static uint16_t current_rate = 200;
  static uint32_t last_led_change = 0;

  if (count == 0)
  {
    gpio_put(ONBOARD_LED_PIN, 0);
    return;
  }

  uint32_t current_us = time_us_32();
  bool current_state = gpio_get(ONBOARD_LED_PIN);

  bool timeup = (current_us - last_led_change) > (current_rate*1000);
  if (timeup)
  {
    last_led_change = current_us;
    if (current_rate == millis_on)
    {
      bool new_state = !current_state;
      gpio_put(ONBOARD_LED_PIN, new_state);
      if (new_state ==  false)
      {
        blink_count++;
        if (blink_count >= count)
        {
          blink_count = 0;
          current_rate = millis_off;
        }
      }
      return;
    }
    
    blink_count = 0;
    current_rate = millis_on;
  }
}

static void change_backlight_level(int direction)
{
    // level 1 to 10 only
    backlight_level += direction;
    backlight_level = backlight_level < 1 ? 1 : backlight_level > 10 ? 10 : backlight_level;

    uint pwm_value = (uint)(BACKLIGHT_PWM_MAX * backlight_level / 10);
    pwm_set_gpio_level(BACKLIGHT_PWM_PIN, pwm_value);
}

static void set_orientation(void)
{
    rect_gamewindow.x = 0;
    rect_gamewindow.y = 0;
    rect_gamewindow.width = DMG_PIXELS_Y;
    rect_gamewindow.height = DMG_PIXELS_X;
    rect_osd.height = OSD_WIDTH;
    rect_osd.width = OSD_HEIGHT;

    rect_osd.x = (rect_gamewindow.width - rect_osd.width)/2;
    rect_osd.y = (rect_gamewindow.height - rect_osd.height)/2;
}

static bool rect_contains_point(const rectangle_t* rect, uint16_t x, uint16_t y)
{
    if (x < rect->x || x >= rect->x + rect->width || y < rect->y || y >= rect->y + rect->height)
        return false;
        
    return true;
}

static void touchup(uint16_t x, uint16_t y)
{
    gpio_put(ONBOARD_LED_PIN, 0);
    x /= 3; //TODO xscale, yscale
    y /= 3;
    if (rect_contains_point(&scaled_home, x, y))
    {
        set_button(BUTTON_HOME, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_start, x, y))
    {
        set_button(BUTTON_START, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_select, x, y))
    {
        set_button(BUTTON_SELECT, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_a, x, y))
    {
        set_button(BUTTON_A, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_b, x, y))
    {
        set_button(BUTTON_B, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_up, x, y))
    {
        set_button(BUTTON_UP, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_down, x, y))
    {
        set_button(BUTTON_DOWN, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_left, x, y))
    {
        set_button(BUTTON_LEFT, BUTTON_STATE_UNPRESSED);
        return;
    }
    if (rect_contains_point(&scaled_right, x, y))
    {
        set_button(BUTTON_RIGHT, BUTTON_STATE_UNPRESSED);
        return;
    }
}

static void touchdown(uint16_t x, uint16_t y)
{
    gpio_put(ONBOARD_LED_PIN, 1);

    // TODO: xscale, xycale...
    x /= 3;
    y /= 3;
    
    if (rect_contains_point(&scaled_home, x, y))
    {
        set_button(BUTTON_HOME, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_start, x, y))
    {
        set_button(BUTTON_START, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_select, x, y))
    {
        set_button(BUTTON_SELECT, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_a, x, y))
    {
        set_button(BUTTON_A, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_b, x, y))
    {
        set_button(BUTTON_B, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_up, x, y))
    {
        set_button(BUTTON_UP, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_down, x, y))
    {
        set_button(BUTTON_DOWN, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_left, x, y))
    {
        set_button(BUTTON_LEFT, BUTTON_STATE_PRESSED);
        return;
    }
    if (rect_contains_point(&scaled_right, x, y))
    {
        set_button(BUTTON_RIGHT, BUTTON_STATE_PRESSED);
        return;
    }
}

// TODO: on touchup, maybe check total count of buttons pressed... if 0, clear all buttons
// TODO: touchmove.. release if moved off


static void gpio_callback_VIDEO(uint gpio, uint32_t events) 
{
//                  ┌─────────────────────────────────────────┐     
// VSYNC ───────────┘                                         └───────────────────────
//                    ┌──────┐                                     ┌──────┐
// HSYNC ─────────────┘      └─────────────────────────────────────┘      └───────────
//                      ┌─┐     ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐      ┌─┐     ┌─┐ ┌─┐ ┌
// CLOCK ───────────────┘ └─────┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └──────┘ └─────┘ └─┘ └─┘
//       ─────────────┐   ┌──┐  ┌┐ ┌┐ ┌──┐ ┌┐ ┌┐ ┌───────────────┐   ┌──┐  ┌┐ ┌┐ ┌──┐ 
// DATA 0/1           └───┘  └──┘└─┘└─┘  └─┘└─┘└─┘               └───┘  └──┘└─┘└─┘  └─


    if(gpio==VSYNC_PIN)
    {
        // ignore falling edge interrupt
        if (events & GPIO_IRQ_EDGE_FALL)
            return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint32_t pos = 0;
    uint16_t rot_x = 0;
    uint16_t rot_y = 0;

    for (y = 0; y < DMG_PIXELS_Y; y++)
    {
        // wait for HSYNC edge to fall
        while (gpio_get(HSYNC_PIN) == 0);
        while (gpio_get(HSYNC_PIN) == 1);
        for (x = 0; x < DMG_PIXELS_X; x++)   //DMG_PIXELS_X
        {
            // Rotate 270
            rot_x = y;
            rot_y = rect_gamewindow.height - 1 - x;
            
            pos = rot_x + (rot_y * rect_gamewindow.width);

            if (pos < DMG_PIXEL_COUNT)
                framebuffer[pos] = (gpio_get(DATA_0_PIN) << 1) + gpio_get(DATA_1_PIN);

            // wait for clock pulse to fall
            while (gpio_get(PIXEL_CLOCK_PIN) == 0);
            while (gpio_get(PIXEL_CLOCK_PIN) == 1);
        }
    }
}