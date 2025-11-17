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
    ref="container"
    style="position: relative; width: 100%; height: 400px;"
  >
    <!-- 主 canvas：只画谱线 -->
    <canvas
      ref="canvas"
      :width="localWidth"
      :height="localHeight"
      style="
        width: 100%;
        height: 100%;
        display: block;
        position: absolute;
        left: 0;
        top: 0;
        z-index: 1;
      "
    ></canvas>
    <!-- 中间层 canvas：只画坐标轴和刻度 -->
    <canvas
      ref="axisCanvas"
      :width="localWidth"
      :height="localHeight"
      style="
        width: 100%;
        height: 100%;
        display: block;
        position: absolute;
        left: 0;
        top: 0;
        z-index: 2;
        pointer-events: none;
      "
    ></canvas>
    <!-- 叠加 canvas：只画跟随线和 tooltip -->
    <canvas
      ref="overlayCanvas"
      :width="localWidth"
      :height="localHeight"
      style="
        width: 100%;
        height: 100%;
        display: block;
        position: absolute;
        left: 0;
        top: 0;
        z-index: 3;
        pointer-events: none;
      "
    ></canvas>
    <div
      v-if="showTooltip"
      :style="tooltipStyle"
      class="spectrum-tooltip"
    >
      <div>
        频率: {{ Math.round(tooltipFreq) }} Hz
      </div>
      <div>
        幅值: {{ tooltipValue.toFixed(2) }}
      </div>
    </div>
  </div>
</template>

<script setup>
import {
  ref, onMounted, onBeforeUnmount, watch, toRefs, nextTick,
} from 'vue';

// Props
const props = defineProps({
  spectrumData: { type: [Array, Float32Array], default: () => [] },
  thresholds: { type: Array, default: () => [] },
  freqMin: { type: Number, default: 0 },
  freqMax: { type: Number, default: 80e9 },
  height: { type: Number, default: 400 },
  width: { type: Number, default: 800 },
});
const {
  spectrumData, thresholds, freqMin, freqMax,
} = toRefs(props);

// 响应式状态
const localWidth = ref(props.width);
const localHeight = ref(props.height);
const canvas = ref(null);
const axisCanvas = ref(null); // 坐标轴画布
const overlayCanvas = ref(null);
const container = ref(null);

// 频谱数据
// let ws = null;
let resizeObserver = null;
let resizeTimeout = null;
let needsSpectrumUpdate = false; // 只在数据变化/缩放时重绘主谱线
let needsAxisUpdate = false; // 只在视图范围变化时重绘坐标轴
let needsOverlayUpdate = false; // 只在交互时重绘跟随线

// WebGL 相关
let gl = null;
let program = null;
let spectrumValueLoc = -1; // 缓存属性位置
let spectrumVAO = null; // 频谱 VAO
let uniformLocs = {}; // 缓存 uniform 位置

// 交互相关
let viewMin = freqMin.value;
let viewMax = freqMax.value;
let isDragging = false;
let dragStartX = 0;
let dragStartViewMin = 0;
let dragStartViewMax = 0;
let hoverIndex = -1;

// Tooltip
const showTooltip = ref(false);
const tooltipFreq = ref(0);
const tooltipValue = ref(0);
const tooltipStyle = ref({});

// Y轴范围
const Y_MIN = -150;
const Y_MAX = 40;

// 工具函数
function getStepByRange(range) {
  // 动态步进，优先常用频谱步进
  const steps = [
    6.25e3, 12.5e3, 25e3, 50e3, 100e3, 250e3, 500e3, 1e6, 2e6, 5e6, 10e6,
    20e6, 50e6, 100e6, 200e6, 500e6, 1e9, 2e9, 5e9, 10e9,
  ];
  for (let i = 0; i < steps.length; i += 1) {
    if (range / steps[i] <= 12) return steps[i];
  }
  return steps[steps.length - 1];
}

