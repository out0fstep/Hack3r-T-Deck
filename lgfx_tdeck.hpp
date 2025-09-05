// lgfx_tdeck.hpp — LilyGO T-Deck / T-Deck Plus display config using LovyanGFX
// - Powers PERI gate, sets up ST7789 + GT911 on I2C_NUM_1 (shared with trackball)

#pragma once
#include <LovyanGFX.hpp>
#include <driver/i2c.h>

class LGFX_TDeck : public lgfx::LGFX_Device {
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
  lgfx::Panel_ST7789 _panel;
  lgfx::Touch_GT911  _touch;

public:
  LGFX_TDeck() {
    // Peripherals power gate
    lgfx::pinMode(GPIO_NUM_10, lgfx::pin_mode_t::output);
    lgfx::gpio_hi(GPIO_NUM_10);
    delay(10);

    // Touch wake pulse on INT
    lgfx::pinMode(GPIO_NUM_16, lgfx::pin_mode_t::output);
    lgfx::gpio_hi(GPIO_NUM_16);
    delay(20);
    lgfx::pinMode(GPIO_NUM_16, lgfx::pin_mode_t::input);

    // Deselect TF and LoRa
    lgfx::pinMode(GPIO_NUM_39, lgfx::pin_mode_t::output); lgfx::gpio_hi(GPIO_NUM_39); // TF_CS
    lgfx::pinMode(GPIO_NUM_9,  lgfx::pin_mode_t::output); lgfx::gpio_hi(GPIO_NUM_9);  // LoRa CS

    // SPI bus for LCD
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_3wire  = true;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk   = GPIO_NUM_40;
      cfg.pin_mosi   = GPIO_NUM_41;
      cfg.pin_miso   = GPIO_NUM_38;   // not used by panel
      cfg.pin_dc     = GPIO_NUM_11;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    // Backlight PWM (GPIO42)
    {
      auto cfg = _light.config();
      cfg.pin_bl      = GPIO_NUM_42;
      cfg.freq        = 12000;
      cfg.pwm_channel = 0;
      cfg.invert      = false;
      _light.config(cfg);
    }

    // ST7789 panel
    {
      auto cfg = _panel.config();
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.pin_cs       = GPIO_NUM_12;
      cfg.pin_rst      = -1;      // tied to ESP reset on many revs
      cfg.invert       = true;    // required on T-Deck ST7789
      _panel.config(cfg);
      _panel.setRotation(1);      // landscape
      _panel.light(&_light);
    }

    // GT911 touch on I2C_NUM_1 (same bus we use from the sketch)
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;  cfg.y_min = 0;
      cfg.x_max = 240; cfg.y_max = 320;
      cfg.i2c_port = I2C_NUM_1;
      cfg.i2c_addr = 0x5D;
      cfg.pin_sda  = GPIO_NUM_18;
      cfg.pin_scl  = GPIO_NUM_8;
      cfg.pin_int  = GPIO_NUM_16;
      cfg.pin_rst  = -1;
      cfg.freq     = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }

    setPanel(&_panel);
  }
};
