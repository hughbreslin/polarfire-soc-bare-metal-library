/*******************************************************************************
 * Copyright 2019-2021 Microchip FPGA Embedded Systems Solution.
 *
 * SPDX-License-Identifier: MIT
 *
 * Application code running on U54_1
 *
 * PolarFire SoC MSS CAN example demonstrating the Data Transmission and
 * Reception using MSSCAN (FullCAN).
 * For Transmission: Get data from UART terminal using MSSUART1 --> Form as CAN
 * packets --> Send to CAN Analyzer.
 * For Reception: Send the CAN Message from CAN Analyzer --> Read the Message-->
 * Send to UART terminal using MSSUART1.
 * Board settings and test procedure clearly mentioned in README.md file.
 */

#include <stdio.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_can/mss_can.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"

/*------------------------------------------------------------------------------
 * Macros.
 */
#define   ENTER               0x0DU

/*------------------------------------------------------------------------------
 * Private functions.
 */
static void display_greeting(void);
static uint8_t get_data_frm_uart(void);
static void display_hex_values(const uint8_t *, uint32_t);
static void ascii_to_hex(uint8_t *, uint32_t );
static void display_option(void);
static void check_rx_buffer(void);

/*------------------------------------------------------------------------------
 * Static Variables.
 */
static uint8_t g_uart_to_can[32];
static uint8_t g_temp[64];
static uint8_t g_can_to_uart[8];

/*------------------------------------------------------------------------------
 * Global Variables.
 */
mss_can_filterobject pfilter;
mss_can_msgobject pmsg;
mss_can_msgobject rx_buf;
mss_can_rxmsgobject rx_msg;

/*------------------------------------------------------------------------------
 * MSS UART instance for UART1
 */
uint64_t uart_lock;

mss_can_instance_t* g_mss_can_0 = &g_mss_can_0_lo;
mss_uart_instance_t *g_uart = &g_mss_uart1_lo;

/* Main function for the HART1(U54_1 processor).
 * Application code running on HART1 is placed here
 *
 * The HART1 goes into WFI. HART0 brings it out of WFI when it raises the first
 * Software interrupt to this HART
 */
void u54_1(void)
{
    volatile uint8_t count = 0u;
    int32_t error_flag;
    uint8_t rx_bytes = 0u;
    uint8_t no_of_msgs = 0u;
    size_t  rx_size;
    uint8_t rx_char;
    uint8_t loop_count;
    uint32_t msg_len;
    uint32_t chunk_size;
    uint8_t init_return_value = 0u;
    uint32_t tx_status = 0u;
    uint8_t ret_status;

#if (IMAGE_LOADED_BY_BOOTLOADER == 0)
    /* Clear pending software interrupt in case there was any.
     * Enable only the software interrupt so that the E51 core can bring this
     * core out of WFI by raising a software interrupt. */
    clear_soft_interrupt();
    set_csr(mie, MIP_MSIP);

    /* Put this hart in WFI. */
    do
    {
        __asm("wfi");
    } while (0 == (read_csr(mip) & MIP_MSIP));
#endif

    /* The hart is out of WFI, clear the SW interrupt. Here onwards Application
     * can enable and use any interrupts as required */
    clear_soft_interrupt();

    (void)mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t) 1, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_CAN0, (uint8_t) 1, PERIPHERAL_ON);

    PLIC_DisableIRQ(CAN0_PLIC);

    PLIC_init();
    __enable_irq();

    MSS_UART_init(g_uart,
                  MSS_UART_115200_BAUD,
                  (MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY |
                  MSS_UART_ONE_STOP_BIT));

    /*--------------------------------------------------------------------------
     * Performs CAN Initialization and Message Buffer Configuration
     */
#ifdef CAN_TARGET_COCO_PC_ACTIVE
    /*
     * Tweaked 20kbs configuration for a 16MHz PCLK1 which works with the
     * CoCo PC Active device to allow transmit and receive functionality be
     * observed.
     *
     * The sample design used for this test had a 100MHz GL0 clock derived from
     * the internal 25/50MHz source to generate a 128MHz processor clock which
     * was divided by 8 to give a 16MHz PCLK1.
     *
     * This configuration uses 16 time quanta per bit. The standard 20kbs
     * divisor should give 50uS per bit but in this design I was seeing a 50.5uS
     * bit time which did not allow the MSS CAN receive data from the CoCo PC
     * Active which had a bit time in the region of 49.5uS to 50uS. Reducing the
     * divisor from 49 to 48 (to give divide by 49 instead of divide by 50) gave
     * an MSS CAN bit time of 49.5uS and allowed reliable communications in both
     * directions.
     */
    MSS_CAN_init(&g_mss_can_0,
                 CAN_SET_BITRATE(48)|CAN_SET_TSEG1(11)|CAN_SET_TSEG2(2)|CAN_THREE_SAMPLES,
                 (PCAN_CONFIG_REG)0,
                 6,
                 6);
