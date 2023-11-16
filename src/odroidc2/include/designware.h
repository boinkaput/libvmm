/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (C) Copyright 2010
 * Vipin Kumar, ST Micoelectronics, vipin.kumar@st.com.
 */

#pragma once

#define CONFIG_TX_DESCR_NUM 16
#define CONFIG_RX_DESCR_NUM 16
#define CONFIG_ETH_BUFSIZE  2048
#define TX_TOTAL_BUFSIZE    (CONFIG_ETH_BUFSIZE * CONFIG_TX_DESCR_NUM)
#define RX_TOTAL_BUFSIZE    (CONFIG_ETH_BUFSIZE * CONFIG_RX_DESCR_NUM)

#define CONFIG_MACRESET_TIMEOUT (3 * CONFIG_SYS_HZ)
#define CONFIG_MDIO_TIMEOUT (3 * CONFIG_SYS_HZ)

/* MAC configuration register definitions */
#define FRAMEBURSTENABLE    (1 << 21)
#define MII_PORTSELECT      (1 << 15)
#define FES_100         (1 << 14)
#define DISABLERXOWN        (1 << 13)
#define FULLDPLXMODE        (1 << 11)
#define RXENABLE        (1 << 2)
#define TXENABLE        (1 << 3)

/* MII address register definitions */
#define MII_BUSY        (1 << 0)
#define MII_WRITE       (1 << 1)
#define MII_CLKRANGE_60_100M    (0)
#define MII_CLKRANGE_100_150M   (0x4)
#define MII_CLKRANGE_20_35M (0x8)
#define MII_CLKRANGE_35_60M (0xC)
#define MII_CLKRANGE_150_250M   (0x10)
#define MII_CLKRANGE_250_300M   (0x14)

#define MIIADDRSHIFT        (11)
#define MIIREGSHIFT     (6)
#define MII_REGMSK      (0x1F << 6)
#define MII_ADDRMSK     (0x1F << 11)
