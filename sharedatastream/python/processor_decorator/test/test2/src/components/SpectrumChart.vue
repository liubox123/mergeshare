<template>
  <div
    class="spectrum-chart-container"
    @wheel="onWheel"
    @mousedown="onMouseDown"
    @mousemove="onMouseMove"
    @mouseup="onMouseUp"
    @mouseleave="onMouseUp"
    @blur="onBlur"
    @focusout="onFocusOut"
    tabindex="0"
  >
    <canvas ref="canvas"></canvas>
  </div>
</template>

<script setup>
import {
  ref, onMounted, onBeforeUnmount, watch, toRefs, nextTick,
} from 'vue';
import * as PIXI from 'pixi.js';

const props = defineProps({
  wsUrl: { type: String, required: true },
  thresholds: { type: Array, default: () => [] },
  freqMin: { type: Number, default: 0 },
  freqMax: { type: Number, default: 80e9 },
  height: { type: Number, default: 400 },
  width: { type: Number, default: 800 },
});

const {
  wsUrl, thresholds, freqMin, freqMax,
} = toRefs(props);

// 本地响应式状态
const localWidth = ref(props.width);
const localHeight = ref(props.height);

// 监听属性变化
watch(() => props.width, (newWidth) => {
  localWidth.value = newWidth;
});

watch(() => props.height, (newHeight) => {
  localHeight.value = newHeight;
});

const canvas = ref(null);
let app = null;
let graphics = null;
let spectrumData = [];
let ws = null;
let resizeObserver = null;
let resizeTimeout = null;

// 缩放相关
let viewMin = freqMin.value;
let viewMax = freqMax.value;
let isDragging = false;
let dragStartX = 0;
let dragStartViewMin = 0;
let dragStartViewMax = 0;

function freqToX(freq) {
  // 确保频率在有效范围内
  const safeFreq = Math.max(viewMin, Math.min(viewMax, freq));
  const x = ((safeFreq - viewMin) / (viewMax - viewMin)) * localWidth.value;

  // 确保 x 在画布范围内
  const safeX = Math.max(0, Math.min(localWidth.value, x));

  if (freq === freqMin.value) {
    console.log('频率转换详情:', {
      原始频率: freq,
      限制后频率: safeFreq,
      原始X: x,
      限制后X: safeX,
      视图范围: { viewMin, viewMax },
      画布宽度: localWidth.value,
    });
  }

  return safeX;
}

function valueToY(value, minVal, maxVal) {
  // 确保值在范围内
  const safeValue = Math.max(minVal, Math.min(maxVal, value));

  // 计算归一化高度
  const normalizedHeight = ((safeValue - minVal) / (maxVal - minVal));

  // 翻转 Y 轴并确保在画布范围内
  const y = Math.max(0, Math.min(localHeight.value, localHeight.value * (1 - normalizedHeight)));

  if (value === spectrumData[0]) {
    console.log('值转换详情:', {
      原始值: value,
      限制后值: safeValue,
      归一化高度: normalizedHeight,
      最终Y: y,
      数据范围: { minVal, maxVal },
      画布高度: localHeight.value,
    });
  }

  return y;
}

// 添加颜色转换函数
function colorToNumber(color) {
  // 移除可能的 # 前缀
  const hex = color.replace('#', '');
  // 将十六进制字符串转换为数字
  const result = parseInt(hex, 16);
  console.log('颜色转换:', { 输入: color, 处理后: hex, 结果: result });
  return result;
}

// 资源清理函数
function cleanup() {
  // 清除 resize 超时
  if (resizeTimeout) {
    clearTimeout(resizeTimeout);
    resizeTimeout = null;
  }

  // 断开 ResizeObserver
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }

  // 关闭 WebSocket
  if (ws) {
    ws.close();
    ws = null;
  }

  // 销毁 PIXI 应用
  if (app) {
    try {
      app.destroy(true, { children: true, texture: true, baseTexture: true });
      app = null;
      graphics = null;
    } catch (e) {
      console.error('销毁 PIXI 应用时出错:', e);
    }
  }
}

