#ifndef DAISY_SSD1327_H
#define DAISY_SSD1327_H

#include "daisy_patch_sm.h"
using namespace daisy;
using namespace patch_sm;

#define SSD1327_BLACK 0x0
#define SSD1327_WHITE 0xF

#define SSD1327_I2C_ADDRESS 0x3D

#define SSD1305_SETBRIGHTNESS 0x82
#define SSD1327_SETCOLUMN 0x15
#define SSD1327_SETROW 0x75
#define SSD1327_SETCONTRAST 0x81
#define SSD1305_SETLUT 0x91

#define SSD1327_SEGREMAP 0xA0
#define SSD1327_SETSTARTLINE 0xA1
#define SSD1327_SETDISPLAYOFFSET 0xA2
#define SSD1327_NORMALDISPLAY 0xA4
#define SSD1327_DISPLAYALLON 0xA5
#define SSD1327_DISPLAYALLOFF 0xA6
#define SSD1327_INVERTDISPLAY 0xA7
#define SSD1327_SETMULTIPLEX 0xA8
#define SSD1327_REGULATOR 0xAB
#define SSD1327_DISPLAYOFF 0xAE
#define SSD1327_DISPLAYON 0xAF

#define SSD1327_PHASELEN 0xB1
#define SSD1327_DCLK 0xB3
#define SSD1327_PRECHARGE2 0xB6
#define SSD1327_GRAYTABLE 0xB8
#define SSD1327_PRECHARGE 0xBC
#define SSD1327_SETVCOM 0xBE

#define SSD1327_FUNCSELB 0xD5

#define SSD1327_CMDLOCK 0xFD

#define SSD1327_LCD_WIDTH 128
#define SSD1327_LCD_HALF_WIDTH 64
#define SSD1327_LCD_HEIGHT 128

#define SSD1327_DISPLAY_BUFFER_SIZE (SSD1327_LCD_HEIGHT * SSD1327_LCD_WIDTH / 2)
#define SSD1327_SET_DRAW_AREA_BUFFER_SIZE 6

// Includes 6 bytes for the column and row selection commands
#define SSD1327_REQUIRED_DMA_BUFFER_SIZE (SSD1327_DISPLAY_BUFFER_SIZE + SSD1327_SET_DRAW_AREA_BUFFER_SIZE)

struct RenderContext {
    uint8_t min_x_byte;
    uint8_t y;
    uint8_t max_y;
    uint8_t bytes_per_row;
    bool isRendering;
    dsy_gpio dc_pin;
    SpiHandle spi_handle;
    uint8_t *display_buffer;
};

class Daisy_SSD1327 {
    public: 
        Daisy_SSD1327();

        static const uint8_t width = SSD1327_LCD_WIDTH;
        static const uint8_t height = SSD1327_LCD_HEIGHT;

        void init(SpiHandle spi_handle, dsy_gpio_pin dc_pin_id, uint8_t *buffer, DaisyPatchSM patch);
        void clear(uint8_t colour);
        void setPixel(uint8_t x, uint8_t y, uint8_t colour);
        void display();

        SpiHandle::Config getSpiConfig(dsy_gpio_pin sclk, dsy_gpio_pin mosi, dsy_gpio_pin miso, dsy_gpio_pin nss);

        bool isRendering();

        uint8_t dirty_window_min_x = UINT8_MAX;
        uint8_t dirty_window_max_x = 0;
        uint8_t dirty_window_min_y = UINT8_MAX;
        uint8_t dirty_window_max_y = 0;

        SpiHandle spi_handle;
        dsy_gpio dc_pin;

    private:
        static void _display(void *uncastContext, SpiHandle::Result result);
    
        uint8_t *_draw_area_command_buffer;
        uint8_t *_display_buffer;
        RenderContext _render_context;
};

#endif