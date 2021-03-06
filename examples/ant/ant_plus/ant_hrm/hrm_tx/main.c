/*
This software is subject to the license described in the license.txt file
included with this software distribution. You may not use this file except in compliance
with this license.

Copyright (c) Dynastream Innovations Inc. 2012
All rights reserved.
*/

/**@file
 * @defgroup ant_hrm_tx_main ANT HRM TX example
 * @{
 * @ingroup nrf_ant_hrm
 *
 * @brief Example of ANT HRM TX profile.
 */

#include <stdio.h>
#include "app_uart.h"
#include "nrf.h"
#include "nrf_soc.h"
#include "bsp.h"
#include "app_error.h"
#include "nordic_common.h"
#include "ant_stack_config.h"
#include "softdevice_handler.h"
#include "ant_hrm.h"
#include "app_trace.h"
#include "ant_state_indicator.h"
#include "ant_key_manager.h"
#include "app_timer.h"
#include "ant_hrm_simulator.h"

#ifndef MODIFICATION_TYPE // can be provided as preprocesor global symbol
/**
 * @brief Depending of this define value heart rate value will be: @n
 *          - periodicaly rise and fall, use value  MODIFICATION_TYPE_AUTO
 *          - changing by button, use value         MODIFICATION_TYPE_BUTTON
 */
    #define MODIFICATION_TYPE (MODIFICATION_TYPE_AUTO)
#endif

#define MODIFICATION_TYPE_BUTTON 0 /* predefined value, MUST REMAIN UNCHANGED */
#define MODIFICATION_TYPE_AUTO   1 /* predefined value, MUST REMAIN UNCHANGED */

#if (MODIFICATION_TYPE != MODIFICATION_TYPE_BUTTON) \
    && (MODIFICATION_TYPE != MODIFICATION_TYPE_AUTO)

    #error Unsupported value of MODIFICATION_TYPE.
#endif

#define APP_TIMER_PRESCALER      0x00 /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE  0x04 /**< Size of timer operation queues. */

#define APP_TICK_EVENT_INTERVAL  APP_TIMER_TICKS(2000, APP_TIMER_PRESCALER) /**< 2 second's tick event interval in timer tick units. */

#define HRM_CHANNEL_NUMBER       0x00 /**< Channel number assigned to HRM profile. */

#define HRM_DEVICE_NUMBER        49u  /**< Denotes the used ANT device number. */
#define HRM_TRANSMISSION_TYPE    1u   /**< Denotes the used ANT transmission type. */

#define HRM_MFG_ID               2u      /**< Manufacturer ID. */
#define HRM_SERIAL_NUMBER        0xABCDu /**< Serial Number. */
#define HRM_HW_VERSION           5u      /**< HW Version. */
#define HRM_SW_VERSION           0       /**< SW Version. */
#define HRM_MODEL_NUMBER         2u      /**< Model Number. */

#define SIMULATOR_MIN            60  /**< Minimal heart rate. */
#define SIMULATOR_MAX            200 /**< Maximal heart rate. */
#define SIMULATOR_INCR           2   /**< Heart rate increment. */

#define ANTPLUS_NETWORK_NUMBER   0 /**< Network number. */

/** @snippet [ANT HRM TX Instance] */
void ant_hrm_evt_handler(ant_hrm_profile_t * p_profile, ant_hrm_evt_t event);

HRM_SENS_CHANNEL_CONFIG_DEF(m_ant_hrm,
                            HRM_CHANNEL_NUMBER,
                            HRM_TRANSMISSION_TYPE,
                            HRM_DEVICE_NUMBER,
                            ANTPLUS_NETWORK_NUMBER);
HRM_SENS_PROFILE_CONFIG_DEF(m_ant_hrm,
                            true,
                            ANT_HRM_PAGE_0,
                            ant_hrm_evt_handler);

static ant_hrm_profile_t m_ant_hrm;
/** @snippet [ANT HRM TX Instance] */

static ant_hrm_simulator_t  m_ant_hrm_simulator;    /**< Simulator used to simulate pulse. */
APP_TIMER_DEF(m_tick_timer);                        /**< Timer used to update cumulative operating time. */


#if MODIFICATION_TYPE == MODIFICATION_TYPE_BUTTON
/**@brief Function for handling bsp events.
 */
/** @snippet [ANT HRM simulator button] */
void bsp_evt_handler(bsp_event_t evt)
{
    switch (evt)
    {
        case BSP_EVENT_KEY_0:
            ant_hrm_simulator_increment(&m_ant_hrm_simulator);
            break;

        case BSP_EVENT_KEY_1:
            ant_hrm_simulator_decrement(&m_ant_hrm_simulator);
            break;

        default:
            return; // no implementation needed
    }
}


/** @snippet [ANT HRM simulator button] */
#endif // MODIFICATION_TYPE == MODIFICATION_TYPE_BUTTON


/**@brief Function for dispatching a ANT stack event to all modules with a ANT stack event handler.
 *
 * @details This function is called from the ANT Stack event interrupt handler after a ANT stack
 *          event has been received.
 *
 * @param[in] p_ant_evt  ANT stack event.
 */
void ant_evt_dispatch(ant_evt_t * p_ant_evt)
{
    ant_hrm_sens_evt_handler(&m_ant_hrm, p_ant_evt);
    ant_state_indicator_evt_handler(p_ant_evt);
}


/**
 * @brief 2 seconds tick handler for updataing cumulative operating time.
 */
static void app_tick_handler(void * p_context)
{
    // Only the first 3 bytes of this value are taken into account
    m_ant_hrm.HRM_PROFILE_operating_time++;
}