// 清理函数
onBeforeUnmount(() => {
  cleanup();
});

function drawSpectrum() {
  console.log('开始绘制频谱图', {
    数据点数: spectrumData.length,
    示例数据: spectrumData.slice(0, 5),
    视图范围: {
      viewMin,
      viewMax,
    },
    画布尺寸: {
      width: localWidth.value,
      height: localHeight.value,
    },
  });

  if (!graphics) {
    console.error('graphics 对象未初始化');
    return;
  }

  // 清除之前的绘制内容
  graphics.clear();

  // 如果没有数据，绘制测试线条
  if (!spectrumData.length) {
    console.warn('没有频谱数据，绘制测试线条');
    graphics.setStrokeStyle({
      width: 2,
      color: 0xff0000,
    });
    graphics.moveTo(0, 0);
    graphics.lineTo(localWidth.value, localHeight.value);
    return;
  }

  // 计算数据范围
  const minValue = Math.min(...spectrumData);
  const maxValue = Math.max(...spectrumData);
  console.log('数据范围:', {
    minValue,
    maxValue,
  });

  // 绘制频谱线
  graphics.setStrokeStyle({
    width: 2,
    color: 0x00ff00,
    alpha: 1,
  });

  const N = spectrumData.length;
  const points = [];

  // 先收集所有点
  for (let i = 0; i < N; i += 1) {
    const freq = freqMin.value + ((freqMax.value - freqMin.value) * i) / (N - 1);
    const x = freqToX(freq);
    const y = valueToY(spectrumData[i], minValue, maxValue);

    if (i === 0) {
      console.log('第一个点:', {
        freq,
        x,
        y,
        value: spectrumData[i],
      });
    }

    points.push(x, y);
  }

  // 一次性绘制所有点
  if (points.length >= 4) {
    console.log('绘制点数:', points.length / 2);
    graphics.moveTo(points[0], points[1]);
    for (let i = 2; i < points.length; i += 2) {
      graphics.lineTo(points[i], points[i + 1]);
    }
  } else {
    console.warn('点数不足，无法绘制线条');
  }

  // 绘制阈值线
  if (thresholds.value.length > 0) {
    console.log('绘制阈值线:', thresholds.value);
    thresholds.value.forEach((threshold) => {
      const y = valueToY(threshold.value, minValue, maxValue);
      const color = colorToNumber(threshold.color || '#ff0000');
      graphics.setStrokeStyle({
        width: 1,
        color,
        alpha: 0.7,
      });
      graphics.moveTo(0, y);
      graphics.lineTo(localWidth.value, y);
    });
  }

  // 绘制坐标轴
  graphics.setStrokeStyle({
    width: 1,
    color: 0xffffff,
    alpha: 0.3,
  });
  graphics.moveTo(0, localHeight.value - 1);
  graphics.lineTo(localWidth.value, localHeight.value - 1);
  graphics.moveTo(1, 0);
  graphics.lineTo(1, localHeight.value);
}

function onWheel(e) {
  const zoom = e.deltaY < 0 ? 0.9 : 1.1;
  const mouseX = e.offsetX;
  const freqAtMouse = viewMin + ((viewMax - viewMin) * mouseX) / localWidth.value;
  let newMin = freqAtMouse - (freqAtMouse - viewMin) * zoom;
  let newMax = freqAtMouse + (viewMax - freqAtMouse) * zoom;
  if ((newMax - newMin) < 1e6) return;
  if (newMin < freqMin.value) newMin = freqMin.value;
  if (newMax > freqMax.value) newMax = freqMax.value;
  viewMin = newMin;
  viewMax = newMax;
  drawSpectrum();
}

