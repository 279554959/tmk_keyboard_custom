/*
Copyright 2011 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
//No2

#include <stdint.h>
#include "keycode.h"
#include "host.h"
#include "timer.h"
#include "print.h"
#include "debug.h"
#include "mousekey.h"
#include <LUFA/Drivers/Peripheral/ADC.h>
//#include "keymap_in_eeprom.h"
#include <math.h>



report_mouse_t mouse_report = {};
static uint8_t mousekey_repeat =  0;
static uint8_t mousekey_accel = 0;

static void mousekey_debug(void);


/*
 * Mouse keys  acceleration algorithm
 *  http://en.wikipedia.org/wiki/Mouse_keys
 *
 *  speed = delta * max_speed * (repeat / time_to_max)**((1000+curve)/1000)
 */
/* milliseconds between the initial key press and first repeated motion event (0-2550) */
uint8_t mk_delay = MOUSEKEY_DELAY/10;
/* milliseconds between repeated motion events (0-255) */
uint8_t mk_interval = MOUSEKEY_INTERVAL;
/* steady speed (in action_delta units) applied each event (0-255) */
uint8_t mk_max_speed = MOUSEKEY_MAX_SPEED;
/* number of events (count) accelerating to steady speed (0-255) */
uint8_t mk_time_to_max = MOUSEKEY_TIME_TO_MAX;
/* ramp used to reach maximum pointer speed (NOT SUPPORTED) */
//int8_t mk_curve = 0;
/* wheel params */
uint8_t mk_wheel_max_speed = MOUSEKEY_WHEEL_MAX_SPEED;
uint8_t mk_wheel_time_to_max = MOUSEKEY_WHEEL_TIME_TO_MAX;
//No2 joystick position
static uint16_t adc_move_x = 0;
static uint16_t adc_move_y = 0;
static bool set_ADC_center;
static bool set_ADC_ranger;
static bool adc_enable;
static uint16_t adc_setting_time = 0;
typedef struct{
    uint16_t xL;
    uint16_t xR;
    uint16_t yL;
    uint16_t yR;
    uint16_t xO;
    uint16_t yO;
}adc_range;
adc_range adc_min, adc_max;
adc_range *adc_range_p;

typedef struct{
    int16_t x;
    int16_t y;
    int16_t r;
}adc_coordinate;
adc_coordinate adc_now = { 0, 0, 0 }, adc_befor = { 0, 0, 0 };

static uint16_t last_timer = 0;


static uint8_t move_unit(void)
{
    uint16_t unit;
    if (mousekey_accel & (1<<0)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_max_speed)/4;
    } else if (mousekey_accel & (1<<1)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_max_speed)/2;
    } else if (mousekey_accel & (1<<2)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_max_speed);
    } else if (mousekey_repeat == 0) {
        unit = MOUSEKEY_MOVE_DELTA;
    } else if (mousekey_repeat >= mk_time_to_max) {
        unit = MOUSEKEY_MOVE_DELTA * mk_max_speed;
    } else {
        unit = (MOUSEKEY_MOVE_DELTA * mk_max_speed * mousekey_repeat) / mk_time_to_max;
    }
    return (unit > MOUSEKEY_MOVE_MAX ? MOUSEKEY_MOVE_MAX : (unit == 0 ? 1 : unit));
}

static uint8_t wheel_unit(void)
{
    uint16_t unit;
    if (mousekey_accel & (1<<0)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wheel_max_speed)/4;
    } else if (mousekey_accel & (1<<1)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wheel_max_speed)/2;
    } else if (mousekey_accel & (1<<2)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wheel_max_speed);
    } else if (mousekey_repeat == 0) {
        unit = MOUSEKEY_WHEEL_DELTA;
    } else if (mousekey_repeat >= mk_wheel_time_to_max) {
        unit = MOUSEKEY_WHEEL_DELTA * mk_wheel_max_speed;
    } else {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wheel_max_speed * mousekey_repeat) / mk_wheel_time_to_max;
    }
    return (unit > MOUSEKEY_WHEEL_MAX ? MOUSEKEY_WHEEL_MAX : (unit == 0 ? 1 : unit));
}

static uint16_t delay(adc_coordinate position){
    if (position.r < 10) {
        return 50;
    } else if (position.r < 45) {
        return 25;//(150 - ((position.r - 10) * 100 / 35));
    } else {
        return 25;
    }
}

