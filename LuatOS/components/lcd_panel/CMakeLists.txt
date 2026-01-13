set(COMPONENT_ADD_INCLUDEDIRS ".")

# 获取当前目录下的所有源文件
file(GLOB_RECURSE src_files ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

# 过滤掉不需要的文件
list(FILTER src_files EXCLUDE REGEX ".*_bak\\.c$")

set(COMPONENT_SRCDIRS ".")
set(COMPONENT_REQUIRES luat_base luat_msgbus esp_lcd lvgl)

register_component()

# 添加预处理器定义
target_compile_definitions(${COMPONENT_TARGET} PRIVATE "-DLV_CONF_INCLUDE_SIMPLE")