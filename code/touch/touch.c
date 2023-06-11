//TODO:  handle touchmove

// touch.c

//*********************************************************************************************
// HEADER FILES
//*********************************************************************************************
#include "touch.h"
#include <string.h> // for memset

//*********************************************************************************************
// CONSTANTS & MACROS
//*********************************************************************************************
#define DRV_I2C_INDEX                           (DRV_I2C_INDEX_1)
#define TOUCH_INTERFACE_TASK_FREQ_HZ            (20)

#define GT911_I2C_SLAVE_ADDRESS                 (0x5D)    // 7-bit address for 0xBA
#define GT911_REG_CONFIG_START                  (0x8047)  // also config version
#define GT911_REG_CONFIG_END                    (0x80FF)  // also config checksum
#define GT911_CONFIG_SIZE                       (GT911_REG_CONFIG_END - GT911_REG_CONFIG_START)
#define GT911_READ_XY_REG                       (0x814E)
#define GT911_STATUS_FLAG_BUFFER_READY          (1<<7)
#define GT911_TOUCH_POINTS_FROM_STATUS(status)  ((status) &= 0xf)
#define GT911_MAX_TOUCH_POINTS                  (5)

#define LCD_WIDTH                               (800)
#define LCD_HEIGHT                              (480)

//*********************************************************************************************
// TYPE DEFINITIONS
//*********************************************************************************************
typedef struct __attribute__((packed))
{
    uint8_t track_id;  // point 1 id = 32, indicates proximity sensing signal
    uint16_t x;
    uint16_t y;
    uint16_t point_size;
    uint8_t reserved;
} touch_point_t;

// typedef struct __attribute__((packed))
// {
//     uint16_t x;
//     uint16_t y;
//     uint16_t point_size;
//     uint8_t reserved;
//     uint8_t track_id;
// } touch_point_t;

typedef struct
{
    uint16_t point_x;
    uint16_t point_y;
    bool valid;
} tracked_touch_t;

// ////
// typedef struct point_data
// {
//     uint8_t status;
//     touch_point_t pd[GT911_MAX_TOUCH_POINTS];
// } point_data_t;
// ////

typedef struct  __attribute__((packed))
{
  uint8_t buffer_status;  //0x814E, Read/Write...  bit 8 = touch present?  low byte = # of touches
  touch_point_t points[GT911_MAX_TOUCH_POINTS];
} operating_data_GT911_t;

// just a taste of the config...
typedef struct __attribute__((packed))
{
    uint8_t version;
    uint16_t x_resolution;
    uint16_t y_resolution;
} config_data_gt911_t;

typedef struct
{
    uint8_t pointCount;
    touch_point_t points[MAX_TOUCH_POINTS];
} TOUCH_DATA_t;

//*********************************************************************************************
// PRIVATE VARIABLES
//*********************************************************************************************
static i2c_inst_t* i2cHandle = NULL;
static tracked_touch_t tracked_touches[MAX_TOUCH_POINTS];
static tracked_touch_t tracked_touches_previous[MAX_TOUCH_POINTS];

//*********************************************************************************************
// PRIVATE FUNCTION PROTOTYPES
//*********************************************************************************************

static void HardwareInit(void);

static bool GetTouchData_GT911(TOUCH_DATA_t *data);

// static bool GetResolution_GT911(leSize *size);
// static bool SetResolution_GT911(void);

static bool GetRegister_GT911(uint16_t registerAddress, uint8_t* read_buffer, uint8_t buffer_length);
static bool ClearStatus_GT911(void);

//static uint8_t Checksum_GT911(uint8_t* bytes,uint8_t length);

static touchDown_FnPtr touchup_callback = NULL;
static touchDown_FnPtr touchdown_callback = NULL;

//*********************************************************************************************
// PUBLIC FUNCTIONS
//*********************************************************************************************
void TOUCH_init(i2c_inst_t* i2c_instance)
{
    i2cHandle = i2c_instance;

    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
    {
        tracked_touches[i].point_x = 0;
        tracked_touches[i].point_y = 0;
        tracked_touches[i].valid = false;
        tracked_touches_previous[i].point_x = 0;
        tracked_touches_previous[i].point_y = 0;
        tracked_touches_previous[i].valid = false;
    }
}

void TOUCH_set_touchup_callback(touchUp_FnPtr cb)
{
    touchup_callback = cb;
}
void TOUCH_set_touchdown_callback(touchDown_FnPtr cb)
{
    touchdown_callback = cb;
}