// 将频率格式化为合适的单位
function formatFreq(freq) {
  // 根据当前视图范围选择合适的单位
  const range = viewMax - viewMin;

  if (range >= 1e9) {
    // 范围超过1GHz时用GHz
    return `${(freq / 1e9).toFixed(1)}GHz`;
  }
  if (range >= 1e6) {
    // 范围超过1MHz时用MHz
    return `${(freq / 1e6).toFixed(1)}MHz`;
  }
  if (range >= 1e3) {
    // 范围超过1kHz时用kHz
    return `${(freq / 1e3).toFixed(1)}kHz`;
  }
  // 其他情况用Hz
  return `${freq.toFixed(1)}Hz`;
}

// WebGL2 shader
const VERT = `#version 300 es
precision mediump float;
in float a_value;
uniform float u_freqMin, u_freqMax, u_viewMin, u_viewMax, u_yMin, u_yMax, u_N;
void main() {
    float idx = float(gl_VertexID);
    float freq = u_freqMin + (u_freqMax - u_freqMin) * idx / (u_N - 1.0);
    float x = (freq - u_viewMin) / (u_viewMax - u_viewMin) * 2.0 - 1.0;
    float y = (a_value - u_yMin) / (u_yMax - u_yMin) * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0, 1);
}
`;
const FRAG = `#version 300 es
precision mediump float;
out vec4 outColor;
uniform vec4 u_color;
void main() {
    outColor = u_color;
}
`;

function createShader(glCtx, type, source) {
  const shader = glCtx.createShader(type);
  glCtx.shaderSource(shader, source);
  glCtx.compileShader(shader);
  if (!glCtx.getShaderParameter(shader, glCtx.COMPILE_STATUS)) {
    throw new Error(glCtx.getShaderInfoLog(shader));
  }
  return shader;
}
function createProgram(glCtx, vert, frag) {
  const v = createShader(glCtx, glCtx.VERTEX_SHADER, vert);
  const f = createShader(glCtx, glCtx.FRAGMENT_SHADER, frag);
  const prog = glCtx.createProgram();
  glCtx.attachShader(prog, v);
  glCtx.attachShader(prog, f);
  glCtx.linkProgram(prog);
  if (!glCtx.getProgramParameter(prog, glCtx.LINK_STATUS)) {
    throw new Error(glCtx.getProgramInfoLog(prog));
  }
  return prog;
}

// function drawLine(glCtx, prog, buffer, count, color, matrix) {
//   glCtx.useProgram(prog);
//   glCtx.uniform4fv(glCtx.getUniformLocation(prog, 'u_color'), color);
//   glCtx.uniformMatrix4fv(glCtx.getUniformLocation(prog, 'u_matrix'), false, matrix);
//   glCtx.bindBuffer(glCtx.ARRAY_BUFFER, buffer);
//   const posLoc = glCtx.getAttribLocation(prog, 'a_position');
//   glCtx.enableVertexAttribArray(posLoc);
//   glCtx.vertexAttribPointer(posLoc, 2, glCtx.FLOAT, false, 0, 0);
//   glCtx.drawArrays(glCtx.LINE_STRIP, 0, count);
// }
// function drawLines(glCtx, prog, buffer, count, color, matrix) {
//   glCtx.useProgram(prog);
//   glCtx.uniform4fv(glCtx.getUniformLocation(prog, 'u_color'), color);
//   glCtx.uniformMatrix4fv(glCtx.getUniformLocation(prog, 'u_matrix'), false, matrix);
//   glCtx.bindBuffer(glCtx.ARRAY_BUFFER, buffer);
//   const posLoc = glCtx.getAttribLocation(prog, 'a_position');
//   glCtx.enableVertexAttribArray(posLoc);
//   glCtx.vertexAttribPointer(posLoc, 2, glCtx.FLOAT, false, 0, 0);
//   glCtx.drawArrays(glCtx.LINES, 0, count);
// }

// function identityMatrix() {
//   return new Float32Array([
//     1, 0, 0, 0,
//     0, 1, 0, 0,
//     0, 0, 1, 0,
//     0, 0, 0, 1,
//   ]);
// }

