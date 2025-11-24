#!/bin/bash

# MultiQueue-SHM 测试运行脚本
# 用法: ./run_tests.sh [选项]
#
# 选项:
#   all       - 运行所有测试 (默认)
#   unit      - 只运行单元测试
#   multiproc - 只运行多进程测试
#   verbose   - 详细输出
#   help      - 显示帮助

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/out/build"
TEST_DIR="${BUILD_DIR}/tests/cpp"

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 显示帮助信息
show_help() {
    cat << EOF
MultiQueue-SHM 测试运行脚本

用法: $0 [选项]

选项:
    all       - 运行所有测试 (默认)
    unit      - 只运行单元测试 (不包括多进程测试)
    multiproc - 只运行多进程测试
    verbose   - 详细输出
    clean     - 清理并重新构建
    help      - 显示此帮助信息

示例:
    $0              # 运行所有测试
    $0 unit         # 只运行单元测试
    $0 verbose      # 详细输出所有测试
    $0 clean all    # 清理、构建并运行所有测试

EOF
}

# 检查构建目录
check_build() {
    if [ ! -d "$BUILD_DIR" ]; then
        print_warning "构建目录不存在，正在创建..."
        mkdir -p "$BUILD_DIR"
        return 1
    fi
    
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        print_warning "Makefile 不存在，需要运行 CMake"
        return 1
    fi
    
    return 0
}

# 运行 CMake 配置
run_cmake() {
    print_info "运行 CMake 配置..."
    cd "$BUILD_DIR"
    cmake ../.. || {
        print_error "CMake 配置失败"
        exit 1
    }
    print_success "CMake 配置成功"
}

# 编译项目
build_project() {
    print_info "编译项目..."
    cd "$BUILD_DIR"
    make -j8 || {
        print_error "编译失败"
        exit 1
    }
    print_success "编译成功"
}

# 清理构建
clean_build() {
    print_info "清理构建目录..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    print_success "清理完成"
}

# 运行单个测试
run_single_test() {
    local test_name=$1
    local test_path="${TEST_DIR}/${test_name}"
    
    if [ ! -f "$test_path" ]; then
        print_error "测试文件不存在: $test_path"
        return 1
    fi
    
    print_info "运行测试: $test_name"
    if [ "$VERBOSE" = true ]; then
        "$test_path" || return 1
    else
        "$test_path" > /dev/null 2>&1 || return 1
    fi
    print_success "$test_name 通过"
    return 0
}

# 运行所有单元测试
run_unit_tests() {
    local tests=(
        "test_types"
        "test_timestamp"
        "test_buffer_metadata"
        "test_buffer_pool"
        "test_buffer_allocator"
        "test_port_queue"
        "test_block"
    )
    
    local passed=0
    local failed=0
    local total=${#tests[@]}
    
    print_info "运行 $total 个单元测试..."
    echo ""
    
    for test in "${tests[@]}"; do
        if run_single_test "$test"; then
            ((passed++))
        else
            ((failed++))
            print_error "$test 失败"
        fi
    done
    
    echo ""
    print_info "单元测试结果: $passed/$total 通过"
    
    if [ $failed -eq 0 ]; then
        print_success "所有单元测试通过！"
        return 0
    else
        print_error "$failed 个测试失败"
        return 1
    fi
}

# 运行多进程测试
run_multiprocess_tests() {
    print_info "运行多进程测试..."
    
    if run_single_test "test_multiprocess"; then
        print_success "多进程测试通过！"
        return 0
    else
        print_error "多进程测试失败"
        return 1
    fi
}

# 运行所有测试
run_all_tests() {
    print_info "运行所有测试..."
    
    if [ "$VERBOSE" = true ]; then
        cd "$BUILD_DIR"
        ctest --verbose
    else
        cd "$BUILD_DIR"
        ctest --output-on-failure
    fi
    
    local result=$?
    
    if [ $result -eq 0 ]; then
        echo ""
        print_success "✅ 所有测试通过！"
        echo ""
        ctest --verbose | grep "tests passed"
    else
        echo ""
        print_error "❌ 部分测试失败"
    fi
    
    return $result
}

# 主函数
main() {
    # 解析参数
    local mode="all"
    VERBOSE=false
    local need_clean=false
    
    while [ $# -gt 0 ]; do
        case "$1" in
            all)
                mode="all"
                ;;
            unit)
                mode="unit"
                ;;
            multiproc)
                mode="multiproc"
                ;;
            verbose)
                VERBOSE=true
                ;;
            clean)
                need_clean=true
                ;;
            help|--help|-h)
                show_help
                exit 0
                ;;
            *)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
        shift
    done
    
    # 显示标题
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║          MultiQueue-SHM 测试运行脚本                       ║"
    echo "║          版本: v2.0.0-phase2                              ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""
    
    # 清理（如果需要）
    if [ "$need_clean" = true ]; then
        clean_build
    fi
    
    # 检查并构建
    if ! check_build; then
        run_cmake
    fi
    
    build_project
    
    # 运行测试
    echo ""
    case "$mode" in
        all)
            run_all_tests
            ;;
        unit)
            run_unit_tests
            ;;
        multiproc)
            run_multiprocess_tests
            ;;
    esac
    
    local result=$?
    
    echo ""
    if [ $result -eq 0 ]; then
        print_success "测试完成！"
    else
        print_error "测试失败！"
    fi
    
    exit $result
}

# 运行主函数
main "$@"

