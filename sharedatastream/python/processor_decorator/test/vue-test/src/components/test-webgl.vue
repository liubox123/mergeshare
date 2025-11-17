<template>
  <div class="test-webgl-container">
    <canvas ref="canvas" />
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue';
import { WebglPlot, WebglLine, ColorRGBA } from 'webgl-plot';

const canvas = ref(null);
let webglPlot = null;
let animationFrameId = null;
let time = 0;

// 创建正弦波数据
function createSineWave(numPoints, phase) {
  const line = new WebglLine(new ColorRGBA(0, 1, 0, 1), numPoints);
  const xCoords = new Float32Array(numPoints);
  const yCoords = new Float32Array(numPoints);

  for (let i = 0; i < numPoints; i += 1) {
    const x = (i / (numPoints - 1)) * 2 - 1;
    xCoords[i] = x;
    yCoords[i] = Math.sin(x * Math.PI * 2 + phase);
  }

  line.setX(xCoords);
  line.setY(yCoords);
  return line;
}

// 渲染循环
function render() {
  if (webglPlot) {
    // 清除之前的线条
    webglPlot.clear();
    // console.log('Rendering frame', time);
    // 创建新的正弦波
    const sineWave = createSineWave(100, time);
    webglPlot.addLine(sineWave);

    // 更新显示
    webglPlot.update();

    // 更新相位
    time += 0.05;
  }

  // 继续动画循环
  animationFrameId = requestAnimationFrame(render);
}

// 初始化 WebGL-Plot
function initWebGLPlot() {
  if (!canvas.value) return;

  // 设置 canvas 尺寸
  const devicePixelRatio = window.devicePixelRatio || 1;
  console.log(canvas.value.clientWidth, canvas.value.clientHeight);
  canvas.value.width = canvas.value.clientWidth * devicePixelRatio;
  canvas.value.height = canvas.value.clientHeight * devicePixelRatio;

  // 创建 WebGL-Plot 实例
  webglPlot = new WebglPlot(canvas.value);

  // 设置视口
  webglPlot.viewport(0, 0, canvas.value.width, canvas.value.height);
  webglPlot.gScaleY = 1;
  webglPlot.gScaleX = 1;
  // 启动渲染循环
  render();
}

// 清理函数
function cleanup() {
  if (animationFrameId !== null) {
    cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }

  if (webglPlot) {
    webglPlot.clear();
    webglPlot = null;
  }
}

onMounted(() => {
  initWebGLPlot();
});

onBeforeUnmount(() => {
  cleanup();
});
</script>

<style scoped>
.test-webgl-container {
  width: 400px;
  height: 400px;
  background: #000;
  margin: 0 auto;
  display: flex;
}

canvas {
  width: 100%;
  height: 100%;
  display: block;
  border: 1px solid red;
}
</style>