// 主谱线渲染（只在数据变化/缩放时重绘）
function renderSpectrum() {
  if (!gl) return;
  gl.viewport(0, 0, localWidth.value, localHeight.value);
  gl.clearColor(0.09, 0.09, 0.09, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);

  // 频谱线（WebGL2+gl_VertexID高性能方案）
  if (spectrumData.value.length > 1) {
    const N = spectrumData.value.length;

    // 使用程序和 VAO
    gl.useProgram(program);
    gl.bindVertexArray(spectrumVAO);

    // 更新数据
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.spectrumValueBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, spectrumData.value, gl.DYNAMIC_DRAW);

    // 更新动态 uniform
    gl.uniform1f(uniformLocs.u_freqMin, freqMin.value);
    gl.uniform1f(uniformLocs.u_freqMax, freqMax.value);
    gl.uniform1f(uniformLocs.u_viewMin, viewMin);
    gl.uniform1f(uniformLocs.u_viewMax, viewMax);
    gl.uniform1f(uniformLocs.u_N, N);

    // 绘制
    gl.drawArrays(gl.LINE_STRIP, 0, N);

    // 解绑 VAO
    gl.bindVertexArray(null);
  }
}

// 绘制坐标轴和刻度（只在缩放时更新）
function renderAxis() {
  const ctx = axisCanvas.value.getContext('2d');
  ctx.clearRect(0, 0, localWidth.value, localHeight.value);

  // 设置通用样式
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  ctx.font = '12px Arial';

  // 1. 坐标轴和网格线（一次性绘制所有线条）
  ctx.beginPath();

  // X轴（零点线）
  const zeroY = ((Y_MAX - 0) / (Y_MAX - Y_MIN)) * localHeight.value;
  ctx.moveTo(0, zeroY);
  ctx.lineTo(localWidth.value, zeroY);

  // Y轴
  ctx.moveTo(0, 0);
  ctx.lineTo(0, localHeight.value);

  // X轴刻度线
  const xStep = getStepByRange(viewMax - viewMin);
  for (let f = Math.ceil(viewMin / xStep) * xStep; f <= viewMax; f += xStep) {
    const x = ((f - viewMin) / (viewMax - viewMin)) * localWidth.value;
    ctx.moveTo(x, 0);
    ctx.lineTo(x, localHeight.value);
  }

  // Y轴刻度线
  const yStep = 20; // 每20dB一个刻度
  for (let y = Math.ceil(Y_MIN / yStep) * yStep; y <= Y_MAX; y += yStep) {
    const yPos = ((Y_MAX - y) / (Y_MAX - Y_MIN)) * localHeight.value;
    ctx.moveTo(0, yPos);
    ctx.lineTo(localWidth.value, yPos);
  }

  // 一次性绘制所有线条
  ctx.strokeStyle = 'rgba(255,255,255,0.15)';
  ctx.stroke();

  // 单独绘制主坐标轴
  ctx.beginPath();
  ctx.moveTo(0, zeroY);
  ctx.lineTo(localWidth.value, zeroY);
  ctx.moveTo(0, 0);
  ctx.lineTo(0, localHeight.value);
  ctx.strokeStyle = 'rgba(255,255,255,0.5)';
  ctx.stroke();

  // 2. 绘制刻度值（文本）
  ctx.fillStyle = 'rgba(255,255,255,0.5)';

  // X轴刻度值
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (let f = Math.ceil(viewMin / xStep) * xStep; f <= viewMax; f += xStep) {
    const x = ((f - viewMin) / (viewMax - viewMin)) * localWidth.value;
    ctx.fillText(formatFreq(f), x, localHeight.value - 20);
  }

  // Y轴刻度值
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let y = Math.ceil(Y_MIN / yStep) * yStep; y <= Y_MAX; y += yStep) {
    const yPos = ((Y_MAX - y) / (Y_MAX - Y_MIN)) * localHeight.value;
    ctx.fillText(`${y}dB`, 30, yPos);
  }
}

