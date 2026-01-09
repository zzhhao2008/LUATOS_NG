/*
* LVGL porting for FT6336U touch controller
*/
#include "lvgl.h"
#include "ft6336u_driver.h"
#include "esp_log.h"

#define TAG "ft6336u_lvgl"

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t state;
} ft6336u_touch_data_t;

static ft6336u_touch_data_t touch_data = {0};

static void ft6336u_lvgl_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    ft6336u_read(&touch_data.x, &touch_data.y, (int*)&touch_data.state);
    
    data->point.x = touch_data.x;
    data->point.y = touch_data.y;
    data->state = touch_data.state ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

lv_indev_t *ft6336u_lvgl_init(ft6336u_cfg_t *cfg) {
    esp_err_t ret = ft6336u_init(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U initialization failed");
        return NULL;
    }

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = ft6336u_lvgl_read;
    
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
    if (indev == NULL) {
        ESP_LOGE(TAG, "Failed to register LVGL input device");
        return NULL;
    }

    ESP_LOGI(TAG, "FT6336U touch driver registered with LVGL");
    return indev;
}

void ft6336u_lvgl_get_state(int16_t *x, int16_t *y, uint8_t *state) {
    ft6336u_read(x, y, (int*)state);
}