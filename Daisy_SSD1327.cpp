#include "Daisy_SSD1327.h"

RenderContext _render_context;
uint8_t DMA_BUFFER_MEM_SECTION _buffer[SSD1327_BUFFERSIZE];
uint8_t DMA_BUFFER_MEM_SECTION _set_draw_area[6];

uint8_t init_128x128[] = {
    // Init sequence for 128x32 OLED module
    0x00,
    SSD1327_DISPLAYOFF, // 0xAE
    SSD1327_SETCONTRAST,
    0x80,             // 0x81, 0x80
    SSD1327_SEGREMAP, // 0xA0 0x53
    0x51, // remap memory, odd even columns, com flip and column swap
    SSD1327_SETSTARTLINE,
    0x00, // 0xA1, 0x00
    SSD1327_SETDISPLAYOFFSET,
    0x00, // 0xA2, 0x00
    SSD1327_DISPLAYALLOFF, SSD1327_SETMULTIPLEX,
    0x7F, // 0xA8, 0x7F (1/64)
    SSD1327_PHASELEN,
    0x11, // 0xB1, 0x11
    //   SSD1327_GRAYTABLE,
    //   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    //   0x07, 0x08, 0x10, 0x18, 0x20, 0x2f, 0x38, 0x3f,
    SSD1327_DCLK,
    0x00, // 0xb3, 0x00 (100hz)
    SSD1327_REGULATOR,
    0x01, // 0xAB, 0x01
    SSD1327_PRECHARGE2,
    0x04, // 0xB6, 0x04
    SSD1327_SETVCOM,
    0x0F, // 0xBE, 0x0F
    SSD1327_PRECHARGE,
    0x08, // 0xBC, 0x08
    SSD1327_FUNCSELB,
    0x62, // 0xD5, 0x62
    SSD1327_CMDLOCK,
    0x12, // 0xFD, 0x12
    SSD1327_NORMALDISPLAY, 
    SSD1327_DISPLAYON
};

Daisy_SSD1327::Daisy_SSD1327() {

}

void Daisy_SSD1327::init(SpiHandle spi_handle, dsy_gpio dc_pin) {
    this->dc_pin = dc_pin;
    this->spi_handle = spi_handle;

    _render_context.dc_pin = dc_pin;
    _render_context.spi_handle = spi_handle;

    dsy_gpio_write(&dc_pin, false);
    spi_handle.BlockingTransmit(init_128x128, sizeof(init_128x128), 1000);
}

void Daisy_SSD1327::clear(uint8_t colour) {
    if (colour > SSD1327_WHITE) colour = SSD1327_WHITE;

    memset (_buffer, (colour << 4 | colour), SSD1327_BUFFERSIZE);

    dirty_window_min_x = 0;
    dirty_window_max_x = SSD1327_LCD_WIDTH - 1;
    dirty_window_min_y = 0;
    dirty_window_max_y = SSD1327_LCD_HEIGHT - 1;
}

void Daisy_SSD1327::setPixel(uint8_t x, uint8_t y, uint8_t colour) {
    // TODO: Debug print for out of bounds
    uint8_t *buf_target = &_buffer[x/2 + (y*SSD1327_LCD_HALF_WIDTH)];
    
    dirty_window_min_x = std::min(dirty_window_min_x, x);
    dirty_window_max_x = std::max(dirty_window_max_x, x);
    dirty_window_min_y = std::min(dirty_window_min_y, y);
    dirty_window_max_y = std::max(dirty_window_max_y, y);

    if (x % 2 == 0) { // even, left pixel
        uint8_t new_left_pixel = (colour & 0x0F) << 4;
        uint8_t original_right_pixel = *buf_target & 0x0F;
        *buf_target = new_left_pixel | original_right_pixel;
    } else { // odd, right pixel
        uint8_t new_right_pixel = colour & 0x0F;
        uint8_t original_left_pixel = *buf_target & 0xF0;
        *buf_target = new_right_pixel | original_left_pixel;
    }
}

void Daisy_SSD1327::_display(void *uncastContext, SpiHandle::Result result) {
    if (_render_context.y > _render_context.max_y) {
        _render_context.isRendering = false;
        return;
    }

    dsy_gpio_write(&_render_context.dc_pin, true);

    uint8_t *data_start = _buffer + _render_context.min_x_byte + (_render_context.y*SSD1327_LCD_HALF_WIDTH);
    _render_context.y++;

    _render_context.spi_handle.DmaTransmit(
        data_start, 
        _render_context.bytes_per_row, 
        NULL, 
        _display,
        NULL
    );
}

void Daisy_SSD1327::display() {
    if (dirty_window_min_x > dirty_window_max_x) {
        // patch.PrintLine("Nothing to draw");
        return;
    }

    uint8_t min_x_byte = dirty_window_min_x / 2;
    uint8_t max_x_byte = dirty_window_max_x / 2;
    size_t bytes_per_row = 1 + max_x_byte - min_x_byte;

    uint8_t tmp[] = {  
        SSD1327_SETROW,    dirty_window_min_y, dirty_window_max_y,
        SSD1327_SETCOLUMN, min_x_byte, max_x_byte,
    };
    memcpy(_set_draw_area, tmp, sizeof(tmp));

    _render_context.min_x_byte = min_x_byte;
    _render_context.y = dirty_window_min_y;
    _render_context.max_y = dirty_window_max_y;
    _render_context.bytes_per_row = bytes_per_row;
    _render_context.isRendering = true;

    // Reset dirty window
    dirty_window_min_x = UINT8_MAX;
    dirty_window_max_x = 0;
    dirty_window_min_y = UINT8_MAX;
    dirty_window_max_y = 0;

    dsy_gpio_write(&dc_pin, false);
    spi_handle.DmaTransmit(_set_draw_area, sizeof(_set_draw_area), NULL, _display, NULL);
}

bool Daisy_SSD1327::isRendering() {
    return _render_context.isRendering;
}