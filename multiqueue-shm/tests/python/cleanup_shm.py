#!/usr/bin/env python3
"""
清理共享内存脚本
"""

import os
import sys

# 在 macOS 上，Boost.Interprocess 使用 System V 共享内存
# 我们需要清理遗留的共享内存段

print("清理共享内存...")

# 获取所有共享内存段
output = os.popen("ipcs -m").read()
lines = output.split('\n')

cleaned = 0
for line in lines:
    parts = line.split()
    if len(parts) >= 2 and parts[0] == 'm':
        shmid = parts[1]
        try:
            os.system(f"ipcrm -m {shmid}")
            print(f"  删除共享内存段: {shmid}")
            cleaned += 1
        except:
            pass

print(f"\n清理完成！删除了 {cleaned} 个共享内存段")

