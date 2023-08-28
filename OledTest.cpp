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

uint32_t last_oled_update_millis = 0;

int main(void)
{
    patch.Init();
    patch.StartLog();

    spi.Init(
        oled.getSpiConfig(
            patch.D10, /* sclk */
            patch.D9, /* mosi */
            patch.D8, /* miso */
            patch.D1 /* nss */
        )
    );

    oled.init(spi, patch.A9, oled_buffer, patch);
    oled.clear(SSD1327_BLACK);

    patch.StartAudio(AudioCallback);

    while(1) {
        if (System::GetNow() - last_oled_update_millis > 8 && !oled.isRendering()) {
            last_oled_update_millis = System::GetNow();
            oled.display();
        }
    }
} 