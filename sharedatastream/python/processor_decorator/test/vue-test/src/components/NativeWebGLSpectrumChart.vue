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
    <!-- 主 canvas：只画谱线、坐标轴、刻度、阈值线 -->
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
        z-index: 2;
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
  spectrumData: { type: Array, default: () => [] },
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
const overlayCanvas = ref(null);
const container = ref(null);

// 频谱数据
// let ws = null;
let resizeObserver = null;
let resizeTimeout = null;
let needsSpectrumUpdate = false; // 只在数据变化/缩放时重绘主谱线
let needsOverlayUpdate = false; // 只在交互时重绘跟随线

// WebGL 相关
let gl = null;
let program = null;
let axisBuffer = null;
let tickBuffer = null;
let thresholdBuffers = [];

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
const Y_MAX = 100;

// 工具函数
function freqToXNorm(freq) {
  // 归一化到[-1, 1]
  return ((freq - viewMin) / (viewMax - viewMin)) * 2 - 1;
}
function valueToYNorm(value) {
  // 归一化到[-1, 1]
  return ((value - Y_MIN) / (Y_MAX - Y_MIN)) * 2 - 1;
}
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
function hexToRgbArray(hex, alpha = 1) {
  // #RRGGBB => [r,g,b,a] 0~1
  const h = hex.replace('#', '');
  return [
    parseInt(h.slice(0, 2), 16) / 255,
    parseInt(h.slice(2, 4), 16) / 255,
    parseInt(h.slice(4, 6), 16) / 255,
    alpha,
  ];
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

function setBuffer(glCtx, buffer, data) {
  glCtx.bindBuffer(glCtx.ARRAY_BUFFER, buffer);
  glCtx.bufferData(glCtx.ARRAY_BUFFER, new Float32Array(data), glCtx.STATIC_DRAW);
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
function drawLines(glCtx, prog, buffer, count, color, matrix) {
  glCtx.useProgram(prog);
  glCtx.uniform4fv(glCtx.getUniformLocation(prog, 'u_color'), color);
  glCtx.uniformMatrix4fv(glCtx.getUniformLocation(prog, 'u_matrix'), false, matrix);
  glCtx.bindBuffer(glCtx.ARRAY_BUFFER, buffer);
  const posLoc = glCtx.getAttribLocation(prog, 'a_position');
  glCtx.enableVertexAttribArray(posLoc);
  glCtx.vertexAttribPointer(posLoc, 2, glCtx.FLOAT, false, 0, 0);
  glCtx.drawArrays(glCtx.LINES, 0, count);
}

function identityMatrix() {
  return new Float32Array([
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
  ]);
}

// 主谱线渲染（只在数据变化/缩放时重绘）
function renderSpectrum() {
  if (!gl) return;
  gl.viewport(0, 0, localWidth.value, localHeight.value);
  gl.clearColor(0.09, 0.09, 0.09, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  const matrix = identityMatrix();

  // 1. 坐标轴
  const axisData = [
    -1, valueToYNorm(0), 1, valueToYNorm(0), // X轴
    freqToXNorm(freqMin.value), -1, freqToXNorm(freqMin.value), 1, // Y轴
  ];
  setBuffer(gl, axisBuffer, axisData);
  drawLines(gl, program, axisBuffer, 4, [1, 1, 1, 0.5], matrix);

  // 2. 动态刻度线
  const xStep = getStepByRange(viewMax - viewMin);
  const xTicks = [];
  for (let f = Math.ceil(viewMin / xStep) * xStep; f <= viewMax; f += xStep) {
    const x = freqToXNorm(f);
    xTicks.push(x, valueToYNorm(Y_MIN), x, valueToYNorm(Y_MAX));
  }
  setBuffer(gl, tickBuffer, xTicks);
  drawLines(gl, program, tickBuffer, (xTicks.length / 2) * 2, [1, 1, 1, 0.15], matrix);

  // Y轴刻度
  const yStep = 50;
  const yTicks = [];
  for (let y = Math.ceil(Y_MIN / yStep) * yStep; y <= Y_MAX; y += yStep) {
    const yNorm = valueToYNorm(y);
    yTicks.push(freqToXNorm(viewMin), yNorm, freqToXNorm(viewMax), yNorm);
  }
  setBuffer(gl, tickBuffer, yTicks);
  drawLines(gl, program, tickBuffer, (yTicks.length / 2) * 2, [1, 1, 1, 0.15], matrix);

  // 频谱线（WebGL2+gl_VertexID高性能方案）
  if (spectrumData.value.length > 1) {
    const N = spectrumData.value.length;
    if (!gl.spectrumValueBuffer) {
      gl.spectrumValueBuffer = gl.createBuffer();
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.spectrumValueBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(spectrumData.value), gl.STATIC_DRAW);
    gl.useProgram(program);
    const aValueLoc = gl.getAttribLocation(program, 'a_value');
    gl.enableVertexAttribArray(aValueLoc);
    gl.vertexAttribPointer(aValueLoc, 1, gl.FLOAT, false, 0, 0);
    // 设置 uniform
    gl.uniform1f(gl.getUniformLocation(program, 'u_freqMin'), freqMin.value);
    gl.uniform1f(gl.getUniformLocation(program, 'u_freqMax'), freqMax.value);
    gl.uniform1f(gl.getUniformLocation(program, 'u_viewMin'), viewMin);
    gl.uniform1f(gl.getUniformLocation(program, 'u_viewMax'), viewMax);
    gl.uniform1f(gl.getUniformLocation(program, 'u_yMin'), Y_MIN);
    gl.uniform1f(gl.getUniformLocation(program, 'u_yMax'), Y_MAX);
    gl.uniform1f(gl.getUniformLocation(program, 'u_N'), N);
    gl.uniform4fv(gl.getUniformLocation(program, 'u_color'), [0, 1, 0, 1]);
    gl.drawArrays(gl.LINE_STRIP, 0, N);
  }

  // 4. 阈值线
  thresholdBuffers = [];
  (thresholds.value || []).forEach((th) => {
    const y = valueToYNorm(th.value);
    const color = th.color ? hexToRgbArray(th.color, 0.7) : [1, 0, 0, 0.7];
    const buf = gl.createBuffer();
    setBuffer(gl, buf, [freqToXNorm(viewMin), y, freqToXNorm(viewMax), y]);
    drawLines(gl, program, buf, 2, color, matrix);
    thresholdBuffers.push(buf);
  });
}

// 叠加层渲染（只画跟随线）
function renderOverlay() {
  const ctx = overlayCanvas.value.getContext('2d');
  ctx.clearRect(0, 0, localWidth.value, localHeight.value);
  if (showTooltip.value && hoverIndex >= 0 && spectrumData.value.length > 1) {
    const N = spectrumData.value.length;
    const freq = freqMin.value + (
      ((freqMax.value - freqMin.value) * hoverIndex) / (N - 1)
    );
    if (freq >= viewMin && freq <= viewMax) {
      const x = ((freq - viewMin) / (viewMax - viewMin)) * localWidth.value;
      ctx.save();
      ctx.strokeStyle = 'rgba(255,255,0,0.7)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, localHeight.value);
      ctx.stroke();
      ctx.restore();
    }
  }
}

// raf 循环
function rafLoop() {
  requestAnimationFrame(rafLoop);
  if (needsSpectrumUpdate) {
    renderSpectrum();
    needsSpectrumUpdate = false;
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
    needsOverlayUpdate = true;
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
    // console.error('无法初始化WebGL');
    return;
  }
  program = createProgram(gl, VERT, FRAG);
  axisBuffer = gl.createBuffer();
  tickBuffer = gl.createBuffer();
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
        needsSpectrumUpdate = true;
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