void mousekey_task(void)
{

    adc_move_x = ADC_GetChannelReading(ADC_REFERENCE_AVCC | ADC_CHANNEL12);
    adc_move_y = ADC_GetChannelReading(ADC_REFERENCE_AVCC | ADC_CHANNEL13);

    if (set_ADC_center || set_ADC_ranger) {
        adc_range_p = ((set_ADC_center == true) ? &adc_min : &adc_max);
        if (adc_range_p->xL > adc_move_x) adc_range_p->xL = adc_move_x;
        if (adc_range_p->xR < adc_move_x) adc_range_p->xR = adc_move_x;
        if (adc_range_p->yL > adc_move_y) adc_range_p->yL = adc_move_y;
        if (adc_range_p->yR < adc_move_y) adc_range_p->yR = adc_move_y;
        if (timer_elapsed(adc_setting_time) > 10000) {
              mousekey_send();
        }
    }
    // mousekey_debug();
    if (adc_enable == false) return;

    adc_now.x = (adc_move_x - adc_max.xO) * 100 / (adc_max.xR - adc_max.xL);
    adc_now.y = (adc_move_y - adc_max.yO) * 100 / (adc_max.yR - adc_max.yL);
    adc_now.r = sqrt(pow((adc_now.x),2) + pow((adc_now.y),2));

    if (timer_elapsed(last_timer) < delay(adc_now))
        return;

    if (adc_now.r < 6) {
        if ((adc_now.x != adc_befor.x) || (adc_now.y != adc_befor.y)){
            mouse_report.x = adc_now.x - adc_befor.x;
            mouse_report.y = adc_now.y - adc_befor.y;
            memcpy(&adc_befor, &adc_now, sizeof(adc_coordinate));
        } else {
            mouse_report.x = 0;
            mouse_report.y = 0;
        }
    } else {
        mouse_report.x = adc_now.x;
        mouse_report.y = -adc_now.y;
    }
    // if (mouse_report.x == 0 && mouse_report.y == 0 && mouse_report.v == 0 && mouse_report.h == 0)
    //     return;
    //
    // if (mousekey_repeat != UINT8_MAX)
    //     mousekey_repeat++;


    // if (mouse_report.x > 0) mouse_report.x = move_unit();
    // if (mouse_report.x < 0) mouse_report.x = move_unit() * -1;
    // if (mouse_report.y > 0) mouse_report.y = move_unit();
    // if (mouse_report.y < 0) mouse_report.y = move_unit() * -1;

    /* diagonal move [1/sqrt(2) = 0.7] */
    // if (mouse_report.x && mouse_report.y) {
    //     mouse_report.x *= 0.7;
    //     mouse_report.y *= 0.7;
    // }

    // if (mouse_report.v > 0) mouse_report.v = wheel_unit();
    // if (mouse_report.v < 0) mouse_report.v = wheel_unit() * -1;
    // if (mouse_report.h > 0) mouse_report.h = wheel_unit();
    // if (mouse_report.h < 0) mouse_report.h = wheel_unit() * -1;

    mousekey_send();
}

void mousekey_on(uint8_t code)
{
    /*if      (code == KC_MS_UP)       mouse_report.y = move_unit() * -1;
    else if (code == KC_MS_DOWN)     mouse_report.y = move_unit();
    else if (code == KC_MS_LEFT)     mouse_report.x = move_unit() * -1;
    else if (code == KC_MS_RIGHT)    mouse_report.x = move_unit();
    else if (code == KC_MS_WH_UP)    mouse_report.v = wheel_unit();
    else if (code == KC_MS_WH_DOWN)  mouse_report.v = wheel_unit() * -1;
    else if (code == KC_MS_WH_LEFT)  mouse_report.h = wheel_unit() * -1;
    else if (code == KC_MS_WH_RIGHT) mouse_report.h = wheel_unit();
    else*/ if (code == KC_MS_BTN1)     mouse_report.buttons |= MOUSE_BTN1;
    else if (code == KC_MS_BTN2)     mouse_report.buttons |= MOUSE_BTN2;
    else if (code == KC_MS_BTN3)     mouse_report.buttons |= MOUSE_BTN3;
    else if (code == KC_MS_BTN4)     mouse_report.buttons |= MOUSE_BTN4;
    else if (code == KC_MS_BTN5)     mouse_report.buttons |= MOUSE_BTN5;
    else if (code == KC_MS_ACCEL0)   mousekey_accel |= (1<<0);
    else if (code == KC_MS_ACCEL1)   mousekey_accel |= (1<<1);
    else if (code == KC_MS_ACCEL2)   mousekey_accel |= (1<<2);
}