#else
    /* ----------------------- CAN - 0 Initialization   ----------------- */
    init_return_value = MSS_CAN_init(g_mss_can_0, CAN_SPEED_8M_1M,
                                     (pmss_can_config_reg)0, 6u, 6u);
    MSS_CAN_set_mode(g_mss_can_0, CANOP_MODE_NORMAL);
    MSS_CAN_start(g_mss_can_0);
#endif

    /* Display greeting message */
    display_greeting();

    /* Clear receive buffer */
    for (count = 0u; count < 8u; count++)
    {
        rx_buf.DATA[count] = 0u;
    }

    /* Configure for transmit */
    pmsg.ID = 0x78u;
    pmsg.DATALOW = 0xAAAAAAAAu;
    pmsg.DATAHIGH = 0x55555555u;
#ifdef CAN_TX_EXTENDED_ID
    pmsg.L =((1<<20) | 0x00080000u); /* Extended ID, 8 bytes of data */
#else
    pmsg.L = 0x00080000u;          /* Standard ID, 8 bytes of data */
#endif

    /* Configure for receive */
    /* Initialize the rx mailbox */
    rx_msg.ID = 0x80u;
    rx_msg.DATAHIGH = 0u;
    rx_msg.DATALOW = 0u;
    /*
     * use very broad masks here as we want to see any potential traffic for the
     * demo to reduce the chances of failure. You can tweak these to do proper
     * filtering if you know what traffic is available on your specific CAN Bus
     * network.
     */
    rx_msg.AMR.L = 0xFFFFFFFFu;
    rx_msg.ACR.L = 0x00000000u;
    rx_msg.AMR_D = 0xFFFFFFFFu;
    rx_msg.ACR_D = 0x00000000u;
    rx_msg.RXB.DLC = 8u;
    rx_msg.RXB.IDE = 0u;

    ret_status = MSS_CAN_config_buffer_n(g_mss_can_0, 0, &rx_msg);
    if (CAN_OK != ret_status)
    {
        MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rMessage Buffer configuration Error");
    }

    MSS_CAN_send_message_n(g_mss_can_0, 0u, &pmsg);

    while (1)
    {
        /*----------------------------------------------------------------------
         * Read the Data from UART and Transmit using CAN
         */
        rx_bytes = get_data_frm_uart();

        /* Convert ASCII values to Hex */
        ascii_to_hex(g_temp, rx_bytes);

        for (loop_count = 0u; loop_count < rx_bytes / 2u; loop_count++)
        {
            g_uart_to_can[loop_count] = g_temp[loop_count * 2u];
            g_uart_to_can[loop_count] = g_uart_to_can[loop_count] << 4u;
            g_uart_to_can[loop_count] |= g_temp[(loop_count * 2u) + 1u];
        }
        MSS_UART_polled_tx_string(g_uart,
                       (const uint8_t *)"\n\rData transmitted as CAN Message ");
        display_hex_values(g_uart_to_can, loop_count);

        /*------------------------------------------------------------------
         * Identify the number of messages to transmit based on rx_bytes
         */
        no_of_msgs = rx_bytes / 16u;
        if ((rx_bytes % 16u) != 0u)
        {
            no_of_msgs = no_of_msgs + 1u;
        }

        if (0u == loop_count) /* Allow sending an empty packet */
        {
            no_of_msgs = 1u;
        }

        count = 0u;

        msg_len = loop_count;
        error_flag = 0u;
        while ((no_of_msgs != 0u) && (0u == error_flag))
        {
             /* Pack up to 8 bytes into this packet in 2 x 32bit chunks */
            if (msg_len >= 8u)
            {
                chunk_size = 8u;
            }
            else
            {
                chunk_size = msg_len;
            }

            for (loop_count = 0u; loop_count < chunk_size; loop_count++)
            {
                if (loop_count < 4u)
                {
                    pmsg.DATA[3u - loop_count] =  \
                                       g_uart_to_can[loop_count +(count * 8u)];
                }
                else
                {
                    pmsg.DATA[11u - loop_count] = \
                                       g_uart_to_can[loop_count +(count * 8u)];
                }
            }

            pmsg.DLC = chunk_size;
            ret_status = MSS_CAN_send_message_n(g_mss_can_0, 0u, &pmsg);
            if (CAN_VALID_MSG != ret_status)
            {
                error_flag = 1u; /* Didn't succeed in sending packet... */
            }
            else
            {
                /* Wait around for this packet to send before going any
                 * further */
                tx_status = MSS_CAN_get_tx_buffer_status(g_mss_can_0);
                while (1u == (tx_status & 1))
                {
                    tx_status = MSS_CAN_get_tx_buffer_status(g_mss_can_0);
                }

                no_of_msgs--;
                msg_len -= chunk_size;
                count++;
            }
        }

        if (0u == count) /* Nothing sent */
        {
            MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rUnable to send data via CAN Bus");
        }
        else
        {
            if (0u == error_flag) /* Everything sent */
            {
                MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rObserve the data received on CAN Analyzer");
                MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rIt should be same as the data transmitted from UART terminal");
            }
            else /* Some error occurred */
            {
                MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rObserve the data Received on CAN Analyzer");
                MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rSome transmission error(s) were detected.");
            }
        }

        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r------------------------------------------------------------------------------");
        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\rPress any key to continue...");

        do {
            rx_size = MSS_UART_get_rx(g_uart, &rx_char, sizeof(rx_char));
        } while (rx_size == 0u);

         MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r");

       /*----------------------------------------------------------------------
        *  Display options
        */
        display_option();
    }

}