// 叠加层渲染（只画跟随线）
function renderOverlay() {
  const ctx = overlayCanvas.value.getContext('2d');
  ctx.clearRect(0, 0, localWidth.value, localHeight.value);

  // 只绘制跟随线
  if (showTooltip.value && hoverIndex >= 0 && spectrumData.value.length > 1) {
    const N = spectrumData.value.length;
    const freq = freqMin.value + (
      ((freqMax.value - freqMin.value) * hoverIndex) / (N - 1)
    );
    if (freq >= viewMin && freq <= viewMax) {
      const x = ((freq - viewMin) / (viewMax - viewMin)) * localWidth.value;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, localHeight.value);
      ctx.strokeStyle = 'rgba(255,255,0,0.7)';
      ctx.stroke();
    }
  }
}

// raf 循环
function rafLoop() {
  requestAnimationFrame(rafLoop);
  if (needsSpectrumUpdate) {
    renderSpectrum();
    needsSpectrumUpdate = false;
    // 视图范围变化时，需要更新坐标轴
    needsAxisUpdate = true;
  }
  if (needsAxisUpdate) {
    renderAxis();
    needsAxisUpdate = false;
  }
  if (needsOverlayUpdate) {
    renderOverlay();
    needsOverlayUpdate = false;
  }
}

// 事件处理
function onWheel(e) {
  e.preventDefault();
  const zoom = e.deltaY < 0 ? 0.9 : 1.1;
  const rect = canvas.value.getBoundingClientRect();
  const mouse = (e.clientX - rect.left) / rect.width;
  const freqAtMouse = viewMin + (viewMax - viewMin) * mouse;
  let newMin = freqAtMouse - (freqAtMouse - viewMin) * zoom;
  let newMax = freqAtMouse + (viewMax - freqAtMouse) * zoom;
  if ((newMax - newMin) < 1e6) return; // 最小1MHz
  if (newMin < freqMin.value) newMin = freqMin.value;
  if (newMax > freqMax.value) newMax = freqMax.value;
  viewMin = newMin;
  viewMax = newMax;
  needsSpectrumUpdate = true;
  // 视图范围变化，需要更新坐标轴
  needsAxisUpdate = true;
}
function onMouseDown(e) {
  isDragging = true;
  dragStartX = e.clientX;
  dragStartViewMin = viewMin;
  dragStartViewMax = viewMax;
}
function onMouseMove(e) {
  if (isDragging) {
    const dx = e.clientX - dragStartX;
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
    needsSpectrumUpdate = true;
    // 视图范围变化，需要更新坐标轴
    needsAxisUpdate = true;
  } else {
    // 悬停线和tooltip
    if (!canvas.value) return;
    const rect = canvas.value.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const N = spectrumData.value.length;
    if (N > 1) {
      // 计算最近的点
      const freq = viewMin + (((viewMax - viewMin) * (x / rect.width)));
      let idx = Math.round(
        (((freq - freqMin.value) / (freqMax.value - freqMin.value)) * (N - 1)),
      );
      idx = Math.max(0, Math.min(N - 1, idx));
      tooltipFreq.value = freqMin.value + (
        ((freqMax.value - freqMin.value) * idx) / (N - 1)
      );
      tooltipValue.value = spectrumData.value[idx];
      hoverIndex = idx;
      showTooltip.value = true;
      tooltipStyle.value = {
        position: 'absolute',
        left: `${x + 10}px`,
        top: `${e.clientY - rect.top + 10}px`,
        background: 'rgba(30,30,30,0.95)',
        color: '#fff',
        padding: '4px 8px',
        borderRadius: '4px',
        fontSize: '12px',
        pointerEvents: 'none',
        zIndex: 10,
        boxShadow: '0 2px 8px rgba(0,0,0,0.2)',
      };
      needsOverlayUpdate = true;
    }
  }
}
function onMouseUp() {
  isDragging = false;
}
function onMouseLeave() {
  isDragging = false;
  showTooltip.value = false;
  hoverIndex = -1;
  needsOverlayUpdate = true;
}
function onBlur() {
  isDragging = false;
}
function onFocusOut() {
  isDragging = false;
}

