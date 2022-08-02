#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "Daisy_SSD1327.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM patch;

SpiHandle spi;
dsy_gpio dc_pin;
I2CHandle i2c;

uint8_t DMA_BUFFER_MEM_SECTION oled_buffer[SSD1327_REQUIRED_DMA_BUFFER_SIZE];
Daisy_SSD1327 oled;

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

        oled.setPixel(x, y, new_colour);
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
    dc_pin.pin  = patch.A9;
    dsy_gpio_init(&dc_pin);

    // setup the configuration
    SpiHandle::Config spi_conf;
    spi_conf.periph = SpiHandle::Config::Peripheral::SPI_2;
    spi_conf.mode = SpiHandle::Config::Mode::MASTER;
    spi_conf.direction = SpiHandle::Config::Direction::TWO_LINES;
    spi_conf.clock_polarity = SpiHandle::Config::ClockPolarity::HIGH;
    spi_conf.clock_phase = SpiHandle::Config::ClockPhase::TWO_EDGE;
    spi_conf.nss = SpiHandle::Config::NSS::HARD_OUTPUT;
    spi_conf.pin_config.sclk = patch.D10;
    spi_conf.pin_config.mosi = patch.D9;
    spi_conf.pin_config.miso = patch.D8;
    spi_conf.pin_config.nss = patch.D1;
    spi.Init(spi_conf);

    oled.init(spi, dc_pin, oled_buffer, patch);
    oled.clear(SSD1327_BLACK);

    patch.StartAudio(AudioCallback);

    while(1) {
        if (System::GetNow() - last_render_millis > 8 && !oled.isRendering()) {
            last_render_millis = System::GetNow();
            oled.display();
        }
    }
} 