static void check_rx_buffer(void)
{
    uint8_t loop_count;

    /*----------------------------------------------------------------------
     * Read the Data from CAN channel and transmit through UART
     */
    if (CAN_VALID_MSG == MSS_CAN_get_message_n(g_mss_can_0, 0u, &rx_buf))
    {
        for (loop_count = 0u; loop_count < rx_buf.DLC; loop_count++)
        {
            if (loop_count < 4u)
            {
                g_can_to_uart[loop_count] = rx_buf.DATA[3u - loop_count];
            }
            else
            {
                g_can_to_uart[loop_count] = rx_buf.DATA[11u - loop_count];
            }
        }

        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r******************************************************************************\n\r");
        MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rData Received as CAN Message is ");
        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r");

        /* Send to UART */
        display_hex_values(g_can_to_uart, rx_buf.DLC);
        MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rObserve the message sent from the CAN Analyzer ");
        MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rIt should be same as message Received on UART terminal");
        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r******************************************************************************\n\r");
    }
}

/*------------------------------------------------------------------------------
 * Receive data from UART terminal.
 */
static uint8_t get_data_frm_uart(void)
{
    uint8_t complete = 0;
    uint8_t rx_buff[1];
    uint8_t count = 0;
    uint8_t rx_size = 0;

    for (count = 0u; count < 32u; count++)
    {
        g_uart_to_can[count] = 0u;
    }

    MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rEnter the data to transmit through the CAN Channel:\n\r");

    count = 0u;
    while (!complete)
    {
        rx_size = MSS_UART_get_rx(g_uart, rx_buff, sizeof(rx_buff));
        if(rx_size > 0u)
        {
            MSS_UART_polled_tx(g_uart, rx_buff, sizeof(rx_buff));

            if (ENTER == rx_buff[0])
            {
                complete = 1u;
            }
            else
            {
                if (count % 2u == 0u)
                {
                    g_temp[count] = rx_buff[0];
                }
                else
                {
                    g_temp[count] = rx_buff[0];
                }
                count++;
            }

            if (64u == count)
            {
                complete = 1u;
            }
        }
    }
    return(count);
}

/*------------------------------------------------------------------------------
 * Display greeting message when application is started.
 */
static void display_greeting(void)
{
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r******************************************************************************\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"*********** PolarFire SoC MSS CAN Driver Example (FullCAN Mode) **************\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"******************************************************************************\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Example project Demonstrates the using of MSS CAN Transmission and Reception \n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"------------------------------------------------------------------------------\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Read data from the UART1 and Transmit as CAN message using MSS CAN\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"------------------------------------------------------------------------------\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Receive the CAN Message from MSS CAN channel and send this to UART1\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"******************************************************************************\n\r");
}

