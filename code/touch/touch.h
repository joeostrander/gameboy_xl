#ifndef TOUCH_H
#define TOUCH_H

// ******************************************************************************************
// HEADER FILES
// ******************************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "hardware/i2c.h"

// ******************************************************************************************
// CONSTANTS
// ******************************************************************************************
#define MAX_TOUCH_POINTS    5

// ******************************************************************************************
// TYPE DEFINITIONS
// ******************************************************************************************
// typedef struct touch_point
// {
//     uint16_t point_x;
//     uint16_t point_y;
//     uint16_t point_size;
//     uint8_t reserved;
//     uint8_t track_id;
// } touch_point_t;

// typedef struct __attribute__((packed))
// {
//     uint8_t track_id;  // point 1 id = 32, indicates proximity sensing signal
//     uint16_t x;
//     uint16_t y;
//     uint16_t point_size;
//     uint8_t reserved;
// } touch_point_t;

// typedef struct
// {
//     uint8_t pointCount;
//     touch_point_t points[MAX_TOUCH_POINTS];
// } TOUCH_DATA_t;

typedef void (*touchDown_FnPtr)(uint16_t x, uint16_t y);
typedef void (*touchUp_FnPtr)(uint16_t x, uint16_t y);

// ******************************************************************************************
// PUBLIC FUNCTION PROTOTYPES
// ******************************************************************************************

void TOUCH_init(i2c_inst_t* i2c_instance);
bool TOUCH_tasks(void);
void TOUCH_set_touchup_callback(touchUp_FnPtr cb);
void TOUCH_set_touchdown_callback(touchDown_FnPtr cb);

#endif /* TOUCH_H */
