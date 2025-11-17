<template>
  <div
    class="waterfall-chart-container"
    @wheel="onWheel"
    @mousedown="onMouseDown"
    @mousemove="onMouseMove"
    @mouseup="onMouseUp"
    @mouseleave="onMouseLeave"
    @blur="onBlur"
    tabindex="0"
    ref="container"
    style="position: relative; width: 100%; height: 400px;"
  >
    <!-- 主 canvas：瀑布图像素渲染 -->
    <canvas
      ref="canvas"
      :width="localWidth"
      :height="localHeight"
      style="width: 100%; height: 100%; display: block; position: absolute;
      left: 0; top: 0; z-index: 1;"
    ></canvas>
    <!-- 叠加 canvas：鼠标交互层 -->
    <canvas
      ref="overlayCanvas"
      :width="localWidth"
      :height="localHeight"
      style="width: 100%; height: 100%; display: block; position: absolute;
      left: 0; top: 0; z-index: 2; pointer-events: none;"
    ></canvas>
    <div v-if="showTooltip" :style="tooltipStyle" class="waterfall-tooltip">
      <div>频率: {{ Math.round(tooltipFreq) }} Hz</div>
      <div>时间: {{ tooltipTimeLabel }}</div>
      <div>幅值: {{ tooltipValue.toFixed(2) }}</div>
    </div>
  </div>
</template>

<script setup>
import {
  ref, onMounted, onBeforeUnmount, watch, toRefs, nextTick,
} from 'vue';

// Props
const props = defineProps({
  wsUrl: { type: String, required: true },
  freqMin: { type: Number, default: 0 },
  freqMax: { type: Number, default: 80e9 },
  height: { type: Number, default: 400 },
  width: { type: Number, default: 800 },
  maxHistory: { type: Number, default: 200 }, // 历史帧数
});
const {
  wsUrl, freqMin, freqMax, maxHistory,
} = toRefs(props);

// 响应式状态
const localWidth = ref(props.width);
const localHeight = ref(props.height);
const canvas = ref(null);
const overlayCanvas = ref(null);
const container = ref(null);

// 历史帧数据（二维 float32 buffer，shape: maxHistory × N）
let N = 0; // 单帧点数
let historyBuffer = null; // Float32Array(maxHistory * N)
let ws = null;
let resizeObserver = null;
let resizeTimeout = null;
let needsUpdate = false;
let needsOverlayUpdate = false;

// WebGL2 相关
let gl = null;
let program = null;
let tex = null;
let vao = null;
let uLocs = {};
let resampledBuffer = null; // 保持降采样后的缓冲区

// 添加降采样配置
const MAX_POINTS_PER_FRAME = 8192; // 降低分辨率以提高性能

// 视图窗口
let viewMin = freqMin.value;
let viewMax = freqMax.value;
let yOffset = 0; // 历史帧偏移
let isDragging = false;
let dragStartX = 0;
let dragStartViewMin = 0;
let dragStartViewMax = 0;
let dragStartY = 0;
let dragStartYOffset = 0;

// Tooltip
const showTooltip = ref(false);
const tooltipFreq = ref(0);
const tooltipValue = ref(0);
const tooltipTimeLabel = ref('');
const tooltipStyle = ref({});
let hoverX = -1;
let hoverY = -1;

// Y轴范围
const Y_MIN = -150;
const Y_MAX = 100;

// --- WebGL2 shader ---
const VERT = `#version 300 es
precision mediump float;
layout(location=0) in vec2 a_pos;
out vec2 v_uv;
void main() {
  v_uv = (a_pos + 1.0) * 0.5;
  gl_Position = vec4(a_pos, 0, 1);
}`;

// 片元着色器：查 float32 2D 纹理，做归一化和伪彩色映射
const FRAG = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 v_uv;
out vec4 outColor;
uniform sampler2D u_tex;
uniform float u_freqMin, u_freqMax, u_viewMin, u_viewMax;
uniform float u_yMin, u_yMax;
uniform int u_N, u_H, u_yOffset;