function onMouseDown(e) {
  isDragging = true;
  dragStartX = e.offsetX;
  dragStartViewMin = viewMin;
  dragStartViewMax = viewMax;
}
function onMouseMove(e) {
  if (!isDragging) return;
  const dx = e.offsetX - dragStartX;
  const freqRange = dragStartViewMax - dragStartViewMin;
  const df = (-dx / localWidth.value) * freqRange;
  let newMin = dragStartViewMin + df;
  let newMax = dragStartViewMax + df;
  if (newMin < freqMin.value) {
    newMin = freqMin.value;
    newMax = newMin + freqRange;
  }
  if (newMax > freqMax.value) {
    newMax = freqMax.value;
    newMin = newMax - freqRange;
  }
  viewMin = newMin;
  viewMax = newMax;
  drawSpectrum();
}
function onMouseUp() {
  isDragging = false;
}

function onBlur() {
  // 可访问性辅助，暂不处理
}
function onFocusOut() {
  // 可访问性辅助，暂不处理
}

onMounted(async () => {
  try {
    // 等待 canvas 元素挂载完成
    await nextTick();

    if (!canvas.value) {
      console.error('canvas 元素未找到');
      return;
    }

    // 创建 PIXI 应用
    app = new PIXI.Application({
      view: canvas.value,
      resolution: window.devicePixelRatio || 1,
      autoDensity: true,
      antialias: true,
      backgroundColor: 0x181818,
      width: localWidth.value,
      height: localHeight.value,
    });

    // 等待渲染器初始化完成
    await app.init();

    console.log('PIXI 应用初始化完成', {
      width: app.renderer.width,
      height: app.renderer.height,
      resolution: app.renderer.resolution,
    });

    // 创建图形对象
    graphics = new PIXI.Graphics();
    app.stage.addChild(graphics);

    // 初始绘制
    drawSpectrum();

    // 创建 ResizeObserver
    resizeObserver = new ResizeObserver((entries) => {
      // 使用防抖处理 resize 事件
      if (resizeTimeout) {
        clearTimeout(resizeTimeout);
      }

      resizeTimeout = setTimeout(() => {
        if (!app?.renderer) {
          console.warn('PIXI 应用未完全初始化，跳过 resize');
          return;
        }

        const entry = entries[0];
        if (entry) {
          const { width: newWidth, height: newHeight } = entry.contentRect;
          console.log('容器大小变化:', {
            width: newWidth,
            height: newHeight,
            当前宽度: app.renderer.width,
            当前高度: app.renderer.height,
          });

          try {
            // 更新本地状态
            localWidth.value = Math.floor(newWidth);
            localHeight.value = Math.floor(newHeight);

            // 调整渲染器大小
            app.renderer.resize(localWidth.value, localHeight.value);

            // 重新绘制
            drawSpectrum();
          } catch (e) {
            console.error('调整大小时出错:', e);
          }
        }
      }, 100); // 100ms 防抖延迟
    });

    // 开始观察大小变化
    resizeObserver.observe(canvas.value.parentElement);

    // 连接 WebSocket
    ws = new WebSocket(wsUrl.value);
    ws.onmessage = (event) => {
      try {
        const arr = JSON.parse(event.data);
        console.log('收到数据', arr.length, arr.slice(0, 5));
        if (Array.isArray(arr)) {
          spectrumData = arr;
          drawSpectrum();
        }
      } catch (e) {
        console.error('处理 WebSocket 数据时出错:', e);
      }
    };
  } catch (e) {
    console.error('初始化 PIXI 应用时出错:', e);
    cleanup();
  }
});

watch(() => thresholds.value, drawSpectrum, { deep: true });
</script>

<style scoped>
.spectrum-chart-container {
  width: 100%;
  height: 100%;
  position: relative;
  user-select: none;
  background: #181818;
}
canvas {
  width: 100%;
  height: 100%;
  display: block;
  cursor: grab;
}
</style>
