from setuptools import setup, find_packages
from setuptools.command.build_ext import build_ext
from setuptools.command.build import build as build_orig
import subprocess
import os
import shutil
import sysconfig
import platform

class CMakeBuild(build_ext):
    def run(self):
        # 确定构建类型 (Release/Debug)
        build_type = "Release"  # 默认用 Release，如需 Debug 可通过环境变量切换
        if os.getenv("CMAKE_BUILD_TYPE"):
            build_type = os.getenv("CMAKE_BUILD_TYPE")
        
        build_dir = os.path.abspath("build")
        os.makedirs(build_dir, exist_ok=True)
        
        # 生成构建系统
        subprocess.check_call([
            "cmake", 
            "..",
            f"-DCMAKE_BUILD_TYPE={build_type}"
        ], cwd=build_dir)
        
        # 编译项目
        subprocess.check_call([
            "cmake", 
            "--build", ".", 
            "--config", build_type
        ], cwd=build_dir)

        # Windows 专用处理
        ext_suffix = sysconfig.get_config_var("EXT_SUFFIX") or ".pyd"
        pyd_name = f"shared_ring_queue{ext_suffix}"
        
        # 处理 Windows 的构建目录结构 (Debug/Release)
        print("---------------------", platform.system() == "Windows", platform.system())
        build_subdir = build_type if platform.system() == "Windows" else ""
        print("--------------build_subdir------",build_subdir)
        src_pyd = os.path.join(build_dir, build_subdir, pyd_name)
        # print("-----------src_pyd-----------",)
        # 检查文件是否存在
        if not os.path.exists(src_pyd):
            # 尝试其他可能路径
            src_pyd = os.path.join(build_dir, "python", "processor_decorator",build_subdir, pyd_name)
            if not os.path.exists(src_pyd):
                raise FileNotFoundError(f"Cannot find compiled .pyd: {src_pyd}")

        # 目标路径
        dst_dir = os.path.join("python", "processor_decorator")
        os.makedirs(dst_dir, exist_ok=True)
        dst_pyd = os.path.join(dst_dir, pyd_name)
        
        # 复制文件
        shutil.copy2(src_pyd, dst_pyd)
        print(f"Copied {src_pyd} -> {dst_pyd}")

class Build(build_orig):
    def run(self):
        self.run_command("build_ext")
        super().run()

setup(
    name="processor-decorator",
    version="0.1",
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    package_data={
        "processor_decorator": [
            "*.pyd",  # Windows 扩展名
            "*.dll",  # 可能的依赖项
            "*.py"
        ]
    } if platform.system() == "Windows" else {"processor_decorator": ["*.so", "*.py"]},
    include_package_data=True,
    zip_safe=False,
    cmdclass={
        "build_ext": CMakeBuild,
        "build": Build,
    },
)