bool TOUCH_tasks(void)
{
    static uint16_t pos_x = 0;
    static uint16_t pos_y = 0;
    static bool touching = false;
    
    static TOUCH_DATA_t touchData;

    // Initialize the xLastWakeTime variable with the current time.
    //TickType_t xLastWakeTime = xTaskGetTickCount();
    static uint32_t last_poll_micros = 0;
    uint32_t current_micros = time_us_32();

    if (i2cHandle == NULL)
        return false;

    // Initialize the device with the proper I2C address
    //HardwareInit();

    // Need a slight delay to allow the i2c device to fully initialize
    //vTaskDelay(500);
    //sleep_ms(500);

    //TODO(void)SetResolution_GT911();

    // Enter the cyclic portion of the TouchInterface task
    
    if (current_micros - last_poll_micros < 50000)
        return false;

    if( GetTouchData_GT911(&touchData) == true)
    {
        for (int i = 0; i < GT911_MAX_TOUCH_POINTS; i++)
        {
            tracked_touches[i].valid = false;   //reset
            for (int j = 0; j < touchData.pointCount; j++)
            {
                if (touchData.points[j].track_id == i)
                {
                    tracked_touches[i].valid = true;    //TODO:  change valid to touching?
                    tracked_touches[i].point_x = touchData.points[j].x;
                    tracked_touches[i].point_y = touchData.points[j].y;
                }
            }

            if (tracked_touches[i].valid && !tracked_touches_previous[i].valid)
            {
                //touchdown
                if (touchdown_callback != NULL)
                {
                    touchdown_callback(tracked_touches[i].point_x, tracked_touches[i].point_y);
                }
            }
            if (!tracked_touches[i].valid && tracked_touches_previous[i].valid)
            {
                //touchup
                if (touchup_callback != NULL)
                {
                    // send touchup at previous location
                    touchup_callback(tracked_touches_previous[i].point_x, tracked_touches_previous[i].point_y);
                }
            }

            
                 tracked_touches_previous[i].point_x = tracked_touches[i].point_x;
                 tracked_touches_previous[i].point_y = tracked_touches[i].point_y;
                 tracked_touches_previous[i].valid = tracked_touches[i].valid;
        }

        // // TODO:  send touchup to previous x,y (of touchdown)?
        // // any changes?
        // if (memcmp(tracked_touches, tracked_touches_previous, sizeof(tracked_touches)/sizeof(tracked_touch_t)) != 0)
        // {
        //     for (int i = 0; i < GT911_MAX_TOUCH_POINTS; i++)
        //     {
        //         if (tracked_touches[i].valid && !tracked_touches_previous[i].valid)
        //         {
        //             //touchdown
        //             if (touchdown_callback != NULL)
        //             {
        //                 touchdown_callback(tracked_touches[i].point_x, tracked_touches[i].point_y);
        //             }
        //         }
        //         if (!tracked_touches[i].valid && tracked_touches_previous[i].valid)
        //         {
        //             //touchup
        //             if (touchup_callback != NULL)
        //             {
        //                 touchup_callback(tracked_touches[i].point_x, tracked_touches[i].point_y);
        //             }
        //         }

        //         // tracked_touches_previous[i].point_x = tracked_touches[i].point_x;
        //         // tracked_touches_previous[i].point_y = tracked_touches[i].point_y;
        //         // tracked_touches_previous[i].valid = tracked_touches[i].valid;

        //         //TODO:  touchmove
        //     }
        // }
        //    memcpy(tracked_touches_previous, tracked_touches, sizeof(tracked_touches)/sizeof(tracked_touch_t));
    }
    
    last_poll_micros = time_us_32();
    return true;
}

//*********************************************************************************************
// PRIVATE FUNCTIONS
//*********************************************************************************************


static void HardwareInit(void)
{
    //TCH_RST_Clear();
    //TRISHbits.TRISH14 = 0;  // set TCH_INT as output
    //LATHbits.LATH14 = 0;    // low for 0xBA/0xBB... high for 0x28/0x29  (R/W 8bit values)
    //Nop();
    //TCH_RST_Set();
    //vTaskDelay(5/portTICK_PERIOD_MS);
    //TRISHbits.TRISH14 = 1;  // set TCH_INT as input
    //vTaskDelay(50/portTICK_PERIOD_MS);  // required before sending any config
}


static bool GetTouchData_GT911(TOUCH_DATA_t *data)
{
    memset(data, 0, sizeof(TOUCH_DATA_t));
    operating_data_GT911_t operating_data;

    if(GetRegister_GT911((uint16_t)GT911_READ_XY_REG, (uint8_t*)&operating_data, sizeof(operating_data_GT911_t)) == true)
    {
        (void)ClearStatus_GT911();

        if((operating_data.buffer_status & GT911_STATUS_FLAG_BUFFER_READY) == 0)
            return false;

        data->pointCount = GT911_TOUCH_POINTS_FROM_STATUS(operating_data.buffer_status);

        if(data->pointCount > GT911_MAX_TOUCH_POINTS)
            return false;

        uint8_t p;
        for(p=0;p<data->pointCount;p++)
        {
            data->points[p].x = (uint32_t)operating_data.points[p].x;
            data->points[p].y = (uint32_t)operating_data.points[p].y;
            data->points[p].track_id = operating_data.points[p].track_id;
        }

        return true;
    }

    return false;
}


