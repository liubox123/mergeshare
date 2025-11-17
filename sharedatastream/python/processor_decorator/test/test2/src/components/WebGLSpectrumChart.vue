<template>
  <div
    class="spectrum-chart-container"
    @wheel="onWheel"
    @mousedown="onMouseDown"
    @mousemove="onMouseMove"
    @mouseup="onMouseUp"
    @mouseleave="onMouseLeave"
    @blur="onBlur"
    @focusout="onFocusOut"
    tabindex="0"
  >
    <canvas ref="canvas"  style="width: 100%; height: 400px;" id="my_canvas"></canvas>
  </div>
</template>

<script setup>
import {
  ref, onMounted, onBeforeUnmount, watch, toRefs, nextTick,
} from 'vue';
import {
  WebglPlot, WebglLine, ColorRGBA,
} from 'webgl-plot';

// Props 定义
const props = defineProps({
  wsUrl: { type: String, required: true },
  thresholds: { type: Array, default: () => [-10] },
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
const canvas = ref(null);

// 内部状态变量
let webglPlot = null;
let spectrumLine = null;
let spectrumData = [];
let ws = null;
let resizeObserver = null;
let resizeTimeout = null;
let animationFrameId = null;
let needsUpdate = false;

// 缩放相关
let viewMin = freqMin.value;
let viewMax = freqMax.value;
let isDragging = false;
let dragStartX = 0;
let dragStartViewMin = 0;
let dragStartViewMax = 0;

// 颜色转换函数
function hexToRgba(hex, alpha = 1) {
  const r = parseInt(hex.slice(1, 3), 16) / 255;
  const g = parseInt(hex.slice(3, 5), 16) / 255;
  const b = parseInt(hex.slice(5, 7), 16) / 255;
  return new ColorRGBA(r, g, b, alpha);
}

// 绘图相关函数
function drawSpectrum() {
  // 基本验证
  if (!webglPlot || !spectrumLine) {
    return;
  }

  // 清除之前的绘制
  webglPlot.clear();

  try {
    // 1. 绘制坐标轴
    // X轴（0-80GHz，可缩放）
    const xAxis = new WebglLine(new ColorRGBA(1, 1, 1, 0.5));
    const yAxis = new WebglLine(new ColorRGBA(1, 1, 1, 0.5));

    // X轴线（在y=0处）
    xAxis.setX(new Float32Array([-1, 1]));
    xAxis.setY(new Float32Array([0, 0]));
    webglPlot.addLine(xAxis);

    // Y轴线（在当前视图的左边缘）
    yAxis.setX(new Float32Array([-1, -1]));
    yAxis.setY(new Float32Array([-1, 1]));
    webglPlot.addLine(yAxis);

    // 2. 绘制刻度线
    // X轴刻度（每10GHz一个刻度）
    const xTicks = new WebglLine(new ColorRGBA(1, 1, 1, 0.3));
    const xTicksCount = 9;
    const xTicksX = new Float32Array(xTicksCount * 2);
    const xTicksY = new Float32Array(xTicksCount * 2);

    for (let i = 0; i < xTicksCount; i += 1) {
      const freq = viewMin + ((viewMax - viewMin) * (i / (xTicksCount - 1)));
      const x = ((freq - viewMin) / (viewMax - viewMin)) * 2 - 1;
      xTicksX[i * 2] = x;
      xTicksX[i * 2 + 1] = x;
      xTicksY[i * 2] = -0.05;
      xTicksY[i * 2 + 1] = 0.05;
    }
    xTicks.setX(xTicksX);
    xTicks.setY(xTicksY);
    webglPlot.addLine(xTicks);

    // Y轴刻度（每20dB一个刻度）
    const yTicks = new WebglLine(new ColorRGBA(1, 1, 1, 0.3));
    const yTicksCount = 11;
    const yTicksX = new Float32Array(yTicksCount * 2);
    const yTicksY = new Float32Array(yTicksCount * 2);

    for (let i = 0; i < yTicksCount; i += 1) {
      const y = (i / (yTicksCount - 1)) * 2 - 1;
      yTicksX[i * 2] = -1;
      yTicksX[i * 2 + 1] = -0.95;
      yTicksY[i * 2] = y;
      yTicksY[i * 2 + 1] = y;
    }
    yTicks.setX(yTicksX);
    yTicks.setY(yTicksY);
    webglPlot.addLine(yTicks);

    // 3. 绘制频谱数据
    if (Array.isArray(spectrumData) && spectrumData.length > 0) {
      const N = spectrumData.length;

      // 确保点数匹配
      if (spectrumLine.numPoints !== N) {
        spectrumLine = new WebglLine(new ColorRGBA(0, 1, 0, 1));
        webglPlot.addLine(spectrumLine);
      }

      // 计算X坐标（考虑缩放）
      const xCoords = new Float32Array(N);
      for (let i = 0; i < N; i += 1) {
        const freq = freqMin.value + ((freqMax.value - freqMin.value) * (i / (N - 1)));
        xCoords[i] = ((freq - viewMin) / (viewMax - viewMin)) * 2 - 1;
      }
      spectrumLine.setX(xCoords);

      // 计算Y坐标（固定-100到100的范围）
      const yCoords = new Float32Array(N);
      for (let i = 0; i < N; i += 1) {
        yCoords[i] = spectrumData[i] / 100;
      }
      spectrumLine.setY(yCoords);
    }

    // 4. 绘制阈值线（如果有）
    if (thresholds.value.length > 0) {
      thresholds.value.forEach((threshold) => {
        const y = threshold.value / 100;
        const defaultColor = new ColorRGBA(1, 0, 0, 0.7);
        const color = threshold.color ? hexToRgba(threshold.color, 0.7) : defaultColor;
        const line = new WebglLine(color);
        line.setX(new Float32Array([-1, 1]));
        line.setY(new Float32Array([y, y]));
        webglPlot.addLine(line);
      });
    }

    // 5. 更新显示
    webglPlot.update();
  } catch (e) {
    console.error('绘制频谱时出错:', e);
  }
}

// 渲染循环函数
function render() {
  animationFrameId = requestAnimationFrame(render);
  if (needsUpdate && webglPlot) {
    drawSpectrum();
    needsUpdate = false;
  }
}

// 事件处理函数
function onWheel(e) {
  e.preventDefault();
  const zoom = e.deltaY < 0 ? 0.9 : 1.1;
  const mouseX = e.offsetX / localWidth.value;
  const freqAtMouse = viewMin + (viewMax - viewMin) * mouseX;

  // 计算新的视图范围
  let newMin = freqAtMouse - (freqAtMouse - viewMin) * zoom;
  let newMax = freqAtMouse + (viewMax - freqAtMouse) * zoom;

  // 限制最小缩放范围（至少1GHz）
  if ((newMax - newMin) < 1e9) {
    return;
  }

  // 限制在总频率范围内
  if (newMin < freqMin.value) newMin = freqMin.value;
  if (newMax > freqMax.value) newMax = freqMax.value;

  viewMin = newMin;
  viewMax = newMax;
  needsUpdate = true;
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
  needsUpdate = true;
}

function onMouseUp() {
  isDragging = false;
}

function onMouseLeave() {
  isDragging = false;
}

function onBlur() {
  isDragging = false;
}

function onFocusOut() {
  isDragging = false;
}

// 资源清理函数
function cleanup() {
  // 停止动画循环
  if (animationFrameId !== null) {
    cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }

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

  // 销毁 WebGL 上下文
  if (webglPlot) {
    webglPlot.clear();
    webglPlot = null;
    spectrumLine = null;

    // 如果 canvas 还存在，清除其上下文
    if (canvas.value) {
      const gl = canvas.value.getContext('webgl');
      if (gl) {
        gl.getExtension('WEBGL_lose_context')?.loseContext();
      }
    }
  }
}

// 监听属性变化
watch(() => props.width, (newWidth) => {
  localWidth.value = newWidth;
  if (canvas.value && webglPlot) {
    canvas.value.width = newWidth;
    canvas.value.height = localHeight.value;
    webglPlot.viewport(0, 0, newWidth, localHeight.value);
    needsUpdate = true;
  }
});

watch(() => props.height, (newHeight) => {
  localHeight.value = newHeight;
  if (canvas.value && webglPlot) {
    canvas.value.height = newHeight;
    canvas.value.width = localWidth.value;
    webglPlot.viewport(0, 0, localWidth.value, newHeight);
    needsUpdate = true;
  }
});

// 监听阈值变化
watch(() => thresholds.value, () => {
  if (webglPlot) {
    needsUpdate = true;
  }
}, { deep: true });

// 组件挂载
onMounted(async () => {
  try {
    // 等待 canvas 元素挂载完成
    await nextTick();

    if (!canvas.value) {
      console.error('canvas 元素未找到');
      return;
    }

    // 设置 canvas 尺寸
    const devicePixelRatio = window.devicePixelRatio || 1;
    canvas.value.width = canvas.value.clientWidth * devicePixelRatio;
    canvas.value.height = canvas.value.clientHeight * devicePixelRatio;

    // 创建 WebGL-Plot 实例
    webglPlot = new WebglPlot(canvas.value);
    webglPlot.viewport(0, 0, canvas.value.width, canvas.value.height);

    // 创建初始频谱线
    spectrumLine = new WebglLine(new ColorRGBA(0, 1, 0, 1));
    const initialData = new Float32Array(512).fill(0);
    spectrumLine.setX(initialData);
    spectrumLine.setY(initialData);
    webglPlot.addLine(spectrumLine);

    // 启动渲染循环
    render();

    // 创建 ResizeObserver
    resizeObserver = new ResizeObserver((entries) => {
      if (resizeTimeout) {
        clearTimeout(resizeTimeout);
      }

      resizeTimeout = setTimeout(() => {
        const entry = entries[0];
        if (entry && webglPlot) {
          const { width: newWidth, height: newHeight } = entry.contentRect;
          try {
            // 更新本地状态
            localWidth.value = Math.floor(newWidth);
            localHeight.value = Math.floor(newHeight);

            // 更新 canvas 尺寸
            canvas.value.width = newWidth * devicePixelRatio;
            canvas.value.height = newHeight * devicePixelRatio;

            // 更新视口
            webglPlot.viewport(0, 0, canvas.value.width, canvas.value.height);
            needsUpdate = true;
          } catch (e) {
            console.error('调整大小时出错:', e);
          }
        }
      }, 100);
    });

    // 开始观察大小变化
    resizeObserver.observe(canvas.value.parentElement);

    // 连接 WebSocket
    ws = new WebSocket(wsUrl.value);
    ws.onmessage = (event) => {
      try {
        const arr = JSON.parse(event.data);
        if (Array.isArray(arr) && webglPlot) {
          spectrumData = arr;
          needsUpdate = true;
        }
      } catch (e) {
        console.error('处理 WebSocket 数据时出错:', e);
      }
    };
  } catch (e) {
    console.error('初始化 WebGL-Plot 时出错:', e);
    cleanup();
  }
});

// 组件卸载
onBeforeUnmount(() => {
  cleanup();
});
</script>

<style scoped>
.spectrum-chart-container {
  width: 100%;
  height: 400px;
  position: relative;
  user-select: none;
  background: #1818187a;
  /* 确保容器正确显示内容 */
  display: block;
  overflow: hidden;
}

canvas {
  width: 100%;
  height: 100%;
}

/* canvas {
  width: 100%;
  height: 100%;
  display: block;
  cursor: grab;
  position: absolute;
  top: 0;
  left: 0;
  image-rendering: pixelated;
  touch-action: none;
} */
</style>
