set(srcs
    luat_lib_lcd.c
    luat_lcd.c
    luat_lcd_custom.c
    luat_lcd_gc9106l.c
    luat_lcd_gc9306x.c
    luat_lcd_gc9a01.c
    luat_lcd_h050iwv.c
    luat_lcd_ili9341.c
    luat_lcd_ili9486.c
    luat_lcd_jd9261t.c
    luat_lcd_nv3037.c
    luat_lcd_sh8601z.c
    luat_lcd_st7735.c
    luat_lcd_st7735s.c
    luat_lcd_st7735v.c
    luat_lcd_st7789.c
    luat_lcd_st7796.c
    luat_lib_lcd_jpg.c
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS .
    REQUIRES luat_common luat_fs printf
)