// static bool GetResolution_GT911(leSize *size)
// {
//     config_data_gt911_t config;

//     if(GetRegister_GT911((uint16_t)GT911_REG_CONFIG_START, (uint8_t*)&config, sizeof(config_data_gt911_t)) != true)
//     {
//         (void)printf("GT911 - Failed to get resolution!\n");
//         return false;
//     }

//     if((config.version < 'A') || (config.version > 'Z'))
//         return false;

//     size->width = config.x_resolution;
//     size->height = config.y_resolution;

//     return true;
// }

// static bool SetResolution_GT911(void)
// {
//     leSize current_resolution;
//     if(GetResolution_GT911(&current_resolution) == false)
//         return false;

//     if((current_resolution.width == LCD_WIDTH) && (current_resolution.height == LCD_HEIGHT))
//         return true;

//     uint8_t buffer_length = GT911_CONFIG_SIZE+1;    // full config + checksum
//     uint8_t config[buffer_length];

//     // get the current config
//     static uint8_t write_buffer[GT911_CONFIG_SIZE+4] = {0};    // address + data + checksum + 0x01 (config_fresh flag)
//     write_buffer[0] = GT911_REG_CONFIG_START >> 8;
//     write_buffer[1] = GT911_REG_CONFIG_START & 0xFF;

//     if(!DRV_I2C_WriteReadTransfer(drvI2CHandle, GT911_I2C_SLAVE_ADDRESS, write_buffer,2, config, buffer_length))  // only sending 2 bytes for now
//     {
//         (void)printf("GT911 - Set resolution:  Failed to get current config!\n");
//         return false;
//     }

//     (void)printf("GT911 - Current config version:  %c\n", config[0]);

//     // change the resolution
//     config[0]++;    // set to zero to reset to 'A'
//     config[1] = LCD_WIDTH & 0xFF;
//     config[2] = LCD_WIDTH >> 8;
//     config[3] = LCD_HEIGHT & 0xFF;
//     config[4] = LCD_HEIGHT >> 8;

//     // update checksum...
//     config[GT911_CONFIG_SIZE] = Checksum_GT911(config, GT911_CONFIG_SIZE-1); // last byte is for checksum

//     memcpy(&write_buffer[2], &config[0], buffer_length);
//     write_buffer[sizeof(write_buffer)-1] = 0x01;

//     if(DRV_I2C_WriteTransfer(drvI2CHandle, GT911_I2C_SLAVE_ADDRESS, write_buffer,sizeof(write_buffer)))
//     {
//         (void)printf("GT911 - New config written.\n");
//         return true;
//     }

//     (void)printf("GT911 - Config failure.\n");
//     return false;
// }

static bool GetRegister_GT911(uint16_t registerAddress, uint8_t* read_buffer, uint8_t buffer_length)
{
    static uint8_t write_buffer[2];
    write_buffer[0] = registerAddress >> 8;
    write_buffer[1] = registerAddress & 0xFF;
    (void)i2c_write_blocking(i2cHandle, GT911_I2C_SLAVE_ADDRESS, write_buffer, sizeof(write_buffer), false);

    int ret = i2c_read_blocking(i2cHandle, GT911_I2C_SLAVE_ADDRESS, read_buffer, buffer_length, false);


    if (ret < 0)
    {
        //???last_micros = time_us_32();
        return false;
    }


    return true;

    // //uint16_t registerAddressBigEndian = __swap16gen(registerAddress);   //lint !e160
    // uint16_t registerAddressBigEndian = (registerAddress & 0xff) << 8 | (registerAddress & 0xff00) >> 8;
    // bool result = DRV_I2C_WriteReadTransfer(drvI2CHandle, GT911_I2C_SLAVE_ADDRESS, (void*)&registerAddressBigEndian, sizeof(registerAddressBigEndian), read_buffer, buffer_length);
    // return result ? true : false;
}

static bool ClearStatus_GT911(void)
{
    static uint8_t write_buffer[3];
    write_buffer[0] = GT911_READ_XY_REG >> 8;
    write_buffer[1] = GT911_READ_XY_REG & 0xFF;
    write_buffer[2] = 0x00;
    int ret = i2c_write_blocking(i2cHandle, GT911_I2C_SLAVE_ADDRESS, write_buffer, sizeof(write_buffer), false);
    return ret >= 0;
}

// static uint8_t Checksum_GT911(uint8_t* bytes, uint8_t length)
// {
//     uint8_t checksum = 0;
//     uint8_t i;
//     for(i=0; i < length; i++)
//     {
//        checksum += bytes[i];
//     }
//     checksum = (~checksum) + 1;

//     return checksum;
// }