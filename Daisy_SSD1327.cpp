#include "Daisy_SSD1327.h"

DaisyPatchSM _patch;

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
    SSD1327_GRAYTABLE,
    1, 2, 3, 4, 
    5, 7, 10, 14, 
    18, 22, 27, 33, 
    40, 47, 55, 63, 
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

void Daisy_SSD1327::init(SpiHandle spi_handle, dsy_gpio_pin dc_pin_id, uint8_t *buffer, DaisyPatchSM patch) {
    dc_pin.mode = DSY_GPIO_MODE_OUTPUT_PP;
    dc_pin.pull = DSY_GPIO_NOPULL;
    dc_pin.pin  = dc_pin_id;
    dsy_gpio_init(&dc_pin);

    this->spi_handle = spi_handle;

    _draw_area_command_buffer = buffer;
    _display_buffer = buffer + SSD1327_SET_DRAW_AREA_BUFFER_SIZE;

    _patch = patch;
    _render_context.dc_pin = dc_pin;
    _render_context.spi_handle = spi_handle;
    _render_context.display_buffer = _display_buffer;

    dsy_gpio_write(&dc_pin, false);
    spi_handle.BlockingTransmit(init_128x128, sizeof(init_128x128), 1000);

    System::Delay(100);
    uint8_t displayCmd[1] = {SSD1327_DISPLAYON};
    spi_handle.BlockingTransmit(displayCmd, 1, 1000);

    uint8_t setContrastCmds[2] = {SSD1327_SETCONTRAST, 0x4F};
    spi_handle.BlockingTransmit(setContrastCmds, 2, 1000);
}

SpiHandle::Config Daisy_SSD1327::getSpiConfig(dsy_gpio_pin sclk, dsy_gpio_pin mosi, dsy_gpio_pin miso, dsy_gpio_pin nss) {
    // TODO: Is this a memory leak?
    SpiHandle::Config spi_conf;

    spi_conf.periph = SpiHandle::Config::Peripheral::SPI_2;
    spi_conf.mode = SpiHandle::Config::Mode::MASTER;
    spi_conf.direction = SpiHandle::Config::Direction::TWO_LINES;
    spi_conf.clock_polarity = SpiHandle::Config::ClockPolarity::HIGH;
    spi_conf.clock_phase = SpiHandle::Config::ClockPhase::TWO_EDGE;
    spi_conf.nss = SpiHandle::Config::NSS::HARD_OUTPUT;
    spi_conf.pin_config.sclk = sclk;
    spi_conf.pin_config.mosi = mosi;
    spi_conf.pin_config.miso = miso;
    spi_conf.pin_config.nss = nss;

    return spi_conf;
}

void Daisy_SSD1327::clear(uint8_t colour) {
    if (colour > SSD1327_WHITE) colour = SSD1327_WHITE;

    memset (_display_buffer, (colour << 4 | colour), SSD1327_DISPLAY_BUFFER_SIZE);

    dirty_window_min_x = 0;
    dirty_window_max_x = SSD1327_LCD_WIDTH - 1;
    dirty_window_min_y = 0;
    dirty_window_max_y = SSD1327_LCD_HEIGHT - 1;
}

// Set the pixel to the new colour ONLY if it's lighter
void Daisy_SSD1327::lightenPixel(uint8_t x, uint8_t y, uint8_t colour) {
    // TODO: Debug print for out of bounds
    uint8_t *buf_target = &_display_buffer[x/2 + (y*SSD1327_LCD_HALF_WIDTH)];
    
    uint8_t original_left_pixel = *buf_target & 0xF0;
    uint8_t original_right_pixel = *buf_target & 0x0F;

    if (x % 2 == 0) { // even, left pixel
        uint8_t new_left_pixel = (colour & 0x0F) << 4;
        if (new_left_pixel < original_left_pixel) return;

        *buf_target = new_left_pixel | original_right_pixel;
    } else { // odd, right pixel
        uint8_t new_right_pixel = colour & 0x0F;
        if (new_right_pixel < original_right_pixel) return;

        *buf_target = new_right_pixel | original_left_pixel;
    }

    dirty_window_min_x = std::min(dirty_window_min_x, x);
    dirty_window_max_x = std::max(dirty_window_max_x, x);
    dirty_window_min_y = std::min(dirty_window_min_y, y);
    dirty_window_max_y = std::max(dirty_window_max_y, y);
}

void Daisy_SSD1327::setPixel(uint8_t x, uint8_t y, uint8_t colour) {
    // TODO: Debug print for out of bounds
    uint8_t *buf_target = &_display_buffer[x/2 + (y*SSD1327_LCD_HALF_WIDTH)];
    
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
    RenderContext *context = static_cast<RenderContext*>(uncastContext);

    if (context->y > context->max_y) {
        context->isRendering = false;
        return;
    }

    dsy_gpio_write(&context->dc_pin, true);

    uint8_t *data_start = context->display_buffer + context->min_x_byte + (context->y*SSD1327_LCD_HALF_WIDTH);
    context->y++;

    context->spi_handle.DmaTransmit(
        data_start, 
        context->bytes_per_row, 
        NULL, 
        _display,
        uncastContext
    );
}

void Daisy_SSD1327::display() {
    if (dirty_window_min_x > dirty_window_max_x) {
        return;
    }

    uint8_t min_x_byte = dirty_window_min_x / 2;
    uint8_t max_x_byte = dirty_window_max_x / 2;
    size_t bytes_per_row = 1 + max_x_byte - min_x_byte;

    uint8_t tmp[] = {  
        SSD1327_SETROW,    dirty_window_min_y, dirty_window_max_y,
        SSD1327_SETCOLUMN, min_x_byte, max_x_byte,
    };
    memcpy(_draw_area_command_buffer, tmp, sizeof(tmp));

    _render_context.min_x_byte = min_x_byte;
    _render_context.y = dirty_window_min_y;
    _render_context.max_y = dirty_window_max_y;
    _render_context.bytes_per_row = bytes_per_row;
    _render_context.isRendering = true;

    dsy_gpio_write(&dc_pin, false);
    spi_handle.DmaTransmit(
        _draw_area_command_buffer,
        SSD1327_SET_DRAW_AREA_BUFFER_SIZE,
        NULL, 
        _display, 
        &_render_context
    );

    // Reset dirty window
    dirty_window_min_x = UINT8_MAX;
    dirty_window_max_x = 0;
    dirty_window_min_y = UINT8_MAX;
    dirty_window_max_y = 0;
}

bool Daisy_SSD1327::isRendering() {
    return _render_context.isRendering;
}