void mousekey_off(uint8_t code)
{
    /*if      (code == KC_MS_UP       && mouse_report.y < 0) mouse_report.y = 0;
    else if (code == KC_MS_DOWN     && mouse_report.y > 0) mouse_report.y = 0;
    else if (code == KC_MS_LEFT     && mouse_report.x < 0) mouse_report.x = 0;
    else if (code == KC_MS_RIGHT    && mouse_report.x > 0) mouse_report.x = 0;
    else if (code == KC_MS_WH_UP    && mouse_report.v > 0) mouse_report.v = 0;
    else if (code == KC_MS_WH_DOWN  && mouse_report.v < 0) mouse_report.v = 0;
    else if (code == KC_MS_WH_LEFT  && mouse_report.h < 0) mouse_report.h = 0;
    else if (code == KC_MS_WH_RIGHT && mouse_report.h > 0) mouse_report.h = 0;
    else*/ if (code == KC_MS_BTN1) mouse_report.buttons &= ~MOUSE_BTN1;
    else if (code == KC_MS_BTN2) mouse_report.buttons &= ~MOUSE_BTN2;
    else if (code == KC_MS_BTN3) mouse_report.buttons &= ~MOUSE_BTN3;
    else if (code == KC_MS_BTN4) mouse_report.buttons &= ~MOUSE_BTN4;
    else if (code == KC_MS_BTN5) mouse_report.buttons &= ~MOUSE_BTN5;
    else if (code == KC_MS_ACCEL0) mousekey_accel &= ~(1<<0);
    else if (code == KC_MS_ACCEL1) mousekey_accel &= ~(1<<1);
    else if (code == KC_MS_ACCEL2) mousekey_accel &= ~(1<<2);

    // if (mouse_report.x == 0 && mouse_report.y == 0 && mouse_report.v == 0 && mouse_report.h == 0)
    //     mousekey_repeat = 0;
}

void mousekey_send(void)
{
    mousekey_debug();
    host_mouse_send(&mouse_report);
    last_timer = timer_read();
}

void mousekey_clear(void)
{
    mouse_report = (report_mouse_t){};
    // mousekey_repeat = 0;
    mousekey_accel = 0;
}

static void mousekey_debug(void)
{
    if (!debug_mouse) return;
    if ((mousekey_accel & (1<<1)) && (mouse_report.buttons & MOUSE_BTN1)) {
        //set center position
        print("mousekey adc set center\n");
        adc_setting_time = timer_read();
        set_ADC_ranger = false;
        set_ADC_center = true;
        adc_min.xL = 1024;
        adc_min.xR = 0;
        adc_min.yL = 1024;
        adc_min.yR = 0;
        return;
    }
    if ((mousekey_accel & (1<<1)) && (mouse_report.buttons & MOUSE_BTN2)) {
        //set ranger
        print("mousekey adc set ranger\n");
        adc_setting_time = timer_read();
        set_ADC_center = false;
        set_ADC_ranger = true;
        adc_max.xL = 1024;
        adc_max.xR = 0;
        adc_max.yL = 1024;
        adc_max.yR = 0;
        return;
    }
    if ((mousekey_accel & (1<<1)) && (mouse_report.buttons & MOUSE_BTN3)) {
        //adc enable
        print("mousekey adc enable\n");
        adc_enable = true;
        return;
    }
    if ((mousekey_accel & (1<<1)) && (mouse_report.buttons & MOUSE_BTN4)) {
        //adc enable
        print("mousekey adc disable\n");
        adc_enable = false;
        return;
    }
    if (set_ADC_center || set_ADC_ranger) {
        if (timer_elapsed(adc_setting_time) > 10000) {
            set_ADC_center = false;
            set_ADC_ranger = false;
            adc_range_p->xO = (adc_range_p->xL + adc_range_p->xR) / 2;
            adc_range_p->yO = (adc_range_p->yL + adc_range_p->yR) / 2;
            print("adc set over\n<");
            print_decs(adc_range_p->xL); print(",");
            print_decs(adc_range_p->xR); print("> <");
            print_decs(adc_range_p->yL); print(",");
            print_decs(adc_range_p->yR); print(">\n");
        }
    }
    print("mousekey [btn|x y v h](rep/acl): [");
    phex(mouse_report.buttons); print("|");
    print_decs(mouse_report.x); print(" ");
    print_decs(mouse_report.y); print(" ");
    print_decs(mouse_report.v); print(" ");
    print_decs(mouse_report.h); print("](");
    print_dec(mousekey_repeat); print("/");
    print_dec(mousekey_accel); print(")\n");
}