/**
 * @brief Function for setup all thinks not directly associated witch ANT stack/protocol.
 * @desc Initialization of: @n
 *         - app_tarce for debug.
 *         - app_timer, presetup for bsp and ant pulse simulation.
 *         - bsp for signaling leds and user buttons (if use button is enabled in example).
 *         - ant pulse simulate for task of filling hrm profile data.
 */
static void utils_setup(void)
{
    uint32_t err_code;

    app_trace_init();

    // Initialize and start a single continuous mode timer, which is used to update the event time
    // on the main data page.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, NULL);

    #if (MODIFICATION_TYPE == MODIFICATION_TYPE_BUTTON)
    /** @snippet [ANT Pulse simulator button init] */
    err_code = bsp_init(BSP_INIT_LED | BSP_INIT_BUTTONS,
                        APP_TIMER_TICKS(100, APP_TIMER_PRESCALER),
                        bsp_evt_handler);
    /** @snippet [ANT Pulse simulator button init] */
    #else
    err_code = bsp_init(BSP_INIT_LED, APP_TIMER_TICKS(100, APP_TIMER_PRESCALER), NULL);
    APP_ERROR_CHECK(err_code);
    #endif

    err_code = app_timer_create(&m_tick_timer,
                                APP_TIMER_MODE_REPEATED,
                                app_tick_handler);
    APP_ERROR_CHECK(err_code);

    // Schedule a timeout event every 2 seconds
    err_code = app_timer_start(m_tick_timer, APP_TICK_EVENT_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for the HRM simulator initialization.
 */
static void simulator_setup(void)
{
    /** @snippet [ANT HRM simulator init] */
    const ant_hrm_simulator_cfg_t simulator_cfg = DEFAULT_ANT_HRM_SIMULATOR_CFG(&m_ant_hrm,
                                                                                SIMULATOR_MIN,
                                                                                SIMULATOR_MAX,
                                                                                SIMULATOR_INCR);

    /** @snippet [ANT HRM simulator init] */

#if MODIFICATION_TYPE == MODIFICATION_TYPE_AUTO
    /** @snippet [ANT HRM simulator auto init] */
    ant_hrm_simulator_init(&m_ant_hrm_simulator, &simulator_cfg, true);
    /** @snippet [ANT HRM simulator auto init] */
#else
    /** @snippet [ANT HRM simulator button init] */
    ant_hrm_simulator_init(&m_ant_hrm_simulator, &simulator_cfg, false);
    /** @snippet [ANT HRM simulator button init] */
#endif
}


/**
 * @brief Function for ANT stack initialization.
 *
 * @details Initializes the SoftDevice and the ANT event interrupt.
 */
static void softdevice_setup(void)
{
    uint32_t err_code;

    err_code = softdevice_ant_evt_handler_set(ant_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    err_code = softdevice_handler_init(NRF_CLOCK_LFCLKSRC, NULL, 0, NULL);
    APP_ERROR_CHECK(err_code);

    err_code = ant_stack_static_config(); // set ant resource
    APP_ERROR_CHECK(err_code);

    err_code = ant_plus_key_set(ANTPLUS_NETWORK_NUMBER);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling ANT HRM events.
 */
/** @snippet [ANT HRM simulator call] */
void ant_hrm_evt_handler(ant_hrm_profile_t * p_profile, ant_hrm_evt_t event)
{
    switch (event)
    {
        case ANT_HRM_PAGE_0_UPDATED:
            /* fall through */
        case ANT_HRM_PAGE_1_UPDATED:
            /* fall through */
        case ANT_HRM_PAGE_2_UPDATED:
            /* fall through */
        case ANT_HRM_PAGE_3_UPDATED:
            /* fall through */
        case ANT_HRM_PAGE_4_UPDATED:
            ant_hrm_simulator_one_iteration(&m_ant_hrm_simulator);
            break;

        default:
            break;
    }
}


/** @snippet [ANT HRM simulator call] */

/**
 * @brief Function for HRM profile initialization.
 *
 * @details Initializes the HRM profile and open ANT channel.
 */
static void profile_setup(void)
{
/** @snippet [ANT HRM TX Profile Setup] */
    uint32_t err_code;

    err_code = ant_hrm_sens_init(&m_ant_hrm,
                                 HRM_SENS_CHANNEL_CONFIG(m_ant_hrm),
                                 HRM_SENS_PROFILE_CONFIG(m_ant_hrm));
    APP_ERROR_CHECK(err_code);

    m_ant_hrm.HRM_PROFILE_manuf_id   = HRM_MFG_ID;
    m_ant_hrm.HRM_PROFILE_serial_num = HRM_SERIAL_NUMBER;
    m_ant_hrm.HRM_PROFILE_hw_version = HRM_HW_VERSION;
    m_ant_hrm.HRM_PROFILE_sw_version = HRM_SW_VERSION;
    m_ant_hrm.HRM_PROFILE_model_num  = HRM_MODEL_NUMBER;

    err_code = ant_hrm_sens_open(&m_ant_hrm);
    APP_ERROR_CHECK(err_code);

    err_code = ant_state_indicator_channel_opened();
    APP_ERROR_CHECK(err_code);
/** @snippet [ANT HRM TX Profile Setup] */
}


/**@brief Function for application main entry, does not return.
 */
int main(void)
{
    uint32_t err_code;

    utils_setup();
    softdevice_setup();
    ant_state_indicator_init(m_ant_hrm.channel_number, HRM_SENS_CHANNEL_TYPE);
    simulator_setup();

    profile_setup();

    for (;; )
    {
        err_code = sd_app_evt_wait();
        APP_ERROR_CHECK(err_code);
    }
}


/**
 *@}
 **/
