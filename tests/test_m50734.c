#include "test.h"
#include "machine/m50734.h"

static m50734 g_p;
static const uint8_t k_in[5] = { 0x00, 0x00, 0x70, 0x06, 0x0A };

static void test_ports(void)
{
    m50734_reset(&g_p, k_in);
    CHECK_EQ(m50734_read(&g_p, M50734_P2), 0x70);          /* all inputs: pins follow the board */
    m50734_write(&g_p, M50734_P2, 0xF1);
    m50734_write(&g_p, M50734_P2_DIR, 0xFD);
    CHECK_EQ(m50734_port_pins(&g_p, 2), 0xF1);             /* bit 1 is an input and reads 0 */
    CHECK_EQ(m50734_read(&g_p, M50734_P2), 0xF1);
    CHECK_EQ(m50734_read(&g_p, M50734_P2_DIR), 0xFD);
    CHECK_EQ(m50734_read(&g_p, M50734_P3) & 0x06, 0x06);   /* P3 jumpers */
    CHECK_EQ(m50734_read(&g_p, M50734_P4), 0x0A);
}

static void test_timer3_period(void)
{
    m50734_reset(&g_p, k_in);
    m50734_write(&g_p, 0xE0, 0x00);                        /* timer 3 prescaler */
    m50734_write(&g_p, 0xE1, 0x31);                        /* reload: 4*(0+1)*(0x31+1) = 200 cycles */
    m50734_tick(&g_p, 199);
    CHECK_EQ(g_p.icr2 & 0x20, 0);
    m50734_tick(&g_p, 1);
    CHECK_EQ(g_p.icr2 & 0x20, 0x20);                       /* request set even while disabled */
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    m50734_write(&g_p, M50734_ICR2, 0x30);                 /* enable + request */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_TIMER);
    m50734_write(&g_p, M50734_ICR2, 0x10);                 /* ISR clears the request (CLB5 $FE) */
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    m50734_tick(&g_p, 200);
    CHECK_EQ(g_p.icr2 & 0x20, 0x20);                       /* fires again one period later */
    m50734_write(&g_p, M50734_ICR2, 0xFF);
    CHECK_EQ(m50734_read(&g_p, M50734_ICR2), 0x3F);        /* ICR2 is 6 bits wide */
}

static void test_uart_rx_tx(void)
{
    m50734_reset(&g_p, k_in);
    CHECK(m50734_uart_receive(&g_p, 'A'));
    CHECK_EQ(m50734_irq_lines(&g_p), 0);                   /* RX interrupt not enabled */
    CHECK_EQ(m50734_read(&g_p, M50734_UART_STATUS), 0x03); /* RX full + TX ready */
    m50734_write(&g_p, M50734_UART_CTRL, 0xD1);            /* the ROM's value: bit 4 = RX int */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_INT1);
    CHECK(!m50734_uart_receive(&g_p, 'B'));                /* previous byte unread */
    CHECK_EQ(m50734_read(&g_p, M50734_UART_RX), 'A');
    CHECK_EQ(m50734_irq_lines(&g_p), 0);
    CHECK_EQ(m50734_read(&g_p, M50734_UART_STATUS), 0x02);
    m50734_write(&g_p, M50734_UART_TX, 0x55);
    CHECK_EQ(g_p.uart_tx_count, 1);
    CHECK_EQ(g_p.uart_tx_last, 0x55);
}

static void test_misc_registers(void)
{
    m50734_reset(&g_p, k_in);
    m50734_write(&g_p, M50734_ICR1, 0x03);                 /* _INT1 enable + request */
    CHECK_EQ(m50734_irq_lines(&g_p), 1u << M50734_LINE_INT1);
    CHECK(m50734_is_modelled(0xE5));
    CHECK(m50734_is_modelled(0xFF));
    CHECK(!m50734_is_modelled(0xDA));
    CHECK(!m50734_is_modelled(0xFC));
    m50734_write(&g_p, 0xDA, 0x12);
    CHECK_EQ(m50734_read(&g_p, 0xDA), 0x12);               /* unmodelled: plain storage */
    m50734_write(&g_p, M50734_AD_CTRL, 0x01);
    CHECK_EQ(m50734_read(&g_p, M50734_AD_CTRL), 0x05);     /* conversion completes at once */
    m50734_write(&g_p, M50734_P23_FUNC, 0xFF);
    CHECK_EQ(m50734_read(&g_p, M50734_P23_FUNC), 0xC7);
    CHECK(m50734_is_write_only(M50734_UART_TX));
    CHECK(!m50734_is_write_only(M50734_UART_RX));
}

void suite_m50734(void)
{
    test_ports();
    test_timer3_period();
    test_uart_rx_tx();
    test_misc_registers();
}
