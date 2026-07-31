#pragma once

#define ILI9341_2_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1

#define USE_HSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