// 资源清理
function cleanup() {
  if (resizeTimeout) {
    clearTimeout(resizeTimeout);
    resizeTimeout = null;
  }
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }
  if (gl) {
    if (gl.spectrumValueBuffer) gl.deleteBuffer(gl.spectrumValueBuffer);
    if (spectrumVAO) gl.deleteVertexArray(spectrumVAO);
    gl.getExtension('WEBGL_lose_context')?.loseContext();
    gl = null;
  }
}

// 监听尺寸变化
watch(() => props.width, (newWidth) => {
  localWidth.value = newWidth;
  if (canvas.value) {
    canvas.value.width = newWidth;
    needsSpectrumUpdate = true;
  }
});
watch(() => props.height, (newHeight) => {
  localHeight.value = newHeight;
  if (canvas.value) {
    canvas.value.height = newHeight;
    needsSpectrumUpdate = true;
  }
});
watch(() => thresholds.value, renderSpectrum, { deep: true });

// 监听 spectrumData 变化自动重绘
watch(spectrumData, () => { needsSpectrumUpdate = true; });

// 组件挂载
onMounted(async () => {
  await nextTick();
  if (!canvas.value) return;
  // 初始化 WebGL2
  gl = canvas.value.getContext('webgl2', { antialias: true });
  if (!gl) {
    console.error('无法初始化WebGL');
    return;
  }
  program = createProgram(gl, VERT, FRAG);

  // 获取并缓存属性位置
  spectrumValueLoc = gl.getAttribLocation(program, 'a_value');
  if (spectrumValueLoc === -1) {
    console.error('无法获取 a_value 属性位置');
    return;
  }

  // 缓存 uniform 位置
  uniformLocs = {
    u_freqMin: gl.getUniformLocation(program, 'u_freqMin'),
    u_freqMax: gl.getUniformLocation(program, 'u_freqMax'),
    u_viewMin: gl.getUniformLocation(program, 'u_viewMin'),
    u_viewMax: gl.getUniformLocation(program, 'u_viewMax'),
    u_yMin: gl.getUniformLocation(program, 'u_yMin'),
    u_yMax: gl.getUniformLocation(program, 'u_yMax'),
    u_N: gl.getUniformLocation(program, 'u_N'),
    u_color: gl.getUniformLocation(program, 'u_color'),
  };

  // 创建并初始化 VAO
  spectrumVAO = gl.createVertexArray();
  gl.bindVertexArray(spectrumVAO);

  // 创建并绑定缓冲区
  gl.spectrumValueBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, gl.spectrumValueBuffer);
  gl.enableVertexAttribArray(spectrumValueLoc);
  gl.vertexAttribPointer(spectrumValueLoc, 1, gl.FLOAT, false, 0, 0);

  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);

  // 设置固定的 uniform 值
  gl.useProgram(program);
  gl.uniform1f(uniformLocs.u_yMin, Y_MIN);
  gl.uniform1f(uniformLocs.u_yMax, Y_MAX);
  gl.uniform4fv(uniformLocs.u_color, [0, 1, 0, 1]);

  // 初始绘制
  renderSpectrum();

  // ResizeObserver
  resizeObserver = new ResizeObserver((entries) => {
    if (resizeTimeout) clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => {
      const entry = entries[0];
      if (entry) {
        localWidth.value = Math.floor(entry.contentRect.width);
        localHeight.value = Math.floor(entry.contentRect.height);
        canvas.value.width = localWidth.value;
        canvas.value.height = localHeight.value;
        axisCanvas.value.width = localWidth.value;
        axisCanvas.value.height = localHeight.value;
        overlayCanvas.value.width = localWidth.value;
        overlayCanvas.value.height = localHeight.value;
        needsSpectrumUpdate = true;
        needsAxisUpdate = true;
      }
    }, 100);
  });
  resizeObserver.observe(container.value);
  // 启动 raf 循环
  rafLoop();
});

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
  background: #181818;
  overflow: hidden;
}
.spectrum-tooltip {
  pointer-events: none;
  transition: left 0.05s, top 0.05s;
}
</style>