// 蓝色亮度 colormap
vec3 blueMap(float t) {
    if (t < 0.3) {
        // 黑到蓝
        return vec3(0.0, 0.0, t / 0.3);
    } else {
        // 蓝到白
        float s = (t - 0.3) / 0.7;
        return vec3(s, s, 1.0);
    }
}

void main() {
  // 计算纹理坐标
  float x = v_uv.x;
  float y = v_uv.y;
  // 频率映射到纹理x
  float freq = u_viewMin + (u_viewMax - u_viewMin) * x;
  float tx = (freq - u_freqMin) / (u_freqMax - u_freqMin);
  tx = clamp(tx, 0.0, 1.0);
  // 历史帧映射到纹理y
  float ty = float(u_yOffset + int(float(u_H) * y)) / float(u_H);
  ty = clamp(ty, 0.0, 1.0);
  // 查 float32 纹理
  float val = texture(u_tex, vec2(tx, ty)).r;
  float t = clamp((val - u_yMin) / (u_yMax - u_yMin), 0.0, 1.0);
  outColor = vec4(blueMap(t), 1.0);
}`;

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

function initWebGL() {
  gl = canvas.value.getContext('webgl2', { antialias: false });
  if (!gl) return;

  // 检查浮点纹理支持
  const ext = gl.getExtension('EXT_color_buffer_float');
  if (!ext) {
    console.error('浏览器不支持 EXT_color_buffer_float，无法正确显示瀑布图！');
    return;
  }

  program = createProgram(gl, VERT, FRAG);

  // 全屏矩形
  vao = gl.createVertexArray();
  gl.bindVertexArray(vao);
  const vbo = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1, -1, 1, -1, -1, 1, 1, 1,
  ]), gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  gl.bindVertexArray(null);

  // 纹理
  tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);

  // uniform 位置
  uLocs = {
    u_tex: gl.getUniformLocation(program, 'u_tex'),
    u_freqMin: gl.getUniformLocation(program, 'u_freqMin'),
    u_freqMax: gl.getUniformLocation(program, 'u_freqMax'),
    u_viewMin: gl.getUniformLocation(program, 'u_viewMin'),
    u_viewMax: gl.getUniformLocation(program, 'u_viewMax'),
    u_yMin: gl.getUniformLocation(program, 'u_yMin'),
    u_yMax: gl.getUniformLocation(program, 'u_yMax'),
    u_N: gl.getUniformLocation(program, 'u_N'),
    u_H: gl.getUniformLocation(program, 'u_H'),
    u_yOffset: gl.getUniformLocation(program, 'u_yOffset'),
  };
}

// 快速抽点采样，直接按步长取点，不做平均
function fastResample(src, srcOffset, srcLength, targetLength) {
  const step = srcLength / targetLength;
  const result = new Float32Array(targetLength);

  for (let i = 0; i < targetLength; i += 1) {
    const srcIdx = Math.min(srcOffset + Math.floor(i * step), srcOffset + srcLength - 1);
    result[i] = src[srcIdx];
  }

  return result;
}

function uploadTexture() {
  if (!gl || !tex || !historyBuffer || N === 0) return;

  const targetN = Math.min(N, MAX_POINTS_PER_FRAME);

  if (targetN <= 0 || maxHistory.value <= 0) {
    console.warn('纹理尺寸不能为0或负数');
    return;
  }

  // 首次创建或大小变化时重新分配缓冲区
  if (!resampledBuffer || resampledBuffer.length !== targetN * maxHistory.value) {
    resampledBuffer = new Float32Array(targetN * maxHistory.value);
  }

  // 对每一帧进行快速抽点
  for (let i = 0; i < maxHistory.value; i += 1) {
    const srcOffset = i * N;
    const dstOffset = i * targetN;
    const resampled = fastResample(historyBuffer, srcOffset, N, targetN);
    resampledBuffer.set(resampled, dstOffset);
  }

  // 上传降采样后的数据到纹理
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texImage2D(
    gl.TEXTURE_2D,
    0,
    gl.R32F,
    targetN,
    maxHistory.value,
    0,
    gl.RED,
    gl.FLOAT,
    resampledBuffer,
  );
  gl.bindTexture(gl.TEXTURE_2D, null);
}

function renderWaterfall() {
  if (!gl || !program || !tex) return;
  gl.viewport(0, 0, localWidth.value, localHeight.value);
  gl.clearColor(0.09, 0.09, 0.09, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.useProgram(program);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.uniform1i(uLocs.u_tex, 0);
  gl.uniform1f(uLocs.u_freqMin, freqMin.value);
  gl.uniform1f(uLocs.u_freqMax, freqMax.value);
  gl.uniform1f(uLocs.u_viewMin, viewMin);
  gl.uniform1f(uLocs.u_viewMax, viewMax);
  gl.uniform1f(uLocs.u_yMin, Y_MIN);
  gl.uniform1f(uLocs.u_yMax, Y_MAX);
  gl.uniform1i(uLocs.u_N, N);
  gl.uniform1i(uLocs.u_H, maxHistory.value);
  gl.uniform1i(uLocs.u_yOffset, yOffset);
  gl.bindVertexArray(vao);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  gl.bindVertexArray(null);
}

// 叠加层渲染（鼠标悬停线/tooltip）
function renderOverlay() {
  const ctx = overlayCanvas.value.getContext('2d');
  ctx.clearRect(0, 0, localWidth.value, localHeight.value);
  if (showTooltip.value && hoverX >= 0 && hoverY >= 0) {
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,0,0.7)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(hoverX, 0);
    ctx.lineTo(hoverX, localHeight.value);
    ctx.moveTo(0, hoverY);
    ctx.lineTo(localWidth.value, hoverY);
    ctx.stroke();
    ctx.restore();
  }
}

function rafLoop() {
  requestAnimationFrame(rafLoop);
  if (needsUpdate) {
    renderWaterfall();
    needsUpdate = false;
  }
  if (needsOverlayUpdate) {
    renderOverlay();
    needsOverlayUpdate = false;
  }
}

// 事件处理
function onWheel(e) {
  e.preventDefault();
  if (e.ctrlKey) {
    // Y 轴滚动浏览历史
    yOffset += e.deltaY > 0 ? 10 : -10;
    yOffset = Math.max(0, Math.min(yOffset, Math.max(0, maxHistory.value - localHeight.value)));
    needsUpdate = true;
  } else {
    // X 轴缩放
    const zoom = e.deltaY < 0 ? 0.9 : 1.1;
    const rect = canvas.value.getBoundingClientRect();
    const mouse = (e.clientX - rect.left) / rect.width;
    const freqAtMouse = viewMin + (viewMax - viewMin) * mouse;
    let newMin = freqAtMouse - (freqAtMouse - viewMin) * zoom;
    let newMax = freqAtMouse + (viewMax - freqAtMouse) * zoom;
    if ((newMax - newMin) < 1e6) return;
    if (newMin < freqMin.value) newMin = freqMin.value;
    if (newMax > freqMax.value) newMax = freqMax.value;
    viewMin = newMin;
    viewMax = newMax;
    needsUpdate = true;
  }
}
function onMouseDown(e) {
  isDragging = true;
  dragStartX = e.clientX;
  dragStartY = e.clientY;
  dragStartViewMin = viewMin;
  dragStartViewMax = viewMax;
  dragStartYOffset = yOffset;
}
function onMouseMove(e) {
  if (isDragging) {
    // X 轴拖拽
    const dx = e.clientX - dragStartX;
    const freqRange = dragStartViewMax - dragStartViewMin;
    const df = (-(dx) / localWidth.value) * freqRange;
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
    // Y 轴拖拽
    const dy = e.clientY - dragStartY;
    yOffset = Math.max(
      0,
      Math.min(
        dragStartYOffset + dy,
        Math.max(0, maxHistory.value - localHeight.value),
      ),
    );
    needsUpdate = true;
    needsOverlayUpdate = true;
  } else {
    // 悬停 tooltip
    if (!canvas.value || !historyBuffer) return;
    const rect = canvas.value.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    hoverX = x;
    hoverY = y;
    if (N > 1 && maxHistory.value > 0) {
      const freq = viewMin + (viewMax - viewMin) * (x / localWidth.value);
      const frameIdx = Math.floor(
        ((y + yOffset) * maxHistory.value) / localHeight.value,
      );
      const idx = Math.floor(
        ((freq - freqMin.value) / (freqMax.value - freqMin.value)) * (N - 1),
      );
      const val = historyBuffer[frameIdx * N + idx] ?? Y_MIN;
      tooltipFreq.value = freq;
      tooltipValue.value = val;
      tooltipTimeLabel.value = frameIdx === 0 ? '最新' : `-${frameIdx} 帧`;
      showTooltip.value = true;
      tooltipStyle.value = {
        position: 'absolute',
        left: `${x + 10}px`,
        top: `${y + 10}px`,
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
  hoverX = -1;
  hoverY = -1;
  needsOverlayUpdate = true;
}
function onBlur() {
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
  if (ws) {
    ws.close();
    ws = null;
  }
  if (gl) {
    if (resampledBuffer) {
      resampledBuffer = null;
    }
    if (tex) gl.deleteTexture(tex);
    gl.getExtension('WEBGL_lose_context')?.loseContext();
    gl = null;
  }
}

// 监听尺寸变化
watch(() => props.width, (newWidth) => {
  localWidth.value = newWidth;
  if (canvas.value) {
    canvas.value.width = newWidth;
    needsUpdate = true;
  }
});
watch(() => props.height, (newHeight) => {
  localHeight.value = newHeight;
  if (canvas.value) {
    canvas.value.height = newHeight;
    needsUpdate = true;
  }
});

// 组件挂载
onMounted(async () => {
  await nextTick();
  if (!canvas.value) return;
  initWebGL();
  resizeObserver = new ResizeObserver((entries) => {
    if (resizeTimeout) clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => {
      const entry = entries[0];
      if (entry) {
        localWidth.value = Math.floor(entry.contentRect.width);
        localHeight.value = Math.floor(entry.contentRect.height);
        canvas.value.width = localWidth.value;
        canvas.value.height = localHeight.value;
        needsUpdate = true;
      }
    }, 100);
  });
  resizeObserver.observe(container.value);
  ws = new WebSocket(wsUrl.value);
  ws.onmessage = async (event) => {
    let arrayBuffer;
    if (typeof event.data === 'string') {
      // 首包参数，忽略
      return;
    }
    if (event.data instanceof Blob) {
      arrayBuffer = await event.data.arrayBuffer();
    } else if (event.data instanceof ArrayBuffer) {
      arrayBuffer = event.data;
    }
    if (arrayBuffer) {
      const arr = new Float32Array(arrayBuffer);
      if (N === 0) {
        N = arr.length;
        historyBuffer = new Float32Array(maxHistory.value * N);
        historyBuffer.fill(Y_MIN);
      }
      // 整体上移，最新帧写入第0行
      historyBuffer.copyWithin(N, 0, (maxHistory.value - 1) * N);
      historyBuffer.set(arr, 0);
      uploadTexture();
      needsUpdate = true;
    }
  };
  rafLoop();
});

onBeforeUnmount(() => {
  cleanup();
});
</script>

<style scoped>
.waterfall-chart-container {
  width: 100%;
  height: 400px;
  position: relative;
  user-select: none;
  background: #181818;
  overflow: hidden;
}
.waterfall-tooltip {
  pointer-events: none;
  transition: left 0.05s, top 0.05s;
}
</style>
