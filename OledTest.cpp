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

uint8_t _buffer[SSD1327_BUFFERSIZE + 1] = { 0x40 };
uint8_t *buffer = _buffer + 1;
uint8_t _drawBuffer[SSD1327_BUFFERSIZE + 1] = { 0x40 };
uint8_t *drawBuffer = _drawBuffer + 1;
uint8_t dirty_window_min_x = UINT8_MAX;
uint8_t dirty_window_max_x = 0;
uint8_t dirty_window_min_y = UINT8_MAX;
uint8_t dirty_window_max_y = 0;

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

void fillStuff() {
    for (int y = 0; y < SSD1327_LCD_HEIGHT; y++) {
        for (int x = 0; x < SSD1327_LCD_HALF_WIDTH; x++) {
            u_int8_t colour = (y / 8) % (SSD1327_WHITE + 1);

            buffer[y * SSD1327_LCD_HALF_WIDTH + x] = (colour << 4 | colour);
        }
    }

    // for (int i = 0; i < SSD1327_BUFFERSIZE; i++) {
    //     u_int8_t colour = (seed + i) % (SSD1327_WHITE + 1);

    //     buffer[i] = (colour << 4 | colour);
    // }
}

void square(u_int8_t colour) {
    for (int y = 32; y < SSD1327_LCD_HEIGHT - 32; y++) {
        for (int x = 16; x < SSD1327_LCD_HALF_WIDTH - 16; x++) {
            buffer[y * SSD1327_LCD_HALF_WIDTH + x] = (colour << 4 | colour);
        }
    }
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
        uint8_t original_left_pixel = *buf_target & 0xF0;
        uint8_t new_right_pixel = colour & 0x0F;
        *buf_target = new_right_pixel | original_left_pixel;
    }
}

void SSD1327_Display (void)
{
    if (dirty_window_min_x > dirty_window_max_x) {
        patch.PrintLine("Nothing to draw");
        return;
    }

    uint8_t min_x_byte = dirty_window_min_x / 2;
    uint8_t max_x_byte = dirty_window_max_x / 2;

    // TODO: Why does it ignore the first command in this list? Does it think it's still data?
    uint8_t set_draw_area[] = {  
        SSD1327_SETCOLUMN, min_x_byte, max_x_byte,
        SSD1327_SETROW,    dirty_window_min_y, dirty_window_max_y,
        SSD1327_SETCOLUMN, min_x_byte, max_x_byte
    };

    i2c.TransmitBlocking(SSD1327_I2C_ADDRESS, set_draw_area, sizeof(set_draw_area), 100000);

    size_t bytes_per_row = 1 + max_x_byte - min_x_byte;
    size_t total_bytes = bytes_per_row * (1 + dirty_window_max_y - dirty_window_min_y);
    size_t write_head = 0;
    for (uint8_t y = dirty_window_min_y; y <= dirty_window_max_y; y++) {
        memcpy(
            drawBuffer + write_head,
            buffer + min_x_byte + (y*SSD1327_LCD_HALF_WIDTH),
            bytes_per_row
        );

        write_head += bytes_per_row;
    }

    i2c.TransmitBlocking(SSD1327_I2C_ADDRESS, _drawBuffer, total_bytes + 1, 100000);

    // Reset dirty window
    dirty_window_min_x = UINT8_MAX;
    dirty_window_max_x = 0;
    dirty_window_min_y = UINT8_MAX;
    dirty_window_max_y = 0;
}

float minOutL = 0.f;
float maxOutL = 0.f;

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
    }
}

int main(void)
{
    patch.Init();
    patch.StartLog();

    // setup the configuration
    I2CHandle::Config i2c_conf;
    i2c_conf.periph = I2CHandle::Config::Peripheral::I2C_1;
    i2c_conf.speed  = I2CHandle::Config::Speed::I2C_1MHZ;
    i2c_conf.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    i2c_conf.pin_config.scl  = {DSY_GPIOB, 8};
    i2c_conf.pin_config.sda  = {DSY_GPIOB, 9};
    // initialise the peripheral
    i2c.Init(i2c_conf);

    i2c.TransmitBlocking(SSD1327_I2C_ADDRESS, init_128x128, sizeof(init_128x128), 100000);

    SSD1327_Clear(SSD1327_BLACK);

    int y = 0;

    patch.StartAudio(AudioCallback);

    while(1) {
        int minX = minOutL * 10 * 64 + 64; 
        int maxX = maxOutL * 10 * 64 + 64; 

        // patch.PrintLine("outL: (%d, %d) (%d, %d) " FLT_FMT3, minX, maxX, FLT_VAR3(minOutL));

        for (int x = 0; x < SSD1327_LCD_WIDTH; x++) {
            uint8_t new_colour = (x >= minX && x <= maxX) ? 0xF : 0x0;

            setPixel(x, y, new_colour);
        }

        SSD1327_Display();

        if (y == 128) break;
        y = (y + 1) % SSD1327_LCD_HEIGHT;
    }
} 