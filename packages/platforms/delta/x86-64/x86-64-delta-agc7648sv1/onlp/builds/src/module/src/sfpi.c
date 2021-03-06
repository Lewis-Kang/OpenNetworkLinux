/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright (C) 2017  Delta Networks, Inc. 
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <onlp/platformi/sfpi.h>
#include <fcntl.h> /* For O_RDWR && open */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>
#include <onlplib/file.h>
#include <onlplib/i2c.h>
#include "platform_lib.h"

/******************* Utility Function *****************************************/
int sfp_map_bus[] = {
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, /* SFP */
    61, 62, 63, 64, 65, 66, 67, 68, 69, 70, /* SFP */
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, /* SFP */
    81, 82, 83, 84, 85, 86, 87, 88, 89, 90, /* SFP */
    91, 92, 93, 94, 95, 96, 97, 98, /* SFP */
    41, 42, 43, 44, 45, 46, /* QSFP */
};

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/
int onlp_sfpi_init(void)
{
    /* Called at initialization time */
    lockinit();
    return ONLP_STATUS_OK;
}

int onlp_sfpi_map_bus_index(int port)
{
    if(port < 0 || port > 54)
        return ONLP_STATUS_E_INTERNAL;
    return sfp_map_bus[port-1];
}

int onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /* Ports {1, 54} */
    int p;
    AIM_BITMAP_CLR_ALL(bmap);

    for(p = 1; p <= NUM_OF_PORT; p++) {
        AIM_BITMAP_SET(bmap, p);
    }
    return ONLP_STATUS_OK;
}
  
int onlp_sfpi_is_present(int port)
{
    char port_data[3] = {'\0'};
    int present, present_bit;

    if(port > 0 && port < 55)
    {
        /* Select QSFP/SFP port */
        sprintf(port_data, "%d", port );
        if(dni_i2c_lock_write_attribute(NULL, port_data, SFP_SELECT_PORT_PATH) < 0){
            AIM_LOG_ERROR("Unable to select port(%d)\r\n", port);
        }

        /* Read QSFP/SFP MODULE is present or not */
        present_bit = dni_i2c_lock_read_attribute(NULL, SFP_IS_PRESENT_PATH);
        if(present_bit < 0){
            AIM_LOG_ERROR("Unable to read present or not from port(%d)\r\n", port);
        }
    }

    /* From sfp_is_present value,
     * return 0 = The module is preset
     * return 1 = The module is NOT present */
    if(present_bit == 0) {
        present = 1;
    } else if (present_bit == 1) {
        present = 0;
        AIM_LOG_ERROR("Unble to present status from port(%d)\r\n", port);
    } else {
        /* Port range over 1-54, return -1 */
        AIM_LOG_ERROR("Error to present status from port(%d)\r\n", port);
        present = -1;
    }
    return present;
}

int onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    char present_all_data[21] = {'\0'};
    char *r_byte;
    char *r_array[7];
    uint8_t bytes[7] = {0};
    int count = 0;

    /* Read presence bitmap from SWPLD2 SFP+ and SWPLD1 QSFP28 Presence Register
     * if only port 0 is present,    return 3F FF FF FF FF FF FE
     * if only port 0 and 1 present, return 3F FF FF FF FF FF FC */

    if(dni_i2c_read_attribute_string(SFP_IS_PRESENT_ALL_PATH, present_all_data,
                                         sizeof(present_all_data), 0) < 0) {
        return -1;
    }

    /* String split */
    r_byte = strtok(present_all_data, " ");
    while (r_byte != NULL) {
        r_array[count++] = r_byte;
        r_byte = strtok(NULL, " ");
    }

    /* Convert a string to unsigned 8 bit integer
     * and saved into bytes[] */
    for (count = 0; count < 7; count++) {
        bytes[count] = ~strtol(r_array[count], NULL, 16);
    }

    /* Mask out non-existant SFP/QSFP ports */
    bytes[0] &= 0x3F;

    /* Convert to 64 bit integer in port order */
    int i = 0;
    uint64_t presence_all = 0;

    for(i = 0; i < AIM_ARRAYSIZE(bytes); i++) {
        presence_all <<= 8;
        presence_all |= bytes[i];
    }

    /* Populate bitmap & remap */
    for(i = 0; presence_all; i++)
    {
        AIM_BITMAP_MOD(dst, i+1, (presence_all & 1));
        presence_all >>= 1;
    }
    return ONLP_STATUS_OK;
}

int onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    int size = 0;

    memset(data, 0, 256);

    if(onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, onlp_sfpi_map_bus_index(port)) != ONLP_STATUS_OK) {
        AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        AIM_LOG_ERROR("Unable to read eeprom from port(%d), size is different!\r\n", port);
        return ONLP_STATUS_E_INTERNAL;
    }
    return ONLP_STATUS_OK;
}

int onlp_sfpi_port_map(int port, int* rport)
{
    *rport = port;
    return ONLP_STATUS_OK;
}

int onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    int value_t;
    char port_data[3] = {'\0'};

    if(port > 0 && port < 55)
    {
        /* Select SFP(1-48), QSFP(49-54) port */
        sprintf(port_data, "%d", port );
        if(dni_i2c_lock_write_attribute(NULL, port_data, SFP_SELECT_PORT_PATH) < 0){
            AIM_LOG_INFO("Unable to select port(%d)\r\n", port);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    switch (control) {   
        case ONLP_SFP_CONTROL_RESET_STATE:
            /* From sfp_reset value,
             * return 0 = The module is in Reset
             * return 1 = The module is NOT in Reset */
            *value = dni_i2c_lock_read_attribute(NULL, QSFP_RESET_PATH);
            if (*value == 0)
            {
                *value = 1;
            }
            else if (*value == 1)
            {
                *value = 0;
            }
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_RX_LOS:
            /* From sfp_rx_los value,
             * return 0 = The module is Normal Operation
             * return 1 = The module is Error */
            *value = dni_i2c_lock_read_attribute(NULL, SFP_RX_LOS_PATH);
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_TX_DISABLE:
            /* From sfp_tx_disable value,
             * return 0 = The module is Enable Transmitter on
             * return 1 = The module is Transmitter Disabled */
            *value = dni_i2c_lock_read_attribute(NULL, SFP_TX_DISABLE_PATH);
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_TX_FAULT:
            /* From sfp_tx_fault value,
             * return 0 = The module is Normal
             * return 1 = The module is Fault */
            *value = dni_i2c_lock_read_attribute(NULL, SFP_TX_FAULT_PATH);
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_LP_MODE:
            /* From sfp_lp_mode value,
             * return 0 = The module is NOT in LP mode
             * return 1 = The module is in LP mode */
            *value = dni_i2c_lock_read_attribute(NULL, QSFP_LP_MODE_PATH);
            value_t = ONLP_STATUS_OK;
            break;
        default:
            value_t = ONLP_STATUS_E_UNSUPPORTED;
            break;
    }
    return value_t;
}

int onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int value_t;
    char port_data[3] = {'\0'};

    if(port > 0 && port < 55)
    {
        /* Select SFP(1-48), QSFP(49-54) port */
        sprintf(port_data, "%d", port );
        if(dni_i2c_lock_write_attribute(NULL, port_data, SFP_SELECT_PORT_PATH) < 0){
            AIM_LOG_INFO("Unable to select port(%d)\r\n", port);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    switch (control) {
        case ONLP_SFP_CONTROL_RESET_STATE:
            sprintf(port_data, "%d", value);
            if(dni_i2c_lock_write_attribute(NULL, port_data, QSFP_RESET_PATH) < 0){
                AIM_LOG_INFO("Unable to control reset state from port(%d)\r\n", port);
                value_t = ONLP_STATUS_E_INTERNAL;
            }
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_RX_LOS:
            value_t = ONLP_STATUS_E_UNSUPPORTED;
            break;
        case ONLP_SFP_CONTROL_TX_DISABLE:
            sprintf(port_data, "%d", value);
            if(dni_i2c_lock_write_attribute(NULL, port_data, SFP_TX_DISABLE_PATH) < 0){
                AIM_LOG_INFO("Unable to control tx disable from port(%d)\r\n", port);
                value_t = ONLP_STATUS_E_INTERNAL;
            }
            value_t = ONLP_STATUS_OK;
            break;
        case ONLP_SFP_CONTROL_TX_FAULT:
            value_t = ONLP_STATUS_E_UNSUPPORTED;
            break;
        case ONLP_SFP_CONTROL_LP_MODE:
            sprintf(port_data, "%d", value);
            if(dni_i2c_lock_write_attribute(NULL, port_data, QSFP_LP_MODE_PATH) < 0){
                AIM_LOG_INFO("Unable to control LP mode from port(%d)\r\n", port);
                value_t = ONLP_STATUS_E_INTERNAL;
            }
            value_t = ONLP_STATUS_OK;
            break;
        default:
            value_t = ONLP_STATUS_E_UNSUPPORTED;
            break;
    }
    return value_t;
}

int onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    int bus;
 
    bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_readb(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
    int bus;

    bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_writeb(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
    
}

int onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    int bus;

    bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_readw(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    int bus;
 
    bus = onlp_sfpi_map_bus_index(port);
    return onlp_i2c_writew(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_control_supported(int port, onlp_sfp_control_t control, int* rv)
{
    char port_data[3] = {'\0'};

    if(port > 0 && port < 55)
    {
        /* Select SFP(1-48), QSFP(49-54) port */
        sprintf(port_data, "%d", port);
        if(dni_i2c_lock_write_attribute(NULL, port_data, SFP_SELECT_PORT_PATH) < 0){
            AIM_LOG_INFO("Unable to select port(%d)\r\n", port);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    switch (control) {
        case ONLP_SFP_CONTROL_RESET_STATE:
            if(port > 48 && port < 55) /* QSFP */
                *rv = 1;
            else
                *rv = 0;
            break;
        case ONLP_SFP_CONTROL_RX_LOS:
            if(port > 0 && port < 49)  /* SFP */
                *rv = 1;
            else
                *rv = 0;
            break;
        case ONLP_SFP_CONTROL_TX_DISABLE:
            if(port > 0 && port < 49)  /* SFP */
                *rv = 1;
            else
                *rv = 0;
            break;
        case ONLP_SFP_CONTROL_TX_FAULT:
            if(port > 0 && port < 49)  /* SFP */
                *rv = 1;
            else
                *rv = 0;
            break;
        case ONLP_SFP_CONTROL_LP_MODE:
            if(port > 48 && port < 55) /* QSFP */
                *rv = 1;
            else
                *rv = 0;
            break;
        default:
            break;
    }
    return ONLP_STATUS_OK;
}

int onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}

int onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    char rxlos_all_data[18] = {'\0'};
    char *r_byte;
    char *r_array[6];
    uint8_t bytes[6] = {0};
    int count = 0;

    /* Read rx_los bitmap from SWPLD2 SFP+ LOSS Register
     * if only port 0 is normal operation,    return FF FF FF FF FF FE
     * if only port 0 and 1 normal operation, return FF FF FF FF FF FC */

    if(dni_i2c_read_attribute_string(SFP_RX_LOS_ALL_PATH, rxlos_all_data,
                                         sizeof(rxlos_all_data), 0) < 0) {
        return -1;
    }

    /* String split */
    r_byte = strtok(rxlos_all_data, " ");
    while (r_byte != NULL) {
        r_array[count++] = r_byte;
        r_byte = strtok(NULL, " ");
    }

    /* Convert a string to unsigned 8 bit integer
     * and saved into bytes[] */
    for (count = 0; count < 6; count++) {
        bytes[count] = strtol(r_array[count], NULL, 16);
    }

    /* Convert to 64 bit integer in port order */
    int i = 0;
    uint64_t rxlos_all = 0;

    for(i = 0; i < AIM_ARRAYSIZE(bytes); i++) {
        rxlos_all <<= 8;
        rxlos_all |= bytes[i];
    }

    for(i = 0; i < 48; i++)
    {
        AIM_BITMAP_MOD(dst, i+1, (rxlos_all & 1));
        rxlos_all >>= 1;
    }

    /* Mask out non-existant QSFP ports */
    for(i = 48; i < 54; i++)
        AIM_BITMAP_MOD(dst, i+1, 0);

    return ONLP_STATUS_OK;
}

int onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    return ONLP_STATUS_OK;
}

int onlp_sfpi_post_insert(int port, sff_info_t* info)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

void onlp_sfpi_debug(int port, aim_pvs_t* pvs)
{
}

int onlp_sfpi_ioctl(int port, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

