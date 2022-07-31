#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM patch;

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

#define SSD1327_BUFFERSIZE (SSD1327_LCD_HEIGHT * SSD1327_LCD_WIDTH / 2)

uint8_t DMA_BUFFER_MEM_SECTION buffer[SSD1327_BUFFERSIZE];
uint8_t dirty_window_min_x = UINT8_MAX;
uint8_t dirty_window_max_x = 0;
uint8_t dirty_window_min_y = UINT8_MAX;
uint8_t dirty_window_max_y = 0;

SpiHandle spi;
dsy_gpio dc_pin;
I2CHandle i2c;

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

uint8_t data[2] = {};

void SSD1327_Clear (uint8_t colour)
{
	if (colour > SSD1327_WHITE) colour = SSD1327_WHITE;

	memset (buffer, (colour << 4 | colour), SSD1327_BUFFERSIZE);

    dirty_window_min_x = 0;
    dirty_window_max_x = SSD1327_LCD_WIDTH - 1;
    dirty_window_min_y = 0;
    dirty_window_max_y = SSD1327_LCD_HEIGHT - 1;
}

void setPixel(uint8_t x, uint8_t y, uint8_t colour) {
    // TODO: Debug for out of bounds, etc
    uint8_t *buf_target = &buffer[x/2 + (y*SSD1327_LCD_HALF_WIDTH)];
    
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

uint8_t DMA_BUFFER_MEM_SECTION set_draw_area[] = {  
    SSD1327_SETROW,    0x00, 0x7F,
    SSD1327_SETCOLUMN, 0x00, 0x3F,
};

struct RenderContext {
    uint8_t min_x_byte;
    uint8_t y;
    uint8_t max_y;
    uint8_t bytes_per_row;
};

RenderContext render_context;

bool isRendering = false;

void display(void *uncastContext, SpiHandle::Result result) {
    // TODO: Off by one?
    if (render_context.y > render_context.max_y) {
        isRendering = false;
        return;
    }

    dsy_gpio_write(&dc_pin, true);

    uint8_t *data_start = buffer + render_context.min_x_byte + (render_context.y*SSD1327_LCD_HALF_WIDTH);
    render_context.y++;

    spi.DmaTransmit(
        data_start, 
        render_context.bytes_per_row, 
        NULL, 
        display,
        NULL
    );
}

void SSD1327_Display (void)
{
    if (dirty_window_min_x > dirty_window_max_x) {
        patch.PrintLine("Nothing to draw");
        return;
    }

    isRendering = true;

    uint8_t min_x_byte = dirty_window_min_x / 2;
    uint8_t max_x_byte = dirty_window_max_x / 2;
    size_t bytes_per_row = 1 + max_x_byte - min_x_byte;

    set_draw_area[1] = dirty_window_min_y;
    set_draw_area[2] = dirty_window_max_y;
    set_draw_area[4] = min_x_byte;
    set_draw_area[5] = max_x_byte;

    render_context.min_x_byte = min_x_byte;
    render_context.y = dirty_window_min_y;
    render_context.max_y = dirty_window_max_y;
    render_context.bytes_per_row = bytes_per_row;

    // Reset dirty window
    dirty_window_min_x = UINT8_MAX;
    dirty_window_max_x = 0;
    dirty_window_min_y = UINT8_MAX;
    dirty_window_max_y = 0;

    dsy_gpio_write(&dc_pin, false);
    spi.DmaTransmit(set_draw_area, sizeof(set_draw_area), NULL, display, NULL);
}

float minOutL = 0.f;
float maxOutL = 0.f;
int y = 0;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    minOutL = 100000000.f;
    maxOutL = -100000000.f;
    for(size_t i = 0; i < size; i++)
    {
        minOutL = std::min(in[0][i], minOutL);
        maxOutL = std::max(in[0][i], maxOutL);

        out[0][i] = in[0][i];
    }

    int minX = minOutL * 10 * 64 + 64; 
    int maxX = maxOutL * 10 * 64 + 64; 

    for (int x = 0; x < SSD1327_LCD_WIDTH; x++) {
        uint8_t new_colour = (x >= minX && x <= maxX) ? 0xF : 0x0;

        setPixel(x, y, new_colour);
    }

    y = (y + 1) % (SSD1327_LCD_HEIGHT);
}

uint32_t last_render_millis = 0;

int main(void)
{
    patch.Init();
    patch.StartLog();

    dc_pin.mode = DSY_GPIO_MODE_OUTPUT_PP;
    dc_pin.pull = DSY_GPIO_NOPULL;
    dc_pin.pin  = patch.B7;
    dsy_gpio_init(&dc_pin);

    // setup the configuration
    SpiHandle::Config spi_conf;
    spi_conf.periph = SpiHandle::Config::Peripheral::SPI_2;
    spi_conf.mode = SpiHandle::Config::Mode::MASTER;
    spi_conf.direction = SpiHandle::Config::Direction::TWO_LINES;
    spi_conf.clock_polarity = SpiHandle::Config::ClockPolarity::HIGH;
    spi_conf.clock_phase = SpiHandle::Config::ClockPhase::TWO_EDGE;
    spi_conf.nss = SpiHandle::Config::NSS::HARD_OUTPUT; // TODO: What's this? 
    spi_conf.pin_config.sclk = patch.D10;
    spi_conf.pin_config.mosi = patch.D9;
    spi_conf.pin_config.miso = patch.D8;
    spi_conf.pin_config.nss = patch.D1;

    // initialise the peripheral
    SpiHandle::Result result = spi.Init(spi_conf);

    dsy_gpio_write(&dc_pin, false);
    spi.BlockingTransmit(init_128x128, sizeof(init_128x128), 1000);

    SSD1327_Clear(SSD1327_BLACK);

    patch.StartAudio(AudioCallback);

    while(1) {
        if (System::GetNow() - last_render_millis > 8 && !isRendering) {
            last_render_millis = System::GetNow();
            SSD1327_Display();
        }
    }
} 