/*------------------------------------------------------------------------------
 * Display content of buffer passed as parameter as hex values.
 */
static void display_hex_values
(
    const uint8_t * in_buffer,
    uint32_t byte_length
)
{
    uint8_t display_buffer[128];
    uint32_t inc;

    if (0u == byte_length)
    {
        MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"<No data present>\n\r");
    }
    else
    {
        if (byte_length > 16u)
        {
            MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r");
        }

        for (inc = 0u; inc < byte_length; ++inc)
        {
            if ((inc > 1u) && (0u == (inc % 16u)))
            {
                MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r");
            }
            snprintf((char *)display_buffer, sizeof(display_buffer), "%02x ", in_buffer[inc]);
            MSS_UART_polled_tx_string(g_uart, display_buffer);
        }
    }
}

/*------------------------------------------------------------------------------
 * Converts ASCII values to HEX values
 */
static void ascii_to_hex
(
    uint8_t * in_buffer,
    uint32_t byte_length
)
{
    uint32_t inc;

    for (inc = 0u; inc < byte_length; inc++)
    {
        if ((in_buffer[inc] <= 0x39u) && (in_buffer[inc] >= 0x30u))
        {
            in_buffer[inc] = in_buffer[inc] - 0x30u;
        }
        else if ((in_buffer[inc] <= 0x5Au) && (in_buffer[inc] >= 0x41u))
        {
            in_buffer[inc] = 0x0Au + in_buffer[inc] - 0x41u;
        }
        else if ((in_buffer[inc] <= 0x7Au) && (in_buffer[inc] >= 0x61u))
        {
            in_buffer[inc] = 0x0au + (in_buffer[inc] - 0x61u);
        }
        else
        {
            ;/* Do Nothing. */
        }
    }
}

/*------------------------------------------------------------------------------
 * Display the Option to continue or exit.
 */
static void display_option(void)
{
    uint8_t rx_size=0;
    uint8_t rx_buff[1];

    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"\n\r******************* Select the Option to proceed further *********************\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Press Key '7' to send data.\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Press Key '5' to reinitalize MSS CAN device.\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"Press Key '0' to reset the MSS CAN device using SYSREG.\n\r");
    MSS_UART_polled_tx_string(g_uart, (const uint8_t*)"******************************************************************************\n\r");
    do
    {
        /* Start command line interface if any key is pressed. */
        rx_size = MSS_UART_get_rx(g_uart, rx_buff, sizeof(rx_buff));
        if (rx_size > 0u)
        {
            switch(rx_buff[0])
            {
                case '7':
                break;

                case '5':
#ifdef CAN_TARGET_COCO_PC_ACTIVE
                    /*
                     * Tweaked 20kbs configuration for a 16MHz PCLK1 which works with the
                     * CoCo PC Active device to allow transmit and receive functionality be
                     * observed. See comment in main() for details.
                     */
                    MSS_CAN_init(g_mss_can_0,
                                 CAN_SET_BITRATE(48)|CAN_SET_TSEG1(11)|CAN_SET_TSEG2(2)|CAN_THREE_SAMPLES,
                                 (PCAN_CONFIG_REG)0,
                                 6,
                                 6);
#else
                    MSS_CAN_init(g_mss_can_0, CAN_SPEED_16M_1M,
                                (pmss_can_config_reg)0, 6u, 6u);
#endif
                    MSS_CAN_set_mode(g_mss_can_0, CANOP_MODE_NORMAL);
                    MSS_CAN_start(g_mss_can_0);
                    MSS_CAN_config_buffer_n(g_mss_can_0, 0, &rx_msg);
                break;

                case '0':
                    MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rCAN Controller has been reset: \n\r");
                    MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rNo more Data transfer through CAN: \n\r");
                    MSS_CAN_set_mode(g_mss_can_0, CANOP_SW_RESET);
                    MSS_UART_polled_tx_string(g_uart, (const uint8_t *)"\n\rPress Key '5' to re-initialize the CAN Controller \n\r");
                break;

                default:
                break;
            }
        }

        /*----------------------------------------------------------------------
         *  Read the Data from CAN channel and Transmit Through UART1
         */
        check_rx_buffer();

    }while ((rx_buff[0]!= '7') & (rx_buff[0]!= '5'));
}
