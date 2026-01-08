#!/usr/bin/python3
# -*- coding: UTF-8 -*-

import os
import shutil
import re
import json
import csv
import zipfile
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('config_json', type=str, help='the config_json of idf')

# 打包路径
out_path = os.path.dirname(os.path.abspath(__file__))
pack_path = os.path.join(out_path, "pack")

# bin路径
bootloader_bin = os.path.join(out_path, "build", "bootloader", "bootloader.bin")
partition_table_bin = os.path.join(out_path, "build", "partition_table", "partition-table.bin")
luatos_bin = os.path.join(out_path, "build", "luatos.bin")

luat_conf_bsp = os.path.join(out_path, "include", "luat_conf_bsp.h")
info_json = os.path.join(pack_path, "info.json")
soc_download_exe = os.path.join(pack_path, "soc_download.exe")
sdkconfig_path = os.path.join(pack_path, "..", "sdkconfig")


def zip_dir(dirname, zipfilename):
    filelist = []
    for root, dirs, files in os.walk(dirname):
        for name in files:
            filelist.append(os.path.join(root, name))
    zf = zipfile.ZipFile(zipfilename, "w", compression=zipfile.ZIP_LZMA)
    for tar in filelist:
        arcname = tar[len(dirname):]
        zf.write(tar, arcname)
    zf.close()


def parse_partition_csv(csv_path):
    """解析分区表 CSV，返回各分区信息字典"""
    partitions = {}
    with open(csv_path, 'r') as f:
        # 跳过注释行，但保留第一行作为 header（可能以 # 开头）
        lines = [line.strip() for line in f if line.strip() and not line.startswith('##')]
        if not lines:
            raise ValueError("分区表为空")

        # 第一行是 header
        header = lines[0]
        if header.startswith('#'):
            header = header[1:].strip()  # 去掉开头的 #
        # 分割字段并去除空格
        fieldnames = [h.strip() for h in header.split(',')]

        # 解析后续行
        for line in lines[1:]:
            if line.startswith('#') or not line.strip():
                continue
            values = [v.strip() for v in line.split(',')]
            if len(values) < len(fieldnames):
                continue
            row = dict(zip(fieldnames, values))
            name = row.get('Name') or row.get('# Name')
            if name:
                partitions[name] = {
                    'Offset': row.get('Offset', '0x0'),
                    'Size': row.get('Size', '0x0')
                }
    return partitions


if __name__ == '__main__':
    args = parser.parse_args()
    config_json = args.config_json

    with open(config_json) as f:
        config_json_data = json.load(f)
        bsp = config_json_data["IDF_TARGET"].upper()
        partition_table_name = config_json_data["PARTITION_TABLE_FILENAME"]
        partition_table_csv = os.path.join(out_path, partition_table_name)

    # === 解析分区表 ===
    try:
        parts = parse_partition_csv(partition_table_csv)
        core_addr = parts['app0']['Offset']
        script_addr = parts['script']['Offset']
        script_size_hex = parts['script']['Size']
        fs_addr = parts['spiffs']['Offset']
    except Exception as e:
        print(f"解析分区表失败: {e}")
        raise

    # === 转换 script_size 为 KB（十进制）===
    try:
        script_size_kb = int(script_size_hex, 16) // 1024
    except Exception as e:
        print(f"转换 script_size 失败: {e}")
        script_size_kb = 128

    print(f"rom size {script_size_kb}k")
    print(f"rom addr {script_addr}")

    # === 读取 luat_conf_bsp.h ===
    vm_64bit = False
    bsp_version = "unknown"
    with open(luat_conf_bsp, "r", encoding="UTF-8") as f:
        for line in f:
            version_match = re.search(r'#define LUAT_BSP_VERSION\s+"(.+?)"', line)
            if version_match:
                bsp_version = version_match.group(1)
            if line.strip().startswith("#define LUAT_CONF_VM_64bit"):
                vm_64bit = True
                print("固件是64bit的VM")

    out_file_name = f"LuatOS-SoC_{bsp_version}_{bsp}"
    out_file = os.path.join(out_path, out_file_name)

    temp = os.path.join(pack_path, "temp")
    if os.path.exists(temp):
        shutil.rmtree(temp)
    os.mkdir(temp)

    for src in [bootloader_bin, partition_table_bin, luatos_bin,
                luat_conf_bsp, info_json, soc_download_exe, sdkconfig_path]:
        if os.path.exists(src):
            shutil.copy(src, temp)

    info_json_temp = os.path.join(temp, "info.json")

    with open(info_json_temp, "r") as f:
        info_json_data = json.load(f)

    # === 更新 info.json ===
    with open(info_json_temp, "w") as f:
        print("script_size (KB):", script_size_kb)
        if script_size_kb > 256 and bsp == "ESP32S3":
            print("=" * 85)
            print("= USE ROMFS for script. Pls use LuaTools 2.2.3 or later")
            print("=" * 85)
            info_json_data["rom"]["fs"]["script"]["type"] = "romfs"

        info_json_data["rom"]["fs"]["script"]["size"] = script_size_kb
        info_json_data["download"]["core_addr"] = core_addr.replace("0x", "").zfill(8)
        info_json_data["download"]["script_addr"] = script_addr.replace("0x", "").zfill(8)
        info_json_data["download"]["fs_addr"] = fs_addr.replace("0x", "").zfill(8)

        extra_params = {
            "ESP32C3": "00ff0200",
            "ESP32S3": "01ff0200",
            "ESP32":   "02ff0200",
            "ESP32C2": "03ff0200"
        }
        info_json_data["download"]["extra_param"] = extra_params.get(bsp, "01ff0200")

        if vm_64bit:
            info_json_data["script"]["bitw"] = 64

        json.dump(info_json_data, f, indent=2)
        print(json.dumps(info_json_data, indent=2))

    # === 生成 .soc 文件 ===
    for suffix in ['', '_USB']:
        soc_file = out_file + suffix + '.soc'
        if os.path.exists(soc_file):
            os.remove(soc_file)

    zip_dir(temp, out_file + '.soc')

    if bsp == "ESP32C3":
        with open(info_json_temp, "r") as f:
            info_json_data = json.load(f)
        info_json_data["download"]["force_br"] = "0"
        with open(info_json_temp, "w") as f:
            json.dump(info_json_data, f, indent=2)
        zip_dir(temp, out_file + '_USB.soc')

    shutil.rmtree(temp)
    print(__file__, 'done')